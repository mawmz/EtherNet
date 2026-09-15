#pragma once
#include <cstdint>
#include <engine/xc2/gf/Object.hpp>

namespace ethernet {
// A binding is published only after the native AI -> player transition created
// P2's PadAgent. Consumers must still validate the current slot and object.
bool IsPlayerTwoBound(gf::GF_OBJ_HANDLE* handle);
std::uint64_t PlayerBindingGeneration();
}
