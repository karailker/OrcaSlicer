cmake_minimum_required(VERSION 3.13)
project(libslic3r)

include(PrecompiledHeader)

if(NOT DEFINED ORCA_CHECK_GCODE_PLACEHOLDERS)
    set(ORCA_CHECK_GCODE_PLACEHOLDERS "0")
endif()

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/libslic3r_version.h.in ${CMAKE_CURRENT_BINARY_DIR}/libslic3r_version.h @ONLY)

if (MINGW)
    add_compile_options(-Wa,-mbig-obj)
endif ()

set(OpenVDBUtils_SOURCES "")
if (TARGET OpenVDB::openvdb)
    set(OpenVDBUtils_SOURCES OpenVDBUtils.cpp OpenVDBUtils.hpp)
endif()

option(BUILD_SHARED_LIBS "Build shared libs" OFF)
option(USE_SLIC3R_CONSOLE_LOG "Enable console logging in RelWithDebInfo builds" OFF)

# SolveSpace constraint solver (2D sketch solver backbone), built in deps/SLVS.
if (SLIC3R_CAD)
    find_package(SLVS REQUIRED)
endif ()

if (SLIC3R_CAD)
    cmake_policy(PUSH)
    cmake_policy(SET CMP0011 NEW)
    # CGAL's config resets policies (cmake_minimum_required ...3.31), so a plain SET
    # can't reach it; the default opts its Boost lookup into BoostConfig (CMP0167).
    set(CMAKE_POLICY_DEFAULT_CMP0167 NEW)
    find_package(CGAL 6.2 REQUIRED)
    find_package(OpenCV REQUIRED core)
    unset(CMAKE_POLICY_DEFAULT_CMP0167)
    cmake_policy(POP)

    set(CGAL_SOURCES
        "Geometry/VoronoiUtilsCgal.cpp;Geometry/VoronoiUtilsCgal.hpp"
        "CutSurface.hpp;CutSurface.cpp"
    )

    add_library(libslic3r_cgal STATIC
        CutSurface.hpp CutSurface.cpp
        IntersectionPoints.hpp IntersectionPoints.cpp
        MeshBoolean.hpp MeshBoolean.cpp
        MeshRepair.hpp MeshRepair.cpp
        TryCatchSignal.hpp TryCatchSignal.cpp
        Triangulation.hpp Triangulation.cpp
    )

    target_include_directories(libslic3r_cgal PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

    # Reset compile options of libslic3r_cgal. Despite it being linked privately, CGAL options
    # (-frounding-math) still propagate to dependent libs which is not desired.
    get_target_property(_cgal_tgt CGAL::CGAL ALIASED_TARGET)
    if (NOT TARGET ${_cgal_tgt})
        set (_cgal_tgt CGAL::CGAL)
    endif ()
    get_target_property(_opts ${_cgal_tgt} INTERFACE_COMPILE_OPTIONS)
    if (_opts)
        set(_opts_bad "${_opts}")
        set(_opts_good "${_opts}")
        list(FILTER _opts_bad INCLUDE REGEX frounding-math)
        list(FILTER _opts_good EXCLUDE REGEX frounding-math)
        set_target_properties(${_cgal_tgt} PROPERTIES INTERFACE_COMPILE_OPTIONS "${_opts_good}")
        target_compile_options(libslic3r_cgal PRIVATE "${_opts_bad}")
    endif()

    if (IS_CLANG_CL)
        # CGAL passes /fp:strict /fp:except-. clang-cl reports the second as overriding part of
        # the first; the settings cc1 receives are the same ones MSVC produces from that pair.
        target_compile_options(libslic3r_cgal PRIVATE -Wno-overriding-option)
    endif ()

    # Lets CGAL algorithms given CGAL::Parallel_if_available_tag run on TBB.
    target_compile_definitions(libslic3r_cgal PRIVATE CGAL_LINKED_WITH_TBB)
    target_link_libraries(libslic3r_cgal PRIVATE ${_cgal_tgt} admesh libigl mcut boost_libs TBB::tbb)

    if (MSVC AND "${CMAKE_SIZEOF_VOID_P}" STREQUAL "4") # 32 bit MSVC workaround
        target_compile_definitions(libslic3r_cgal PRIVATE CGAL_DO_NOT_USE_MPZF)
    endif ()
else()
    set(CGAL_SOURCES "")
endif()

# Provide fallback implementations when CGAL is not available (SLIC3R_CAD=OFF)
if (NOT SLIC3R_CAD)
    # Provide fallback VoronoiUtilsCgal when CGAL is not available
    file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/Geometry/VoronoiUtilsCgal.cpp
        "// Fallback implementation for non-CGAL builds\n"
        "#include \"libslic3r/Geometry/VoronoiUtilsCgal.hpp\"\n"
        "#include \"libslic3r/Geometry/Voronoi.hpp\"\n"
        "#include \"libslic3r/Arachne/utils/PolygonsSegmentIndex.hpp\"\n"
        "#include \"libslic3r/MultiMaterialSegmentation.hpp\"\n"
        "#include \"libslic3r/Line.hpp\"\n"
        "#include \"libslic3r/Point.hpp\"\n"
        "namespace Slic3r::Geometry {\n"
        "using VD = VoronoiDiagram;\n"
        "using LinesIt = std::vector<Line>::iterator;\n"
        "using ColoredLinesConstIt = std::vector<ColoredLine>::const_iterator;\n"
        "using PolygonsSegmentIndexConstIt = std::vector<Arachne::PolygonsSegmentIndex>::const_iterator;\n"
        "    template<typename SegmentIterator>\n"
        "    typename boost::polygon::enable_if<\n"
        "        typename boost::polygon::gtl_if<typename boost::polygon::is_segment_concept<\n"
        "            typename boost::polygon::geometry_concept<typename std::iterator_traits<SegmentIterator>::value_type>::type>::type>::type,\n"
        "        bool>::type\n"
        "    VoronoiUtilsCgal::is_voronoi_diagram_planar_angle(const VoronoiDiagram &, SegmentIterator, SegmentIterator) {\n"
        "        return true; // Assume planar when CGAL not available\n"
        "    }\n"
        "    bool VoronoiUtilsCgal::is_voronoi_diagram_planar_intersection(const VoronoiDiagram &) {\n"
        "        return true; // Assume planar when CGAL not available\n"
        "    }\n"
        "    // Explicit template instantiations\n"
        "    template bool VoronoiUtilsCgal::is_voronoi_diagram_planar_angle<LinesIt>(const VD &, LinesIt, LinesIt);\n"
        "    template bool VoronoiUtilsCgal::is_voronoi_diagram_planar_angle<VD::SegmentIt>(const VD &, VD::SegmentIt, VD::SegmentIt);\n"
        "    template bool VoronoiUtilsCgal::is_voronoi_diagram_planar_angle<ColoredLinesConstIt>(const VD &, ColoredLinesConstIt, ColoredLinesConstIt);\n"
        "    template bool VoronoiUtilsCgal::is_voronoi_diagram_planar_angle<PolygonsSegmentIndexConstIt>(const VD &, PolygonsSegmentIndexConstIt, PolygonsSegmentIndexConstIt);\n"
        "} // namespace Slic3r::Geometry\n"
    )
    
    # Provide fallback implementations for CAD-dependent functions
    file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/MeshRepairFallback.cpp
        "#include \"libslic3r/MeshRepairFallback.hpp\"
