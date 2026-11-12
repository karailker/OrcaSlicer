#pragma once

#include <cstddef>
#include <functional>
#include <string>

namespace Slic3r {

class TriangleMesh;

struct MeshRepairStats
{
    std::size_t welded_vertices    = 0;
    std::size_t removed_triangles  = 0;
    std::size_t reversed_shells    = 0;
    std::size_t filled_holes       = 0;
    std::size_t removed_shells     = 0;
    std::size_t self_intersections = 0;
    bool        modified           = false;
};

// Fallback implementation when CGAL is not available
bool repair_mesh(TriangleMesh &, MeshRepairStats *, std::string *, const std::function<void(int)> &);

} // namespace Slic3r
