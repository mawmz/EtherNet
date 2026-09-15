//
// Created by block on 1/18/2023.
//

#pragma once

#include <ethernet/core/Utils.hpp>

namespace fw {

	struct UpdateInfo {
#if ETHERNET_CODENAME(bfsw)
		float unk1;
#endif
#if ETHERNET_OLD_ENGINE
		float updateRate;
#endif
		float updateDelta;
		float updateRatio;
	};

}