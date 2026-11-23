#include "formats/gltf/gltf_skin_reader.h"

#include "formats/gltf/cgltf_prefixed.h"

#include <cstring>
#include <memory>

namespace sh::gltf {
namespace {

struct DataDeleter {
    void operator()(cgltf_data* d) const { cgltf_free(d); }
};

std::string text(const char* s) {
    return s ? std::string(s) : std::string();
}

const cgltf_accessor* attribute(const cgltf_primitive& p, cgltf_attribute_type type) {
    for (cgltf_size i = 0; i < p.attributes_count; ++i) {
        if (p.attributes[i].type == type && p.attributes[i].index == 0) {
            return p.attributes[i].data;
        }
    }
    return nullptr;
}

int joint_index(const cgltf_skin& skin, const cgltf_node* node) {
    for (cgltf_size j = 0; j < skin.joints_count; ++j) {
        if (skin.joints[j] == node) {
            return static_cast<int>(j);
        }
    }
    return -1;
}

SkinMaterialData read_material(const cgltf_material& m) {
    SkinMaterialData out;
    out.name = text(m.name);
    out.double_sided = m.double_sided != 0;
    out.alpha = m.alpha_mode == cgltf_alpha_mode_mask    ? SkinAlpha::Mask
                : m.alpha_mode == cgltf_alpha_mode_blend ? SkinAlpha::Blend
                                                         : SkinAlpha::Opaque;
    out.alpha_cutoff = m.alpha_cutoff;
    out.extras = text(m.extras.data);
    if (m.has_pbr_metallic_roughness) {
        const cgltf_pbr_metallic_roughness& pbr = m.pbr_metallic_roughness;
        for (int i = 0; i < 4; ++i) {
            out.base_color[static_cast<std::size_t>(i)] = pbr.base_color_factor[i];
        }
        const cgltf_texture* tex = pbr.base_color_texture.texture;
        const cgltf_image* image = tex ? tex->image : nullptr;
        if (image && image->buffer_view) {
            const std::uint8_t* bytes = cgltf_buffer_view_data(image->buffer_view);
            if (bytes) {
                out.image_bytes.assign(bytes, bytes + image->buffer_view->size);
            }
        } else if (image && image->uri && std::strncmp(image->uri, "data:", 5) != 0) {
            std::string uri = image->uri;
            uri.resize(cgltf_decode_uri(uri.data()));
            out.image_uri = std::move(uri);
        }
    }
    return out;
}

}  // namespace

Result<SkinnedGltf> read_skinned_gltf(const std::filesystem::path& file) {
    using Out = Result<SkinnedGltf>;
    const std::string path = file.string();
    cgltf_options options;
    std::memset(&options, 0, sizeof options);
    cgltf_data* raw = nullptr;
    if (cgltf_parse_file(&options, path.c_str(), &raw) != cgltf_result_success) {
        return Out::fail("cannot be parsed as glTF");
    }
    std::unique_ptr<cgltf_data, DataDeleter> data(raw);
    if (cgltf_load_buffers(&options, data.get(), path.c_str()) != cgltf_result_success) {
        return Out::fail("its buffers cannot be loaded");
    }
    if (cgltf_validate(data.get()) != cgltf_result_success) {
        return Out::fail("is not valid glTF");
    }
    if (data->skins_count == 0) {
        return Out::fail("has no skin");
    }
    const cgltf_skin& skin = data->skins[0];
    if (skin.joints_count == 0 || skin.joints_count > 0xFFFF) {
        return Out::fail("its skin has no joints");
    }

    SkinnedGltf out;
    out.asset_extras = text(data->asset.extras.data);
    out.joints.resize(skin.joints_count);
    for (cgltf_size j = 0; j < skin.joints_count; ++j) {
        SkinJointData& joint = out.joints[j];
        const cgltf_node* node = skin.joints[j];
        joint.name = text(node->name);
        joint.extras = text(node->extras.data);
        for (const cgltf_node* p = node->parent; p && joint.parent < 0; p = p->parent) {
            joint.parent = joint_index(skin, p);
        }
        if (skin.inverse_bind_matrices) {
            cgltf_accessor_read_float(skin.inverse_bind_matrices, j, joint.inverse_bind.data(), 16);
        }
    }
    out.materials.reserve(data->materials_count);
    for (cgltf_size i = 0; i < data->materials_count; ++i) {
        out.materials.push_back(read_material(data->materials[i]));
    }

    for (cgltf_size ni = 0; ni < data->nodes_count; ++ni) {
        const cgltf_node& node = data->nodes[ni];
        if (!node.mesh || node.skin != &skin) {
            continue;
        }
        for (cgltf_size pi = 0; pi < node.mesh->primitives_count; ++pi) {
            const cgltf_primitive& p = node.mesh->primitives[pi];
            const std::string where = text(node.name) + " primitive " + std::to_string(pi);
            if (p.type != cgltf_primitive_type_triangles) {
                return Out::fail(where + " is not a triangle list");
            }
            const cgltf_accessor* pos = attribute(p, cgltf_attribute_type_position);
            const cgltf_accessor* jnt = attribute(p, cgltf_attribute_type_joints);
            const cgltf_accessor* wgt = attribute(p, cgltf_attribute_type_weights);
            const cgltf_accessor* nrm = attribute(p, cgltf_attribute_type_normal);
            const cgltf_accessor* uv = attribute(p, cgltf_attribute_type_texcoord);
            if (!pos || !jnt || !wgt || jnt->count != pos->count || wgt->count != pos->count) {
                return Out::fail(where + " lacks POSITION, JOINTS_0 or WEIGHTS_0");
            }
            const cgltf_size n = pos->count;
            SkinPrimitiveData prim;
            prim.node = text(node.name);
            prim.node_extras = text(node.extras.data);
            prim.material = p.material ? static_cast<int>(cgltf_material_index(data.get(), p.material)) : -1;
            prim.positions.resize(n * 3);
            cgltf_accessor_unpack_floats(pos, prim.positions.data(), prim.positions.size());
            if (nrm && nrm->count == n) {
                prim.normals.resize(n * 3);
                cgltf_accessor_unpack_floats(nrm, prim.normals.data(), prim.normals.size());
            }
            if (uv && uv->count == n) {
                prim.uvs.resize(n * 2);
                cgltf_accessor_unpack_floats(uv, prim.uvs.data(), prim.uvs.size());
            }
            prim.joints.resize(n * 4);
            prim.weights.resize(n * 4);
            for (cgltf_size v = 0; v < n; ++v) {
                cgltf_uint j[4] = {0, 0, 0, 0};
                cgltf_accessor_read_uint(jnt, v, j, 4);
                cgltf_accessor_read_float(wgt, v, &prim.weights[v * 4], 4);
                for (int k = 0; k < 4; ++k) {
                    if (j[k] >= skin.joints_count) {
                        return Out::fail(where + " names a joint the skin does not have");
                    }
                    prim.joints[v * 4 + static_cast<cgltf_size>(k)] = static_cast<std::uint16_t>(j[k]);
                }
            }
            if (p.indices) {
                prim.indices.resize(p.indices->count);
                for (cgltf_size i = 0; i < p.indices->count; ++i) {
                    prim.indices[i] = static_cast<std::uint32_t>(cgltf_accessor_read_index(p.indices, i));
                }
            } else {
                prim.indices.resize(n);
                for (cgltf_size i = 0; i < n; ++i) {
                    prim.indices[i] = static_cast<std::uint32_t>(i);
                }
            }
            prim.indices.resize(prim.indices.size() / 3 * 3);
            for (const std::uint32_t i : prim.indices) {
                if (i >= n) {
                    return Out::fail(where + " has an index past its vertices");
                }
            }
            out.primitives.push_back(std::move(prim));
        }
    }
    if (out.primitives.empty()) {
        return Out::fail("has no skinned mesh");
    }
    return Out::ok(std::move(out));
}

}  // namespace sh::gltf