"
        "#include \"libslic3r/TriangleMesh.hpp\"
"
        "namespace Slic3r {\n"
        "static float compute_volume(const indexed_triangle_set &its)\n"
        "{\n"
        "    if (its.empty()) return 0.;\n"
        "    auto p0 = its.vertices.front();\n"
        "    float volume = 0.f;\n"
        "    for (size_t i = 0; i < its.indices.size(); ++i) {\n"
        "        const Vec3f &a = its.vertices[its.indices[i][0]];\n"
        "        const Vec3f &b = its.vertices[its.indices[i][1]];\n"
        "        const Vec3f &c = its.vertices[its.indices[i][2]];\n"
        "        Vec3f U = b - a;\n"
        "        Vec3f V = c - a;\n"
        "        Vec3f C = U.cross(V);\n"
        "        Vec3f normal = C.normalized();\n"
        "        float area = 0.5f * C.norm();\n"
        "        float height = normal.dot(a - p0);\n"
        "        volume += (area * height) / 3.0f;\n"
        "    }\n"
        "    return volume;\n"
        "}\n"
        "bool repair_mesh(TriangleMesh &mesh, MeshRepairStats *stats_out, std::string *error, const std::function<void(int)> &progress)\n"
        "{\n"
        "    if (mesh.its.indices.empty()) {\n"
        "        if (stats_out) {\n"
        "            stats_out->modified = false;\n"
        "            stats_out->welded_vertices = 0;\n"
        "            stats_out->removed_triangles = 0;\n"
        "            stats_out->reversed_shells = 0;\n"
        "            stats_out->filled_holes = 0;\n"
        "            stats_out->removed_shells = 0;\n"
        "            stats_out->self_intersections = 0;\n"
        "        }\n"
        "        return true;\n"
        "    }\n"
        "    float vol = compute_volume(mesh.its);\n"
        "    size_t inverted_count = 0;\n"
        "    for (size_t i = 0; i < mesh.its.indices.size(); ++i) {\n"
        "        const Vec3f &a = mesh.its.vertices[mesh.its.indices[i][0]];\n"
        "        const Vec3f &b = mesh.its.vertices[mesh.its.indices[i][1]];\n"
        "        const Vec3f &c = mesh.its.vertices[mesh.its.indices[i][2]];\n"
        "        Vec3f U = b - a;\n"
        "        Vec3f V = c - a;\n"
        "        Vec3f C = U.cross(V);\n"
        "        Vec3f normal = C.normalized();\n"
        "        float height = normal.dot(a - mesh.its.vertices.front());\n"
        "        if (height < 0) {\n"
        "            inverted_count++;\n"
        "        }\n"
        "    }\n"
        "    if (inverted_count > 0 || vol < 0) {\n"
        "        if (stats_out) {\n"
        "            stats_out->modified = true;\n"
        "            stats_out->welded_vertices = 0;\n"
        "            stats_out->removed_triangles = 0;\n"
        "            stats_out->reversed_shells = mesh.its.indices.size();\n"
        "            stats_out->filled_holes = 0;\n"
        "            stats_out->removed_shells = 0;\n"
        "            stats_out->self_intersections = 0;\n"
        "        }\n"
        "        mesh.flip_triangles();\n"
        "        return true;\n"
        "    }\n"
        "    if (stats_out) {\n"
        "        stats_out->modified = false;\n"
        "        stats_out->welded_vertices = 0;\n"
        "        stats_out->removed_triangles = 0;\n"
        "        stats_out->reversed_shells = 0;\n"
        "        stats_out->filled_holes = 0;\n"
        "        stats_out->removed_shells = 0;\n"
        "        stats_out->self_intersections = 0;\n"
        "    }\n"
        "    return true;\n"
        "}\n"
        "} // namespace Slic3r\n"
    )
    file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/MeshBooleanFallback.cpp
        "#include \"libslic3r/MeshBooleanFallback.hpp\"\n"
        "namespace Slic3r {\n"
        "namespace MeshBoolean {\n"
        "TriangleMesh eigen_to_triangle_mesh(const EigenMesh &) { return TriangleMesh(); }\n"
        "EigenMesh triangle_mesh_to_eigen(const TriangleMesh &) { return {}; }\n"
        "void minus(EigenMesh &, const EigenMesh &) {}\n"
        "void self_union(EigenMesh &) {}\n"
        "void minus(TriangleMesh&, const TriangleMesh&) {}\n"
        "void self_union(TriangleMesh&) {}\n"
        "namespace cgal {\n"
        "struct CGALMesh { };\n"
        "CGALMeshPtr clone(const CGALMesh &) { return nullptr; }\n"
        "void save_CGALMesh(const std::string&, const CGALMesh&) {}\n"
        "CGALMeshPtr triangle_mesh_to_cgal(const std::vector<stl_vertex> &, const std::vector<stl_triangle_vertex_indices> &) { return nullptr; }\n"
        "TriangleMesh cgal_to_triangle_mesh(const CGALMesh &) { return TriangleMesh(); }\n"
        "indexed_triangle_set cgal_to_indexed_triangle_set(const CGALMesh &) { return indexed_triangle_set(); }\n"
        "void minus(TriangleMesh &, const TriangleMesh &) {}\n"
        "void plus(TriangleMesh &, const TriangleMesh &) {}\n"
        "void intersect(TriangleMesh &, const TriangleMesh &) {}\n"
        "void minus(indexed_triangle_set &, const indexed_triangle_set &) {}\n"
        "void plus(indexed_triangle_set &, const indexed_triangle_set &) {}\n"
        "void intersect(indexed_triangle_set &, const indexed_triangle_set &) {}\n"
        "void minus(CGALMesh &, CGALMesh &) {}\n"
        "void plus(CGALMesh &, CGALMesh &) {}\n"
        "void intersect(CGALMesh &, CGALMesh &) {}\n"
        "bool does_self_intersect(const TriangleMesh &) { return false; }\n"
        "bool does_self_intersect(const CGALMesh &) { return false; }\n"
        "std::vector<TriangleMesh> segment(const TriangleMesh&, double, int) { return {}; }\n"
        "TriangleMesh merge(std::vector<TriangleMesh>) { return TriangleMesh(); }\n"
        "bool does_bound_a_volume(const CGALMesh &) { return false; }\n"
        "bool empty(const CGALMesh &) { return true; }\n"
        "std::optional<std::vector<Vec2f>> parameterize_lscm(const indexed_triangle_set &) { return std::nullopt; }\n"
        "indexed_triangle_set remesh_isotropic(const indexed_triangle_set &mesh, double, unsigned, double, unsigned) { return mesh; }\n"
        "}\n"
        "namespace mcut {\n"
        "void make_boolean(const TriangleMesh &, const TriangleMesh &, std::vector<TriangleMesh> &, const std::string &) {}\n"
        "}\n"
        "}\n"
        "} // namespace Slic3r\n"
    )
    file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/TriangulationFallback.cpp
        "#include \"libslic3r/TriangulationFallback.hpp\"\n"
        "namespace Slic3r {\n"
        "namespace Triangulation {\n"
        "Indices triangulate(const Points &, const Points &) { return {}; }\n"
        "Indices triangulate(const Polygon &) { return {}; }\n"
        "Indices triangulate(const Polygons &) { return {}; }\n"
        "Indices triangulate(const ExPolygon &) { return {}; }\n"
        "Indices triangulate(const ExPolygons &) { return {}; }\n"
        "Indices triangulate(const ExPolygons &, const Points&) { return {}; }\n"
        "Indices triangulate(const ExPolygons &, const Points&, const Changes&) { return {}; }\n"
        "Changes create_changes(const Points &, const Points &) { return {}; }\n"
        "}\n"
        "} // namespace Slic3r\n"
    )
    file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/IntersectionsFallback.cpp
        "#include \"libslic3r/IntersectionsFallback.hpp\"\n"
        "namespace Slic3r {\n"
        "IntersectionsLines get_intersections(const std::vector<Line>&) { return {}; }\n"
        "IntersectionsLines get_intersections(const Polygon&) { return {}; }\n"
        "IntersectionsLines get_intersections(const Polygons&) { return {}; }\n"
        "IntersectionsLines get_intersections(const ExPolygon&) { return {}; }\n"
        "IntersectionsLines get_intersections(const ExPolygons&) { return {}; }\n"
        "} // namespace Slic3r\n"
    )
    file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/TextureDisplacementFallback.cpp
        "#include \"libslic3r/TextureDisplacementFallback.hpp\"\n"
        "#include \"libslic3r/TextureDisplacement.hpp\"\n"
        "namespace Slic3r {\n"
        "bool texture_displacement_layers_from_json(const std::string &, std::vector<TextureDisplacementLayer> &, TextureDisplacementOptions &) { return false; }\n"
        "std::string texture_displacement_layers_to_json(const std::vector<TextureDisplacementLayer> &, const TextureDisplacementOptions &) { return \"{}\"; }\n"
        "DecodedHeightTexture decode_height_texture(const TextureDisplacementLayer &, bool) { return DecodedHeightTexture(); }\n"
        "DecodedHeightTexture apply_texture_adjustments(const DecodedHeightTexture &src, const TextureAdjustments &) { return src; }\n"
        "void sample_layer_height(const DecodedHeightTexture&, const TextureDisplacementLayer&, const Eigen::Matrix<float, 3, 1>&, const Eigen::Matrix<float, 3, 1>&, const Eigen::Matrix<float, 3, 1>&, const Eigen::Matrix<float, 3, 1>&, const Eigen::Matrix<float, 2, 1>*) {}\n"
        "DecodedHeightTexture downscale_height_texture(const DecodedHeightTexture &src, int) { return src; }\n"
        "void cut_mesh_at_steps(const indexed_triangle_set&, const std::vector<unsigned char>&, const std::function<float(const Eigen::Matrix<float, 3, 1>&, const Eigen::Matrix<float, 3, 1>&)>&, float, float, float, std::vector<int>*, unsigned long*) {}\n"
        "indexed_triangle_set subdivide_mesh_uniform(const indexed_triangle_set &mesh, float, int) { return mesh; }\n"
        "void build_texture_displacement(const indexed_triangle_set&, const std::vector<TextureDisplacementLayer>&, const std::array<TriangleSelector::TriangleSplittingData, 8>&, const TextureDisplacementOptions&, const std::function<bool(int)>&, const TextureColorRequest*, const Eigen::Transform<double, 3, 2>&, BakeStageRecorder*, TextureBakeStats*, const std::vector<Eigen::Matrix<float, 2, 1>>*, const Eigen::Matrix<double, 3, 1>*, float) {}\n"
        "void make_combined_displacement_sampler(const indexed_triangle_set&, const std::vector<TextureDisplacementLayer>&, const std::array<TriangleSelector::TriangleSplittingData, 8>&, const std::vector<Eigen::Matrix<float, 2, 1>>*) {}\n"
        "std::vector<Vec3f> texture_displacement_vertex_normals(const indexed_triangle_set&) { return {}; }\n"
        "void texture_displacement_patch_frame(const indexed_triangle_set&, const std::vector<Eigen::Matrix<float, 3, 1>>&, Eigen::Matrix<float, 3, 1>&, Eigen::Matrix<float, 3, 1>&, Eigen::Matrix<float, 3, 1>&) {}\n"
        "std::vector<uint8_t> transfer_facet_paint(const indexed_triangle_set &, const TriangleSelector::TriangleSplittingData &, const indexed_triangle_set &) { return {}; }\n"
        "} // namespace Slic3r\n"
    )
    file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/EmbossFallback.cpp
        "#include \"libslic3r/EmbossFallback.hpp\"\n"
        "#include \"libslic3r/EmbossShape.hpp\"\n"
        "namespace Slic3r {\n"
        "namespace Emboss {\n"
        "HealedExPolygons heal_polygons(const Polygons &, bool, unsigned) { return HealedExPolygons(); }\n"
        "bool heal_expolygons(ExPolygons &, unsigned) { return true; }\n"
        "} // namespace Emboss\n"
        "void center(ExPolygonsWithIds &) {}\n"
        "} // namespace Slic3r\n"
    )
    file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/Format/DRCFallback.cpp
        "#include \"libslic3r/Format/DRCFallback.hpp\"\n"
        "namespace Slic3r {\n"
        "bool load_drc(const char *, TriangleMesh *) { return false; }\n"
        "bool load_drc(const char *, Model *, const char *) { return false; }\n"
        "} // namespace Slic3r\n"
    )
    
    # Store fallback sources for later addition via target_sources (to avoid source_group issues)
    set(LIBSLIC3R_CAD_FALLBACK_SOURCES 
        ${CMAKE_CURRENT_BINARY_DIR}/Geometry/VoronoiUtilsCgal.cpp
        ${CMAKE_CURRENT_BINARY_DIR}/MeshRepairFallback.cpp
        ${CMAKE_CURRENT_BINARY_DIR}/MeshBooleanFallback.cpp
        ${CMAKE_CURRENT_BINARY_DIR}/TriangulationFallback.cpp
        ${CMAKE_CURRENT_BINARY_DIR}/IntersectionsFallback.cpp
        ${CMAKE_CURRENT_BINARY_DIR}/TextureDisplacementFallback.cpp
        ${CMAKE_CURRENT_BINARY_DIR}/EmbossFallback.cpp
        ${CMAKE_CURRENT_BINARY_DIR}/Format/DRCFallback.cpp
    )
