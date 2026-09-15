// Created by block on 5/28/23.

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_xeno.h>

#include <algorithm>

#include <skylaunch/hookng/Hooks.hpp>
#include <ethernet/core/DebugWrappers.hpp>
#include <ethernet/core/HidInput.hpp>
#include <ethernet/core/ImGuiExtensions.hpp>
#include <ethernet/core/Logger.hpp>
#include <ethernet/core/NnFile.hpp>
#include <ethernet/core/State.hpp>
#include <ethernet/core/Utils.hpp>
#include <ethernet/core/Version.hpp>
#include <ethernet/core/menu/Menu.hpp>
#include <ethernet/core/menu/Themes.hpp>

#include "helpers/InputHelper.h"

namespace ethernet::core {
	namespace {
		std::string UiSettingsPath() {
			return fmt::format(
				ETHERNET_CONFIG_PATH "/{}/uiWindows.toml",
				ETHERNET_CODENAME_STR
			);
		}

		std::string UiLayoutPath() {
			return fmt::format(
				ETHERNET_CONFIG_PATH "/{}/uiLayout.ini",
				ETHERNET_CODENAME_STR
			);
		}

		void LoadUiLayout() {
			NnFile file(UiLayoutPath(), nn::fs::OpenMode_Read);
			if (!file || file.Size() <= 0)
				return;

			std::string settings(static_cast<std::size_t>(file.Size()), '\0');
			if (file.Read(settings.data(), file.Size()))
				ImGui::LoadIniSettingsFromMemory(settings.data(), settings.size());
		}

		void SaveUiLayoutIfChanged() {
			ImGuiIO& io = ImGui::GetIO();
			if (!io.WantSaveIniSettings)
				return;

			size_t size = 0;
			const char* settings = ImGui::SaveIniSettingsToMemory(&size);
			const auto path = UiLayoutPath();
			if (NnFile::Preallocate(path, static_cast<s64>(size))) {
				NnFile file(path, nn::fs::OpenMode_Write);
				file.Write(settings, static_cast<s64>(size));
				file.Flush();
			} else {
				g_Logger->LogWarning("Could not save EtherNet UI layout to {}", path);
			}
			// ImGui will request another save after the next meaningful layout edit.
			// Clear this request even on failure so a storage error does not write every frame.
			io.WantSaveIniSettings = false;
		}
	}

	struct NvnBootstrapHook : skylaunch::hook::Trampoline<NvnBootstrapHook> {
		static void* Hook(const char* name) {
			return imgui_xeno_bootstrap_hook(name, reinterpret_cast<OrigNvnBootstrap>(Backup()));
		}
	};

	void ImGuiPreInitCallback() {
		ImGuiIO& io = ImGui::GetIO();
		LoadUiLayout();

		// add as much as we reasonably can from the loaded fonts
		static const ImWchar ranges[] = {
			0x0020, 0x00FF, // Basic Latin + Latin Supplement
			0x0102, 0x0103, // Vietnamese
			0x0110, 0x0111, // Vietnamese
			0x0128, 0x0129, // Vietnamese
			0x0168, 0x0169, // Vietnamese
			0x01A0, 0x01A1, // Vietnamese
			0x01AF, 0x01B0, // Vietnamese
			0x0370, 0x03FF, // Greek and Coptic
			0x2000, 0x206F, // General Punctuation
			0x3000, 0x30FF, // CJK Symbols and Punctuations, Hiragana, Katakana
			0x3131, 0x3163, // Korean alphabets
			0x31F0, 0x31FF, // Katakana Phonetic Extensions
			0x4e00, 0x9FAF, // CJK Ideograms
			0xAC00, 0xD7A3, // Korean characters
			0xFF00, 0xFFEF, // Half-width characters
			0xFFFD, 0xFFFD, // Invalid
			0,
		};

		for(std::pair<std::string, float> thingy : GetState().config.menuFonts) {
			std::string path = thingy.first;

			if (!path.starts_with("sd:"))
				path = ETHERNET_CONFIG_PATH "/fonts/" + path;

			// check that the file exists first
			ethernet::core::NnFile file(path, nn::fs::OpenMode_Read);

			if(file) {
				file.Close();
				//g_Logger->LogDebug("{} at {}px", path, thingy.second);
				io.Fonts->AddFontFromFileTTF(path.c_str(), thingy.second, nullptr, &ranges[0]);
			}
		}
		// Match Dear ImGui's classic look: use the embedded 13 px ProggyClean
		// font at startup while keeping configured fonts available in the menu.
		io.FontDefault = io.Fonts->AddFontDefault();

		g_Menu->SetTheme(GetState().config.menuTheme);

		// I am fine hardcoding these for now. In the future, if we make support
		// for full file-based theming, make sure we can do colors and style.
		ImGuiStyle& style = ImGui::GetStyle();

		style.WindowPadding = ImVec2(6,6);
		style.FramePadding = ImVec2(2,1);
		style.ItemSpacing = ImVec2(8,3);
		style.ItemInnerSpacing = ImVec2(3,4);

		style.ScrollbarSize = 16;
		style.ScrollbarRounding = 0;
	}

