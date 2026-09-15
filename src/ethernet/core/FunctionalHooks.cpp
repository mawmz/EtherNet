// BF2-only subset of the EtherNet functional hooks. No gameplay modules.
#include "FunctionalHooks.hpp"
#include "main.hpp"
#include <engine/xc2/apps/FrameworkLauncher.hpp>
#include <engine/xc2/fw/Framework.hpp>
#include <engine/xc2/gf/Data.hpp>

namespace {
struct FrameworkUpdate : skylaunch::hook::Trampoline<FrameworkUpdate> {
    static void Hook(apps::FrameworkLauncher* launcher) {
        Orig(launcher);
        auto singleton = skylaunch::hook::detail::ResolveSymbol<fw::Framework**>(
            "_ZZN2mm3mtl12PtrSingletonIN2fw9FrameworkEE3sysEvE10s_instance");
        if (singleton && *singleton) ethernet::core::update((*singleton)->getUpdateInfo());
    }
};
struct NpadStyles : skylaunch::hook::Trampoline<NpadStyles> {
    static void Hook(int) { Orig(7); }
};
struct ControllerSupport : skylaunch::hook::Trampoline<ControllerSupport> {
    static void Hook(nn::hid::ControllerSupportResultInfo* result, nn::hid::ControllerSupportArg* args) {
        nn::hid::NpadFullKeyState state{};
        nn::hid::GetNpadState(&state, 0);
        if (state.mAttributes.isBitSet(nn::hid::NpadAttribute::IsConnected)) args->mSingleMode = false;
        Orig(result, args);
    }
};
struct MapChange : skylaunch::hook::Trampoline<MapChange> {
    static void Hook(uint* request) {
        const auto mapId = request ? gf::GfDataMap::getMapID(*request) : 0;
        ethernet::core::BeginSceneTransition();
        Orig(request);
        ethernet::core::MapChangeForAllRegisteredModules(mapId);
        ethernet::core::ArmSceneTransitionCompletion();
    }
};
}
namespace ethernet::core {
int ClampNumberOfControllers::Hook() { return std::min(Orig(), 1); }

void FunctionalHooks::Hook() {
    FrameworkUpdate::HookAt(&apps::FrameworkLauncher::update);
    NpadStyles::HookAt(&nn::hid::SetSupportedNpadStyleSet);
    ControllerSupport::HookAt(nn::hid::ShowControllerSupport);
    ClampNumberOfControllers::HookAt("_ZN2ml8DevPadNx23getLocalConnectPadCountEv");
    // Leave native controller polling untouched, including while ImGui is open.
    MapChange::HookAt("_ZN2gf12GfReqCommand11execMapjumpEPKNS_10GfReqParamE");
}
}
