// Saves feature imported from XenoModsTAS DebugStuff.cpp and UtilityMenu.cpp.
// Only feature extraction, EtherNet registration, and camera lifecycle glue differ.
#include <ethernet/core/UpdatableModule.hpp>
#include <ethernet/core/ImGuiExtensions.hpp>
#include <engine/xc2/fw/Framework.hpp>
#include <engine/xc2/gf/Manager.hpp>
#include <engine/xc2/ml/Scene.hpp>
#include <engine/xc2/tl/title.hpp>

namespace {
    constexpr std::size_t TitleMenuOffset = 0x198;
    constexpr unsigned int TitleMenuResultContinue = 2;
    constexpr const char* WeatherFrameEndSymbol =
        "_ZN2ml21ScnRenderDrSysParmAcc13weatherFrmEndEv";
    using WeatherFrameEndFn = void (*)(ml::ScnRenderDrSysParmAcc*);
    WeatherFrameEndFn weatherFrameEnd = nullptr;
    bool reloadPrimarySavePending = false;

	struct TitleSkipEventHook : skylaunch::hook::Trampoline<TitleSkipEventHook> {
		static void Hook(void* state, tl::TitleMain* titleMain) {
			if(!reloadPrimarySavePending) {
				Orig(state, titleMain);
				return;
			}

			// playTitleEvent() blocks while the title background event runs,
			// then performs these two renderer cleanup operations. Preserve
			// that cleanup without constructing or displaying the title scene.
			fw::RenderParam::resetScene();
			if(weatherFrameEnd != nullptr) {
				ml::ScnRenderDrSysParmAcc renderParams;
				weatherFrameEnd(&renderParams);
			}
			ethernet::core::g_Logger->LogInfo(
				"[Reload save] Skipped title event and reset its render state"
			);
		}
	};

	struct TitleSkipMenuHook : skylaunch::hook::Trampoline<TitleSkipMenuHook> {
		static bool Hook(void* state, tl::TitleMain* titleMain, bool unk) {
			if(reloadPrimarySavePending)
				return false;

			return Orig(state, titleMain, unk);
		}
	};

	struct TitleContinueHook : skylaunch::hook::Trampoline<TitleContinueHook> {
		static bool Hook(void* state, tl::TitleMain* titleMain) {
			if(reloadPrimarySavePending && titleMain != nullptr) {
				*reinterpret_cast<unsigned int*>(
					reinterpret_cast<std::uintptr_t>(titleMain)
					+ TitleMenuOffset
				) = TitleMenuResultContinue;
				reloadPrimarySavePending = false;
				ethernet::core::g_Logger->LogInfo(
					"[Reload save] Skipped title menu and submitted Continue"
				);

				// Returning false sends TitleStateMainScreen into its native
				// menu-destroy/Continue transition. The required title event
				// and render-scene synchronization have already completed.
				return false;
			}

			return Orig(state, titleMain);
		}
	};


}

namespace ethernet::core {
struct SaveLoader : UpdatableModule {
    bool waitingForReload = false;
    static bool LoadSaveOnStartup;
    static void ReloadSave();
    static void ReturnTitle(unsigned int slot = -1);
    void Initialize() override;
    bool NeedsUpdate() const override { return true; }
    bool UpdatesDuringSceneTransition() const override { return true; }
    void OnSceneTransition() override {
        waitingForReload = reloadPrimarySavePending;
    }
    void Update(fw::UpdateInfo*) override {
        // EtherNet-only observer: release the shared-camera transition after
        // the imported loader has completed. Never gates a save action.
        if (!waitingForReload || !IsSceneTransitionActive() || reloadPrimarySavePending ||
            !gf::GfGameManager::isField() || !gf::GfGameManager::isControlFree()) return;
        auto* mover = gf::GfGameManager::getControlMover();
        if (mover && mover != reinterpret_cast<gf::GF_OBJ_HANDLE*>(-1) &&
            gf::GfObjUtil::getObj(mover)) {
            ArmSceneTransitionCompletion();
            EndSceneTransition();
            waitingForReload = false;
        }
    }
};
bool SaveLoader::LoadSaveOnStartup = false;

void SaveLoader::ReturnTitle(unsigned int slot) {
    tl::TitleMain::returnTitle((gf::SAVESLOT)slot);
}

	void SaveLoader::ReloadSave() {
#if ETHERNET_OLD_ENGINE
		reloadPrimarySavePending = true;
		BeginSceneTransition();
		// Start the normal field-to-title teardown. The required title event
		// provides the render-scene transition barrier; only the fullscreen
		// title menu itself is skipped before submitting Continue.
		tl::TitleMain::returnTitle(gf::CurrentSlot);
#endif
	}


namespace {
    // UtilityMenu's persistence, reduced to the imported Saves feature only.
    bool lastSaved = false;
    std::string SettingsPath() {
        return fmt::format(
            ETHERNET_CONFIG_PATH "/{}/toolWindows.toml",
            ETHERNET_CODENAME_STR
        );
    }
    void SaveStateIfChanged() {
        const bool state = SaveLoader::LoadSaveOnStartup;
        if (state == lastSaved) return;
        const auto contents = fmt::format("load_save_on_startup = {}\n", state);
        const auto path = SettingsPath();
        if (NnFile::Preallocate(path, contents.size())) {
            NnFile file(path, nn::fs::OpenMode_Write);
            file.Write(contents.c_str(), contents.size());
            file.Flush();
            lastSaved = state;
        }
    }
    void TopBar() {
		if(ImGui::BeginMenu("Saves")) {
			imguiext::MenuItemToggle(
				"Load Save on Startup",
				&SaveLoader::LoadSaveOnStartup
			);
			if(ImGui::MenuItem("Reload Save"))
				SaveLoader::ReloadSave();
			if(ImGui::MenuItem("Return to Title"))
				SaveLoader::ReturnTitle();
			ImGui::EndMenu();
		}

        SaveStateIfChanged();
    }
}

void SaveLoader::Initialize() {
    UpdatableModule::Initialize();
	const auto weatherFrameEndAddress =
		skylaunch::hook::detail::ResolveSymbolBase(WeatherFrameEndSymbol);
	if(weatherFrameEndAddress != 0 &&
	   weatherFrameEndAddress != skylaunch::hook::INVALID_FUNCTION_PTR) {
		weatherFrameEnd = reinterpret_cast<WeatherFrameEndFn>(
			weatherFrameEndAddress
		);
	} else {
		g_Logger->LogWarning(
			"[Reload save] weatherFrmEnd is unavailable; using resetScene-only title cleanup"
		);
	}
    const toml::parse_result settings = toml::parse_file(SettingsPath());
    if (settings) {
        const auto& table = settings.table();
        LoadSaveOnStartup = table["load_save_on_startup"].value_or(false);
    }
    lastSaved = LoadSaveOnStartup;
    g_Menu->RegisterTopBarCallback(&TopBar);

    TitleSkipEventHook::HookAt(
        "_ZN2tl20TitleStateMainScreen14playTitleEventEPNS_9TitleMainE"
    );
    TitleSkipMenuHook::HookAt(
        "_ZN2tl20TitleStateMainScreen13dispTitleMenuEPNS_9TitleMainEb"
    );
    TitleContinueHook::HookAt(
        "_ZN2tl20TitleStateMainScreen15checkMenuResultEPNS_9TitleMainE"
    );
    reloadPrimarySavePending = LoadSaveOnStartup;
}
ETHERNET_REGISTER_MODULE(SaveLoader);
}