	void Section_State() {
		ImGui::PushItemWidth(ImGui::GetFrameHeight() * 10.f);

		if(imguiext::EnumComboBox("Menu Theme", &GetState().config.menuTheme))
			g_Menu->SetTheme(GetState().config.menuTheme);

		// copied from the demo
		ImGuiIO& io = ImGui::GetIO();
		ImFont* font_current = ImGui::GetFont();
		if(ImGui::BeginCombo("Menu Font", font_current->GetDebugName())) {
			for(ImFont* font : io.Fonts->Fonts) {
				ImGui::PushID((void*)font);
				ImGui::PushFont(font);
				if(ImGui::Selectable(font->GetDebugName(), font == font_current))
					io.FontDefault = font;
				ImGui::PopFont();
				ImGui::PopID();
			}
			ImGui::EndCombo();
		}

		ImGui::PopItemWidth();

		bool loggingDisabled = g_Logger->IsLoggingDisabled();
		if(ImGui::Checkbox("Disable logging", &loggingDisabled))
			g_Logger->SetLoggingDisabled(loggingDisabled);

		if (ImGui::Button("Toggle Log"))
			g_Menu->ToggleLog();
		if (ImGui::Button("Reload config")) {
			EtherNetState::ReloadConfig();
			g_Logger->LogInfo("EtherNet config reloaded; TCP port and fonts require restart.");
		}

		ImGui::Separator();
	}

	std::string about_build {};
	std::string about_runtime {};
	std::string about_executable {};
	void Section_About() {
		if (about_build.empty())
			about_build = fmt::format("Compiled on {}", version::BuildTimestamp());
		if (about_runtime.empty())
			about_runtime = fmt::format("Currently running {} ({:c}) version {}", version::RuntimeGame(), version::RuntimeGame(), version::RuntimeVersion());
		if (about_executable.empty()) {
			if(std::string_view(version::RuntimeBuildRevision()).starts_with("Rev"))
				about_executable = fmt::format("Game executable {}", version::RuntimeBuildRevision());
			else
				about_executable = fmt::format("Game executable version {}", version::RuntimeBuildRevision());
		}

		ImGui::TextUnformatted(version::EtherNetFullVersion());
		ImGui::TextUnformatted(about_build.c_str());
		ImGui::TextUnformatted(about_runtime.c_str());
		ImGui::TextUnformatted(about_executable.c_str());

		bool& showImGuiAbout = g_Menu->WindowVisibility("imgui_about");
		if (ImGui::SmallButton("About Dear ImGui"))
			showImGuiAbout = true;
		if (showImGuiAbout)
			ImGui::ShowAboutWindow(&showImGuiAbout);
	}

