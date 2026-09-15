#pragma once

namespace ethernet {
// Validated, current registered actor, including field/weapon-draw entry.
// Registration is not proof of an active encounter or initialized Arts data.
// Borrow only during the calling update;
// never retain or dereference this pointer from a menu-render callback.
const void* GetBoundBattleActor(unsigned player);
}
