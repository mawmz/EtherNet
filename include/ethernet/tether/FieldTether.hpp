#pragma once
#include <ethernet/camera/SharedFraming.hpp>

namespace ethernet::tether {
using camera::Vec3;
struct ScreenBoundary {
    Vec3 eye{}, right{1,0,0}, up{0,1,0}, forward{0,0,1};
    float verticalFovRadians{}, aspect{};
    bool valid{}, atNativeLimit{};
    float insetPercent{};
};
// Clips only the forbidden XZ step at a visible frustum edge. No soft band.
Vec3 ConstrainToScreen(Vec3 velocity, const camera::Subject& body,
                       const ScreenBoundary& view, float deltaSeconds,
                       const camera::Subject* partner = nullptr);
}
