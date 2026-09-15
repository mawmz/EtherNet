#include <ethernet/core/UpdatableModule.hpp>
#include <ethernet/BattleTargets.hpp>
#include <ethernet/BattlePlayers.hpp>
#include <ethernet/BattleEntry.hpp>
#include <ethernet/LocalPlayers.hpp>
#include <ethernet/FieldRecovery.hpp>
#include <engine/xc2/gf/Party.hpp>
#include <engine/xc2/gf/Manager.hpp>
#include <array>
#include <cstdint>
#include <cstring>

namespace ethernet {
namespace {
using Handle = gf::GF_OBJ_HANDLE*;
Handle None() { return reinterpret_cast<Handle>(-1); }
template<class T> T Read(const void* base, std::size_t offset) {
    T value{};
    std::memcpy(&value, static_cast<const std::uint8_t*>(base) + offset, sizeof(value));
    return value;
}
template<class T> void Write(void* base, std::size_t offset, T value) {
    std::memcpy(static_cast<std::uint8_t*>(base) + offset, &value, sizeof(value));
}
bool Valid(Handle handle) { return handle && handle != None(); }
void** TargetSlot{};
void** SearchSlot{};
void** GlobalSlot{};
void** ChainSlot{};
void** UISlot{};
void (*RelayCtor)(void*){};
void (*RelayDtor)(void*){};
void (*RelayBind)(void*, int, int, void*){};
void (*RelayUpdate)(void*){};
// AArch64 member-pointer pair is passed in x3/x4, verified at 0x3b52b8.
void (*BindAction)(void*, const char*, void*, void(*)(void*), std::uintptr_t){};
void* (*Allocator)(unsigned){};
Handle (*CreateSearch)(const void*){};
void (*DestroySearch)(Handle){};
void* (*SearchInfo)(Handle){};
void* (*SearchList)(Handle){};
bool (*Nearest)(void*, const void*){};
bool (*NearestBattle)(void*, const void*){};
void* (*Cycle)(void*, const void*){};
bool (*SetTarget)(void*, Handle){};
bool (*Enabled)(){};
bool (*CommonWindow)(){};
bool (*Pause)(void*, int){};
bool (*AutoOperate)(void*){};
bool (*ChainProc)(void*, bool){};
bool (*Destroyed)(Handle){};
bool Installed{};

// Storage for ONLY the leaf selection helpers' verified non-virtual fields:
// b8 target, c0/c2 change flags, c9/ca direction, 110 borrowed parameter holder.
// Not a constructed GfGameTarget: no FSM, registration, singleton or notification.
struct alignas(8) SelectionFields { std::array<std::uint8_t, 0x118> bytes{}; } Selection;
struct alignas(8) RelayStorage { std::array<std::uint8_t, 0x38> bytes{}; } Relay;
bool RelayLive{};
Handle Owner{};
void* OwnerObject{};
std::uint64_t Generation{};
Handle Query = None();
void* QueryManager{};
bool Right{}, Left{}, Acquire{}, Release{}, Holding{}, Shifted{};
float HoldTime{};
float TapTime{};

Handle LivePlayer(unsigned player) {
    if (player > 1 || ethernet::core::IsSceneTransitionActive()) return None();
    auto* handle = gf::GfGameParty::getHandleMover(player);
    if (!Valid(handle) || !gf::GfObjUtil::getObj(handle) ||
        (player == 1 && !IsPlayerTwoBound(handle))) return None();
    return handle;
}
void* Property(Handle handle) {
    auto* object = Valid(handle) ? gf::GfObjUtil::getObj(handle) : nullptr;
    return object ? Read<void*>(object, 0x60) : nullptr;
}
Handle PropertyTarget(Handle handle) {
    auto* property = Property(handle);
    auto* slot = property ? Read<Handle*>(property, 0x18) : nullptr;
    return slot ? *slot : None();
}
bool LiveQuery() {
    if (Query == None() || !SearchSlot || !QueryManager || *SearchSlot != QueryManager ||
        !Read<void*>(QueryManager, 0x2428)) return false;
    auto* info = SearchInfo(Query);
    return info && Read<Handle>(info, 0x70) == Query;
}
void Reset() {
    ResetBattleEntry();
    if (LiveQuery()) DestroySearch(Query);
    Query = None(); QueryManager = nullptr;
    if (RelayLive) RelayDtor(&Relay);
    RelayLive = false;
    Owner = nullptr; OwnerObject = nullptr; Generation = 0;
    Right = Left = Acquire = Release = Holding = Shifted = false;
    HoldTime = 0;
}
bool FieldTargetMode() {
    return UISlot && *UISlot && Read<std::int8_t>(*UISlot, 0xf1) >= 0;
}
void SelectRight(void*) { if (FieldTargetMode() || Shifted) { Right = true; Holding = false; } }
void SelectLeft(void*) { if (FieldTargetMode() || Shifted) { Left = true; Holding = false; } }
void ReleaseTarget(void*) { if (FieldTargetMode()) { Release = true; Holding = false; } }
void BeginNear(void*) { Holding = true; HoldTime = 0; }
void EndNear(void*) { Acquire = Holding && HoldTime < TapTime; Holding = Shifted = false; }

bool Prepare(void* nativeTarget, Handle owner) {
    auto* object = gf::GfObjUtil::getObj(owner);
    if (Owner != owner || OwnerObject != object || Generation != PlayerBindingGeneration()) Reset();
    auto* holder = Read<void*>(nativeTarget, 0x110);
    auto* resource = holder && Read<int>(holder, 0xc) ? Read<void*>(holder, 0x10) : nullptr;
    auto* parameter = resource ? Read<void*>(resource, 8) : nullptr;
    if (!parameter || !SearchSlot || !*SearchSlot || !Read<void*>(*SearchSlot, 0x2428)) return false;
    Owner = owner; OwnerObject = object; Generation = PlayerBindingGeneration();
    Write(&Selection, 0x110, holder); // borrowed only for this synchronous update
    TapTime = Read<float>(parameter, 0xc);
    if (!RelayLive) {
        RelayCtor(&Relay);
        RelayBind(&Relay, 1, 3, Allocator(0));
        BindAction(&Relay, "change_mode", &Selection, ReleaseTarget, 0);
        BindAction(&Relay, "select_right", &Selection, SelectRight, 0);
        BindAction(&Relay, "select_left", &Selection, SelectLeft, 0);
        BindAction(&Relay, "close_target_on", &Selection, BeginNear, 0);
        BindAction(&Relay, "close_target_off", &Selection, EndNear, 0);
        RelayLive = true;
    }
    if (!LiveQuery()) {
        // Native createObj asserts on an exhausted 64-entry pool.
        Query = None();
        if (Read<unsigned>(*SearchSlot, 0x2434) >= 64) return false;
        alignas(8) std::array<std::uint8_t, 0x68> param{};
        // Same enemy query as GfGameTarget::createSearchObj's a0 query. Native
        // updateObj supports a handle at +40 instead of a retained transform:
        // use P2's current transform for distance/facing and native visibility.
        Write(param.data(), 0, std::uint64_t{0x2000000000});
        Write(param.data(), 0x2c, 1.f);
        Write(param.data(), 0x40, owner);
        Write(param.data(), 0x50, std::uint8_t{1});
        Write(param.data(), 0x60, owner);
        QueryManager = *SearchSlot;
        Query = CreateSearch(param.data());
    }
    return LiveQuery();
}

void ApplyTarget(Handle owner, Handle target) {
    auto* object = gf::GfObjUtil::getObj(owner);
    auto* behavior = object ? Read<void*>(object, 0x68) : nullptr;
    auto* property = object ? Read<void*>(object, 0x60) : nullptr;
    if (behavior && property && Read<void*>(property, 0x18) && PropertyTarget(owner) != target)
        SetTarget(behavior, target);
}

void Update(void* nativeTarget, const fw::UpdateInfo& update) {
    if (!Installed) return;
    auto* owner = LivePlayer(1);
    if (!Valid(owner)) { Reset(); return; }
    if (!Enabled() || IsFieldPlayerRecovering(1)) {
        ResetBattleEntry();
        Holding = Shifted = false;
        return;
    }
    if (!Prepare(nativeTarget, owner)) { ResetBattleEntry(); return; }
    if (!GlobalSlot || !*GlobalSlot || Pause(*GlobalSlot, -1) || CommonWindow() ||
        AutoOperate(*GlobalSlot) || Read<std::uint8_t>(nativeTarget, 0xc1) ||
        (ChainSlot && *ChainSlot && ChainProc(*ChainSlot, false))) {
        ResetBattleEntry();
        Holding = Shifted = false;
        return;
    }
    if (Holding) {
        HoldTime += Read<float>(&update, 4);
        // Native NotifyShiftTimeOver -> BattleGlobal::PreUpdateProc. Do not
        // publish P2's modifier into the shared P1 HUD flags.
        if (HoldTime >= TapTime) Shifted = true;
    }
    Right = Left = Acquire = Release = false;
    RelayUpdate(&Relay); // native bindings, edges/repeat; never ImGui input
    const auto* actor = GetBoundBattleActor(1);
    const bool engaged = actor && (Read<std::uint64_t>(actor, 0xefd) & (1ull << 16));
    auto* target = PropertyTarget(owner);
    auto* object = Valid(target) ? gf::GfObjUtil::getObj(target) : nullptr;
    const bool invalid = !object || !(Read<std::uint32_t>(object, 0x110) & 4) || Destroyed(target);
    Write(&Selection, 0xb8, target);
    Write(&Selection, 0xc9, std::uint8_t(Right));
    Write(&Selection, 0xca, std::uint8_t(Left));
    auto* list = SearchList(Query);
    if (!list) return;
    if (Release && !engaged) Write(&Selection, 0xb8, None());
    else if (Acquire || ((Right || Left) && invalid) || (engaged && invalid)) {
        if (!engaged || !NearestBattle(&Selection, list)) Nearest(&Selection, list);
    } else if (Right || Left) Cycle(&Selection, list);
    else if (invalid && Valid(target)) Write(&Selection, 0xb8, None());
    ApplyTarget(owner, Read<Handle>(&Selection, 0xb8));
    Write(&Selection, 0x110, static_cast<void*>(nullptr));
    ProcessPlayerTwoBattleEntry();
}

constexpr const char* UpdateSymbol = "_ZN2gf12GfGameTarget10updateImplERKN2fw10UpdateInfoE";
constexpr const char* CleanupSymbol = "_ZN2gf12GfGameTarget7cleanupEv";
constexpr const char* MessageSymbol = "_ZN2gf12GfGameTarget23procMsgGameChangeTargetEPN2fw13MessageObjectERNS_19MsgGameChangeTargetE";
struct TargetUpdate : skylaunch::hook::Trampoline<TargetUpdate> {
    static void Hook(void* self, const fw::UpdateInfo& update) { Orig(self, update); Update(self, update); }
};
struct TargetCleanup : skylaunch::hook::Trampoline<TargetCleanup> {
    static void Hook(void* self) { Reset(); Orig(self); }
};
struct TargetMessage : skylaunch::hook::Trampoline<TargetMessage> {
    static void Hook(void* self, void* sender, void* message) {
        auto* owner = Installed ? LivePlayer(1) : None();
        auto* object = Valid(owner) ? gf::GfObjUtil::getObj(owner) : nullptr;
        auto* dispatcher = object ? static_cast<std::uint8_t*>(object) + 0xe0 : nullptr;
        if (dispatcher && sender == dispatcher) {
            // StateUtil already set this actor's property target. Use the
            // SAME native actor message delivery that notifyTarget uses, but
            // omit its P1 camera/UI notification. No temporary party swap.
            using Send = void(*)(void*, void*, void*);
            auto* table = Read<void*>(dispatcher, 0);
            Read<Send>(table, 0x18)(dispatcher, sender, message);
            return;
        }
        Orig(self, sender, message);
    }
};

template<class T> bool Resolve(T& function, const char* symbol) {
    auto address = skylaunch::hook::detail::ResolveSymbolBase(symbol);
    if (!address || address == skylaunch::hook::INVALID_FUNCTION_PTR) {
        ethernet::core::g_Logger->LogError("EtherNet targeting: missing {}", symbol);
        return false;
    }
    function = reinterpret_cast<T>(address);
    return true;
}
struct BattleTargets : ethernet::core::UpdatableModule {
    void Initialize() override {
        UpdatableModule::Initialize();
        if (ethernet::core::version::RuntimeGame() != ethernet::core::version::GameType::BF2) return;
        bool ok = true;
#define LOAD(variable, name) ok = Resolve(variable, name) && ok
        LOAD(TargetSlot, "_ZZN2mm3mtl12PtrSingletonIN2gf12GfGameTargetEE3sysEvE10s_instance");
        LOAD(SearchSlot, "_ZZN2mm3mtl12PtrSingletonIN2gf15GfGameSearchObjEE3sysEvE10s_instance");
        LOAD(GlobalSlot, "_ZZN2mm3mtl12PtrSingletonIN3btl12BattleGlobalEE3sysEvE10s_instance");
        LOAD(ChainSlot, "_ZZN2mm3mtl12PtrSingletonIN3btl18ChainAttackManagerEE3sysEvE10s_instance");
        LOAD(UISlot, "_ZZN2mm3mtl12PtrSingletonIN3btl9UIManagerEE3sysEvE10s_instance");
        LOAD(RelayCtor, "_ZN2fw8PadRelayC1Ev");
        LOAD(RelayDtor, "_ZN2fw8PadRelayD1Ev");
        LOAD(RelayBind, "_ZN2fw8PadRelay4bindEiiPN2mm3mtl10IAllocatorE");
        LOAD(RelayUpdate, "_ZN2fw8PadRelay6updateEv");
        LOAD(BindAction, "_ZN2fw8PadRelay10bindActionIN2gf12GfGameTargetEMS3_FvvEEEvPKcPT_T0_");
        LOAD(Allocator, "_ZN2gf12getAllocatorENS_9ALLOCATORE");
        LOAD(CreateSearch, "_ZN2gf15GfGameSearchObj9createObjERKNS_13GfSearchParamE");
        LOAD(DestroySearch, "_ZN2gf15GfGameSearchObj10destroyObjEPNS_13GF_SEARCH_OBJE");
        LOAD(SearchInfo, "_ZN2gf15GfGameSearchObj7getInfoEPNS_13GF_SEARCH_OBJE");
        LOAD(SearchList, "_ZN2gf15GfGameSearchObj6getObjEPNS_13GF_SEARCH_OBJE");
        LOAD(Nearest, "_ZN2gf12GfGameTarget21setTargetNearestEnemyERKN2mm3mtl9FixedListIPNS_12GfSearchInfoELm256EEE");
        LOAD(NearestBattle, "_ZN2gf12GfGameTarget27setTargetNearestEnemyBattleERKN2mm3mtl9FixedListIPNS_12GfSearchInfoELm256EEE");
        LOAD(Cycle, "_ZN2gf12GfGameTarget12selectTargetERKN2mm3mtl9FixedListIPNS_12GfSearchInfoELm256EEE");
        LOAD(SetTarget, "_ZN2gf2pc9StateUtil9setTargetERNS_15GfComBehaviorPcEPNS_13GF_OBJ_HANDLEE");
        LOAD(Enabled, "_ZN2gf13GfGameManager25isEnableFieldBattleTargetEv");
        LOAD(CommonWindow, "_ZN2gf13GfMenuManager18isOpenCommonWindowEv");
        LOAD(Pause, "_ZNK3btl12BattleGlobal7IsPauseEi");
        LOAD(AutoOperate, "_ZNK3btl12BattleGlobal19IsPlayerAutoOperateEv");
        LOAD(ChainProc, "_ZNK3btl18ChainAttackManager6IsProcEb");
        LOAD(Destroyed, "_ZN2gf9GfGameObj16isDestroyGameObjEPNS_13GF_OBJ_HANDLEE");
        std::uintptr_t address{};
        for (auto* symbol : {UpdateSymbol, CleanupSymbol, MessageSymbol}) ok = Resolve(address, symbol) && ok;
#undef LOAD
        if (!ok) return;
        TargetUpdate::HookAt(UpdateSymbol);
        TargetCleanup::HookAt(CleanupSymbol);
        TargetMessage::HookAt(MessageSymbol);
        Installed = TargetUpdate::HasApplied() && TargetCleanup::HasApplied() && TargetMessage::HasApplied();
        if (Installed) Installed = InitializeBattleEntry();
        ethernet::core::g_Logger->LogInfo("EtherNet independent targeting: {}", Installed ? "installed" : "failed");
    }
    void OnSceneTransition() override { Reset(); }
    void OnMapChange(unsigned short) override { Reset(); }
};
ETHERNET_REGISTER_MODULE(BattleTargets);
}

gf::GF_OBJ_HANDLE* GetPlayerTarget(unsigned player) {
    if (!Installed) return None();
    auto* handle = LivePlayer(player);
    if (!Valid(handle)) return None();
    if (const auto* actor = GetBoundBattleActor(player);
        actor && (Read<std::uint64_t>(actor, 0xefd) & (1ull << 16)))
        return Read<Handle>(actor, 0x120);
    return PropertyTarget(handle);
}
bool IsPlayerEngaged(unsigned player) {
    const auto* actor = GetBoundBattleActor(player);
    return actor && (Read<std::uint64_t>(actor, 0xefd) & (1ull << 16));
}
bool IsPlayerTwoTargetShifted() { return Installed && Shifted; }
}
