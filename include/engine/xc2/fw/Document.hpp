//
// Created by block on 7/21/2022.
//

#pragma once

#include <engine/xc2/fw/UpdateInfo.hpp>
#include <engine/xc2/ml/WinView.hpp>

#if ETHERNET_NEW_ENGINE
namespace fw {

	class Applet {
	   public:
		INSERT_PADDING_BYTES(0x1f8);
		ml::WinView* winView;
		INSERT_PADDING_BYTES(0x18);
		UpdateInfo updateInfo;

		void setupSystem();

		ml::Scn* getScn() const;
	};

	class Document /*: public game::DocAccessor*/ {
	   public:
		Applet* applet;
	};

} // namespace fw

namespace ethernet::core {

	extern fw::Document* DocumentPtr;

} // namespace ethernet::core
#endif