#pragma once

namespace ethernet {
// Installed by BattleTargets after its target-routing hooks are ready.
bool InitializeBattleEntry();

// Called after P2 target selection for the frame. A native accepted
// btl_start pulse is consumed through BattleManager's actor-specific entry.
void ProcessPlayerTwoBattleEntry();

// Clears a pending field input across scene/object lifetimes.
void ResetBattleEntry();
}
