#pragma once

#include "TextureDisplacement.hpp"
#include "TriangleMesh.hpp"
#include <string>
#include <vector>

namespace Slic3r {

// Fallback implementations for texture displacement when CAD is not available
bool texture_displacement_layers_from_json(const std::string &text, std::vector<TextureDisplacementLayer> &layers, TextureDisplacementOptions &opts);
std::string texture_displacement_layers_to_json(const std::vector<TextureDisplacementLayer> &layers, const TextureDisplacementOptions &opts);
DecodedHeightTexture decode_height_texture(const TextureDisplacementLayer &layer, bool);
DecodedHeightTexture apply_texture_adjustments(const DecodedHeightTexture &src, const TextureAdjustments &adj);
void sample_layer_height(const DecodedHeightTexture&, const TextureDisplacementLayer&, const Eigen::Matrix<float, 3, 1>&, const Eigen::Matrix<float, 3, 1>&, const Eigen::Matrix<float, 3, 1>&, const Eigen::Matrix<float, 3, 1>&, const Eigen::Matrix<float, 2, 1>*);
DecodedHeightTexture downscale_height_texture(const DecodedHeightTexture &src, int max_px);
void cut_mesh_at_steps(const indexed_triangle_set&, const std::vector<unsigned char>&, const std::function<float(const Eigen::Matrix<float, 3, 1>&, const Eigen::Matrix<float, 3, 1>&)>&, float, float, float, std::vector<int>*, unsigned long*);
indexed_triangle_set subdivide_mesh_uniform(const indexed_triangle_set &mesh, float max_edge_length_mm, int max_iterations);
void build_texture_displacement(const indexed_triangle_set&, const std::vector<TextureDisplacementLayer>&, const std::array<TriangleSelector::TriangleSplittingData, 8>&, const TextureDisplacementOptions&, const std::function<bool(int)>&, const TextureColorRequest*, const Eigen::Transform<double, 3, 2>&, BakeStageRecorder*, TextureBakeStats*, const std::vector<Eigen::Matrix<float, 2, 1>>*, const Eigen::Matrix<double, 3, 1>*, float);
void make_combined_displacement_sampler(const indexed_triangle_set&, const std::vector<TextureDisplacementLayer>&, const std::array<TriangleSelector::TriangleSplittingData, 8>&, const std::vector<Eigen::Matrix<float, 2, 1>>*);
std::vector<Vec3f> texture_displacement_vertex_normals(const indexed_triangle_set&);
void texture_displacement_patch_frame(const indexed_triangle_set&, const std::vector<Eigen::Matrix<float, 3, 1>>&, Eigen::Matrix<float, 3, 1>&, Eigen::Matrix<float, 3, 1>&, Eigen::Matrix<float, 3, 1>&);
std::vector<uint8_t> transfer_facet_paint(const indexed_triangle_set &src_mesh, const TriangleSelector::TriangleSplittingData &splitting_data, const indexed_triangle_set &dst_mesh);

} // namespace Slic3r
