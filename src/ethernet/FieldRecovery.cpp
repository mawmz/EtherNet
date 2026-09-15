#include <ethernet/core/UpdatableModule.hpp>
#include <ethernet/FieldRecovery.hpp>
#include <ethernet/RecoveryTimer.hpp>
#include <ethernet/PlayerFieldAudio.hpp>
#include <ethernet/Notifications.hpp>
#include <ethernet/LocalPlayers.hpp>
#include <ethernet/PartnerMovement.hpp>
#include <engine/xc2/gf/Manager.hpp>
#include <engine/xc2/gf/Party.hpp>
#include <engine/xc2/gf/PlayerController.hpp>
#include <array>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace ethernet {
namespace {
using Handle = gf::GF_OBJ_HANDLE*;
template<class T> T Read(const void* p, std::size_t offset) {
    T value; std::memcpy(&value,static_cast<const char*>(p)+offset,sizeof(value)); return value;
}
template<class T> void Write(void* p, std::size_t offset, const T& value) {
    std::memcpy(static_cast<char*>(p)+offset,&value,sizeof(value));
}
bool Valid(Handle h) { return h && h != reinterpret_cast<Handle>(-1); }
Handle Player(unsigned slot) {
    return slot ? gf::GfGameParty::getHandleMover(1) : gf::GfGameManager::getControlMover();
}
Handle Actor(void* component) {
    auto* object = component ? Read<void*>(component,8) : nullptr;
    return object ? Read<Handle>(object,0xf8) : nullptr;
}
struct Recovery {
    Handle handle{};
    void* object{};
    void* behavior{};
    void* property{};
    std::uint64_t generation{};
    RecoveryTimer timer;
    fw::Transform safe{}, candidate{}, destination{};
    float sampleSeconds{};
    unsigned health{};
    bool safeValid{}, candidateValid{}, fieldDisplayed{}, startedThisFrame{};
};
std::array<Recovery,2> Players{};
bool Installed{};
bool NativeWipeActive{};
bool NativeEmergencyEscapeContext{};
bool NativeEmergencyWipe{};
bool BroadcastingEmergencyDeath{};
bool EmergencyPartnerDispatch{};
unsigned LastDeathType{};
using EventFn = bool(*)(bool);
using WarpFn = bool(*)(void*,void*,const fw::Transform&);
using ResetFn = void(*)(void*,void*);
using SetHpFn = void(*)(Handle,unsigned);
using DynamicsFn = void(*)(void*);
using PlayerDeadFn = std::uintptr_t(*)(unsigned);
using FallDeadFn = void(*)(Handle);
EventFn IsEvent{};
WarpFn WarpExternal{};
ResetFn OnWarpNormal{};
ResetFn OnWarpBattle{};
SetHpFn SetHp{};
DynamicsFn ResetMovement{};
PlayerDeadFn CreatePlayerDead{};
FallDeadFn NativeFallDead{};

bool SameObject(const Recovery& r) {
    return Valid(r.handle) && gf::GfObjUtil::getObj(r.handle) == r.object;
}
void Clear(Recovery& r) {
    // Release only the display channel we own, and never touch a replaced actor.
    if (r.timer.active && SameObject(r)) {
        gf::GfObjAcc access(r.handle);
        access.setDisp(gf::OBJDISP::Field,r.fieldDisplayed);
    }
    r = {};
}
Recovery* Find(Handle actor) {
    if (!Installed || !Valid(actor)) return nullptr;
    for (unsigned i=0;i<Players.size();++i) {
        auto& r = Players[i];
        if (r.handle != actor) continue;
        if (Player(i) != actor || r.generation != PlayerBindingGeneration() ||
            !SameObject(r) || (i && !IsPlayerTwoBound(actor))) {
            Clear(r); return nullptr;
        }
        return &r;
    }
    return nullptr;
}
bool FieldRunning() {
    return (gf::GfGameManager::isField() && gf::GfGameManager::isControlFree()) ||
        IsPartnerFieldActive();
}
bool CanStart() {
    return Installed && !NativeWipeActive && !ethernet::core::IsSceneTransitionActive() &&
        ethernet::core::version::RuntimeGame() != ethernet::core::version::GameType::IRA &&
        !gf::GfGameManager::isBattle() &&
        FieldRunning() && (!IsEvent(false) || IsPartnerFieldActive()) &&
        Valid(Player(0)) && Player(0) != Player(1) && IsPlayerTwoBound(Player(1)) &&
        ethernet::core::HidInput::GetPlayer(1)->padConnected &&
        ethernet::core::HidInput::GetPlayer(2)->padConnected;
}
bool ReadGroundedTransform(const Recovery& r, fw::Transform& transform) {
    const unsigned flags = Read<unsigned>(r.property,0x110);
    const auto* generic = gf::GfObjUtil::getProperty(r.handle);
    const auto* movement = generic ? Read<void*>(generic,0x18) : nullptr;
    // GfComPropertyPc::flush derives bit 4 from native ground collision.
    // Do not copy a partner's airborne, submerged, attached or pending-warp pose.
    if (!(flags & 0x10) || (flags & ((1u<<5)|(1u<<6)|(1u<<9)|(1u<<11)|(1u<<12)|(1u<<26))) ||
        Read<unsigned>(r.property,0x114) != 0 || !movement ||
        Read<std::uintptr_t>(movement,0xc0) != ~std::uintptr_t{}) return false;
    transform = Read<fw::Transform>(r.property,0x160);
    const auto values = Read<std::array<float,8>>(&transform,0);
    for (unsigned i : {0u,1u,2u,4u,5u,6u,7u})
        if (!std::isfinite(values[i])) return false;
    return true;
}
bool PartnerDestination(const Recovery& fallen, fw::Transform& destination) {
    // Only the other controlled slot, never an AI follower. Resolve at expiry,
    // so five seconds of survivor movement are reflected in the return point.
    auto* partner = Find(Player(fallen.handle == Player(0) ? 1 : 0));
    if (!partner || partner->timer.active) return false;
    auto* hp = Read<void*>(partner->property,0x10);
    if (!hp || !Read<unsigned>(hp,0x14)) return false;
    if (ReadGroundedTransform(*partner,destination)) return true;
    if (!partner->safeValid) return false;
    destination = partner->safe;
    return true;
}
void RestoreHealth(Recovery& r) {
    // onDead's callers have already set native HP to zero. Keep native party-
    // death polling from seeing a dead leader; the penalty is our timer alone.
    auto* hp = Read<void*>(r.property,0x10);
    if (!hp) return;
    const unsigned maximum = Read<unsigned>(hp,0x10);
    SetHp(r.handle,std::min(maximum,std::max(1u,r.health)));
}
bool Begin(Handle actor, const void* message) {
    // Death-code setup must run the native HFSM/flags even when a transition
    // prevents a fade. Those retained flags are needed for zombie hover.
    if (NativeWipeActive || NativeEmergencyEscapeContext) return false;
    auto* r = Find(actor);
    if (!r) return false;
    if (r->timer.active) { RestoreHealth(*r); return true; }
    // Match BehaviorPc's existing-death guard before starting local recovery.
    // Native fallout (type 4) may repeat; other notifications must not turn an
    // already handled native death state into a fresh five-second respawn.
    if ((Read<unsigned>(r->property,0x110) & (1u<<11)) &&
        Read<unsigned>(message,8) != 4) return false;
    if (!CanStart()) return false;
    r->destination = r->safe;
    if (!PartnerDestination(*r,r->destination) && !r->safeValid) return false;
    r->timer.Begin();
    LastDeathType = Read<unsigned>(message,8);
    r->startedThisFrame = true;
    RestoreHealth(*r);
    Write<std::uint64_t>(r->property,0x68,0);
    Write<std::array<float,3>>(r->property,0x80,{});
    Write<std::uint64_t>(r->property,0x90,0);
    if (auto* dynamics = Read<void*>(r->object,0x78)) ResetMovement(dynamics);
    gf::GfObjAcc access(actor);
    r->fieldDisplayed = access.isDisp(gf::OBJDISP::Field);
    // Retail only screams for fallout (GfPlayerDead 4), but incorrectly uses
    // getControlMover. The native voice routine accepts the actual actor.
    if (Read<unsigned>(message,8) == 4) PlayFallDeadVoice(actor);
    access.setDisp(gf::OBJDISP::Field,false);
    NotifyFieldRespawn(actor);
    return true;
}

// Intercept BEFORE native onDead forwards to the behavior/HFSM, not at the
// later fade command. No party gauge reset, battle notification or global play.
struct PropertyDead : skylaunch::hook::Trampoline<PropertyDead> {
    static void Hook(void* property, void* message) {
        const auto actor = Actor(property);
        if (NativeEmergencyEscapeContext && !BroadcastingEmergencyDeath &&
            Read<unsigned>(message,8) == 4 && (actor == Player(0) || actor == Player(1))) {
            BroadcastingEmergencyDeath = true;
            Orig(property,message); // Native HP, HFSM, fall flags and fade decision.
            const auto partner = Player(actor == Player(0) ? 1 : 0);
            if (Valid(partner) && partner != actor && gf::GfObjUtil::getObj(partner)) {
                EmergencyPartnerDispatch = true;
                NativeFallDead(partner); // Same native message, not just setHp(0).
                EmergencyPartnerDispatch = false;
            }
            BroadcastingEmergencyDeath = false;
            return;
        }
        if (!Begin(Actor(property),message)) Orig(property,message);
    }
};
struct NativeDeathRequest : skylaunch::hook::Trampoline<NativeDeathRequest> {
    static std::uintptr_t Hook(unsigned type) {
        // Both actors run their native death handlers; only the original
        // death-code recipient decides the shared fade. Never enqueue twice.
        if (NativeEmergencyEscapeContext && EmergencyPartnerDispatch) return 0;
        const auto accepted = Orig(type);
        if (NativeEmergencyEscapeContext && accepted) {
            NativeWipeActive = true;
            NativeEmergencyWipe = true;
            for (auto& r : Players) Clear(r);
        }
        return accepted;
    }
};
struct BehaviorDead : skylaunch::hook::Trampoline<BehaviorDead> {
    static void Hook(void* behavior, void* sender, void* message) {
        if (!Begin(Actor(behavior),message)) Orig(behavior,sender,message);
    }
};
struct NotifyDead : skylaunch::hook::Trampoline<NotifyDead> {
    static void Hook(void* behavior, void* message, bool sequence) {
        // FallIntoColiPlugin directly invokes this, without a property message.
        if (!Begin(Actor(behavior),message)) Orig(behavior,message,sequence);
    }
};
struct Ground : skylaunch::hook::Trampoline<Ground> {
    static void Hook(void* behavior) {
        Orig(behavior); // Includes fatal landing detection; do not cache that landing.
        auto* r = Find(Actor(behavior));
        if (!r || r->timer.active || !CanStart()) return;
        fw::Transform transform;
        if (!ReadGroundedTransform(*r,transform)) {
            r->candidateValid = false; r->sampleSeconds = 0; return;
        }
        if (!r->safeValid) { r->safe = transform; r->safeValid = true; }
        // Keep a slightly older confirmed ground sample, not the lip of a fall.
        if (!r->candidateValid || r->sampleSeconds >= 0.25f) {
            if (r->candidateValid) r->safe = r->candidate;
            r->candidate = transform; r->candidateValid = true; r->sampleSeconds = 0;
        }
    }
};
struct Integrate : skylaunch::hook::Trampoline<Integrate> {
    static void Hook(void* behavior, const fw::UpdateInfo& update) {
        // Death can arrive inside notifyGround/plugins during this same update.
        // Stop its remaining integration too, not just next frame's PadAgent.
        if (!IsFieldRecovering(Actor(behavior))) Orig(behavior,update);
    }
};
struct NativeDeathEnd : skylaunch::hook::Trampoline<NativeDeathEnd> {
    static void Hook(void* command) {
        Orig(command);
        NativeWipeActive = false;
        NativeEmergencyWipe = false;
    }
};
struct WipeFallVoice : skylaunch::hook::Trampoline<WipeFallVoice> {
    static void* Hook(const void* param) {
        if (!NativeWipeActive || !param || Read<Handle>(param,0) != Player(0))
            return Orig(param);
        // Individual recoveries already played both voices. An emergency wipe
        // instead keeps retail's voice timing and adds the partner's own voice.
        if (!NativeEmergencyWipe) return nullptr;
        auto* result = Orig(param);
        const auto partner = Player(1);
        if (Valid(partner) && partner != Player(0) && gf::GfObjUtil::getObj(partner))
            PlayFallDeadVoice(partner);
        return result;
    }
};

constexpr const char* PropertyDeadSymbol = "_ZN2gf13GfComProperty6onDeadERNS_19MsgObjectNotifyDeadE";
constexpr const char* BehaviorDeadSymbol = "_ZN2gf15GfComBehaviorPc23procMsgObjectNotifyDeadEPN2fw13MessageObjectERNS_19MsgObjectNotifyDeadE";
constexpr const char* NotifyDeadSymbol = "_ZN2gf2pc9StateUtil10notifyDeadERNS_15GfComBehaviorPcERNS_19MsgObjectNotifyDeadEb";
constexpr const char* GroundSymbol = "_ZN2gf15GfComBehaviorPc12notifyGroundEv";
constexpr const char* IntegrateSymbol = "_ZN2gf15GfComBehaviorPc13integrateMoveERKN2fw10UpdateInfoE";
constexpr const char* WarpSymbol = "_ZN2gf15GfComBehaviorPc12warpExternalERNS_15GfComPropertyPcERKN2fw9TransformE";
constexpr const char* ResetSymbol = "_ZN2gf15GfComBehaviorPc12onWarpNormalERNS_15GfComPropertyPcE";
constexpr const char* BattleResetSymbol = "_ZN2gf15GfComBehaviorPc12onWarpBattleERNS_15GfComPropertyPcE";
constexpr const char* HpSymbol = "_ZN2gf10GfGameUtil5setHpEPNS_13GF_OBJ_HANDLEEj";
constexpr const char* EventSymbol = "_ZN2gf13GfGameManager7isEventEb";
constexpr const char* DynamicsSymbol = "_ZN2gf13GfComDynamics13resetMovementEv";
constexpr const char* PlayerDeadSymbol = "_ZN2gf13GfPlayFactory16createPlayerDeadENS_12GfPlayerDeadE";
constexpr const char* NativeFallDeadSymbol = "_ZN2gf10GfGameUtil13debugFallDeadEPNS_13GF_OBJ_HANDLEE";
constexpr const char* DeathEndSymbol = "_ZN2gf19GfPlayComPlayerDead3endEv";
constexpr const char* FallVoiceSymbol = "_ZN2gf16SCOM_FIELD_VOICE14createFallDeadERKNS_8ParamObjE";
}

