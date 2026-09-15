#include <ethernet/core/UpdatableModule.hpp>
#include <ethernet/camera/MenuCamera.hpp>
#include <ethernet/PartnerMovement.hpp>
#include <ethernet/FieldRecovery.hpp>
#include <engine/xc2/gf/Manager.hpp>
#include <engine/xc2/gf/Party.hpp>

#include <cstddef>
#include <array>
#include <cstdint>
#include <cstring>

namespace ethernet {
namespace {
// XC2 2.1.0 native paths recovered in ui-pad-native.txt,
// menu-talk-control-native.txt, and field-menu-stop-native.txt:
//   * UIUtil::getPadData() delegates to the game's player-pad provider.
//   * GfGamePad::update() runs its menu-action PadRelay on control pad 0,
//     then copies that pad's complete 0x48-byte PadData to this provider.
// Preserve those retail consumers and add pad 1 to the same native path.
struct PadData {
    std::uint32_t words[18];
};
static_assert(sizeof(PadData) == 0x48);
// DevPadNx::updateTpad translates nn::hid ZL (bit 8) into FW bit 6.
// PadData is already converted: 0x100 here is Minus, not raw HID ZL.
constexpr std::uint32_t NativeZL = 0x40;
// GfGamePad::update tests this held mask before advancing its native death-code
// counter. Keep its threshold and game-condition checks entirely native.
constexpr std::uint32_t NativeEmergencyEscape = 0x40f8;

PadData SharedMenuPad{};
std::array<PadData,2> RecoveringPads{};
bool RouteNativeMenuInput{};
int InteractionOwner = -1;
bool Installed{};
bool RouteFieldBlades{};
gf::GF_OBJ_HANDLE* BladeRequestDriver{}; // Synchronous selection call only.
std::uintptr_t BladeRequestAddress{};
using BladeRequestFn = bool(*)(void*);
BladeRequestFn RequestBlade{};
void** GameSceneSlot{};

// Temporary per-press trace: requestFieldBladeChange returns true even when
// createResetBladeForBladeSwitch refuses to enqueue the physical switch.
struct BladeTrace {
    bool selectedDriver{};
    int party = -1;
    int current = -1;
    int target = -1;
    bool targetExists{};
    bool queued{};
};
BladeTrace* ActiveBladeTrace{};
using BladeSetFn = int(*)(unsigned, int);
using BladeHandleFn = gf::GF_OBJ_HANDLE*(*)(unsigned, int);
BladeSetFn GetBladeSet{};
BladeHandleFn GetBladeHandle{};
using ValidHandleFn = bool(*)(gf::GF_OBJ_HANDLE*);
ValidHandleFn IsValidHandle{};

float Axis(const PadData& pad, std::size_t word) {
    float value{};
    std::memcpy(&value, &pad.words[word], sizeof(value));
    return value;
}

void MergeStick(PadData& destination, const PadData& source,
                std::size_t xWord, std::size_t yWord) {
    const float sourceX = Axis(source, xWord);
    const float sourceY = Axis(source, yWord);
    const float destinationX = Axis(destination, xWord);
    const float destinationY = Axis(destination, yWord);
    if (sourceX * sourceX + sourceY * sourceY >
        destinationX * destinationX + destinationY * destinationY) {
        destination.words[xWord] = source.words[xWord];
        destination.words[yWord] = source.words[yWord];
    }
}

void MergePads(PadData& destination, const PadData& playerTwo) {
    // PadManager::makePadData establishes held, trigger, release, and double
    // in words 0, 1, 2, and 4; updatePadData supplies repeat in word 3.
    // Merge those exact native button states so the retail handler retains its
    // own edge and repeat behavior.
    for (std::size_t word = 0; word <= 4; ++word)
        destination.words[word] |= playerTwo.words[word];

    // The adapter writes the two native stick pairs to words 7-10. A single UI
    // focus cannot consume two opposing vectors, so use the stronger vector for
    // each stick while leaving the provider's thresholds/metadata untouched.
    MergeStick(destination, playerTwo, 7, 8);
    MergeStick(destination, playerTwo, 9, 10);
}

struct ControlPad : skylaunch::hook::Trampoline<ControlPad> {
    static const PadData* ForPlayer(int pad) {
        const auto* original = Orig(pad);
        if (!original || pad < 0 || pad > 1 || !IsFieldPlayerRecovering(pad)) return original;
        auto& muted = RecoveringPads[pad];
        muted = *original;
        for (unsigned word=0;word<=4;++word) muted.words[word] = 0;
        for (unsigned word=7;word<=10;++word) muted.words[word] = 0;
        return &muted;
    }
    static const PadData* Hook(int pad) {
        const auto* original = ForPlayer(pad);
        if (!Installed || !RouteNativeMenuInput || pad != 0 || !original)
            return original;

        // Shops/dialogue capture their initiator only while the other actor
        // continues field gameplay. Feed that pad through the SAME retail
        // relay/provider; do not merge the moving partner's buttons or sticks.
        if (InteractionOwner >= 0) return ForPlayer(InteractionOwner);

        const auto* playerTwo = ForPlayer(1);
        SharedMenuPad = *original;
        if (playerTwo) MergePads(SharedMenuPad, *playerTwo);
        // Field ZL is dispatched separately below for its actual controller.
        // Do not also publish it to the leader-only Blade UI listener. Other
        // menu buttons and ZL in retail menus/battle remain native.
        if (RouteFieldBlades)
            // The Blade listener reads trigger/repeat, not held. Removing held
            // ZL also breaks GfGamePad's emergency-escape combination.
            for (unsigned word=1;word<=4;++word) SharedMenuPad.words[word] &= ~NativeZL;
        return &SharedMenuPad;
    }
};

struct MenuButton : skylaunch::hook::Trampoline<MenuButton> {
    static void Hook() {
        // Native menu permission checks precede this GfEvent call.
        camera::PreserveMenuEntryShot();
        Orig();
    }
};

struct BladeLeader : skylaunch::hook::Trampoline<BladeLeader> {
    static gf::GF_OBJ_HANDLE* Hook() {
        const auto caller = reinterpret_cast<std::uintptr_t>(__builtin_return_address(0));
        // Exactly requestFieldBladeChange's actor-selection call, not any
        // camera, party, script or unrelated native leader query.
        if (BladeRequestDriver && caller == BladeRequestAddress + 0x44) {
            if (ActiveBladeTrace) ActiveBladeTrace->selectedDriver = true;
            return BladeRequestDriver;
        }
        return Orig();
    }
};

struct BladeQueue : skylaunch::hook::Trampoline<BladeQueue> {
    static bool Hook(unsigned party, int set) {
        auto* trace = ActiveBladeTrace;
        if (trace) {
            trace->party = static_cast<int>(party);
            trace->current = GetBladeSet(party, -1);
            trace->target = set;
            const auto blade = GetBladeHandle(party, set);
            trace->targetExists = blade &&
                blade != reinterpret_cast<gf::GF_OBJ_HANDLE*>(-1) &&
                IsValidHandle(blade);
        }
        const bool queued = Orig(party, set);
        if (trace) trace->queued = queued;
        return queued;
    }
};

void SwitchPlayerBlade(unsigned player) {
    if (!GameSceneSlot || !*GameSceneSlot || IsFieldPlayerRecovering(player) ||
        !ethernet::core::HidInput::GetPlayer(player+1)->padConnected) return;
    // Resolve the current controlled character on every press, including after
    // a lineup change. No old setup-time binding or fixed Driver identity.
    const auto driver = gf::GfGameParty::getHandleMover(player);
    if (!driver || driver == reinterpret_cast<gf::GF_OBJ_HANDLE*>(-1) ||
        !gf::GfObjUtil::getObj(driver)) return;
    const auto previous = BladeRequestDriver;
    const auto previousTrace = ActiveBladeTrace;
    BladeTrace trace{};
    ActiveBladeTrace = &trace;
    BladeRequestDriver = driver;
    // The same native function for BOTH players: native permission checks,
    // this Driver's equipped-Blade selection, and the ordinary summon queue.
    const bool accepted = RequestBlade(*GameSceneSlot);
    BladeRequestDriver = previous;
    ActiveBladeTrace = previousTrace;
    ethernet::core::g_Logger->LogInfo(
        "ZL P{}: accepted={} actor={} party={} set={}->{} object={} queued={}",
        player+1, accepted, trace.selectedDriver, trace.party,
        trace.current, trace.target, trace.targetExists, trace.queued);
}

struct GamePadUpdate : skylaunch::hook::Trampoline<GamePadUpdate> {
    static void Hook(const fw::UpdateInfo& update) {
        // The original runs its pad-0 relay once, then copies the same pad into
        // the native getPadPlayer provider. Scope aggregation to that function
        // so PadField movement continues reading separate logical pads.
        const bool previousRouting = RouteNativeMenuInput;
        const int previousOwner = InteractionOwner;
        const bool previousBlades = RouteFieldBlades;
        InteractionOwner = PartnerInteractionOwner();
        RouteFieldBlades = Installed && InteractionOwner < 0 && GameSceneSlot && *GameSceneSlot &&
            !gf::GfManager::isGameTypeIra() && !ethernet::core::IsSceneTransitionActive() &&
            gf::GfGameManager::isField() &&
            gf::GfGameManager::isControlFree() && !gf::GfGameManager::isBattle();
        // Read each unmerged native trigger, already muted for recovery. A tap
        // is handled once; no UI owner token survives to a later frame.
        const auto* p1 = ControlPad::ForPlayer(0);
        const auto* p2 = ControlPad::ForPlayer(1);
        const bool switchP1 = RouteFieldBlades && p1 && (p1->words[1] & NativeZL) &&
            (p1->words[0] & NativeEmergencyEscape) != NativeEmergencyEscape;
        const bool switchP2 = RouteFieldBlades && p2 && (p2->words[1] & NativeZL) &&
            (p2->words[0] & NativeEmergencyEscape) != NativeEmergencyEscape;
        RouteNativeMenuInput = Installed;
        const bool previousEscape = SetNativeEmergencyEscapeContext(true);
        Orig(update);
        SetNativeEmergencyEscapeContext(previousEscape);
        RouteNativeMenuInput = previousRouting;
        InteractionOwner = previousOwner;
        RouteFieldBlades = previousBlades;
        // P1 wins simultaneous requests, matching the previous input policy.
        if (switchP1) SwitchPlayerBlade(0);
        else if (switchP2) SwitchPlayerBlade(1);
    }
};

constexpr const char* ControlPadSymbol = "_ZN2fw10PadManager17getControlPadDataEi";
constexpr const char* GamePadUpdateSymbol = "_ZN2gf9GfGamePad6updateERKN2fw10UpdateInfoE";
constexpr const char* MenuButtonSymbol = "_ZN2gf7GfEvent16callOpenMeinMenuEv";
constexpr const char* BladeRequestSymbol = "_ZN2gf11GfGameScene23requestFieldBladeChangeEv";
constexpr const char* BladeLeaderSymbol = "_ZN2gf13GfGameManager20getPartyLeaderDriverEv";
constexpr const char* SceneSymbol = "_ZZN2mm3mtl12PtrSingletonIN2gf11GfGameSceneEE3sysEvE10s_instance";
constexpr const char* BladeQueueSymbol = "_ZN2gf13GfPlayFactory30createResetBladeForBladeSwitchEji";
constexpr const char* BladeSetSymbol = "_ZN2gf11GfGameParty13getBladeSetIdEji";
constexpr const char* BladeHandleSymbol = "_ZN2gf11GfGameParty14getHandleBladeEji";
constexpr const char* ValidHandleSymbol = "_ZN2gf9GfObjUtil7isValidEPNS_13GF_OBJ_HANDLEE";
}

struct PlayerMenus : ethernet::core::UpdatableModule {
    void Initialize() override {
        UpdatableModule::Initialize();
        if (ethernet::core::version::RuntimeGame() != ethernet::core::version::GameType::BF2) return;
        const auto runtime = ethernet::core::version::RuntimeVersion();
        if (runtime.major != 2 || runtime.minor != 1 || runtime.patch != 0) return;

        for (const auto* name : {ControlPadSymbol, GamePadUpdateSymbol, MenuButtonSymbol,
                                BladeRequestSymbol,BladeLeaderSymbol,SceneSymbol,
                                BladeQueueSymbol,BladeSetSymbol,BladeHandleSymbol,ValidHandleSymbol}) {
            const auto address = skylaunch::hook::detail::ResolveSymbolBase(name);
            if (!address || address == skylaunch::hook::INVALID_FUNCTION_PTR) {
                ethernet::core::g_Logger->LogError("EtherNet menu: missing {}", name);
                return;
            }
        }

        BladeRequestAddress = skylaunch::hook::detail::ResolveSymbolBase(BladeRequestSymbol);
        RequestBlade = skylaunch::hook::detail::ResolveSymbol<BladeRequestFn>(BladeRequestSymbol);
        GameSceneSlot = skylaunch::hook::detail::ResolveSymbol<void**>(SceneSymbol);
        GetBladeSet = skylaunch::hook::detail::ResolveSymbol<BladeSetFn>(BladeSetSymbol);
        GetBladeHandle = skylaunch::hook::detail::ResolveSymbol<BladeHandleFn>(BladeHandleSymbol);
        IsValidHandle = skylaunch::hook::detail::ResolveSymbol<ValidHandleFn>(ValidHandleSymbol);
        BladeQueue::HookAt(BladeQueueSymbol);
        BladeLeader::HookAt(BladeLeaderSymbol);
        ControlPad::HookAt(ControlPadSymbol);
        MenuButton::HookAt(MenuButtonSymbol);
        GamePadUpdate::HookAt(GamePadUpdateSymbol);
        Installed = ControlPad::HasApplied() && MenuButton::HasApplied() &&
            GamePadUpdate::HasApplied() && BladeLeader::HasApplied() && BladeQueue::HasApplied();
        ethernet::core::g_Logger->LogInfo(
            "EtherNet native shared menu input: {}", Installed ? "installed" : "failed");
    }
};
ETHERNET_REGISTER_MODULE(PlayerMenus);
}
