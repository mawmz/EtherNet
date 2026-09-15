// Created by block on 5/16/2023.

#pragma once

#include <ethernet/core/Utils.hpp>
#include <engine/xc2/ml/Drivers.hpp>
#include <engine/xc2/ml/Scene.hpp>
#include <engine/xc2/mtl/Allocator.hpp>

namespace layer {

	class LayerManager {
	   public:
		INSERT_PADDING_BYTES(0x1C);
		void* updateRateRedHerring;

#if ETHERNET_OLD_ENGINE
		INSERT_PADDING_BYTES(0x4C);
#elif ETHERNET_CODENAME(bfsw)
		INSERT_PADDING_BYTES(0x3C);
#elif ETHERNET_CODENAME(bf3)
		INSERT_PADDING_BYTES(0x48);
#endif
		float frameRateFreq;
		float frameRateTime;
		float anotherThing;

		LayerManager(ml::Scn*, mtl::ALLOC_HANDLE, unsigned int);

		virtual void finalRender(ml::IDrDrawWorkInfo*);
	};

}