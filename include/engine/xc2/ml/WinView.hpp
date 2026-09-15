// Created by block on 2/12/2023.

#pragma once

#include <ethernet/core/Utils.hpp>

#include <engine/xc2/ml/DebugDrawing.hpp>
#include <engine/xc2/ml/Scene.hpp>
#include <engine/xc2/mm/mtl/FixStr.hpp>

namespace ml {

	class WinView {
	   public:
		INSERT_PADDING_BYTES(0x8);
		mm::mtl::FixStr<64> windowName;
		int idk;
		unsigned int windowID; // 80
		INSERT_PADDING_BYTES(0x150);
		void* fontLayer;
		INSERT_PADDING_BYTES(0x200);
#if !ETHERNET_CODENAME(bf3)
		INSERT_PADDING_BYTES(0x8);
#endif
		// bit 6 (32) - full toggle?
		// bit 8 (256) - debug font rendering?
		unsigned int renderingFlags; // 0x3b8 (952), 0x3b0 in 3

		void renderView();
	};

	class WinViewRoot {
	   public:
		static WinView* getCurrent();
	};

} // namespace ml