#pragma once
#include <atomic>
#include <toml++/toml.h>
#include <ethernet/core/Logger.hpp>
#include <ethernet/core/menu/Menu.hpp>
#include <ethernet/camera/SharedFraming.hpp>

#define ETHERNET_CONFIG_PATH "sd:/config/EtherNet"

namespace ethernet::core {
struct Config {
    uint16_t port = 6969;
    Logger::Severity loggingLevel = Logger::Severity::Info;
    Menu::Theme menuTheme = Menu::Theme::Comfy;
    std::map<std::string, float> menuFonts;
    ethernet::camera::Settings sharedCamera;
    void Reset();
    void LoadFromFile();
};
struct EtherNetState {
    Config config;
    std::atomic_bool sceneTransitionActive = false;
    std::atomic_bool sceneTransitionCompletionArmed = false;
    EtherNetState() { config.LoadFromFile(); }
    static void ReloadConfig();
};
EtherNetState& GetState();
bool IsSceneTransitionActive();
bool IsSceneTransitionCompletionArmed();
void BeginSceneTransition();
void ArmSceneTransitionCompletion();
void EndSceneTransition();
}
