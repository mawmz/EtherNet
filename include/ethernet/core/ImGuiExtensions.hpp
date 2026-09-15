// Created by block on 8/3/23.

#pragma once

#include <imgui.h>

#include <magic_enum/magic_enum.hpp>
#include <string_view>

#include <ethernet/core/menu/Themes.hpp>

namespace ethernet::core::imguiext {
	bool MenuItemToggle(const char* label, bool* selected);

	template<typename T>
	bool EnumComboBox(std::string_view label, T* value) {
		const auto selected_name = magic_enum::enum_name<T>(*value);

		bool result = false;

		if(ImGui::BeginCombo(label.data(), selected_name.data())) {
			for(auto enum_value : magic_enum::enum_values<T>()) {
				const bool selected = (enum_value == *value);
				const auto name = magic_enum::enum_name(enum_value);

				if(ImGui::Selectable(name.data(), selected)) {
					*value = enum_value;
					result = true;
				}

				// meow
				if(selected)
					ImGui::SetItemDefaultFocus();
			}

			ImGui::EndCombo();
		}

		return result;
	}

	bool InputFloatExt(const char* label, float* v, float step = 0.0f, float step_fast = 0.0f, float step_mult = 2.0f, const char* format = "%.3f", ImGuiInputTextFlags flags = 0);

} // namespace ethernet::core::imgui