bool IsFieldRecovering(Handle actor) {
    auto* r = Find(actor); return r && r->timer.active;
}
bool IsFieldPlayerRecovering(unsigned slot) {
    return slot < 2 && IsFieldRecovering(Player(slot));
}
bool SetNativeEmergencyEscapeContext(bool active) {
    const bool previous = NativeEmergencyEscapeContext;
    NativeEmergencyEscapeContext = active;
    return previous;
}
bool UpdateFieldRecovery(void* behavior, const fw::UpdateInfo& update) {
    const auto actor = Actor(behavior);
    auto* r = Find(actor);
    if (!r && CanStart()) {
        for (unsigned i=0;i<Players.size();++i) if (Player(i) == actor) {
            auto* object = Read<void*>(behavior,8);
            auto* property = Read<gf::GfComPropertyPc*>(object,0x60);
            if (!property || !property->getRTTI()->isKindOf(&gf::GfComPropertyPc::m_rtti) ||
                (Read<unsigned>(property,0x110)&0x401) != 0x401) return false;
            Clear(Players[i]); r = &Players[i];
            r->handle = actor; r->object = object; r->behavior = behavior;
            r->property = property; r->generation = PlayerBindingGeneration();
            break;
        }
    }
    if (!r) return false;
    if (!r->timer.active) {
        auto* hp = Read<void*>(r->property,0x10);
        if (hp && Read<unsigned>(hp,0x14)) r->health = Read<unsigned>(hp,0x14);
        if (std::isfinite(update.updateDelta) && update.updateDelta > 0)
            r->sampleSeconds += update.updateDelta;
        return false;
    }
    // The module advances the timer independently of actor visibility/update
    // scheduling. This hook only suspends the fallen actor's native controller.
    return true;
}

