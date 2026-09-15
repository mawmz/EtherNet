#include <ethernet/core/UpdatableModule.hpp>
#include <ethernet/BattleEntry.hpp>
#include <ethernet/BattleTargets.hpp>
#include <ethernet/LocalPlayers.hpp>
#include <engine/xc2/gf/Manager.hpp>
#include <engine/xc2/gf/Party.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace ethernet {
namespace {
using Handle = gf::GF_OBJ_HANDLE*;

constexpr std::size_t PadPropertyOffset = 0x78;
constexpr std::size_t ComponentObjectOffset = 0x8;
constexpr std::size_t ObjectHandleOffset = 0xf8;
constexpr std::size_t InputBitsOffset = 0x68;
constexpr unsigned BattleStartInput = 5;
constexpr std::uint32_t BattleEndInputs = (1u << 6) | (1u << 9);

constexpr const char* ManagerSlotSymbol =
    "_ZZN2mm3mtl12PtrSingletonIN3btl13BattleManagerEE3sysEvE10s_instance";
constexpr const char* LookupSymbol =
    "_ZNK3btl16CharacterManager17GetCharacterConstEPN2gf13GF_OBJ_HANDLEE";
constexpr const char* CharacterManagerSlotSymbol =
    "_ZZN2mm3mtl12PtrSingletonIN3btl16CharacterManagerEE3sysEvE10s_instance";
constexpr const char* GlobalSlotSymbol =
    "_ZZN2mm3mtl12PtrSingletonIN3btl12BattleGlobalEE3sysEvE10s_instance";
constexpr const char* ChallengeSlotSymbol =
    "_ZZN2mm3mtl12PtrSingletonIN3btl22ChallengeBattleManagerEE3sysEvE10s_instance";
constexpr const char* ChallengeExitSymbol =
    "_ZNK3btl22ChallengeBattleManager6IsExitEv";
constexpr const char* ChainSlotSymbol =
    "_ZZN2mm3mtl12PtrSingletonIN3btl18ChainAttackManagerEE3sysEvE10s_instance";
constexpr const char* ChainProcSymbol = "_ZNK3btl18ChainAttackManager6IsProcEb";
constexpr const char* CheckInSymbol =
    "_ZN3btl13BattleManager22CheckInBattleCharacterEPN2gf13GF_OBJ_HANDLEEb";
constexpr const char* PartyCheckInSymbol = "_ZN3btl13BattleManager13CheckInBattleEv";
constexpr const char* ExitSymbol =
    "_ZN3btl15BattleCharacter13SetExitBattleEbb";
constexpr const char* ActionUpdateSymbol =
    "_ZN2gf2pc21StateBattleActionBase6UpdateEPN3btl15BattleCharacterEPNS_15GfComBehaviorPcERKN2fw10UpdateInfoE";
constexpr const char* StartSymbol = "_ZNK2gf2pc9PadBattle11battleStartEv";
constexpr const char* EndSymbol = "_ZNK2gf2pc9PadBattle9battleEndEv";
constexpr const char* ArtSymbols[] = {
    "_ZNK2gf2pc9PadBattle5art01Ev",
    "_ZNK2gf2pc9PadBattle5art02Ev",
    "_ZNK2gf2pc9PadBattle5art03Ev"
};
constexpr const char* FieldRequestSymbol =
    "_ZN2gf2pc14StateUtilField18requestBattleStartERNS_15GfComPropertyPcE";

using LookupFn = void*(*)(void*, Handle);
using CheckInFn = void(*)(void*, Handle, bool);
using ChallengeExitFn = bool(*)(void*);
using ChainProcFn = bool(*)(void*, bool);

void** ManagerSlot{};
void** CharacterManagerSlot{};
void** GlobalSlot{};
void** ChallengeSlot{};
void** ChainSlot{};
LookupFn Lookup{};
CheckInFn CheckIn{};
ChallengeExitFn ChallengeExit{};
ChainProcFn ChainProc{};
std::uintptr_t AcceptedExitReturn{};
std::uintptr_t FollowerCheckInReturn{};
bool Installed{};

struct AcceptedInput {
    Handle handle{};
    void* object{};
    void* property{};
    std::uint64_t generation{};
    std::uint32_t bits{};
};
AcceptedInput PendingP2Start{};
std::array<AcceptedInput, 2> PendingExit{};

template<class T> T Read(const void* base, std::size_t offset) {
    T value{};
    std::memcpy(&value, static_cast<const std::uint8_t*>(base) + offset, sizeof(value));
    return value;
}

Handle None() { return reinterpret_cast<Handle>(-1); }
bool Valid(Handle handle) { return handle && handle != None(); }

Handle PadOwner(void* pad) {
    auto* property = pad ? Read<void*>(pad, PadPropertyOffset) : nullptr;
    auto* object = property ? Read<void*>(property, ComponentObjectOffset) : nullptr;
    return object ? Read<Handle>(object, ObjectHandleOffset) : None();
}

bool IsCurrentPlayer(unsigned player, Handle handle) {
    return Valid(handle) && gf::GfGameParty::getHandleMover(player) == handle &&
        gf::GfObjUtil::getObj(handle) && (player != 1 || IsPlayerTwoBound(handle));
}

AcceptedInput Capture(void* pad, Handle owner, std::uint32_t bits) {
    AcceptedInput result{};
    auto* property = pad ? Read<void*>(pad, PadPropertyOffset) : nullptr;
    auto* object = Valid(owner) ? gf::GfObjUtil::getObj(owner) : nullptr;
    if (property && object && Read<void*>(object, 0x60) == property) {
        result = {owner, object, property, PlayerBindingGeneration(), bits};
    }
    return result;
}

bool Live(const AcceptedInput& input) {
    return Valid(input.handle) && input.generation == PlayerBindingGeneration() &&
        gf::GfObjUtil::getObj(input.handle) == input.object && input.object &&
        Read<void*>(input.object, 0x60) == input.property;
}

void* Actor(Handle handle) {
    return Valid(handle) && CharacterManagerSlot && *CharacterManagerSlot
        ? Lookup(*CharacterManagerSlot, handle) : nullptr;
}

bool ChallengeAllowsInput() {
    return ChallengeSlot && *ChallengeSlot && !ChallengeExit(*ChallengeSlot);
}

std::uint32_t BattleEndInput(bool playerTwo) {
    if (playerTwo) return IsPlayerTwoTargetShifted() ? (1u << 6) : (1u << 9);
    if (!GlobalSlot || !*GlobalSlot) return 0;
    return (Read<std::uint32_t>(*GlobalSlot, 0x838d) & 0x100) ? (1u << 6) : (1u << 9);
}

struct BattleStart : skylaunch::hook::Trampoline<BattleStart> {
    static void Hook(void* pad) {
        auto* property = pad ? Read<void*>(pad, PadPropertyOffset) : nullptr;
        const auto before = property ? Read<std::uint32_t>(property, InputBitsOffset) : 0;
        Orig(pad);
        auto* owner = PadOwner(pad);
        auto after = property ? Read<std::uint32_t>(property, InputBitsOffset) : 0;
        // In shared battle the stock callback rejects any non-active actor at
        // its final identity comparison. Extend only that identity case for
        // bound P2; the PadRelay event and Challenge guard remain native.
        if (!(after & (1u << BattleStartInput)) && IsCurrentPlayer(1, owner) &&
            ChallengeAllowsInput()) {
            after |= 1u << BattleStartInput;
            std::memcpy(static_cast<std::uint8_t*>(property) + InputBitsOffset,
                &after, sizeof(after));
        }
        // Orig is the native Challenge/chain/active-actor gate. Remember only
        // a start pulse it actually accepted for the current bound P2.
        if (!(before & (1u << BattleStartInput)) && (after & (1u << BattleStartInput)) &&
            IsCurrentPlayer(1, owner)) {
            PendingP2Start = Capture(pad, owner, 1u << BattleStartInput);
        }
    }
};

struct BattleEnd : skylaunch::hook::Trampoline<BattleEnd> {
    static void Hook(void* pad) {
        auto* property = pad ? Read<void*>(pad, PadPropertyOffset) : nullptr;
        const auto before = property ? Read<std::uint32_t>(property, InputBitsOffset) : 0;
        Orig(pad);
        auto after = property ? Read<std::uint32_t>(property, InputBitsOffset) : 0;
        auto* owner = PadOwner(pad);
        const bool playerTwo = IsCurrentPlayer(1, owner);
        const auto emitted = (after & ~before) & BattleEndInputs;
        const auto endInput = BattleEndInput(playerTwo);
        if (endInput && playerTwo && ChallengeAllowsInput()) {
            // Replace P1's global long-hold mode with P2's equivalent local
            // mode, including when Orig accepted the callback but chose the
            // other action ID.
            after = (after & ~emitted) | endInput;
            std::memcpy(static_cast<std::uint8_t*>(property) + InputBitsOffset,
                &after, sizeof(after));
        }
        const auto accepted = (after & ~before) & BattleEndInputs;
        if (!accepted) return;

        if (IsCurrentPlayer(0, owner)) PendingExit[0] = Capture(pad, owner, accepted);
        else if (IsCurrentPlayer(1, owner)) PendingExit[1] = Capture(pad, owner, accepted);
    }
};

// Native art01/02/03 (ELF 0x61be44/0x61bf2c/0x61c014) only gate and
// emit semantic input bits. P2 needs its own identity and R modifier here;
// the native BattleCharacter still decides whether an Art can execute.
template<unsigned Normal, unsigned Shifted>
struct ArtInput : skylaunch::hook::Trampoline<ArtInput<Normal, Shifted>> {
    static void Hook(void* pad) {
        auto* property = pad ? Read<void*>(pad, PadPropertyOffset) : nullptr;
        auto* owner = PadOwner(pad);
        const auto before = property ? Read<std::uint32_t>(property, InputBitsOffset) : 0;
        const bool playerTwo = IsCurrentPlayer(1, owner);
        // Serialized chain input retains the retail handler and its routing.
        if (playerTwo && ChainSlot && *ChainSlot && !ChainProc(*ChainSlot, false)) {
            if (ChallengeAllowsInput()) {
                const auto input = IsPlayerTwoTargetShifted() ? Shifted : Normal;
                const auto after = before | (1u << input);
                std::memcpy(static_cast<std::uint8_t*>(property) + InputBitsOffset,
                    &after, sizeof(after));
            }
        } else {
            ArtInput::Orig(pad);
        }
        // R+B is art03's shifted action (ID 6), not merely battleEnd.
        // Remember either player's accepted pulse for the existing local
        // sheath adapter, without changing ordinary input buffering.
        if (Normal == 9 && property) {
            const auto accepted = (Read<std::uint32_t>(property, InputBitsOffset) & ~before)
                & BattleEndInputs;
            if (accepted) {
                if (IsCurrentPlayer(0, owner)) PendingExit[0] = Capture(pad, owner, accepted);
                else if (playerTwo) PendingExit[1] = Capture(pad, owner, accepted);
            }
        }
    }
};
using ArtOne = ArtInput<7, 13>;
using ArtTwo = ArtInput<8, 22>;
using ArtThree = ArtInput<9, 6>;

struct CheckInCharacter : skylaunch::hook::Trampoline<CheckInCharacter> {
    static void Hook(void* manager, Handle handle, bool force) {
        const auto caller = reinterpret_cast<std::uintptr_t>(__builtin_return_address(0));
        // CheckInBattle+0x124 automatically enlists every non-leader Driver
        // while the leader is engaged. P2 chooses when to draw independently.
        // Leave own input, Blade admission, status-driven and scripted entry
        // on their original paths, as well as every AI teammate.
        if (caller == FollowerCheckInReturn && !force && IsCurrentPlayer(1, handle)) return;
        Orig(manager, handle, force);
    }
};

struct FieldBattleRequest : skylaunch::hook::Trampoline<FieldBattleRequest> {
    static bool Hook(void* property) {
        auto* object = property ? Read<void*>(property, 8) : nullptr;
        auto* owner = object ? Read<Handle>(object, 0xf8) : None();
        if (!IsCurrentPlayer(1, owner)) return Orig(property);
        // StateFieldOnGround has already passed Utility::IsBattleStart here.
        // Native 0x6321b0's mode-2 fallback queues a *leader* battle request;
        // do not allow P2's field state to enqueue that request alongside ours.
        PendingP2Start = {};
        if (!Valid(GetPlayerTarget(1)) || !ManagerSlot || !*ManagerSlot || !Actor(owner)) return false;
        CheckIn(*ManagerSlot, owner, false);
        return true;
    }
};

struct SetExitBattle : skylaunch::hook::Trampoline<SetExitBattle> {
    static void Hook(void* actor, bool exit, bool shared) {
        const auto caller = reinterpret_cast<std::uintptr_t>(__builtin_return_address(0));
        for (unsigned player = 0; player < PendingExit.size(); ++player) {
            const auto accepted = PendingExit[player];
            if (!(exit && shared && caller == AcceptedExitReturn && Live(accepted) &&
                Actor(accepted.handle) == actor &&
                (Read<std::uint32_t>(accepted.property, InputBitsOffset) & accepted.bits))) continue;
            PendingExit[player] = {};
            const bool p2Exit = IsCurrentPlayer(1, accepted.handle);
            auto* partnerHandle = gf::GfGameParty::getHandleMover(player ^ 1);
            auto* partner = IsCurrentPlayer(player ^ 1, partnerHandle) ? Actor(partnerHandle) : nullptr;
            const auto flags = partner ? Read<std::uint64_t>(partner, 0xefd) : 0;
            // SetExitBattle's native bit29 marks an accepted exit. If both
            // sheath this frame, the second request may close the encounter.
            const bool partnerContinuing = (flags & (1ull << 16)) && !(flags & (1ull << 29));
            if (partnerContinuing) {
                // This is the native StateBattleActionBase input-consumption
                // callsite. Keep its status/weapon decision, but do not raise
                // BattleGlobal's shared-exit flag while another player fights.
                if (p2Exit) PendingP2Start = {};
                Orig(actor, exit, false);
                return;
            }
        }
        Orig(actor, exit, shared);
    }
};

template<class T> bool Resolve(T& destination, const char* symbol) {
    const auto address = skylaunch::hook::detail::ResolveSymbolBase(symbol);
    if (!address || address == skylaunch::hook::INVALID_FUNCTION_PTR) {
        ethernet::core::g_Logger->LogError("EtherNet battle entry: missing {}", symbol);
        return false;
    }
    destination = reinterpret_cast<T>(address);
    return true;
}
}

