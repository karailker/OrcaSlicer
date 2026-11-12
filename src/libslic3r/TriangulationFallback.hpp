#pragma once

#include "Polygon.hpp"
#include "ExPolygon.hpp"
#include "Point.hpp"
#include <vector>
#include <cstdint>

namespace Slic3r {

// Fallback triangulation when CGAL not available
namespace Triangulation {

using Indices = std::vector<Vec3i32>;
using Points = std::vector<Point>;
using Changes = std::vector<uint32_t>;

Indices triangulate(const Points &points, const Points &duplicits);
Indices triangulate(const Polygon &polygon);
Indices triangulate(const Polygons &polygons);
Indices triangulate(const ExPolygon &expolygon);
Indices triangulate(const ExPolygons &expolygons);
Indices triangulate(const ExPolygons &expolygons, const Points& points);
Indices triangulate(const ExPolygons &expolygons, const Points& points, const Changes& changes);
Changes create_changes(const Points &points, const Points &duplicits);

} // namespace Triangulation

} // namespace Slic3r
