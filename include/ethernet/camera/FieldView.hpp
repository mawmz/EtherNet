#pragma once
#include <cstdint>
#include <ethernet/tether/FieldTether.hpp>

namespace ethernet::camera {
struct FieldView {
    std::uint64_t serial{};
    bool valid{}, fits{};
    tether::ScreenBoundary boundary;
    std::array<Subject,2> subjects{};
    std::uint64_t bindingGeneration{};
};
// Read-only observer. Does not move the shot or consume native pause flags.
FieldView ReadFieldView();
bool AllowsFieldTether();
}
