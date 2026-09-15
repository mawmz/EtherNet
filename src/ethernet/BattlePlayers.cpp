#include <ethernet/core/UpdatableModule.hpp>
#include <ethernet/LocalPlayers.hpp>
#include <ethernet/BattlePlayers.hpp>
#include <engine/xc2/gf/Manager.hpp>
#include <engine/xc2/gf/Party.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace ethernet {
namespace {
// Effective XC2 1.5.1 ExeFS. See docs/ethernet-stage3-native-research.md.
// This is the native lazy identity cache, NOT an input/status permission flag.
constexpr std::size_t SelfHandleOffset = 0x118;
constexpr std::size_t FlagsOffset = 0xefd; // packed, unaligned
constexpr std::uint64_t Manual = 1ull << 8;
constexpr std::uint64_t Cached = 1ull << 9;
constexpr std::uint64_t PlayerEligible = 1ull << 15;
constexpr std::uint64_t IdentityMask = Manual | Cached;

constexpr const char* ManagerSlotSymbol = "_ZZN2mm3mtl12PtrSingletonIN3btl16CharacterManagerEE3sysEvE10s_instance";
constexpr const char* GlobalSlotSymbol = "_ZZN2mm3mtl12PtrSingletonIN3btl12BattleGlobalEE3sysEvE10s_instance";
constexpr const char* ChainSlotSymbol = "_ZZN2mm3mtl12PtrSingletonIN3btl18ChainAttackManagerEE3sysEvE10s_instance";
constexpr const char* LookupSymbol = "_ZNK3btl16CharacterManager17GetCharacterConstEPN2gf13GF_OBJ_HANDLEE";
constexpr const char* DriverSymbol = "_ZN2gf11GfGameParty15getHandleDriverEjNS_5PTPOSE";
constexpr const char* ChainProcSymbol = "_ZNK3btl18ChainAttackManager6IsProcEb";
constexpr const char* ManagerUpdateSymbol = "_ZN3btl16CharacterManager10UpdateProcERKN2fw10UpdateInfoE";
constexpr const char* PreUpdateSymbol = "_ZN3btl15BattleCharacter16PreManagerUpdateERKN2fw10UpdateInfoE";
constexpr const char* InitializeSymbol = "_ZN3btl15BattleCharacter10InitializeEPN2gf13GF_OBJ_HANDLEEi";
constexpr const char* ControlSymbol = "_ZNK3btl15BattleCharacter18IsControlCharacterEv";
constexpr const char* ButtonSymbol = "_ZNK3btl15BattleCharacter13IsInputButtonEN2gf2pc7InputIDE";
constexpr const char* StickSymbol = "_ZNK3btl15BattleCharacter12IsInputStickEPN2mm4Vec3Eb";
constexpr const char* AISymbol = "_ZNK3btl15BattleCharacter8IsProcAIEb";
constexpr const char* ChainSelectSymbol = "_ZN3btl15BattleCharacter25SelectChainAttackSlotProcEv";
constexpr const char* BattleEndSymbol = "_ZN3btl16CharacterManager19BattleEndInitializeEv";
constexpr const char* DestroySymbol = "_ZN3btl16CharacterManager7DestroyEv";
constexpr const char* UpdateTargetSymbol = "_ZN3btl15BattleCharacter12UpdateTargetEv";
constexpr const char* AttackTargetSymbol = "_ZN3btl15BattleCharacter15SetAttackTargetEPN2gf13GF_OBJ_HANDLEE";
constexpr const char* ArtsSymbol = "_ZN3btl15BattleCharacter7AI_ArtsENS0_9ACTION_IDEi";
constexpr const char* AutoOperateSymbol = "_ZNK3btl12BattleGlobal19IsPlayerAutoOperateEv";
constexpr const char* ExitInputSymbol = "_ZN3btl15BattleCharacter7AI_ExitENS0_9ACTION_IDE";
constexpr const char* UISlotSymbol = "_ZZN2mm3mtl12PtrSingletonIN3btl9UIManagerEE3sysEvE10s_instance";
constexpr const char* GameOverSymbol = "_ZN3btl13BattleManager13CheckGameOverERKN2fw10UpdateInfoE";
constexpr const char* DeadSymbol = "_ZNK3btl15BattleCharacter6IsDeadEj";

using LookupFn = void*(*)(void*, gf::GF_OBJ_HANDLE*);
using DriverFn = gf::GF_OBJ_HANDLE*(*)(unsigned, int);
using ChainProcFn = bool(*)(void*, bool);
using AutoOperateFn = bool(*)(void*);
LookupFn Lookup{};
DriverFn Driver{};
ChainProcFn ChainProc{};
AutoOperateFn AutoOperate{};
void** ManagerSlot{};
void** GlobalSlot{};
void** ChainSlot{};
void** UISlot{};
std::uintptr_t CanonicalSelectorReturn{};
std::uintptr_t LeaderDefeatReturn{};
bool Installed{};
bool EndingLifecycle{};

template<class T> T Read(const void* base, std::size_t offset) {
    T value{};
    std::memcpy(&value, static_cast<const std::uint8_t*>(base) + offset, sizeof(value));
    return value;
}

void WriteIdentity(void* actor, std::uint64_t identity) {
    auto flags = (Read<std::uint64_t>(actor, FlagsOffset) & ~IdentityMask) | identity;
    std::memcpy(static_cast<std::uint8_t*>(actor) + FlagsOffset, &flags, sizeof(flags));
}

bool Valid(gf::GF_OBJ_HANDLE* handle) {
    return handle && handle != reinterpret_cast<gf::GF_OBJ_HANDLE*>(-1);
}

struct Binding {
    gf::GF_OBJ_HANDLE* handle{};
    void* object{};
    void* actor{};
    void* manager{};
    std::uint64_t generation{};
    bool promoted{};
};
std::array<Binding, 2> Players{};

// Never dereference a saved BattleCharacter until its current registration and
// object identity have been checked. Release/reuse goes through Initialize.
bool Live(const Binding& binding) {
    return binding.actor && ManagerSlot && *ManagerSlot == binding.manager &&
        gf::GfObjUtil::getObj(binding.handle) == binding.object &&
        Lookup(binding.manager, binding.handle) == binding.actor;
}

void Clear(Binding& binding) {
    if (binding.promoted && Live(binding)) WriteIdentity(binding.actor, 0);
    binding = {};
}

void ClearAll() {
    for (auto& binding : Players) Clear(binding);
}

void RefreshBindings() {
    if (!Installed || EndingLifecycle || ethernet::core::IsSceneTransitionActive() ||
        !ManagerSlot || !*ManagerSlot) {
        ClearAll();
        return;
    }
    // Actors are registered during field setup, before the global encounter
    // flag. Native targeting/input/AI already run while weapons are drawn.
    // Waiting for isBattle() makes P2 use the follower path during that gap.
    const auto generation = PlayerBindingGeneration();
    for (unsigned player = 0; player < Players.size(); ++player) {
        Binding next{};
        auto* handle = gf::GfGameParty::getHandleMover(player);
        // Never promote a supporting Blade or a stale former P2. The field
        // binding proves that native PadAgent setup has installed pad index 1.
        if (Valid(handle) && Driver(player, 0) == handle &&
            (player == 0 || IsPlayerTwoBound(handle))) {
            next.object = gf::GfObjUtil::getObj(handle);
            if (next.object) next.actor = Lookup(*ManagerSlot, handle);
            if (next.actor && Read<gf::GF_OBJ_HANDLE*>(next.actor, SelfHandleOffset) == handle) {
                next.handle = handle;
                next.manager = *ManagerSlot;
                next.generation = generation;
            } else next = {};
        }
        auto& old = Players[player];
        if (old.handle != next.handle || old.object != next.object ||
            old.actor != next.actor || old.manager != next.manager ||
            old.generation != next.generation) {
            Clear(old);
            old = next;
        }
    }
}

bool NativeIdentity(void* actor) {
    return (Read<std::uint64_t>(actor, FlagsOffset) & PlayerEligible) &&
        GlobalSlot && *GlobalSlot &&
        Read<gf::GF_OBJ_HANDLE*>(actor, SelfHandleOffset) ==
            Read<gf::GF_OBJ_HANDLE*>(*GlobalSlot, 0x10);
}

bool InChain() {
    return ChainSlot && *ChainSlot && ChainProc(*ChainSlot, false);
}

void SyncIdentity(void* actor) {
    RefreshBindings();
    auto& p2 = Players[1];
    if (actor != p2.actor || !actor) return;
    // Chain selection deliberately reads BattleGlobal's active actor instead
    // of this actor's pad. Leave that serialized native flow to Stage 4.
    const bool promote = !InChain() &&
        (Read<std::uint64_t>(actor, FlagsOffset) & PlayerEligible);
    if (promote) {
        WriteIdentity(actor, IdentityMask);
        p2.promoted = true;
    } else if (p2.promoted) {
        WriteIdentity(actor, 0); // re-evaluate native identity, never force AI
        p2.promoted = false;
    }
}

struct PreUpdate : skylaunch::hook::Trampoline<PreUpdate> {
    static void Hook(void* actor, const fw::UpdateInfo& update) {
        Orig(actor, update);
        // Native 0x38bec clears bit 9 every frame. Must run AFTER that reset,
        // before ManagerUpdate's inlined target/input gates and Update's AI.
        SyncIdentity(actor);
    }
};

struct InitializeActor : skylaunch::hook::Trampoline<InitializeActor> {
    static void Hook(void* actor, gf::GF_OBJ_HANDLE* handle, int index) {
        for (auto& binding : Players) {
            if (binding.actor == actor) {
                if (binding.promoted) WriteIdentity(actor, 0);
                binding = {};
            }
        }
        // Register and both Release routes converge here; native Initialize
        // preserves the identity bits, so remove ours BEFORE object reuse.
        Orig(actor, handle, index);
        if (Valid(handle) && index >= 0) SyncIdentity(actor);
    }
};

struct ControlCharacter : skylaunch::hook::Trampoline<ControlCharacter> {
    static bool Hook(void* actor) {
        const auto caller = reinterpret_cast<std::uintptr_t>(__builtin_return_address(0));
        SyncIdentity(actor);
        // UpdateProc+0x80 (ELF 0x7fe8c) chooses the ONE global control actor.
        // Do not let sorted registration order make P2 steal that singleton.
        if (Installed && caller == CanonicalSelectorReturn) return NativeIdentity(actor);
        return Orig(actor);
    }
};

struct LeaderDefeat : skylaunch::hook::Trampoline<LeaderDefeat> {
    static bool Hook(const void* actor, unsigned flags) {
        const auto caller = reinterpret_cast<std::uintptr_t>(__builtin_return_address(0));
        const bool dead = Orig(actor, flags);
        if (!dead || !Installed || caller != LeaderDefeatReturn) return dead;

        // CheckGameOver+0x118 (ELF 0xf0c4) tests the leader before its
        // zero-gauge / rescue-timeout defeat path. A living second human can
        // continue fighting. Use the native false branch, which also resets
        // the rescue timeout; do not mask actual death for any other caller.
        // Earlier native forced-defeat and all-party-dead checks still run.
        RefreshBindings();
        if (actor == GetBoundBattleActor(0)) {
            const auto* partner = GetBoundBattleActor(1);
            if (partner && !Orig(partner, 0)) return false;
        }
        return dead;
    }
};

struct InputButton : skylaunch::hook::Trampoline<InputButton> {
    static bool Hook(void* actor, unsigned input) {
        SyncIdentity(actor);
        return Orig(actor, input);
    }
};

struct ManualArts : skylaunch::hook::Trampoline<ManualArts> {
    static bool Hook(void* actor, unsigned action, int slot) {
        SyncIdentity(actor);
        if (slot < 0 && actor == GetBoundBattleActor(1) && !InChain() &&
            GlobalSlot && *GlobalSlot && !AutoOperate(*GlobalSlot) &&
            (Read<std::uint64_t>(actor, FlagsOffset) & (Manual | (1ull << 16))) ==
                (Manual | (1ull << 16))) {
            // Native AI_Arts polls 7/8/9 only if the ONE retail HUD is active
            // (ELF 0x7a530). Select P2's slot through the same native input
            // permission checks, then use AI_Arts' explicit-slot path. Keep
            // native StartArts, costs, range/status checks and cancel logic;
            // don't mutate P1's UI state or send it P2's NotifyInput feedback.
            for (unsigned candidate = 0; candidate < 3; ++candidate) {
                if (InputButton::Hook(actor, 7 + candidate)) {
                    slot = static_cast<int>(candidate);
                    break;
                }
            }
        }
        return Orig(actor, action, slot);
    }
};

struct ManualExit : skylaunch::hook::Trampoline<ManualExit> {
    static bool Hook(void* actor, unsigned action) {
        SyncIdentity(actor);
        auto* ui = UISlot ? *UISlot : nullptr;
        if (!ui || actor != GetBoundBattleActor(1) || InChain() ||
            (Read<std::uint64_t>(actor, FlagsOffset) & (Manual | (1ull << 16))) !=
                (Manual | (1ull << 16))) return Orig(actor, action);

        // AI_Exit has no explicit-input overload. Its final input check is
        // gated by UIManager+f1 bit8 (ELF 0x71fd8), the same P1-only HUD flag
        // used by AI_Arts. Supply that permission only for this P2 invocation;
        // all native status/auto/chain/input checks still execute unchanged.
        auto* flags = static_cast<std::uint8_t*>(ui) + 0xf2;
        const auto hudVisible = *flags & 1u;
        *flags |= 1u;
        const bool result = Orig(actor, action);
        // Restore only the borrowed bit, retaining native updates to other
        // UI flags. Do not make the retail HUD visible or change its owner.
        *flags = (*flags & ~1u) | hudVisible;
        return result;
    }
};

struct InputStick : skylaunch::hook::Trampoline<InputStick> {
    static bool Hook(void* actor, void* direction, bool worldSpace) {
        SyncIdentity(actor);
        return Orig(actor, direction, worldSpace);
    }
};

struct ProcAI : skylaunch::hook::Trampoline<ProcAI> {
    static bool Hook(void* actor, bool check) {
        SyncIdentity(actor);
        // Preserve auto-operate, buffs, death, tutorial, Special and cinematic
        // restrictions. AIUpdate also runs human auto-attacks: never skip it.
        return Orig(actor, check);
    }
};

struct ChainSelection : skylaunch::hook::Trampoline<ChainSelection> {
    static void Hook(void* actor) {
        SyncIdentity(actor);
        // Covers a chain starting after this frame's PreManagerUpdate. Native
        // P1 remains responsible for selection, including P2's chain turn.
        auto& p2 = Players[1];
        if (actor == p2.actor && p2.promoted) {
            WriteIdentity(actor, 0);
            p2.promoted = false;
        }
        Orig(actor);
    }
};

// Both functions inline the manual identity test. Their final global target
// store has no following callbacks (ELF 0x3b070 / 0x2fe58). Keep every native
// actor-local update, but don't publish a second human as the retail HUD owner.
struct PreserveCanonicalTarget {
    void* global{};
    gf::GF_OBJ_HANDLE* target{};
    std::uint32_t forced{};
    explicit PreserveCanonicalTarget(void* actor) {
        if (actor != GetBoundBattleActor(1) || InChain() || !GlobalSlot) return;
        global = *GlobalSlot;
        if (!global) return;
        target = Read<gf::GF_OBJ_HANDLE*>(global, 0x18);
        forced = Read<std::uint32_t>(global, 0x8389) & 0x400;
    }
    ~PreserveCanonicalTarget() {
        if (!global || *GlobalSlot != global) return;
        std::memcpy(static_cast<std::uint8_t*>(global) + 0x18, &target, sizeof(target));
        auto flags = (Read<std::uint32_t>(global, 0x8389) & ~0x400u) | forced;
        std::memcpy(static_cast<std::uint8_t*>(global) + 0x8389, &flags, sizeof(flags));
    }
};
struct UpdateTarget : skylaunch::hook::Trampoline<UpdateTarget> {
    static void Hook(void* actor) {
        SyncIdentity(actor);
        const PreserveCanonicalTarget preserve(actor);
        Orig(actor);
    }
};
struct AttackTarget : skylaunch::hook::Trampoline<AttackTarget> {
    static void Hook(void* actor, gf::GF_OBJ_HANDLE* target) {
        SyncIdentity(actor);
        const PreserveCanonicalTarget preserve(actor);
        Orig(actor, target);
    }
};

struct BattleEnd : skylaunch::hook::Trampoline<BattleEnd> {
    static void Hook(void* manager) {
        const bool previous = EndingLifecycle;
        EndingLifecycle = true;
        ClearAll();
        Orig(manager);
        EndingLifecycle = previous;
    }
};

struct DestroyManager : skylaunch::hook::Trampoline<DestroyManager> {
    static void Hook() {
        const bool previous = EndingLifecycle;
        EndingLifecycle = true;
        ClearAll();
        Orig();
        EndingLifecycle = previous;
    }
};

struct BattlePlayers : ethernet::core::UpdatableModule {
    void Initialize() override {
        UpdatableModule::Initialize();
        if (ethernet::core::version::RuntimeGame() != ethernet::core::version::GameType::BF2) return;
        for (auto* name : {ManagerSlotSymbol,GlobalSlotSymbol,ChainSlotSymbol,LookupSymbol,
             DriverSymbol,ChainProcSymbol,ManagerUpdateSymbol,PreUpdateSymbol,InitializeSymbol,
             ControlSymbol,ButtonSymbol,StickSymbol,AISymbol,ChainSelectSymbol,BattleEndSymbol,DestroySymbol,
             UpdateTargetSymbol,AttackTargetSymbol,ArtsSymbol,AutoOperateSymbol,
             ExitInputSymbol,UISlotSymbol,GameOverSymbol,DeadSymbol}) {
            const auto address = skylaunch::hook::detail::ResolveSymbolBase(name);
            if (!address || address == skylaunch::hook::INVALID_FUNCTION_PTR) {
                ethernet::core::g_Logger->LogError("EtherNet battle players: missing {}", name);
                return;
            }
        }
        ManagerSlot = skylaunch::hook::detail::ResolveSymbol<void**>(ManagerSlotSymbol);
        GlobalSlot = skylaunch::hook::detail::ResolveSymbol<void**>(GlobalSlotSymbol);
        ChainSlot = skylaunch::hook::detail::ResolveSymbol<void**>(ChainSlotSymbol);
        UISlot = skylaunch::hook::detail::ResolveSymbol<void**>(UISlotSymbol);
        Lookup = skylaunch::hook::detail::ResolveSymbol<LookupFn>(LookupSymbol);
        Driver = skylaunch::hook::detail::ResolveSymbol<DriverFn>(DriverSymbol);
        ChainProc = skylaunch::hook::detail::ResolveSymbol<ChainProcFn>(ChainProcSymbol);
        AutoOperate = skylaunch::hook::detail::ResolveSymbol<AutoOperateFn>(AutoOperateSymbol);
        CanonicalSelectorReturn = skylaunch::hook::detail::ResolveSymbolBase(ManagerUpdateSymbol) + 0x80;
        // Validate the exact callsite used above, not just a symbol-relative
        // guess on another executable. BL must still target IsControlCharacter.
        const auto callsite = CanonicalSelectorReturn - 4;
        const auto instruction = Read<std::uint32_t>(reinterpret_cast<void*>(callsite), 0);
        std::int64_t displacement = instruction & 0x03ffffff;
        if (displacement & 0x02000000) displacement -= 0x04000000;
        const auto target = static_cast<std::uintptr_t>(static_cast<std::int64_t>(callsite) + displacement * 4);
        if ((instruction & 0xfc000000) != 0x94000000 ||
            target != skylaunch::hook::detail::ResolveSymbolBase(ControlSymbol)) {
            ethernet::core::g_Logger->LogError("EtherNet battle players: unsupported native control-selector callsite");
            return;
        }
        LeaderDefeatReturn = skylaunch::hook::detail::ResolveSymbolBase(GameOverSymbol) + 0x11c;
        const auto defeatCallsite = LeaderDefeatReturn - 4;
        const auto defeatInstruction = Read<std::uint32_t>(reinterpret_cast<void*>(defeatCallsite), 0);
        std::int64_t defeatDisplacement = defeatInstruction & 0x03ffffff;
        if (defeatDisplacement & 0x02000000) defeatDisplacement -= 0x04000000;
        const auto defeatTarget = static_cast<std::uintptr_t>(
            static_cast<std::int64_t>(defeatCallsite) + defeatDisplacement * 4);
        if ((defeatInstruction & 0xfc000000) != 0x94000000 ||
            defeatTarget != skylaunch::hook::detail::ResolveSymbolBase(DeadSymbol)) {
            ethernet::core::g_Logger->LogError("EtherNet battle players: unsupported native leader-defeat callsite");
            return;
        }
        PreUpdate::HookAt(PreUpdateSymbol);
        InitializeActor::HookAt(InitializeSymbol);
        ControlCharacter::HookAt(ControlSymbol);
        InputButton::HookAt(ButtonSymbol);
        InputStick::HookAt(StickSymbol);
        ProcAI::HookAt(AISymbol);
        ChainSelection::HookAt(ChainSelectSymbol);
        BattleEnd::HookAt(BattleEndSymbol);
        DestroyManager::HookAt(DestroySymbol);
        UpdateTarget::HookAt(UpdateTargetSymbol);
        AttackTarget::HookAt(AttackTargetSymbol);
        ManualArts::HookAt(ArtsSymbol);
        ManualExit::HookAt(ExitInputSymbol);
        LeaderDefeat::HookAt(DeadSymbol);
        Installed = PreUpdate::HasApplied() && InitializeActor::HasApplied() &&
            ControlCharacter::HasApplied() && InputButton::HasApplied() && InputStick::HasApplied() &&
            ProcAI::HasApplied() && ChainSelection::HasApplied() && BattleEnd::HasApplied() &&
            DestroyManager::HasApplied() && UpdateTarget::HasApplied() && AttackTarget::HasApplied() &&
            ManualArts::HasApplied() && ManualExit::HasApplied() && LeaderDefeat::HasApplied();
        ethernet::core::g_Logger->LogInfo("EtherNet battle players: {}", Installed ? "installed" : "failed");
    }
    void OnSceneTransition() override { ClearAll(); }
    void OnMapChange(unsigned short) override { ClearAll(); }
};
ETHERNET_REGISTER_MODULE(BattlePlayers);
}

const void* GetBoundBattleActor(unsigned player) {
    // Observation only: do not refresh/promote bindings for a readout. Closing
    // the panel must not remove anything the battle implementation depends on.
    if (!Installed || EndingLifecycle || player >= Players.size() ||
        ethernet::core::IsSceneTransitionActive()) return nullptr;
    const auto& binding = Players[player];
    if (binding.generation != PlayerBindingGeneration() || !Live(binding) ||
        gf::GfGameParty::getHandleMover(player) != binding.handle ||
        Driver(player, 0) != binding.handle ||
        (player == 1 && !IsPlayerTwoBound(binding.handle))) return nullptr;
    return Read<gf::GF_OBJ_HANDLE*>(binding.actor, SelfHandleOffset) == binding.handle
        ? binding.actor : nullptr;
}
}
