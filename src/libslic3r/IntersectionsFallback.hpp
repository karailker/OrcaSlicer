#pragma once

#include "Polygon.hpp"
#include "ExPolygon.hpp"
#include "Line.hpp"
#include <vector>

namespace Slic3r {

struct IntersectionsLines {
    std::vector<Line> lines;
    std::vector<size_t> indices;
};

// Fallback for get_intersections when CGAL not available
IntersectionsLines get_intersections(const std::vector<Line>&);
IntersectionsLines get_intersections(const Polygon&);
IntersectionsLines get_intersections(const Polygons&);
IntersectionsLines get_intersections(const ExPolygon&);
IntersectionsLines get_intersections(const ExPolygons&);

} // namespace Slic3r
