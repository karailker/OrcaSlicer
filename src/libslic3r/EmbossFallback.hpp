#pragma once

#include "Polygon.hpp"
#include "ExPolygon.hpp"
#include "EmbossShape.hpp"
#include <vector>

namespace Slic3r {

namespace Emboss {

struct IntersectionsLines {
    std::vector<Line> lines;
    std::vector<size_t> indices;
};

// Fallback implementations for Emboss when CAD is not available
HealedExPolygons heal_polygons(const Polygons &shape, bool is_non_zero = true, unsigned max_iteration = 10);
bool heal_expolygons(ExPolygons &shape, unsigned max_iteration = 10);

} // namespace Emboss

// In Slic3r namespace (not Emboss)
void center(ExPolygonsWithIds &e);

} // namespace Slic3r
