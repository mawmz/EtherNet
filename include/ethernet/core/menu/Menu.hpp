// Created by block on 5/28/23.

#pragma once

#include <magic_enum/magic_enum.hpp>
#include <ethernet/core/HidInput.hpp>

#include <map>
#include <string>

#include <ethernet/core/menu/MenuLog.hpp>
#include <ethernet/core/menu/Section.hpp>

namespace ethernet::core {

	class Menu {
	   private:
		bool isOpen { false };
		bool windowVisibilityLoaded { false };
		std::map<std::string, bool> windowVisibility {};
		std::string lastWindowVisibilitySettings {};

		std::vector<Section*> sections {};
		std::vector<void(*)()> topBarCallbacks {};
		std::vector<void(*)()> callbacks {};
		std::vector<void(*)()> backgroundCallbacks {};

		void LoadWindowVisibility();
		void SaveUiSettingsIfChanged();

	   public:
		void Initialize();

		void Update(HidInput* input);
		static void Render();

		void Toggle() {
			isOpen = !isOpen;
		};
		bool IsOpen() {
			return isOpen;
		};

		void ToggleLog() {
			auto& open = WindowVisibility("log");
			open = !open;
		};

		bool& WindowVisibility(const std::string& key, bool defaultOpen = false);

		enum class Theme {
			Auto = 0,
			Titans,
			Alrest,
			Aionios,
			ImGuiDark,
			ImGuiLight,
			ImGuiClassic,
			DougBinks,
			Comfy,
		};

		Theme SetTheme(Theme theme);

		Section* FindSection(const std::string& key);
		Section* RegisterSection(const std::string& key, const std::string& display);
		void RegisterTopBarCallback(void(*func)());
		void RegisterRenderCallback(void(*func)(), bool foregroundOnly);

		friend class Section;

		MenuLog Log {};
	};

	extern Menu* g_Menu;

} // namespace ethernet::core

template<>
constexpr magic_enum::customize::customize_t magic_enum::customize::enum_name<ethernet::core::Menu::Theme>(ethernet::core::Menu::Theme value) noexcept {
	// clang-format off
	switch (value) {
		using enum ethernet::core::Menu::Theme;

		case ImGuiDark: return "Dear ImGui Dark";
		case ImGuiLight: return "Dear ImGui Light";
		case ImGuiClassic: return "Dear ImGui Classic";
		case DougBinks: return "Doug Binks";
		case Comfy: return "Comfy";
	}
	// clang-format on
	return default_tag;
}