endif()

set(lisbslic3r_sources
    AABBMesh.cpp
    AABBMesh.hpp
    AABBTreeIndirect.hpp
    AABBTreeLines.hpp
    Algorithm/LineSplit.cpp
    Algorithm/LineSplit.hpp
    Algorithm/RegionExpansion.cpp
    Algorithm/RegionExpansion.hpp
    AnyPtr.hpp
    AppConfig.cpp
    AppConfig.hpp
    Arachne/BeadingStrategy/BeadingStrategy.cpp
    Arachne/BeadingStrategy/BeadingStrategyFactory.cpp
    Arachne/BeadingStrategy/BeadingStrategyFactory.hpp
    Arachne/BeadingStrategy/BeadingStrategy.hpp
    Arachne/BeadingStrategy/DistributedBeadingStrategy.cpp
    Arachne/BeadingStrategy/DistributedBeadingStrategy.hpp
    Arachne/BeadingStrategy/LimitedBeadingStrategy.cpp
    Arachne/BeadingStrategy/LimitedBeadingStrategy.hpp
    Arachne/BeadingStrategy/OuterWallInsetBeadingStrategy.cpp
    Arachne/BeadingStrategy/OuterWallInsetBeadingStrategy.hpp
    Arachne/BeadingStrategy/RedistributeBeadingStrategy.cpp
    Arachne/BeadingStrategy/RedistributeBeadingStrategy.hpp
    Arachne/BeadingStrategy/WideningBeadingStrategy.cpp
    Arachne/BeadingStrategy/WideningBeadingStrategy.hpp
    Arachne/SkeletalTrapezoidation.cpp
    Arachne/SkeletalTrapezoidationEdge.hpp
    Arachne/SkeletalTrapezoidationGraph.cpp
    Arachne/SkeletalTrapezoidationGraph.hpp
    Arachne/SkeletalTrapezoidation.hpp
    Arachne/SkeletalTrapezoidationJoint.hpp
    Arachne/utils/ExtrusionJunction.hpp
    Arachne/utils/ExtrusionLine.cpp
    Arachne/utils/ExtrusionLine.hpp
    Arachne/utils/HalfEdgeGraph.hpp
    Arachne/utils/HalfEdge.hpp
    Arachne/utils/HalfEdgeNode.hpp
    Arachne/utils/PolygonsPointIndex.hpp
    Arachne/utils/PolygonsSegmentIndex.hpp
    Arachne/utils/PolylineStitcher.cpp
    Arachne/utils/PolylineStitcher.hpp
    Arachne/utils/SparseGrid.hpp
    Arachne/utils/SparseLineGrid.hpp
    Arachne/utils/SparsePointGrid.hpp
    Arachne/utils/SquareGrid.cpp
    Arachne/utils/SquareGrid.hpp
    Arachne/WallToolPaths.cpp
    Arachne/WallToolPaths.hpp
    ArcFitter.cpp
    ArcFitter.hpp
    Arrange.cpp
    Arrange.hpp
    BlacklistedLibraryCheck.cpp
    BlacklistedLibraryCheck.hpp
    BoundingBox.cpp
    BoundingBox.hpp
    BridgeDetector.cpp
    BridgeDetector.hpp
    BeltBrim.cpp
    BeltBrim.hpp
    BeltGCode.cpp
    BeltGCode.hpp
    BeltPurge.cpp
    BeltSliceStrategy.cpp
    BeltSliceStrategy.hpp
    BeltTransform.cpp
    BeltTransform.hpp
    Brim.cpp
    BrimEarsPoint.hpp
    Brim.hpp
    BuildVolume.cpp
    BuildVolume.hpp
    calib.cpp
    calib.hpp
    Circle.cpp
    Circle.hpp
    ClipperUtils.cpp
    ClipperUtils.hpp
    ClipperZUtils.cpp
    ClipperZUtils.hpp
    Color.cpp
    Color.hpp
    CommonDefs.hpp
    Config.cpp
    Config.hpp
    ConnectedBodies.cpp
    ConnectedBodies.hpp
    ContourZ.cpp
    CustomGCode.cpp
    CustomGCode.hpp
    CutUtils.cpp
    CutUtils.hpp
    EdgeGrid.cpp
    EdgeGrid.hpp
    ElephantFootCompensation.cpp
    ElephantFootCompensation.hpp
    EmbossShape.hpp
    enum_bitmask.hpp
    ExclusionVolumeGeometry.cpp
    ExclusionVolumeGeometry.hpp
    Execution/Execution.hpp
    Execution/ExecutionSeq.hpp
    Execution/ExecutionTBB.hpp
    ExPolygon.cpp
    ExPolygon.hpp
    ExPolygonSerialize.hpp
    ExPolygonsIndex.cpp
    ExPolygonsIndex.hpp
    Extruder.cpp
    Extruder.hpp
    ExtrusionEntityCollection.cpp
    ExtrusionEntityCollection.hpp
    ExtrusionEntity.cpp
    ExtrusionEntity.hpp
    ExtrusionSimulator.cpp
    ExtrusionSimulator.hpp
    FaceDetector.cpp
    FaceDetector.hpp
    Feature/FuzzySkin/FuzzySkin.cpp
    Feature/FuzzySkin/FuzzySkin.hpp
    Feature/Interlocking/InterlockingGenerator.cpp
    Feature/Interlocking/InterlockingGenerator.hpp
    Feature/Interlocking/VoxelUtils.cpp
    Feature/Interlocking/VoxelUtils.hpp
    FileParserError.hpp
    Fill/Fill3DHoneycomb.cpp
    Fill/Fill3DHoneycomb.hpp
    Fill/FillAdaptive.cpp
    Fill/FillAdaptive.hpp
    Fill/FillBase.cpp
    Fill/FillBase.hpp
    Fill/FillConcentric.cpp
    Fill/FillConcentric.hpp
    Fill/FillSpiralInset.cpp
    Fill/FillSpiralInset.hpp
    Fill/FillConcentricInternal.cpp
    Fill/FillConcentricInternal.hpp
    Fill/FillCornerSmoothing.cpp
    Fill/FillCornerSmoothing.hpp
    Fill/Fill.cpp
    Fill/FillCrossHatch.cpp
    Fill/FillCrossHatch.hpp
    Fill/FillGyroid.cpp
    Fill/FillGyroid.hpp
    Fill/FillHoneycomb.cpp
    Fill/FillHoneycomb.hpp
    Fill/Fill.hpp
    Fill/FillLightning.cpp
    Fill/FillLightning.hpp
    Fill/FillLine.cpp
    Fill/FillLine.hpp
    Fill/FillPlanePath.cpp
    Fill/FillPlanePath.hpp
    Fill/FillRectilinear.cpp
    Fill/FillRectilinear.hpp
    Fill/AdaptiveInfillField.cpp
    Fill/AdaptiveInfillField.hpp
    Fill/FillTpmsD.cpp
    Fill/FillTpmsD.hpp
    Fill/FillTpmsFK.cpp
    Fill/FillTpmsFK.hpp
    Fill/Lightning/DistanceField.cpp
    Fill/Lightning/DistanceField.hpp
    Fill/Lightning/Generator.cpp
    Fill/Lightning/Generator.hpp
    Fill/Lightning/Layer.cpp
    Fill/Lightning/Layer.hpp
    Fill/Lightning/TreeNode.cpp
    Fill/Lightning/TreeNode.hpp
    FilamentMixer.cpp
    FilamentMixer.hpp
    FilamentMixerModel.hpp
    ColorDecomposeRecipe.cpp
    ColorDecomposeRecipe.hpp
    ${OPENCV_SOURCES}
    Flow.cpp
    Flow.hpp
    FlushVolCalc.cpp
    FlushVolCalc.hpp
    Format/3mf.cpp
    Format/3mf.hpp
    Format/AMF.cpp
    Format/AMF.hpp
    Format/ImportNames.cpp
    Format/ImportNames.hpp
    Format/AssembleList.cpp
    Format/AssembleList.hpp
    ${DRACO_SOURCES}
    Format/bbs_3mf.cpp
    Format/bbs_3mf.hpp
    Format/ProjectStrategy.hpp
    format.hpp
    Format/OBJ.cpp
    Format/OBJ.hpp
    Format/AssimpImport.hpp
    Format/AssimpImport.cpp
    Format/ResourcePathUtils.hpp
    Format/objparser.cpp
    Format/objparser.hpp
    Format/SL1.cpp
    Format/SL1.hpp
    Format/STEP.cpp
    Format/STEP.hpp
    Format/STL.cpp
    Format/STL.hpp
    Format/svg.cpp
    Format/svg.hpp
    Format/ZipperArchiveImport.cpp
    Format/ZipperArchiveImport.hpp
    GCode/AdaptivePAInterpolator.cpp
    GCode/AdaptivePAInterpolator.hpp
    GCode/AdaptivePAProcessor.cpp
    GCode/AdaptivePAProcessor.hpp
    GCode/AvoidCrossingPerimeters.cpp
    GCode/AvoidCrossingPerimeters.hpp
    GCode/BeltBackTransform.cpp
    GCode/BeltBackTransform.hpp
    GCode/MachineFrameTransform.cpp
    GCode/MachineFrameTransform.hpp
    GCode/BeltKinematics.cpp
    GCode/BeltKinematics.hpp
    GCode/MachineKinematics.cpp
    GCode/MachineKinematics.hpp
    GCode/ConflictChecker.cpp
    GCode/ConflictChecker.hpp
    GCode/CoolingBuffer.cpp
    GCode/CoolingBuffer.hpp
    GCode/ExclusionVolumePathCheck.cpp
    GCode/ExclusionVolumePathCheck.hpp
    GCode/ExclusionVolumeTravelAvoidance.cpp
    GCode/ExclusionVolumeTravelAvoidance.hpp
    GCode/TimelapsePosPicker.cpp
    GCode/TimelapsePosPicker.hpp
    GCode.cpp
    GCode/ExtrusionProcessor.hpp
    GCode/FanMover.cpp
    GCode/FanMover.hpp
    GCode/GCodeProcessor.cpp
    GCode/GCodeProcessor.hpp
    GCode/GCodeStatistics.cpp
    GCode/GCodeStatistics.hpp
    GCode/ElegooGCodeProcessorHelper.cpp
    GCode.hpp
    GCode/PchipInterpolatorHelper.cpp
    GCode/PchipInterpolatorHelper.hpp
    GCode/PressureEqualizer.cpp
    GCode/PressureEqualizer.hpp
    GCode/PrintExtents.cpp
    GCode/PrintExtents.hpp
    GCodeReader.cpp
    GCodeReader.hpp
    GCode/RetractWhenCrossingPerimeters.cpp
    GCode/RetractWhenCrossingPerimeters.hpp
    GCode/SeamPlacer.cpp
    GCode/SeamPlacer.hpp
    GCode/SeamContour.cpp
    GCode/SeamContour.hpp
    GCode/PreciseSeam.cpp
    GCode/PreciseSeam.hpp
    GCode/PreciseSeamInternal.hpp
    GCode/SmallAreaInfillFlowCompensator.cpp
    GCode/SmallAreaInfillFlowCompensator.hpp
    GCode/SpiralVase.cpp
    GCode/SpiralVase.hpp
    GCode/WipePathHelpers.cpp
    GCode/WipePathHelpers.hpp
    GCode/ThumbnailData.cpp
    GCode/ThumbnailData.hpp
    GCode/Thumbnails.cpp
    GCode/Thumbnails.hpp
    GCode/ToolOrdering.cpp
    GCode/ToolOrdering.hpp
    GCode/OrderingStrategies.cpp
    GCode/OrderingStrategies.hpp
    GCode/WipeTower2.cpp
    GCode/WipeTower2.hpp
    GCode/WipeTower.cpp
    GCode/WipeTower.hpp
    GCode/WipeTowerEstimate.cpp
    GCode/WipeTowerEstimate.hpp
    GCodeWriter.cpp
    GCodeWriter.hpp
    Geometry/ArcWelder.hpp
    Geometry/ArcWelder.cpp
    Geometry/Bicubic.hpp
    Geometry/Circle.cpp
    Geometry/Circle.hpp
    Geometry/ConvexHull.cpp
    Geometry/ConvexHull.hpp
    Geometry.cpp
    Geometry/Curves.hpp
    Geometry.hpp
    Geometry/MedialAxis.cpp
    Geometry/MedialAxis.hpp
    Geometry/Voronoi.cpp
    Geometry/Voronoi.hpp
    Geometry/VoronoiOffset.cpp
    Geometry/VoronoiOffset.hpp
    ${CGAL_SOURCES}
    Geometry/VoronoiUtils.cpp
    Geometry/VoronoiUtils.hpp
    Geometry/VoronoiVisualUtils.hpp
    IMEXArrange.cpp
    IMEXArrange.hpp
    IMEXHelpers.cpp
    IMEXHelpers.hpp
    IMEXZones.cpp
    IMEXZones.hpp
    InstanceLock.cpp
    InstanceLock.hpp
    Int128.hpp
    KDTreeIndirect.hpp
    Layer.cpp
    Layer.hpp
    LayerRegion.cpp
    LayOnFace.cpp
    LayOnFace.hpp
    libslic3r.cpp
    libslic3r.h
    Line.cpp
    Line.hpp
    LocalesUtils.cpp
    LocalesUtils.hpp
    MarchingSquares.hpp
    Measure.cpp
    Measure.hpp
    MeasureUtils.hpp
    MeshSplitImpl.hpp
    MinAreaBoundingBox.cpp
    MinAreaBoundingBox.hpp
    MinimumSpanningTree.cpp
    MinimumSpanningTree.hpp
    miniz_extension.cpp
    miniz_extension.hpp
    ModelArrange.cpp
    ModelArrange.hpp
    Model.cpp
    Model.hpp
    MTUtils.hpp
    MultiMaterialSegmentation.cpp
    MultiMaterialSegmentation.hpp
    MultiPoint.cpp
    MultiPoint.hpp
    MutablePolygon.cpp
    MutablePolygon.hpp
    MutablePriorityQueue.hpp
    NormalUtils.cpp
    NormalUtils.hpp
    NSVGUtils.cpp
    NSVGUtils.hpp
    ${OPENCV_SOURCES}
    ObjectID.cpp
    ObjectID.hpp
    Optimize/BruteforceOptimizer.hpp
    Optimize/NLoptOptimizer.hpp
    Optimize/Optimizer.hpp
    Orient.cpp
    Orient.hpp
    ParallelResolve.hpp
    ParameterUtils.cpp
    ParameterUtils.hpp
    pchheader.cpp
    pchheader.hpp
    PerimeterGenerator.cpp
    PerimeterGenerator.hpp
    PlaceholderParser.cpp
    PlaceholderParser.hpp
    Platform.cpp
    Platform.hpp
    PNGReadWrite.cpp
    PNGReadWrite.hpp
    Point.cpp
    Point.hpp
    Polygon.cpp
    Polygon.hpp
    PolygonTrimmer.cpp
    PolygonTrimmer.hpp
    Polyline.cpp
    Polyline.hpp
    PresetBundle.cpp
    PresetBundle.hpp
    PrinterBedAssets.hpp
    PresetCacheFormat.cpp
    PresetCacheFormat.hpp
    Preset.cpp
    Preset.hpp
    PrincipalComponents2D.cpp
    PrincipalComponents2D.hpp
    PublishSettings.cpp
    PublishSettings.hpp
    PrintApply.cpp
    PrintBase.cpp
    PrintBase.hpp
    MaterialType.cpp
    MaterialType.hpp
    PrintConfig.cpp
    PrintConfig.hpp
    Print.cpp
    Print.hpp
    PrintObject.cpp
    PrintObjectSlice.cpp
    PrintRegion.cpp
    ProjectTask.cpp
    ProjectTask.hpp
    QuadricEdgeCollapse.cpp
    QuadricEdgeCollapse.hpp
    Semver.cpp
    ShortEdgeCollapse.cpp
    ShortEdgeCollapse.hpp
    ShortestPath.cpp
    ShortestPath.hpp
    SLA/AGGRaster.hpp
    SLA/BoostAdapter.hpp
    SLA/Clustering.cpp
    SLA/Clustering.hpp
    SLA/ConcaveHull.cpp
    SLA/ConcaveHull.hpp
    SLA/Concurrency.hpp
    ${OPENVDB_SOURCES}
    SLA/IndexedMesh.cpp
    SLA/IndexedMesh.hpp
    SLA/JobController.hpp
    SLA/Pad.cpp
    SLA/Pad.hpp
    SLAPrint.cpp
    SLAPrint.hpp
    SLAPrintSteps.cpp
    SLAPrintSteps.hpp
    SLA/RasterBase.cpp
    SLA/RasterBase.hpp
    SLA/RasterToPolygons.cpp
    SLA/RasterToPolygons.hpp
    SLA/ReprojectPointsOnMesh.hpp
    SLA/Rotfinder.cpp
    SLA/Rotfinder.hpp
    SLA/SpatIndex.cpp
    SLA/SpatIndex.hpp
    SLA/SupportPointGenerator.cpp
    SLA/SupportPointGenerator.hpp
    SLA/SupportPoint.hpp
    SLA/SupportTreeBuilder.cpp
    SLA/SupportTreeBuilder.hpp
    SLA/SupportTreeBuildsteps.cpp
    SLA/SupportTreeBuildsteps.hpp
    SLA/SupportTree.cpp
    SLA/SupportTree.hpp
    SLA/SupportTreeMesher.cpp
    SLA/SupportTreeMesher.hpp
    SlicesToTriangleMesh.cpp
    SlicesToTriangleMesh.hpp
    SlicingAdaptive.cpp
    SlicingAdaptive.hpp
    Slicing.cpp
    Slicing.hpp
    Support/BeltFloorContext.cpp
    Support/BeltFloorContext.hpp
    Support/SupportCommon.cpp
    Support/SupportCommon.hpp
    Support/SupportLayer.hpp
    Support/SupportMaterial.cpp
    Support/SupportMaterial.hpp
    Support/SupportParameters.hpp
    Support/SupportSpotsGenerator.cpp
    Support/SupportSpotsGenerator.hpp
    Support/TreeModelVolumes.cpp
    Support/TreeModelVolumes.hpp
    Support/TreeSupport3D.cpp
    Support/TreeSupport3D.hpp
    Support/TreeSupportCommon.hpp
    Support/TreeSupport.cpp
    Support/TreeSupport.hpp
    SurfaceCollection.cpp
    SurfaceCollection.hpp
    Surface.cpp
    Surface.hpp
    SurfaceMesh.hpp
    SVG.cpp
    SVG.hpp
    Technologies.hpp
    Tesselate.cpp
    Tesselate.hpp
    TextConfiguration.hpp
    TextureBake/TextureBakeIndex.cpp
    TextureBake/TextureBakeIndex.hpp
    TextureBake/TextureBakeSubdivide.cpp
    TextureBake/TextureBakeSubdivide.hpp
    TextureBake/TextureBakeRegularize.cpp
    TextureBake/TextureBakeRegularize.hpp
    TextureBake/TextureBakeRelocate.cpp
    TextureBake/TextureBakeRelocate.hpp
    TextureBake/TextureBakeFlip.cpp
    TextureBake/TextureBakeFlip.hpp
    TextureBake/TextureBakeDebug.cpp
    TextureBake/TextureBakeDebug.hpp
    TextureBake/TextureBakeDisplace.cpp
    TextureBake/TextureBakeDisplace.hpp
    TextureBake/TextureBakeDecimate.cpp
    TextureBake/TextureBakeDecimate.hpp
    TextureBake/TextureBakeRepair.cpp
    TextureBake/TextureBakeRepair.hpp
    TextureBake/TextureBakePipeline.cpp
    TextureBake/TextureBakePipeline.hpp
    TextureBake/TextureBakeMesh.cpp
    TextureBake/TextureBakeMesh.hpp
    Thread.cpp
    Thread.hpp
    Time.cpp
    Time.hpp
    Timer.cpp
    Timer.hpp
    TriangleMesh.cpp
    TriangleMesh.hpp
    TriangleMeshDeal.cpp
    TriangleMeshDeal.hpp
    TriangleMeshSlicer.cpp
    TriangleMeshSlicer.hpp
    TriangleSelector.cpp
    TriangleSelector.hpp
    TriangleSetSampling.cpp
    TriangleSetSampling.hpp
    TriangulateWall.cpp
    TriangulateWall.hpp
    utils.cpp
    Utils.hpp
    VariableWidth.cpp
    VariableWidth.hpp
    Zipper.cpp
    Zipper.hpp
    FilamentGroup.hpp
    FilamentGroup.cpp
    FilamentGroupUtils.hpp
    FilamentGroupUtils.cpp
    MultiNozzleUtils.hpp
    MultiNozzleUtils.cpp
    GCode/ToolOrderUtils.hpp
    GCode/ToolOrderUtils.cpp
    FlushVolPredictor.hpp
    FlushVolPredictor.cpp
)

