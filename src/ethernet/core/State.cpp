#include <ethernet/core/State.hpp>
#include <ethernet/core/UpdatableModule.hpp>
#include <algorithm>

namespace ethernet::core {
void Config::Reset() { *this = Config{}; }
void Config::LoadFromFile() {
    Reset();
    auto parsed = toml::parse_file(ETHERNET_CONFIG_PATH "/config.toml");
    if (!parsed) {
        g_Logger->LogWarning("Config unavailable; using standalone defaults.");
        return;
    }
    const auto& table = parsed.table();
    port = static_cast<uint16_t>(std::clamp(table["port"].value_or(6969), 1, 65535));
    loggingLevel = static_cast<Logger::Severity>(std::clamp(table["loggingLevel"].value_or(1), 0, 4));
    menuTheme = static_cast<Menu::Theme>(std::clamp(table["menuTheme"].value_or(8), 0, 8));
    if (const auto* camera = table["sharedCamera"].as_table()) {
        sharedCamera.playerOneWeight = (*camera)["playerOneWeight"].value_or(sharedCamera.playerOneWeight);
        sharedCamera.safeMargin = (*camera)["safeMargin"].value_or(sharedCamera.safeMargin);
        sharedCamera.anchorSeconds = (*camera)["anchorSeconds"].value_or(sharedCamera.anchorSeconds);
        sharedCamera.expandSeconds = (*camera)["expandSeconds"].value_or(sharedCamera.expandSeconds);
        sharedCamera.contractSeconds = (*camera)["contractSeconds"].value_or(sharedCamera.contractSeconds);
        sharedCamera = ethernet::camera::Sanitize(sharedCamera);
    }
    if (auto* fonts = table["menuFonts"].as_array()) {
        for (const auto& entry : *fonts) {
            const auto* font = entry.as_array();
            if (!font || font->size() != 2) continue;
            auto path = (*font)[0].value<std::string>();
            auto size = (*font)[1].value<double>();
            if (path && size && *size > 0 && *size <= 96)
                menuFonts[*path] = static_cast<float>(*size);
        }
    }
    g_Logger->SetLoggingLevel(loggingLevel);
    if (ImGui::GetCurrentContext()) g_Menu->SetTheme(menuTheme);
    ConfigUpdateForAllRegisteredModules();
}
EtherNetState& GetState() { static EtherNetState state; return state; }
void EtherNetState::ReloadConfig() { GetState().config.LoadFromFile(); }
bool IsSceneTransitionActive() { return GetState().sceneTransitionActive; }
bool IsSceneTransitionCompletionArmed() { return GetState().sceneTransitionCompletionArmed; }
void BeginSceneTransition() {
    auto& state = GetState();
    if (state.sceneTransitionActive) return;
    state.sceneTransitionActive = true;
    state.sceneTransitionCompletionArmed = false;
    SceneTransitionForAllRegisteredModules();
}
void ArmSceneTransitionCompletion() {
    if (GetState().sceneTransitionActive) GetState().sceneTransitionCompletionArmed = true;
}
void EndSceneTransition() {
    auto& state = GetState();
    if (!state.sceneTransitionActive || !state.sceneTransitionCompletionArmed) return;
    state.sceneTransitionActive = false;
    state.sceneTransitionCompletionArmed = false;
}
}
