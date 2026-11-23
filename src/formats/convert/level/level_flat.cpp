#include "formats/convert/level/level_flat.h"

#include <cmath>
#include <cstdio>
#include <map>
#include <utility>

namespace sh::convert::level {
namespace {

constexpr float kUnit = 256.0f;  // Q7.8
constexpr float kCellUnits = 40.0f;

// Normal k owns the next normals[k].count slots of the shade stream.
std::vector<int> slot_groups(const LmMesh& mesh) {
    std::vector<int> groups;
    groups.reserve(mesh.shade.size());
    for (std::size_t k = 0; k < mesh.normals.size(); ++k) {
        groups.insert(groups.end(), mesh.normals[k].count, static_cast<int>(k));
    }
    return groups;
}

std::array<float, 3> flat_normal(const LmNormal& n) {
    // Outward is -n; turning it by 180 degrees about Z gives (nx, ny, -nz).
    float x = static_cast<float>(n.x);
    float y = static_cast<float>(n.y);
    float z = -static_cast<float>(n.z);
    const float length = std::sqrt(x * x + y * y + z * z);
    if (length == 0.0f) {
        return {0.0f, 1.0f, 0.0f};
    }
    return {x / length, y / length, z / length};
}

}  // namespace

FlatModel flatten_model(const LmModel& model, const std::vector<LmMaterial>& materials) {
    FlatModel out;
    out.name = model.name;
    // Bit 15 of a primitive becomes the GPU's semi-transparency bit only on the lit chain and in the far drawer. The
    // flat chain draws every textured primitive without it (the bit there only orders its fog overlay), so the STP
    // texels of a flat model are solid: the hospital catwalk grates of Nowhere are cut-outs, not additive glass.
    const bool semi_capable = model.lit() != 0 || model.far_drawer() != 0;
    for (std::size_t mesh_index = 0; mesh_index < model.meshes.size(); ++mesh_index) {
        const LmMesh& mesh = model.meshes[mesh_index];
        const std::vector<int> groups = slot_groups(mesh);

        struct Builder {
            FlatMesh mesh;
            std::map<std::array<int, 4>, std::uint32_t> vertex_ids;
        };
        std::vector<Builder> builders;
        std::map<std::array<int, 3>, std::size_t> builder_of;

        for (const LmPrimitive& prim : mesh.primitives) {
            ++out.source_primitives;
            const int corners = prim.is_triangle() ? 3 : 4;
            bool in_window = true;
            for (int c = 0; c < corners; ++c) {
                const int local = static_cast<int>(prim.vertex[c]) - model.vertex_offset;
                in_window = in_window && local >= 0 && local < static_cast<int>(mesh.vertices.size());
            }
            if (!in_window) {
                ++out.skipped_primitives;
                continue;
            }

            const bool transparent = semi_capable && prim.transparent();
            const std::array<int, 3> key = {prim.material_index(), prim.clut, transparent ? 1 : 0};
            auto found = builder_of.find(key);
            if (found == builder_of.end()) {
                found = builder_of.emplace(key, builders.size()).first;
                Builder b;
                b.mesh.material_index = prim.material_index();
                if (prim.material_index() < static_cast<int>(materials.size())) {
                    b.mesh.material_name = materials[prim.material_index()].name;
                    b.mesh.clut_base = materials[prim.material_index()].clut_base;
                }
                b.mesh.clut = prim.clut;
                b.mesh.transparent = transparent;
                char suffix[48];
                std::snprintf(suffix, sizeof(suffix), "/%zu/%d/%04x%s", mesh_index, prim.material_index(), prim.clut,
                              transparent ? "/alpha" : "");
                b.mesh.name = model.name + suffix;
                builders.push_back(std::move(b));
            }
            Builder& b = builders[found->second];

            std::uint32_t ids[4] = {};
            for (int c = 0; c < corners; ++c) {
                const int local = static_cast<int>(prim.vertex[c]) - model.vertex_offset;
                const int slot = static_cast<int>(prim.shade_slot[c]) - model.normal_offset;
                int group = -1;
                if (!mesh.shade.empty()) {
                    if (slot >= 0 && slot < static_cast<int>(groups.size())) {
                        group = groups[slot];
                    }
                } else if (slot >= 0 && slot < static_cast<int>(mesh.normals.size())) {
                    group = slot;  // no shade stream: the slot is the normal index itself
                }
                const std::array<int, 4> id_key = {local, group, prim.uv[c][0], prim.uv[c][1]};
                auto [it, inserted] = b.vertex_ids.emplace(id_key, static_cast<std::uint32_t>(b.mesh.positions.size() / 3));
                ids[c] = it->second;
                if (inserted) {
                    const Vec3s& v = mesh.vertices[local];
                    b.mesh.positions.insert(b.mesh.positions.end(),
                                            {-v.x / kUnit, -v.y / kUnit, v.z / kUnit});
                    const std::array<float, 3> n =
                        group >= 0 ? flat_normal(mesh.normals[group]) : std::array<float, 3>{0.0f, 1.0f, 0.0f};
                    b.mesh.normals.insert(b.mesh.normals.end(), {n[0], n[1], n[2]});
                    b.mesh.uvs.push_back(prim.uv[c][0] + 0.5f);
                    b.mesh.uvs.push_back(prim.uv[c][1] + 0.5f);
                }
            }
            // A triangle stored as a quad repeats a corner; the GPU draws nothing for the zero-area half, so it is
            // left out (glTF validators flag degenerate triangles).
            const auto emit = [&](int a, int c1, int c2) {
                if (prim.vertex[a] == prim.vertex[c1] || prim.vertex[a] == prim.vertex[c2] ||
                    prim.vertex[c1] == prim.vertex[c2]) {
                    ++out.degenerate_triangles;
                    return;
                }
                b.mesh.indices.insert(b.mesh.indices.end(), {ids[a], ids[c1], ids[c2]});
            };
            emit(0, 2, 1);
            if (corners == 4) {
                emit(1, 2, 3);
            }
        }
        for (Builder& b : builders) {
            out.meshes.push_back(std::move(b.mesh));
        }
    }
    return out;
}

std::vector<FlatModel> flatten_plm(const LmFile& plm) {
    std::vector<FlatModel> models;
    models.reserve(plm.models.size());
    for (const LmModel& model : plm.models) {
        models.push_back(flatten_model(model, plm.materials));
    }
    return models;
}

FlatLevel flatten_ipd(const IpdFile& ipd, const LmFile* global_plm) {
    FlatLevel level;
    const float origin_x = ipd.cell_x * kCellUnits;
    const float origin_z = ipd.cell_z * kCellUnits;

    // (is_global, index in the LM that holds the model) -> index in level.models
    std::map<std::pair<bool, std::size_t>, int> built;
    auto find_model = [](const LmFile& lm, const std::string& name) -> int {
        for (std::size_t i = 0; i < lm.models.size(); ++i) {
            if (lm.models[i].name == name) {
                return static_cast<int>(i);
            }
        }
        return -1;
    };

    for (const IpdModelBuffer& buffer : ipd.model_buffers) {
        for (const IpdModelInstance& instance : buffer.instances) {
            FlatInstance flat;
            flat.model_info_index = instance.model_info_index;
            const IpdModelInfo& info = ipd.model_infos[instance.model_info_index];
            flat.model_name = info.name;
            flat.is_global_plm = info.is_global_plm != 0;

            const LmFile* source = flat.is_global_plm ? global_plm : &ipd.lm;
            const int model_index = source != nullptr ? find_model(*source, info.name) : -1;
            if (model_index < 0) {
                ++level.unresolved_instances;
            } else {
                const auto key = std::make_pair(flat.is_global_plm, static_cast<std::size_t>(model_index));
                auto it = built.find(key);
                if (it == built.end()) {
                    level.models.push_back(flatten_model(source->models[model_index], source->materials));
                    it = built.emplace(key, static_cast<int>(level.models.size()) - 1).first;
                }
                flat.flat_model = it->second;
            }

            // The same 180-degree turn about Z on both sides of the rotation: m' = S m S, S = diag(-1, -1, 1).
            constexpr float f[3] = {-1.0f, -1.0f, 1.0f};
            for (int r = 0; r < 3; ++r) {
                for (int c = 0; c < 3; ++c) {
                    flat.matrix[c * 4 + r] = f[r] * f[c] * instance.rotation[r * 3 + c] / 4096.0f;
                }
            }
            flat.matrix[12] = -(instance.translation[0] / kUnit + origin_x);
            flat.matrix[13] = -instance.translation[1] / kUnit;
            flat.matrix[14] = instance.translation[2] / kUnit + origin_z;
            flat.matrix[15] = 1.0f;
            level.instances.push_back(std::move(flat));
        }
        for (const IpdVec4& b : buffer.billboards) {
            level.billboards.push_back({{-(b.x / kUnit + origin_x), -b.y / kUnit, b.z / kUnit + origin_z}, b.w});
        }
    }
    return level;
}

}  // namespace sh::convert::level
