#pragma once

namespace ethernet {
// Ordinary NPC dialogue/shop with one initiating actor and a movable partner.
// Does not grant input to full-screen menus, scripted events or transitions.
bool IsPartnerFieldActive();
// Logical pad (0/1) owning an interaction while its partner can move, or -1.
// Only this pad may operate the interaction UI in that context.
int PartnerInteractionOwner();
}
