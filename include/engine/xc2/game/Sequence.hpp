// Created by block on 6/17/23.

#pragma once

#include <ethernet/core/Utils.hpp>

#include <engine/xc2/game/Utils.hpp>

#if ETHERNET_CODENAME(bfsw)
namespace game {

	class SeqStack {
	   public:
		template<class T, class... Args>
		unsigned int addCommandImpl(Args... args);
		template<class T, class... Args>
		unsigned int pushCommandImpl(Args... args);
	};

	class SeqMapJump {
	   public:
		SeqMapJump(const MapJumpSetupInfo& setupInfo);
	};

	class SeqPartyChange {
	   public:
		SeqPartyChange();
	};
	class SeqPartyMake {
	   public:
		SeqPartyMake();
	};

	class SeqManager {
	   public:
		SeqStack* getField();
	};

} // namespace game
#endif