# Parametric Design/CAD kernel. Needs the OCCT toolkits TKFillet, TKOffset and TKBool and the
# vendored SolveSpace solver; both are pulled in only when SLIC3R_CAD is ON.
if (SLIC3R_CAD)
    list(APPEND lisbslic3r_sources
            CAD/GeometryEngine.cpp
            CAD/GeometryEngine.hpp
            CAD/SketchEngine.cpp
            CAD/SketchEngine.hpp
            CAD/SketchConstraints.cpp
            CAD/SketchConstraints.hpp
            CAD/SketchSolver.cpp
            CAD/SketchSolver.hpp
            CAD/SketchInference.cpp
            CAD/SketchInference.hpp
            CAD/SketchImport.cpp
            CAD/SketchImport.hpp
            CAD/CadDocument.cpp
            CAD/CadDocument.hpp
            CAD/ThreadStandards.cpp
            CAD/ThreadStandards.hpp
        )
endif ()

if (APPLE)
    list(APPEND lisbslic3r_sources
            MacUtils.mm
            Format/ModelIO.hpp
            Format/ModelIO.mm
        )
endif ()

add_library(libslic3r STATIC ${lisbslic3r_sources}
    "${CMAKE_CURRENT_BINARY_DIR}/libslic3r_version.h"
    ${OpenVDBUtils_SOURCES})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${lisbslic3r_sources})

