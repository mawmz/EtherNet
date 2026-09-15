//
// Created by block on 7/21/2022.
//

#pragma once

#include <ethernet/core/Utils.hpp>
#include <engine/xc2/fw/Document.hpp>
#include <engine/xc2/mm/MathTypes.hpp>

#if ETHERNET_NEW_ENGINE
namespace game {
	class ScriptCfs {
	   public:
		void returnTitle();
		static unsigned short getMapId();
		static unsigned int makeParty(const mm::mtl::FixedVector<unsigned short, 9ul>&);
	};

	class ScriptUI {
	   public:
		static void* openPartyChange();
	};

	class ScriptUnit {
	   public:
		static void setWarp(void* handle, const mm::Vec3& pos);
		static void* getPartyHandle(unsigned short index);
	};
} // namespace game
#endif