struct FieldRecovery : ethernet::core::UpdatableModule {
    void Initialize() override {
        UpdatableModule::Initialize();
        if (ethernet::core::version::RuntimeGame() != ethernet::core::version::GameType::BF2) return;
        for (const auto* name : {PropertyDeadSymbol,BehaviorDeadSymbol,NotifyDeadSymbol,
             GroundSymbol,IntegrateSymbol,WarpSymbol,ResetSymbol,BattleResetSymbol,HpSymbol,EventSymbol,DynamicsSymbol,
             PlayerDeadSymbol,NativeFallDeadSymbol,DeathEndSymbol,FallVoiceSymbol}) {
            const auto a = skylaunch::hook::detail::ResolveSymbolBase(name);
            if (!a || a == skylaunch::hook::INVALID_FUNCTION_PTR) {
                ethernet::core::g_Logger->LogError("EtherNet recovery: missing {}",name); return;
            }
        }
        WarpExternal = skylaunch::hook::detail::ResolveSymbol<WarpFn>(WarpSymbol);
        OnWarpNormal = skylaunch::hook::detail::ResolveSymbol<ResetFn>(ResetSymbol);
        OnWarpBattle = skylaunch::hook::detail::ResolveSymbol<ResetFn>(BattleResetSymbol);
        SetHp = skylaunch::hook::detail::ResolveSymbol<SetHpFn>(HpSymbol);
        IsEvent = skylaunch::hook::detail::ResolveSymbol<EventFn>(EventSymbol);
        ResetMovement = skylaunch::hook::detail::ResolveSymbol<DynamicsFn>(DynamicsSymbol);
        CreatePlayerDead = skylaunch::hook::detail::ResolveSymbol<PlayerDeadFn>(PlayerDeadSymbol);
        NativeFallDead = skylaunch::hook::detail::ResolveSymbol<FallDeadFn>(NativeFallDeadSymbol);
        NativeDeathRequest::HookAt(PlayerDeadSymbol);
        PropertyDead::HookAt(PropertyDeadSymbol); BehaviorDead::HookAt(BehaviorDeadSymbol);
        NotifyDead::HookAt(NotifyDeadSymbol); Ground::HookAt(GroundSymbol);
        Integrate::HookAt(IntegrateSymbol);
        NativeDeathEnd::HookAt(DeathEndSymbol);
        WipeFallVoice::HookAt(FallVoiceSymbol);
        Installed = PropertyDead::HasApplied() && BehaviorDead::HasApplied() &&
            NotifyDead::HasApplied() && Ground::HasApplied() && Integrate::HasApplied() &&
            NativeDeathEnd::HasApplied() && WipeFallVoice::HasApplied() && NativeDeathRequest::HasApplied();
        ethernet::core::g_Logger->LogInfo("EtherNet five-second field recovery: {}",Installed ? "installed" : "failed");
    }
    bool NeedsUpdate() const override { return true; }
    void Update(fw::UpdateInfo* update) override {
        if (!Installed || NativeWipeActive) return;
        const bool running = !ethernet::core::IsSceneTransitionActive() &&
            (gf::GfGameManager::isControlFree() || IsPartnerFieldActive()) &&
            (!IsEvent(false) || IsPartnerFieldActive());
        auto* p1 = Find(Player(0));
        auto* p2 = Find(Player(1));
        if (p1 && p2 && BothPlayersRecovering(p1->timer,p2->timer)) {
            // Test before either timer is advanced: a second death on the
            // expiry frame must not be turned into two independent warps.
            if (running) {
                NativeWipeActive = true;
                NativeEmergencyWipe = false;
                if (CreatePlayerDead(LastDeathType)) {
                    // Retail owns the fade, landmark mapjump and party spawn.
                    // Release only our timers/visibility; do not fake a mapjump
                    // or keep the native actors' update functions suspended.
                    for (auto& r : Players) Clear(r);
                } else {
                    // Respect native quest/sequence gates; retain both timers
                    // and retry on a later runnable frame, without local warps.
                    NativeWipeActive = false;
                }
            }
            return;
        }
        for (unsigned slot=0;slot<Players.size();++slot) {
            auto* r = Find(Player(slot));
            if (!r || !r->timer.active) continue;
            const bool first = r->startedThisFrame;
            r->startedThisFrame = false;
            if (first || !r->timer.Advance(update->updateDelta,running)) continue;
            auto destination = r->destination;
            PartnerDestination(*r,destination);
            if (WarpExternal(r->behavior,r->property,destination)) {
                // Native immediate field warp: collision/transform first, then
                // HFSM, dynamics, fall caches and actor alpha. Never a map jump.
                // A survivor can start combat during the wait. Retain the new
                // native battle HFSM instead of forcing it back to field idle.
                if (gf::GfGameManager::isBattle()) OnWarpBattle(r->behavior,r->property);
                else OnWarpNormal(r->behavior,r->property);
                RestoreHealth(*r);
                gf::GfObjAcc access(r->handle);
                access.setDisp(gf::OBJDISP::Field,r->fieldDisplayed);
                r->safe = destination; r->safeValid = true;
                r->timer.Reset(); r->candidateValid = false; r->sampleSeconds = 0;
            }
        }
    }
    void OnSceneTransition() override {
        for (auto& r : Players) Clear(r);
        NativeWipeActive = false;
        NativeEmergencyWipe = false;
    }
    void OnMapChange(unsigned short) override {
        for (auto& r : Players) Clear(r);
        NativeWipeActive = false;
        NativeEmergencyWipe = false;
    }
};
ETHERNET_REGISTER_MODULE(FieldRecovery);
}