# Add CAD fallback sources if SLIC3R_CAD is OFF
if (NOT SLIC3R_CAD AND LIBSLIC3R_CAD_FALLBACK_SOURCES)
    target_sources(libslic3r PRIVATE ${LIBSLIC3R_CAD_FALLBACK_SOURCES})
endif()

if (SLIC3R_STATIC)
    set(CGAL_Boost_USE_STATIC_LIBS ON CACHE BOOL "" FORCE)
endif ()
set(CGAL_DO_NOT_WARN_ABOUT_CMAKE_BUILD_TYPE ON CACHE BOOL "" FORCE)

if (SLIC3R_CAD)
    cmake_policy(PUSH)
    cmake_policy(SET CMP0011 NEW)
    # CGAL's config resets policies (cmake_minimum_required ...3.31), so a plain SET
    # can't reach it; the default opts its Boost lookup into BoostConfig (CMP0167).
    set(CMAKE_POLICY_DEFAULT_CMP0167 NEW)
    find_package(CGAL 6.2 REQUIRED)
    find_package(OpenCV REQUIRED core)
    unset(CMAKE_POLICY_DEFAULT_CMP0167)
    cmake_policy(POP)
    set(OPENCV_SOURCES 
        "TexturePainting.hpp;TexturePainting.cpp"
        "TextureToColor/TextureToColor.hpp;TextureToColor/TextureToColor.cpp"
        "TextureToColor/ColorUtils.hpp;TextureToColor/ColorUtils.cpp"
        "ObjColorUtils.cpp;ObjColorUtils.hpp"
    )
