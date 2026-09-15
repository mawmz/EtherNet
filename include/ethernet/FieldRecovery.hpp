#pragma once
#include <engine/xc2/gf/Object.hpp>

namespace fw { struct UpdateInfo; }
namespace ethernet {
bool IsFieldRecovering(gf::GF_OBJ_HANDLE* actor);
bool IsFieldPlayerRecovering(unsigned slot);
// Scope the native GamePad update's emergency-escape death notification.
// Returns the previous context so nested calls can restore it.
bool SetNativeEmergencyEscapeContext(bool active);
// Called by the existing actor-update hook. True skips only this actor's update.
bool UpdateFieldRecovery(void* behavior, const fw::UpdateInfo& update);
}
