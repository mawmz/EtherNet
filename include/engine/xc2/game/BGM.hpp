// Created by block on 8/26/23.

#pragma once

#include <ethernet/core/Utils.hpp>

#include <engine/xc2/game/Controllers.hpp>

#if ETHERNET_CODENAME(bfsw)
namespace game {

	class BGM {
	   public:
		std::uint16_t getBattleBGMID(const GameController& controller) const;
		bool testTensionZero(const GameController& controller) const;
	};

}
#endif