else()
    set(OPENCV_SOURCES "")
endif()

# assimp is used for textured model import (Format/AssimpImport.cpp)
find_package(assimp REQUIRED)



encoding_check(libslic3r)

target_compile_definitions(libslic3r PUBLIC -DUSE_TBB -DTBB_USE_CAPTURED_EXCEPTION=0)
if (SLIC3R_CAD)
    # CGAL changes Kd_tree's member layout when TBB is enabled. Match libslic3r_cgal
    # in every consumer so inline constructors/destructors cannot mix the two layouts.
    target_compile_definitions(libslic3r PUBLIC CGAL_LINKED_WITH_TBB)
endif()
if (USE_SLIC3R_CONSOLE_LOG)
    target_compile_definitions(libslic3r PRIVATE $<$<CONFIG:RelWithDebInfo>:SLIC3R_CONSOLE_LOG>)
endif()
target_include_directories(libslic3r PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} PUBLIC ${CMAKE_CURRENT_BINARY_DIR})
if (SLIC3R_CAD)
    target_include_directories(libslic3r PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/TextureToColor)
endif()
target_include_directories(libslic3r SYSTEM PUBLIC ${EXPAT_INCLUDE_DIRS})
if (WIN32)
    # Expat is linked statically; upstream headers declare the API __declspec(dllimport)
    # unless XML_STATIC is defined.
    target_compile_definitions(libslic3r PUBLIC XML_STATIC)
