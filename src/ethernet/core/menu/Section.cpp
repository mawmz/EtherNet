// Created by block on 5/28/23.

#include <ethernet/core/menu/Section.hpp>

#include <ethernet/core/DebugWrappers.hpp>
#include <ethernet/core/Logger.hpp>
#include <ethernet/core/menu/Menu.hpp>

#include <imgui.h>

namespace ethernet::core {

	Section::Section(const std::string& key, const std::string& display)
		: key(key),
		  display(display),
		  subsections() {
	}

	void Section::Render() {
		for(auto sub : subsections) {
			if(ImGui::BeginMenu(sub->GetName().c_str())) {
				sub->Render();
				ImGui::EndMenu();
			}
		}

		for(auto func : callbacks) {
			func();
		}
	}

	Section* Section::RegisterSection(const std::string& key, const std::string& display) {
		auto sec = new Section(key, display);
		sec->parent = this;
        subsections.push_back(sec);
		return sec;
	}

	void Section::RegisterRenderCallback(void (*func)()) {
		if (func != nullptr)
			callbacks.push_back(func);
	}

} // namespace ethernet::core