	void Menu::Initialize() {
		// imgui-xeno initialization
		NvnBootstrapHook::HookAt("nvnBootstrapLoader");
		imgui_xeno_add_on_pre_init(&ImGuiPreInitCallback);
		imgui_xeno_init(nullptr, &Render);

		auto state = RegisterSection("state", "State");
		state->RegisterRenderCallback(&Section_State);

		auto about = RegisterSection("about", "About");
		about->RegisterRenderCallback(&Section_About);
	}

	static double lastUpdateSeconds = 0;
	static double lastUpdateDiff = 0;
	void Menu::Update(HidInput* input) {
		InputHelper::setPort(input->padId);
		
		auto seconds = nn::os::GetSystemTick()/19200000.;
		lastUpdateDiff = seconds - lastUpdateSeconds;
		lastUpdateSeconds = seconds;
	}

	void Menu::Render() {
		for(auto func : g_Menu->backgroundCallbacks) {
			func();
		}

		if(!g_Menu->IsOpen()) {
			g_Menu->SaveUiSettingsIfChanged();
			return;
		}

		auto& logOpen = g_Menu->WindowVisibility("log");
		if (logOpen)
			g_Menu->Log.Draw(&logOpen);

		if(ImGui::BeginMainMenuBar()) {
			for(auto func : g_Menu->topBarCallbacks)
				func();

			for(std::size_t i = 0; i < g_Menu->sections.size(); i++) {
				Section* sec = g_Menu->sections[i];
				if (sec == nullptr)
					continue;

				if(ImGui::BeginMenu(sec->GetName().c_str())) {
					ImGui::PushItemFlag(ImGuiItemFlags_SelectableDontClosePopup, true);
					sec->Render();
					ImGui::PopItemFlag();
					ImGui::EndMenu();
				}
			}

			const int fps = lastUpdateDiff > 0.0
				? static_cast<int>(1.f / lastUpdateDiff)
				: 0;
			const auto status = fmt::format(
				"{} FPS: {} ({:.4f}ms)",
				version::EtherNetVersion(),
				fps,
				lastUpdateDiff
			);
			const float rightAlignedX =
				ImGui::GetWindowContentRegionMax().x - ImGui::CalcTextSize(status.c_str()).x;
			ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), rightAlignedX));
			ImGui::TextDisabled("%s", status.c_str());
			ImGui::EndMainMenuBar();
		}

		for(auto func : g_Menu->callbacks) {
			func();
		}

		g_Menu->SaveUiSettingsIfChanged();
	}

	void Menu::LoadWindowVisibility() {
		if (windowVisibilityLoaded)
			return;
		windowVisibilityLoaded = true;

		const toml::parse_result settings = toml::parse_file(UiSettingsPath());
		if (settings) {
			if (const auto* windows = settings.table()["windows"].as_table()) {
				for (const auto& [key, node] : *windows) {
					if (const auto open = node.value<bool>())
						windowVisibility.emplace(std::string(key.str()), *open);
				}
			}
		}

		std::string contents = "[windows]\n";
		for (const auto& [key, open] : windowVisibility)
			contents += fmt::format("{} = {}\n", key, open);
		lastWindowVisibilitySettings = std::move(contents);
	}

	bool& Menu::WindowVisibility(const std::string& key, bool defaultOpen) {
		LoadWindowVisibility();
		return windowVisibility.try_emplace(key, defaultOpen).first->second;
	}

	void Menu::SaveUiSettingsIfChanged() {
		LoadWindowVisibility();

		std::string contents = "[windows]\n";
		for (const auto& [key, open] : windowVisibility)
			contents += fmt::format("{} = {}\n", key, open);

		if (contents != lastWindowVisibilitySettings) {
			const auto path = UiSettingsPath();
			if (NnFile::Preallocate(path, static_cast<s64>(contents.size()))) {
				NnFile file(path, nn::fs::OpenMode_Write);
				file.Write(contents.data(), static_cast<s64>(contents.size()));
				file.Flush();
			} else {
				g_Logger->LogWarning("Could not save EtherNet window visibility to {}", path);
			}
			// Avoid retrying every frame if storage is unavailable.
			lastWindowVisibilitySettings = std::move(contents);
		}

		SaveUiLayoutIfChanged();
	}

	Menu::Theme Menu::SetTheme(Theme theme) {
		if (ImGui::GetCurrentContext() == nullptr)
			return Theme::Auto;

		Theme curTheme = theme;

		if (curTheme == Theme::Auto) {
#if ETHERNET_CODENAME(bfsw)
			curTheme = Theme::Titans;
#elif ETHERNET_CODENAME(bf2)
			curTheme = Theme::DougBinks;
#elif ETHERNET_OLD_ENGINE
			curTheme = Theme::Alrest;
#elif ETHERNET_CODENAME(bf3)
			curTheme = Theme::Aionios;
#endif
		}

		// Theme functions are allowed to customize the complete ImGuiStyle. Start
		// every switch from the same compact EtherNet baseline so geometry from a
		// previous theme (notably Comfy's rounding, alignment, and padding) cannot
		// leak into the newly selected theme.
		ImGuiStyle cleanStyle;
		ImGui::GetStyle() = cleanStyle;
		ImGui::StyleColorsDark();

		ImGuiStyle& style = ImGui::GetStyle();
		style.WindowPadding = ImVec2(6, 6);
		style.FramePadding = ImVec2(2, 1);
		style.ItemSpacing = ImVec2(8, 3);
		style.ItemInnerSpacing = ImVec2(3, 4);
		style.ScrollbarSize = 16;
		style.ScrollbarRounding = 0;

		switch (curTheme) {
			case Theme::Titans: ImGuiStyleColorsXB1(); break;
			case Theme::Alrest: ImGuiStyleColorsXB2(); break;
			case Theme::Aionios: ImGuiStyleColorsXB3(); break;
			case Theme::ImGuiDark: ImGui::StyleColorsDark(); break;
			case Theme::ImGuiLight: ImGui::StyleColorsLight(); break;
			case Theme::ImGuiClassic: ImGui::StyleColorsClassic(); break;
			case Theme::DougBinks: ImGuiStyleColorsDougBinks(); break;
			case Theme::Comfy: ImGuiStyleColorsComfy(); break;
			default: ImGui::StyleColorsDark(); return Theme::ImGuiDark;
		}

		return curTheme;
	}

	Section* FindSectionRecurse(Section* section, const std::string& key) {
		if(section == nullptr)
			return nullptr;

		if(section->GetKey() == key)
			return section;

		// not us, lets check our subsections
		for(auto& sec : *section->GetSubsections()) {
			if(sec->GetKey() == key)
				return sec;

			// not this subsection, check its subsections
			auto recurse = FindSectionRecurse(sec, key);
			if(recurse != nullptr)
				return recurse;
		}

		return nullptr;
	}
	Section* Menu::FindSection(const std::string& key) {
		for(auto& sec : sections) {
			auto recurse = FindSectionRecurse(sec, key);
			if(recurse != nullptr)
				return recurse;
		}

		return nullptr;
	}

	Section* Menu::RegisterSection(const std::string& key, const std::string& display) {
		auto sec = new Section(key, display);
		sections.push_back(sec);
		return sec;
	}

	void Menu::RegisterTopBarCallback(void (*func)()) {
		if(func != nullptr)
			topBarCallbacks.push_back(func);
	}

	void Menu::RegisterRenderCallback(void (*func)(), bool foregroundOnly) {
		if(func == nullptr)
			return;

		if(foregroundOnly)
			callbacks.push_back(func);
		else
			backgroundCallbacks.push_back(func);
	}

	// The menu instance.
	static Menu menu;
	Menu* g_Menu = &menu;

} // namespace ethernet::core