endif()

# Find the OCCT and related libraries
set(OpenCASCADE_DIR "${CMAKE_PREFIX_PATH}/lib/cmake/occt")
find_package(OpenCASCADE REQUIRED)
target_include_directories(libslic3r SYSTEM PUBLIC ${OpenCASCADE_INCLUDE_DIR})

find_package(JPEG REQUIRED)
find_package(Draco)
if (DRACO_FOUND)
    set(DRACO_SOURCES "Format/DRC.cpp;Format/DRC.hpp")
else()
    set(DRACO_SOURCES "")
endif()

# deps/OCCT/OCCT.cmake builds exactly these toolkits; keep its _occt_toolkits list in step.
set(OCCT_LIBS
    TKDESTEP
    TKDE
    TKXCAF
    TKXSBase
    TKVCAF
    TKCAF
    TKLCAF
    TKCDF
    TKV3d
    TKService
    TKMesh
    TKBO
    TKPrim
    TKHLR
    TKShHealing
    TKTopAlgo
    TKGeomAlgo
    TKBRep
    TKGeomBase
    TKG3d
    TKG2d
    TKMath
    TKernel
)
# The CAD kernel is the only consumer of TKFillet (BRepFilletAPI), TKOffset (BRepOffsetAPI)
# and TKBool, which the other two need.
#
# PREPEND, never append: this list is single-pass static link order, dependents before
# dependencies — note TKernel, which everything needs, is deliberately last. TKOffset
# references BRepAlgo_Loop, which TKBool defines, so TKOffset must come BEFORE TKBool.
# Appending put it after, and a strictly single-pass linker (the Flatpak build) failed with
#     libTKOffset.a(BRepOffset_MakeLoops.cxx.o): undefined reference to
#     `BRepAlgo_Loop::BRepAlgo_Loop()'
# while the ordinary Linux, macOS and Windows links resolved it anyway. Use set() rather
# than list(PREPEND), which needs CMake 3.15 and this project supports 3.13.
if (SLIC3R_CAD)
    set(OCCT_LIBS TKFillet TKOffset TKBool ${OCCT_LIBS})
    # deps is configured separately, so its SLIC3R_CAD can differ from ours. deps/OCCT/OCCT.cmake
    # builds the three CAD toolkits together under SLIC3R_CAD, so one absent toolkit proves it
    # was OFF there; fail here rather than at link time.
    if (NOT TARGET TKFillet)
        message(FATAL_ERROR
            "SLIC3R_CAD is ON, but the dependencies in ${CMAKE_PREFIX_PATH} were built with "
            "SLIC3R_CAD=OFF (OpenCASCADE has no TKFillet). Rebuild the dependencies with -DSLIC3R_CAD=ON, "
            "or configure this project with -DSLIC3R_CAD=OFF.")
    endif ()
