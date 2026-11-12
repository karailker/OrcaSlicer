#pragma once

#include "TriangleMesh.hpp"
#include "Model.hpp"
#include <string>

namespace Slic3r {

// Fallback implementations for DRC format when CAD is not available
bool load_drc(const char *path, TriangleMesh *meshptr);
bool load_drc(const char *path, Model *model, const char *object_name = nullptr);

} // namespace Slic3r
