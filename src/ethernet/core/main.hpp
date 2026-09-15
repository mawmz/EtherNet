#pragma once

#include <ethernet/core/UpdatableModule.hpp>

#include <ethernet/core/State.hpp>
#include <nn/hid.hpp>

#include <engine/xc2/mm/MathTypes.hpp>

namespace ethernet::core {

	/**
 	 * Called on each Framework update.
 	 */
	void update(fw::UpdateInfo* updateInfo);

	/**
 	 * Called from skylaunch when ready.
 	 */
	void main();

} // namespace ethernet::core
