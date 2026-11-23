#include "formats/gltf/gltf_inspect.h"

#include "formats/gltf/cgltf_prefixed.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <memory>
#include <set>

namespace sh::gltf {
namespace {

struct DataDeleter {
    void operator()(cgltf_data* d) const { cgltf_free(d); }
};

const char* result_text(cgltf_result r) {
    switch (r) {
        case cgltf_result_success: return "success";
        case cgltf_result_data_too_short: return "data too short";
        case cgltf_result_unknown_format: return "unknown format";
        case cgltf_result_invalid_json: return "invalid json";
        case cgltf_result_invalid_gltf: return "invalid gltf";
        case cgltf_result_invalid_options: return "invalid options";
        case cgltf_result_file_not_found: return "file not found";
        case cgltf_result_io_error: return "io error";
        case cgltf_result_out_of_memory: return "out of memory";
        case cgltf_result_legacy_gltf: return "legacy gltf";
        default: return "unknown error";
    }
}

}  // namespace

Result<GlbStats> inspect_glb(std::span<const std::uint8_t> glb, bool keep_triangles) {
    using Out = Result<GlbStats>;
    cgltf_options options;
    std::memset(&options, 0, sizeof options);
    cgltf_data* raw = nullptr;
    cgltf_result r = cgltf_parse(&options, glb.data(), glb.size(), &raw);
    std::unique_ptr<cgltf_data, DataDeleter> data(raw);
    if (r != cgltf_result_success) {
        return Out::fail(std::string("cgltf_parse: ") + result_text(r));
    }
    if (data->file_type != cgltf_file_type_glb) {
        return Out::fail("cgltf_parse: not a binary glTF");
    }
    // The only buffer of a .glb is the BIN chunk; a buffer with a uri would be an external file, which is not
    // allowed here, so no base path is given and cgltf refuses to open anything.
    r = cgltf_load_buffers(&options, data.get(), nullptr);
    if (r != cgltf_result_success) {
        return Out::fail(std::string("cgltf_load_buffers: ") + result_text(r));
    }
    r = cgltf_validate(data.get());
    if (r != cgltf_result_success) {
        return Out::fail(std::string("cgltf_validate: ") + result_text(r));
    }

    GlbStats stats;
    stats.nodes = data->nodes_count;
    stats.materials = data->materials_count;
    stats.skins = data->skins_count;
    stats.animations = data->animations_count;
    for (cgltf_size i = 0; i < data->images_count; ++i) {
        stats.image_uris.emplace_back(data->images[i].uri ? data->images[i].uri : "");
    }

    for (cgltf_size m = 0; m < data->meshes_count; ++m) {
        const cgltf_mesh& mesh = data->meshes[m];
        MeshStats ms;
        ms.name = mesh.name ? mesh.name : "";
        std::set<std::array<std::uint32_t, 3>> seen;
        bool first_position = true;
        for (cgltf_size p = 0; p < mesh.primitives_count; ++p) {
            const cgltf_primitive& prim = mesh.primitives[p];
            if (prim.type != cgltf_primitive_type_triangles) {
                return Out::fail("mesh " + ms.name + ": primitive is not a triangle list");
            }
            const cgltf_accessor* pos = cgltf_find_accessor(&prim, cgltf_attribute_type_position, 0);
            if (!pos || !prim.indices) {
                return Out::fail("mesh " + ms.name + ": primitive without POSITION or indices");
            }
            for (cgltf_size a = 0; a < prim.attributes_count; ++a) {
                const cgltf_accessor* acc = prim.attributes[a].data;
                if (acc->count != pos->count) {
                    return Out::fail("mesh " + ms.name + ": attribute count differs from POSITION");
                }
                // cgltf_validate does not read element data; unpack everything so a bad view fails here.
                const cgltf_size comps = cgltf_num_components(acc->type);
                std::vector<cgltf_float> tmp(acc->count * comps);
                if (cgltf_accessor_unpack_floats(acc, tmp.data(), tmp.size()) != tmp.size()) {
                    return Out::fail("mesh " + ms.name + ": attribute data cannot be read");
                }
            }
            for (cgltf_size v = 0; v < pos->count; ++v) {
                cgltf_float xyz[3];
                if (!cgltf_accessor_read_float(pos, v, xyz, 3)) {
                    return Out::fail("mesh " + ms.name + ": POSITION data cannot be read");
                }
                std::array<std::uint32_t, 3> key;
                std::memcpy(key.data(), xyz, sizeof key);
                seen.insert(key);
                for (std::size_t c = 0; c < 3; ++c) {
                    ms.min[c] = first_position ? xyz[c] : std::min(ms.min[c], xyz[c]);
                    ms.max[c] = first_position ? xyz[c] : std::max(ms.max[c], xyz[c]);
                }
                first_position = false;
            }
            if (prim.indices->count % 3 != 0) {
                return Out::fail("mesh " + ms.name + ": index count is not a multiple of 3");
            }
            for (cgltf_size i = 0; i < prim.indices->count; ++i) {
                if (cgltf_accessor_read_index(prim.indices, i) >= pos->count) {
                    return Out::fail("mesh " + ms.name + ": index out of range");
                }
            }
            if (keep_triangles) {
                for (cgltf_size i = 0; i < prim.indices->count; ++i) {
                    cgltf_float xyz[3];
                    cgltf_accessor_read_float(pos, cgltf_accessor_read_index(prim.indices, i), xyz, 3);
                    ms.triangle_positions.insert(ms.triangle_positions.end(), xyz, xyz + 3);
                }
            }
            ms.primitives += 1;
            ms.vertices += pos->count;
            ms.triangles += prim.indices->count / 3;
        }
        ms.unique_positions = seen.size();
        stats.vertices += ms.vertices;
        stats.triangles += ms.triangles;
        stats.meshes.push_back(std::move(ms));
    }
    return Out::ok(std::move(stats));
}

Result<GlbSceneData> read_glb_scene(std::span<const std::uint8_t> glb) {
    using Out = Result<GlbSceneData>;
    if (auto checked = inspect_glb(glb); !checked) {
        return Out::fail(checked.error());
    }
    cgltf_options options;
    std::memset(&options, 0, sizeof options);
    cgltf_data* raw = nullptr;
    const cgltf_result parsed = cgltf_parse(&options, glb.data(), glb.size(), &raw);
    std::unique_ptr<cgltf_data, DataDeleter> data(raw);
    if (parsed != cgltf_result_success) {
        return Out::fail("cgltf_parse failed");
    }
    if (cgltf_load_buffers(&options, data.get(), nullptr) != cgltf_result_success) {
        return Out::fail("cgltf_load_buffers failed");
    }
    const auto unpack = [](const cgltf_accessor* acc) {
        std::vector<float> v(acc->count * cgltf_num_components(acc->type));
        cgltf_accessor_unpack_floats(acc, v.data(), v.size());
        return v;
    };

    GlbSceneData scene;
    for (cgltf_size i = 0; i < data->nodes_count; ++i) {
        const cgltf_node& n = data->nodes[i];
        GlbNodeData nd;
        nd.name = n.name ? n.name : "";
        nd.parent = n.parent ? static_cast<int>(cgltf_node_index(data.get(), n.parent)) : -1;
        nd.mesh = n.mesh ? static_cast<int>(cgltf_mesh_index(data.get(), n.mesh)) : -1;
        nd.skin = n.skin ? static_cast<int>(cgltf_skin_index(data.get(), n.skin)) : -1;
        if (n.has_translation) {
            std::memcpy(nd.translation.data(), n.translation, sizeof nd.translation);
        }
        if (n.has_rotation) {
            std::memcpy(nd.rotation.data(), n.rotation, sizeof nd.rotation);
        }
        if (n.has_scale) {
            std::memcpy(nd.scale.data(), n.scale, sizeof nd.scale);
        }
        if (n.has_matrix) {
            nd.has_matrix = true;
            std::memcpy(nd.matrix.data(), n.matrix, sizeof nd.matrix);
        }
        scene.nodes.push_back(nd);
    }
    for (cgltf_size i = 0; i < data->skins_count; ++i) {
        const cgltf_skin& sk = data->skins[i];
        GlbSkinData sd;
        for (cgltf_size j = 0; j < sk.joints_count; ++j) {
            sd.joints.push_back(static_cast<int>(cgltf_node_index(data.get(), sk.joints[j])));
        }
        sd.inverse_bind.assign(sk.joints_count, {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1});
        if (sk.inverse_bind_matrices) {
            const std::vector<float> m = unpack(sk.inverse_bind_matrices);
            for (cgltf_size j = 0; j < sk.joints_count && (j + 1) * 16 <= m.size(); ++j) {
                std::copy(m.begin() + static_cast<std::ptrdiff_t>(j * 16),
                          m.begin() + static_cast<std::ptrdiff_t>(j * 16 + 16), sd.inverse_bind[j].begin());
            }
        }
        scene.skins.push_back(std::move(sd));
    }
    for (cgltf_size i = 0; i < data->animations_count; ++i) {
        const cgltf_animation& an = data->animations[i];
        GlbAnimationData ad;
        ad.name = an.name ? an.name : "";
        for (cgltf_size c = 0; c < an.channels_count; ++c) {
            const cgltf_animation_channel& ch = an.channels[c];
            GlbChannelData cd;
            cd.node = ch.target_node ? static_cast<int>(cgltf_node_index(data.get(), ch.target_node)) : -1;
            cd.path = ch.target_path == cgltf_animation_path_type_translation ? "translation"
                      : ch.target_path == cgltf_animation_path_type_rotation  ? "rotation"
                      : ch.target_path == cgltf_animation_path_type_scale     ? "scale"
                                                                              : "other";
            cd.interpolation = ch.sampler->interpolation == cgltf_interpolation_type_step           ? "STEP"
                               : ch.sampler->interpolation == cgltf_interpolation_type_cubic_spline ? "CUBICSPLINE"
                                                                                                    : "LINEAR";
            cd.times = unpack(ch.sampler->input);
            cd.values = unpack(ch.sampler->output);
            ad.channels.push_back(std::move(cd));
        }
        scene.animations.push_back(std::move(ad));
    }
    for (cgltf_size m = 0; m < data->meshes_count; ++m) {
        for (cgltf_size p = 0; p < data->meshes[m].primitives_count; ++p) {
            const cgltf_primitive& prim = data->meshes[m].primitives[p];
            GlbPrimitiveData pd;
            pd.mesh = static_cast<int>(m);
            pd.positions = unpack(cgltf_find_accessor(&prim, cgltf_attribute_type_position, 0));
            if (const cgltf_accessor* j = cgltf_find_accessor(&prim, cgltf_attribute_type_joints, 0)) {
                pd.joints.resize(j->count * 4);
                for (cgltf_size v = 0; v < j->count; ++v) {
                    cgltf_uint u[4] = {0, 0, 0, 0};
                    cgltf_accessor_read_uint(j, v, u, 4);
                    for (cgltf_size k = 0; k < 4; ++k) {
                        pd.joints[v * 4 + k] = static_cast<std::uint16_t>(u[k]);
                    }
                }
            }
            if (const cgltf_accessor* w = cgltf_find_accessor(&prim, cgltf_attribute_type_weights, 0)) {
                pd.weights = unpack(w);
            }
            scene.primitives.push_back(std::move(pd));
        }
    }
    return Out::ok(std::move(scene));
}

}  // namespace sh::gltf