bool InitializeBattleEntry() {
    if (Installed) return true;
    bool ok = true;
    ok = Resolve(ManagerSlot, ManagerSlotSymbol) && ok;
    ok = Resolve(CharacterManagerSlot, CharacterManagerSlotSymbol) && ok;
    ok = Resolve(GlobalSlot, GlobalSlotSymbol) && ok;
    ok = Resolve(ChallengeSlot, ChallengeSlotSymbol) && ok;
    ok = Resolve(ChainSlot, ChainSlotSymbol) && ok;
    ok = Resolve(ChainProc, ChainProcSymbol) && ok;
    ok = Resolve(Lookup, LookupSymbol) && ok;
    ok = Resolve(CheckIn, CheckInSymbol) && ok;
    ok = Resolve(ChallengeExit, ChallengeExitSymbol) && ok;
    std::uintptr_t address{};
    ok = Resolve(address, StartSymbol) && ok;
    ok = Resolve(address, EndSymbol) && ok;
    for (auto* symbol : ArtSymbols) ok = Resolve(address, symbol) && ok;
    ok = Resolve(address, FieldRequestSymbol) && ok;
    ok = Resolve(address, ExitSymbol) && ok;
    std::uintptr_t partyCheckIn{};
    ok = Resolve(partyCheckIn, PartyCheckInSymbol) && ok;
    if (partyCheckIn) {
        FollowerCheckInReturn = partyCheckIn + 0x128;
        const auto callsite = FollowerCheckInReturn - 4;
        const auto instruction = Read<std::uint32_t>(reinterpret_cast<void*>(callsite), 0);
        std::int64_t displacement = instruction & 0x03ffffff;
        if (displacement & 0x02000000) displacement -= 0x04000000;
        const auto target = static_cast<std::uintptr_t>(
            static_cast<std::int64_t>(callsite) + displacement * 4);
        if ((instruction & 0xfc000000) != 0x94000000 ||
            target != skylaunch::hook::detail::ResolveSymbolBase(CheckInSymbol)) {
            ethernet::core::g_Logger->LogError("EtherNet battle entry: unsupported native follower check-in callsite");
            ok = false;
        }
    }
    const auto actionUpdate = skylaunch::hook::detail::ResolveSymbolBase(ActionUpdateSymbol);
    if (!actionUpdate || actionUpdate == skylaunch::hook::INVALID_FUNCTION_PTR) {
        ethernet::core::g_Logger->LogError("EtherNet battle entry: missing {}", ActionUpdateSymbol);
        ok = false;
    } else {
        // Effective 1.5.1: BL SetExitBattle at Update+0x2f8 returns here.
        AcceptedExitReturn = actionUpdate + 0x2fc;
        const auto callsite = AcceptedExitReturn - 4;
        const auto instruction = Read<std::uint32_t>(reinterpret_cast<void*>(callsite), 0);
        std::int64_t displacement = instruction & 0x03ffffff;
        if (displacement & 0x02000000) displacement -= 0x04000000;
        const auto target = static_cast<std::uintptr_t>(
            static_cast<std::int64_t>(callsite) + displacement * 4);
        if ((instruction & 0xfc000000) != 0x94000000 ||
            target != skylaunch::hook::detail::ResolveSymbolBase(ExitSymbol)) {
            ethernet::core::g_Logger->LogError(
                "EtherNet battle entry: unsupported native sheath callsite");
            ok = false;
        }
    }
    if (!ok) return false;
    BattleStart::HookAt(StartSymbol);
    BattleEnd::HookAt(EndSymbol);
    ArtOne::HookAt(ArtSymbols[0]);
    ArtTwo::HookAt(ArtSymbols[1]);
    ArtThree::HookAt(ArtSymbols[2]);
    FieldBattleRequest::HookAt(FieldRequestSymbol);
    SetExitBattle::HookAt(ExitSymbol);
    CheckInCharacter::HookAt(CheckInSymbol);
    Installed = BattleStart::HasApplied() && BattleEnd::HasApplied() &&
        SetExitBattle::HasApplied() && FieldBattleRequest::HasApplied() &&
        ArtOne::HasApplied() && ArtTwo::HasApplied() && ArtThree::HasApplied() &&
        CheckInCharacter::HasApplied();
    ethernet::core::g_Logger->LogInfo("EtherNet battle entry: {}", Installed ? "installed" : "failed");
    return Installed;
}

void ProcessPlayerTwoBattleEntry() {
    if (!Installed) return;
    const auto accepted = PendingP2Start;
    PendingP2Start = {};
    auto* player = gf::GfGameParty::getHandleMover(1);
    if (!Live(accepted) || accepted.handle != player ||
        !(Read<std::uint32_t>(accepted.property, InputBitsOffset) & accepted.bits) ||
        !IsCurrentPlayer(1, player) || !Valid(GetPlayerTarget(1)) ||
        !ManagerSlot || !*ManagerSlot || !Actor(player)) return;
    // force=false preserves CheckInBattleCharacter's native property/status,
    // slide, BladeChange and ChainAttack eligibility checks. It dispatches the
    // stock zero-condition MsgPcBattleStart directly to this actor.
    CheckIn(*ManagerSlot, player, false);
}

void ResetBattleEntry() {
    PendingP2Start = {};
    PendingExit = {};
}
}
