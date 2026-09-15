#include <ethernet/core/UpdatableModule.hpp>
#include <ethernet/LocalPlayers.hpp>
#include <ethernet/PartnerMovement.hpp>
#include <ethernet/InteractionLifetime.hpp>
#include <ethernet/FieldRecovery.hpp>
#include <engine/xc2/gf/Manager.hpp>
#include <engine/xc2/gf/Party.hpp>
#include <array>
#include <cstring>

namespace ethernet {
namespace {
using Handle = gf::GF_OBJ_HANDLE*;
template<class T> T Read(const void* p, std::size_t offset) {
    T value; std::memcpy(&value,static_cast<const char*>(p)+offset,sizeof(value)); return value;
}
template<class T> void Write(void* p, std::size_t offset, T value) {
    std::memcpy(static_cast<char*>(p)+offset,&value,sizeof(value));
}
bool Valid(Handle h) { return h && h != reinterpret_cast<Handle>(-1); }
struct TalkActor {
    Handle handle{};
    void* object{};
    void* state{};
    std::uint64_t generation{};
    InteractionLifetime lifetime{};
};
std::array<TalkActor,2> TalkingActors{};
void* MovingPartner{}; // Synchronous actor-update scope only; never global pause state.
bool Installed{};
using Query = bool(*)();
using EventQuery = bool(*)(bool);
Query IsTutorial{}, IsFullScreen{};
EventQuery IsEvent{};

Handle ActorHandle(void* behavior) {
    const auto object = behavior ? Read<void*>(behavior,8) : nullptr;
    return object ? Read<Handle>(object,0xf8) : nullptr;
}
Handle Player(unsigned slot) {
    return slot ? gf::GfGameParty::getHandleMover(1) : gf::GfGameManager::getControlMover();
}
int PartnerSlot() {
    if (!Installed) return -1;
    // Native isControlFree reads current/reserved field states and player-
    // pause tokens. Do not let a remembered HFSM owner suppress field input
    // after that live native state has resumed, even if no leave/end hook ran.
    const bool controlFree = gf::GfGameManager::isControlFree();
    for (auto& talk : TalkingActors)
        if (talk.state && talk.lifetime.HasFinished(controlFree)) talk = {};
    if (controlFree || ethernet::core::IsSceneTransitionActive() || IsTutorial() ||
        IsFullScreen() || IsEvent(false) || gf::GfGameManager::isBattle() ||
        ethernet::core::version::RuntimeGame() == ethernet::core::version::GameType::IRA ||
        !ethernet::core::HidInput::GetPlayer(1)->padConnected ||
        !ethernet::core::HidInput::GetPlayer(2)->padConnected) return -1;
    const std::array<Handle,2> players{Player(0),Player(1)};
    if (!Valid(players[0]) || !Valid(players[1]) || players[0] == players[1] ||
        !IsPlayerTwoBound(players[1])) return -1;
    int owner = -1;
    for (unsigned i=0;i<2;++i) {
        auto& talk = TalkingActors[i];
        if (talk.state && (talk.handle != players[i] ||
            talk.generation != PlayerBindingGeneration() ||
            gf::GfObjUtil::getObj(talk.handle) != talk.object)) talk = {};
        if (talk.state) {
            if (owner >= 0) return -1; // Both actors entered an interaction.
            owner = static_cast<int>(i);
        }
    }
    return owner < 0 ? -1 : 1-owner;
}

// Native NPC access type 2 selects StateFieldTalk (10), including shops opened
// by its event script. Generic collection/access uses a different state (9).
struct TalkEnter : skylaunch::hook::Trampoline<TalkEnter> {
    static void Hook(void* state, void* behavior, void* previous) {
        const auto actor = ActorHandle(behavior);
        if (Installed && Valid(actor)) {
            for (unsigned i=0;i<2;++i) {
                if (actor == Player(i) && (!i || IsPlayerTwoBound(actor)))
                    TalkingActors[i] = {actor,Read<void*>(behavior,8),state,PlayerBindingGeneration()};
            }
        }
        Orig(state,behavior,previous);
    }
};
struct TalkLeave : skylaunch::hook::Trampoline<TalkLeave> {
    static void Hook(void* state, void* behavior, void* next) {
        // Release before native cleanup can dispatch messages/change the actor.
        for (auto& talk : TalkingActors)
            if (talk.state == state && talk.handle == ActorHandle(behavior)) talk = {};
        Orig(state,behavior,next);
    }
};
struct TalkEnd : skylaunch::hook::Trampoline<TalkEnd> {
    static void Hook(void* command) {
        // Native completion destroys this dialogue's player-pause token and
        // finishes quest callbacks. Also release through the live native
        // field-control check: callbacks alone did not resolve the reported bug.
        Orig(command);
        TalkingActors = {};
    }
};
struct BehaviorUpdate : skylaunch::hook::Trampoline<BehaviorUpdate> {
    static void Hook(void* behavior, const fw::UpdateInfo& update) {
        if (UpdateFieldRecovery(behavior,update)) return;
        const int slot = PartnerSlot();
        const auto previous = MovingPartner;
        MovingPartner = slot >= 0 && ActorHandle(behavior) == Player(static_cast<unsigned>(slot))
            ? behavior : nullptr;
        Orig(behavior,update); // Native PadAgent, HFSM, collision and integration.
        MovingPartner = previous;
    }
};
struct TalkingQuery : skylaunch::hook::Trampoline<TalkingQuery> {
    static bool Hook() {
        // GfComBehaviorPc::update otherwise skips HFSM/integrateMove for EVERY
        // manually controlled actor while talking. Only the partner bypasses it.
        return MovingPartner ? false : Orig();
    }
};
struct StopFlagQuery : skylaunch::hook::Trampoline<StopFlagQuery> {
    static bool Hook(void* scene, unsigned flag) {
        // isGmkTutorialSequenceOK sends ground actors to stop state 61. Exempt
        // only the event-stop reason for this actor; retain other native stops.
        // EvtPlayerStop is scene+0x638; its native PLSTOP id is at +0xc.
        if (MovingPartner && flag == Read<unsigned>(scene,0x644)) return false;
        return Orig(scene,flag);
    }
};
struct PadUpdate : skylaunch::hook::Trampoline<PadUpdate> {
    static void Hook(void* agent, const fw::UpdateInfo& update, void* property) {
        Orig(agent,update,property);
        if (MovingPartner && !gf::GfGameManager::isControlFree() &&
            Read<void*>(property,8) == Read<void*>(MovingPartner,8)) {
            // A shared UI confirmation must not start a second world interaction.
            // Movement/jump remain native, and UI input is not changed here.
            Write<std::uint64_t>(property,0x68,Read<std::uint64_t>(property,0x68)&~std::uint64_t{1});
        }
    }
};

constexpr const char* EnterSymbol = "_ZN2gf2pc14StateFieldTalk5enterEPNS_15GfComBehaviorPcEPN2ai5StateE";
constexpr const char* LeaveSymbol = "_ZN2gf2pc14StateFieldTalk5leaveEPNS_15GfComBehaviorPcEPN2ai5StateE";
constexpr const char* EndSymbol = "_ZN2gf13GfPlayComTalk3endEv";
constexpr const char* UpdateSymbol = "_ZN2gf15GfComBehaviorPc6updateERKN2fw10UpdateInfoE";
constexpr const char* TalkingSymbol = "_ZN2gf13GfGameManager9isTalkingEv";
constexpr const char* StopSymbol = "_ZN2gf11GfGameScene16isPlayerStopFlagENS_6PLSTOPE";
constexpr const char* PadSymbol = "_ZN2gf2pc8PadAgent6updateERKN2fw10UpdateInfoERNS_15GfComPropertyPcE";
constexpr const char* TutorialSymbol = "_ZN2gf13GfGameManager14isExecTutorialEv";
constexpr const char* FullScreenSymbol = "_ZN2gf13GfMenuManager16isOpenFullScreenEv";
constexpr const char* EventSymbol = "_ZN2gf13GfGameManager7isEventEb";
}

bool IsPartnerFieldActive() { return PartnerSlot() >= 0; }
int PartnerInteractionOwner() {
    const int partner = PartnerSlot();
    return partner < 0 ? -1 : 1-partner;
}

struct PartnerMovement : ethernet::core::UpdatableModule {
    void Initialize() override {
        UpdatableModule::Initialize();
        if (ethernet::core::version::RuntimeGame() != ethernet::core::version::GameType::BF2) return;
        for (const auto* name : {EnterSymbol,LeaveSymbol,EndSymbol,UpdateSymbol,TalkingSymbol,StopSymbol,
                                PadSymbol,TutorialSymbol,FullScreenSymbol,EventSymbol}) {
            const auto address = skylaunch::hook::detail::ResolveSymbolBase(name);
            if (!address || address == skylaunch::hook::INVALID_FUNCTION_PTR) {
                ethernet::core::g_Logger->LogError("EtherNet partner movement: missing {}",name); return;
            }
        }
        IsTutorial = skylaunch::hook::detail::ResolveSymbol<Query>(TutorialSymbol);
        IsFullScreen = skylaunch::hook::detail::ResolveSymbol<Query>(FullScreenSymbol);
        IsEvent = skylaunch::hook::detail::ResolveSymbol<EventQuery>(EventSymbol);
        TalkEnter::HookAt(EnterSymbol); TalkLeave::HookAt(LeaveSymbol);
        TalkEnd::HookAt(EndSymbol);
        BehaviorUpdate::HookAt(UpdateSymbol); TalkingQuery::HookAt(TalkingSymbol);
        StopFlagQuery::HookAt(StopSymbol); PadUpdate::HookAt(PadSymbol);
        Installed = TalkEnter::HasApplied() && TalkLeave::HasApplied() && TalkEnd::HasApplied() &&
            BehaviorUpdate::HasApplied() && TalkingQuery::HasApplied() &&
            StopFlagQuery::HasApplied() && PadUpdate::HasApplied();
        ethernet::core::g_Logger->LogInfo("EtherNet NPC/shop partner movement: {}",Installed ? "installed" : "failed");
    }
    void OnSceneTransition() override { TalkingActors = {}; }
    void OnMapChange(unsigned short) override { TalkingActors = {}; }
};
ETHERNET_REGISTER_MODULE(PartnerMovement);
}
