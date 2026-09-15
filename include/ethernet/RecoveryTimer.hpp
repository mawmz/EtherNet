#pragma once
#include <algorithm>
#include <cmath>

namespace ethernet {
struct RecoveryTimer {
    float remaining{};
    bool active{};
    bool Begin() {
        if (active) return false;
        remaining = 5.0f;
        active = true;
        return true;
    }
    // Completion is acknowledged by the native adapter only after the warp.
    bool Advance(float seconds, bool running) {
        if (!active) return false;
        if (running && std::isfinite(seconds) && seconds > 0)
            remaining = std::max(0.0f, remaining-seconds);
        return running && remaining <= 0.00001f;
    }
    void Reset() { *this = {}; }
};

// A ready timer is still a fallen player until a successful warp acknowledges
// it. Check this before advancing either timer to avoid update-order escapes.
inline bool BothPlayersRecovering(const RecoveryTimer& p1, const RecoveryTimer& p2) {
    return p1.active && p2.active;
}
}
