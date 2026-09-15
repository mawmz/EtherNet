#include <ethernet/core/UpdatableModule.hpp>

#include <cstdint>
#include <cstring>

#include <skylaunch/hookng/Hooks.hpp>

#include <ethernet/LocalPlayers.hpp>
#include <ethernet/PlayerFieldAudio.hpp>
#include <ethernet/core/Logger.hpp>
#include <engine/xc2/gf/Manager.hpp>

namespace ethernet {
namespace {

// XC2 2.1.0 ParamObj is two words; its first word is the actor handle.
struct ParamObjView {
    gf::GF_OBJ_HANDLE* handle;
    std::uint64_t attachment;
};
static_assert(sizeof(ParamObjView) == 0x10);

enum class VoiceCall : std::uint8_t {
    None,
    Jump,
    HighLanding,
};

struct OverrideContext {
    gf::GF_OBJ_HANDLE* handle{};
    VoiceCall call{VoiceCall::None};
};

// Field-state voice creation is synchronous on the game thread. Preserve the
// previous context so a nested native call cannot leak or erase its caller.
OverrideContext CurrentOverride{};
std::uintptr_t JumpAddress{};
std::uintptr_t HighLandingAddress{};
using CreateFallDeadVoice = void* (*)(const ParamObjView*);
CreateFallDeadVoice FallDeadVoice{};

template<class T> T Read(const void* p, std::size_t offset) {
    T value;
    std::memcpy(&value, static_cast<const char*>(p) + offset, sizeof(value));
    return value;
}

gf::GF_OBJ_HANDLE* AccessActor{};
gf::GF_OBJ_HANDLE* CollectionVoiceActor{};
struct CollectionOwner {
    void* command{};
    gf::GF_OBJ_HANDLE* actor{};
    std::uint64_t generation{};
} PendingCollection;
using CreateActorVoice = void* (*)(const ParamObjView*, unsigned, unsigned);
CreateActorVoice ActorVoice{};

struct CollectionAccess : skylaunch::hook::Trampoline<CollectionAccess> {
    static void Hook(void* state, void* behavior, void* previousState) {
        const auto previous = AccessActor;
        const auto object = Read<void*>(behavior, 8);
        AccessActor = object ? Read<gf::GF_OBJ_HANDLE*>(object, 0xf8) : nullptr;
        // Native enter synchronously sends NotifyAction to the gimmick, whose
        // collection branch constructs the queued GfPlayComCollect here.
        Orig(state, behavior, previousState);
        AccessActor = previous;
    }
};
struct CollectionCreate : skylaunch::hook::Trampoline<CollectionCreate> {
    static void* Hook(gf::GF_OBJ_HANDLE* point) {
        auto* command = Orig(point);
        // Native permits one live collection command. Capture its actor, not
        // the point handle stored at command+0x20 or the current prompt owner.
        PendingCollection = {command, IsPlayerTwoBound(AccessActor) ? AccessActor : nullptr,
                             PlayerBindingGeneration()};
        return command;
    }
};
struct CollectionStart : skylaunch::hook::Trampoline<CollectionStart> {
    static void Hook(void* command) {
        const auto previous = CollectionVoiceActor;
        CollectionVoiceActor = PendingCollection.command == command &&
            PendingCollection.generation == PlayerBindingGeneration() &&
            IsPlayerTwoBound(PendingCollection.actor) ? PendingCollection.actor : nullptr;
        PendingCollection = {};
        // start calls the default voice overload with CV_FIELD=4, CV_PRIO=1.
        Orig(command);
        CollectionVoiceActor = previous;
    }
};
struct CollectionVoice : skylaunch::hook::Trampoline<CollectionVoice> {
    static void* Hook(unsigned type, unsigned priority) {
        if (CollectionVoiceActor && type == 4 && priority == 1) {
            const ParamObjView actor{CollectionVoiceActor, 0};
            return ActorVoice(&actor, type, priority);
        }
        return Orig(type, priority);
    }
};

void InstallCollectionVoice() {
    constexpr const char* symbols[] = {
        "_ZN2gf2pc16StateFieldAccess5enterEPNS_15GfComBehaviorPcEPN2ai5StateE",
        "_ZN2gf16GfPlayComCollect6createEPNS_13GF_OBJ_HANDLEE",
        "_ZN2gf16GfPlayComCollect5startEv",
        "_ZN2gf16SCOM_FIELD_VOICE6createENS_8CV_FIELDENS_7CV_PRIOE",
        "_ZN2gf16SCOM_FIELD_VOICE6createERKNS_8ParamObjENS_8CV_FIELDENS_7CV_PRIOE"
    };
    for (const auto* symbol : symbols) {
        const auto address = skylaunch::hook::detail::ResolveSymbolBase(symbol);
        if (!address || address == skylaunch::hook::INVALID_FUNCTION_PTR) {
            ethernet::core::g_Logger->LogError("EtherNet collection voice: missing {}", symbol);
            return;
        }
    }
    ActorVoice = skylaunch::hook::detail::ResolveSymbol<CreateActorVoice>(symbols[4]);
    CollectionAccess::HookAt(symbols[0]);
    CollectionCreate::HookAt(symbols[1]);
    CollectionStart::HookAt(symbols[2]);
    CollectionVoice::HookAt(symbols[3]);
    if (!CollectionAccess::HasApplied() || !CollectionCreate::HasApplied() ||
        !CollectionStart::HasApplied() || !CollectionVoice::HasApplied())
        ethernet::core::g_Logger->LogError("EtherNet collection voice: hook installation failed");
}

class ScopedOverride {
public:
    ScopedOverride(gf::GF_OBJ_HANDLE* handle, VoiceCall call)
        : previous(CurrentOverride) {
        CurrentOverride = {handle, call};
    }