endif ()
# Published for the Windows packaging step in the top-level CMakeLists.txt.
set(OCCT_LIBS "${OCCT_LIBS}" CACHE INTERNAL "OCCT toolkits linked by libslic3r")
# OCCT built with BUILD_USE_PCH exports the precompiled headers of TKernel, TKMath, TKBRep
# and TKBool to consumers, so CMake would merge them into libslic3r's own precompiled header
# (pulling <windows.h> into every libslic3r source on Windows). They only speed up OCCT's
# own build; keep them out of ours. OpenCASCADE_LIBRARIES lists every installed toolkit, so
# this also covers toolkits reached only through another toolkit's link interface.
foreach (_occt_lib IN LISTS OpenCASCADE_LIBRARIES OCCT_LIBS)
    if (TARGET ${_occt_lib})
        set_property(TARGET ${_occt_lib} PROPERTY INTERFACE_PRECOMPILE_HEADERS "")
    endif ()
endforeach ()
if (WIN32)
    # OCCT installs RelWithDebInfo and Debug builds to bin/occti and bin/occtd, so each build
    # type's DLL directory is read from the imported target, falling back to the first installed
    # build, as CMake does when linking.
    get_target_property(_occt_configs TKernel IMPORTED_CONFIGURATIONS)
    list(GET _occt_configs 0 _occt_fallback)
    foreach (_build_config IN ITEMS DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
        set(_occt_config ${_occt_fallback})
        if (_build_config IN_LIST _occt_configs)
            set(_occt_config ${_build_config})
        endif ()
        get_target_property(_occt_dll TKernel IMPORTED_LOCATION_${_occt_config})
        get_filename_component(_occt_bin "${_occt_dll}" DIRECTORY)
        set(OCCT_BIN_DIR_${_build_config} "${_occt_bin}" CACHE INTERNAL "OCCT DLL directory for ${_build_config} builds")
    endforeach ()
endif ()

target_link_libraries(libslic3r
    PUBLIC
        Eigen3::Eigen
        admesh
        libigl
        libnest2d
        miniz
        ${OPENCV_TARGET}
        assimp::assimp
    PRIVATE
        ${CMAKE_DL_LIBS}
        ${EXPAT_LIBRARIES}
        ${OCCT_LIBS}
        boost_libs
        cereal::cereal
        Clipper2
        draco::draco
        glu-libtess
        JPEG::JPEG
        ${SLIC3R_CAD_LIBS}
        mcut
        noise::noise
        PNG::PNG
        qhull
        qoi
        semver
        TBB::tbb
        TBB::tbbmalloc
        ZLIB::ZLIB
        OpenSSL::Crypto
    )

if (SLIC3R_CAD)
    set(SLIC3R_CAD_LIBS libslic3r_cgal)
else()
    set(SLIC3R_CAD_LIBS "")
endif()

if (DRACO_FOUND)
    set(DRACO_TARGET draco::draco)
else()
    set(DRACO_TARGET "")
endif()

if (SLIC3R_CAD)
    set(OPENCV_TARGET opencv_world)
else()
    set(OPENCV_TARGET "")
endif()

if (APPLE)
    find_library(FOUNDATION Foundation REQUIRED)
    find_library(MODELIO ModelIO REQUIRED)
    target_link_libraries(libslic3r PRIVATE ${FOUNDATION} ${MODELIO})
endif ()

if (SLIC3R_CAD AND TARGET OpenVDB::openvdb)
    target_link_libraries(libslic3r PRIVATE OpenVDB::openvdb)
endif()

if (SLIC3R_CAD)
    target_link_libraries(libslic3r PUBLIC SLVS::slvs)
endif ()

if(WIN32)
    target_link_libraries(libslic3r PRIVATE Psapi.lib bcrypt.lib)
endif()

if(SLIC3R_PROFILE)
    target_link_libraries(libslic3r PRIVATE Shiny)
endif()

if (WIN32)
    # Public, since BlacklistedLibraryCheck.hpp includes windows.h. Empty
    # WIN32_LEAN_AND_MEAN matches the sources that define it themselves; bare
    # NOMINMAX matches the one libigl already passes.
    target_compile_definitions(libslic3r PUBLIC "WIN32_LEAN_AND_MEAN=" "NOMINMAX")
endif ()

if (SLIC3R_PCH AND NOT SLIC3R_SYNTAXONLY)
    add_precompiled_header(libslic3r pchheader.hpp FORCEINCLUDE)
endif ()
