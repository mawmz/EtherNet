#include "main.hpp"
#include "FunctionalHooks.hpp"
#include <ethernet/core/Version.hpp>
#include <helpers/InputHelper.h>
#include <nn/diag.h>


namespace ethernet::core {

void fmt_assert_failed(const char*, int, const char* message) {
    NN_DIAG_LOG(nn::diag::LogSeverity::Fatal, "fmtlib assert: %s", message);
}

void update(fw::UpdateInfo* updateInfo) {
    // Match the proven EtherNet loop: poll after the retail frame, then
    // select controller 2 for ImGui when connected (otherwise controller 1).
    HidInput::GetPlayer(1)->Poll();
    HidInput::GetPlayer(2)->Poll();
    auto* menuInput = HidInput::GetDebugInput();
    if (menuInput->InputDownStrict(Keybind::MENU_TOGGLE)) g_Menu->Toggle();
    UpdateAllRegisteredModules(updateInfo);
    InputHelper::toggleInput = g_Menu->IsOpen();
    if (g_Menu->IsOpen()) g_Menu->Update(menuInput);
    g_Logger->Draw(updateInfo);
}
void main() {
    g_Logger->SetLoggingDisabled(false);
    g_Logger->SetLoggingLevel(GetState().config.loggingLevel);
    g_Menu->Initialize();
    InitializeAllRegisteredModules();
    FunctionalHooks::Hook();
    g_Logger->LogInfo("{} ready. Open menu with L+R+ZL+ZR.", version::EtherNetVersion());
}
}
