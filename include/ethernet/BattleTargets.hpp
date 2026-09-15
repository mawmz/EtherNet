#pragma once
#include <engine/xc2/gf/Object.hpp>

namespace ethernet {
// Update-thread observation only; actual battle target includes native overrides.
gf::GF_OBJ_HANDLE* GetPlayerTarget(unsigned player);
bool IsPlayerEngaged(unsigned player);
// Native close-target hold threshold, owned by P2 rather than P1's HUD mode.
bool IsPlayerTwoTargetShifted();
}
