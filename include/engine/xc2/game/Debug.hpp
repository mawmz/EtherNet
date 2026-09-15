// Created by block on 2/12/2023.

#pragma once

#include <ethernet/core/Utils.hpp>

#include <engine/xc2/fw/Document.hpp>

#if ETHERNET_NEW_ENGINE
namespace game {

	// This seems to unlock all landmarks, maps, and arts
	bool IsMenuDebugUnlockAll(const fw::Document& doc);

	class Debug {
	   public:
		enum class Group : std::uint32_t {

		};

		static void drawFont(Group group, int x, int y, const mm::Col4& color, const char* text, float unk1);
		static void setEnable(Group group, bool enable);
	};

} // namespace game
#endif