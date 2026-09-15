#pragma once

namespace gf { struct GF_OBJ_HANDLE; }

namespace ethernet {
// One native popup per recovery start, with the fallen actor's own name.
bool NotifyFieldRespawn(gf::GF_OBJ_HANDLE* actor);
}