    ~ScopedOverride() {
        CurrentOverride = previous;
    }

private:
    OverrideContext previous;
};

bool IsP2FieldVoice(const ParamObjView* param) {
    return param && IsPlayerTwoBound(param->handle) &&
        gf::GfGameManager::isField() && gf::GfGameManager::isControlFree();
}

bool IsGatedVoiceCall(std::uintptr_t returnAddress) {
    // These are the return addresses immediately after each routine's first
    // getControlMover call: the actor-equality gate. No other global
    // getControlMover caller observes the override.
    return (CurrentOverride.call == VoiceCall::Jump &&
            returnAddress == JumpAddress + 0x1c) ||
        (CurrentOverride.call == VoiceCall::HighLanding &&
            returnAddress == HighLandingAddress + 0x18);
}

struct ControlMoverHook : skylaunch::hook::Trampoline<ControlMoverHook> {
    static gf::GF_OBJ_HANDLE* Hook() {
        const auto returnAddress = reinterpret_cast<std::uintptr_t>(
            __builtin_return_address(0)
        );
        if(CurrentOverride.handle && IsGatedVoiceCall(returnAddress))
            return CurrentOverride.handle;
        return Orig();
    }
};

struct JumpVoiceHook : skylaunch::hook::Trampoline<JumpVoiceHook> {
    static void* Hook(const ParamObjView* param) {
        if(!IsP2FieldVoice(param))
            return Orig(param);
        ScopedOverride override(param->handle, VoiceCall::Jump);
        return Orig(param);
    }
};

struct HighLandingVoiceHook : skylaunch::hook::Trampoline<HighLandingVoiceHook> {
    static void* Hook(const ParamObjView* param) {
        if(!IsP2FieldVoice(param))
            return Orig(param);
        ScopedOverride override(param->handle, VoiceCall::HighLanding);
        return Orig(param);
    }
};

constexpr const char* ControlMoverSymbol =
    "_ZN2gf13GfGameManager15getControlMoverEv";
constexpr const char* JumpVoiceSymbol =
    "_ZN2gf16SCOM_FIELD_VOICE10createJumpERKNS_8ParamObjE";
constexpr const char* HighLandingVoiceSymbol =
    "_ZN2gf16SCOM_FIELD_VOICE17createHighLandingERKNS_8ParamObjE";
constexpr const char* FallDeadVoiceSymbol =
    "_ZN2gf16SCOM_FIELD_VOICE14createFallDeadERKNS_8ParamObjE";

struct PlayerFieldAudio : ethernet::core::UpdatableModule {
    void Initialize() override {
        UpdatableModule::Initialize();

        if(ethernet::core::version::RuntimeGame() != ethernet::core::version::GameType::BF2) {
            ethernet::core::g_Logger->LogWarning(
                "EtherNet P2 field audio requires XC2; leaving retail voice routing."
            );
            return;
        }

        const auto controlMover =
            skylaunch::hook::detail::ResolveSymbolBase(ControlMoverSymbol);
        JumpAddress = skylaunch::hook::detail::ResolveSymbolBase(JumpVoiceSymbol);
        HighLandingAddress =
            skylaunch::hook::detail::ResolveSymbolBase(HighLandingVoiceSymbol);
        const auto fallDeadAddress =
            skylaunch::hook::detail::ResolveSymbolBase(FallDeadVoiceSymbol);
        if(fallDeadAddress &&
            fallDeadAddress != skylaunch::hook::INVALID_FUNCTION_PTR) {
            FallDeadVoice = reinterpret_cast<CreateFallDeadVoice>(fallDeadAddress);
        } else {
            ethernet::core::g_Logger->LogError(
                "EtherNet field fall-death voice symbol unavailable"
            );
        }
        for(const auto address : {controlMover, JumpAddress, HighLandingAddress}) {
            if(!address || address == skylaunch::hook::INVALID_FUNCTION_PTR) {
                JumpAddress = HighLandingAddress = 0;
                ethernet::core::g_Logger->LogError(
                    "EtherNet P2 field audio: required native symbol unavailable"
                );
                return;
            }
        }

        ControlMoverHook::HookAt(controlMover);
        JumpVoiceHook::HookAt(JumpAddress);
        HighLandingVoiceHook::HookAt(HighLandingAddress);
        if(!ControlMoverHook::HasApplied() || !JumpVoiceHook::HasApplied() ||
            !HighLandingVoiceHook::HasApplied()) {
            ethernet::core::g_Logger->LogError(
                "EtherNet P2 field audio: native hook installation failed"
            );
            return;
        }

        ethernet::core::g_Logger->LogInfo(
            "EtherNet P2 field audio: native jump and high-landing voices enabled"
        );
        InstallCollectionVoice();
    }
    void OnSceneTransition() override { PendingCollection = {}; }
    void OnMapChange(unsigned short) override { PendingCollection = {}; }
};

ETHERNET_REGISTER_MODULE(PlayerFieldAudio);

} // namespace

bool PlayFallDeadVoice(gf::GF_OBJ_HANDLE* handle) {
    if(!handle || handle == reinterpret_cast<gf::GF_OBJ_HANDLE*>(-1) || !FallDeadVoice)
        return false;

    const ParamObjView param{handle, 0};
    return FallDeadVoice(&param) != nullptr;
}

} // namespace ethernet
