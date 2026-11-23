#include "formats/common/crc32.h"
#include "formats/convert/level/level_textures.h"
#include "formats/convert/model/model_build.h"
#include "formats/convert/model/tmd.h"
#include "formats/gltf/gltf_writer.h"
#include "formats/gltf/ps1_blend.h"
#include "formats/common/json.h"

#include <cmath>
#include <cstdio>
#include <algorithm>
#include <map>
#include <optional>
#include <set>
#include <iterator>
#include <tuple>
#include <utility>

namespace sh::convert::model {
namespace {

struct MaterialKey {
    bool textured;
    int tpage;
    int clut;
    bool semi;
    bool double_sided;
    bool operator<(const MaterialKey& o) const {
        return std::tie(textured, tpage, clut, semi, double_sided) <
               std::tie(o.textured, o.tpage, o.clut, o.semi, o.double_sided);
    }
};

struct GroupKey {
    int material;
    bool has_rgb;
    bool unlit;
    bool operator<(const GroupKey& o) const {
        return std::tie(material, has_rgb, unlit) < std::tie(o.material, o.has_rgb, o.unlit);
    }
};

struct Group {
    gltf::Primitive prim;
    std::map<std::array<int, 7>, std::uint32_t> lookup;
};

// The TIM a (texture page, CLUT) pair samples, or why it has none.
struct TextureUse {
    std::optional<ItemTexture> texture;
    std::string unresolved;
    std::array<int, 4> uv_box{255, 255, 0, 0};  // min u, min v, max u, max v over every corner, page texels
};

using PageClut = std::pair<std::uint16_t, std::uint16_t>;

std::map<PageClut, TextureUse> resolve_textures(const Tmd& tmd, std::string_view game_path, const Options& options) {
    std::map<PageClut, TextureUse> uses;
    for (const TmdObject& obj : tmd.objects) {
        for (const TmdPrimitive& p : obj.prims) {
            if (!p.textured) {
                continue;
            }
            TextureUse& use = uses[{p.tpage, p.clut}];
            for (int k = 0; k < p.corners; ++k) {
                const auto& uv = p.uv[static_cast<std::size_t>(k)];
                use.uv_box = {std::min<int>(use.uv_box[0], uv[0]), std::min<int>(use.uv_box[1], uv[1]),
                              std::max<int>(use.uv_box[2], uv[0]), std::max<int>(use.uv_box[3], uv[1])};
            }
        }
    }
    for (auto& [key, use] : uses) {
        auto texture = resolve_item_texture(game_path, key.first, key.second);
        if (!texture) {
            use.unresolved = "no known TIM is uploaded to this texture page and CLUT when the game draws the model";
            continue;
        }
        const ItemTexture& t = *texture;
        if (use.uv_box[0] < t.u0 || use.uv_box[1] < t.v0 || use.uv_box[2] >= t.u0 + t.width ||
            use.uv_box[3] >= t.v0 + t.height) {
            use.unresolved = "texture coordinates leave the picture of " + t.tim;
            continue;
        }
        if (options.sibling) {
            const auto bytes = options.sibling(t.tim);
            const auto info = bytes ? level::read_tim_info(*bytes, t.tim) : Result<level::TimInfo>::fail("missing");
            if (!info || info.value().bpp != 4 || info.value().width != t.width ||
                info.value().height != t.height || info.value().clut_rows <= t.palette) {
                use.unresolved = t.tim + " is missing or its header differs from the expected upload";
                continue;
            }
        }
        use.texture = std::move(texture);
    }
    return uses;
}

}  // namespace

Result<ConvertedModel> build_tmd(std::span<const std::uint8_t> raw, std::string_view game_path,
                                 const Options& options) {
    using Out = Result<ConvertedModel>;
    auto parsed = parse_tmd(raw);
    if (!parsed) {
        return Out::fail(parsed.error());
    }
    const Tmd& tmd = parsed.value();

    gltf::Document doc;
    {
        json::Writer e;
        e.begin_object().member("source", std::string(game_path)).end_object();
        doc.extras = e.take();
    }
    std::map<MaterialKey, int> material_index;
    std::map<std::string, int> texture_index;
    const std::string glb_path = "models/" + path_without_extension(game_path) + ".glb";
    const std::map<PageClut, TextureUse> uses = resolve_textures(tmd, game_path, options);
    const auto use_of = [&](const TmdPrimitive& p) -> const TextureUse* {
        const auto it = uses.find({p.tpage, p.clut});
        return it == uses.end() ? nullptr : &it->second;
    };

    const auto material_for = [&](const TmdPrimitive& p) {
        const MaterialKey key{p.textured, p.textured ? p.tpage : 0, p.textured ? p.clut : 0, p.semi_transparent,
                              p.double_sided};
        const auto it = material_index.find(key);
        if (it != material_index.end()) {
            return it->second;
        }
        gltf::Material mat;
        char name[64];
        if (key.textured) {
            std::snprintf(name, sizeof name, "tex_tp%04x_cl%04x%s%s", key.tpage, key.clut, key.semi ? "_alpha" : "",
                          key.double_sided ? "_2s" : "");
        } else {
            std::snprintf(name, sizeof name, "color%s%s", key.semi ? "_alpha" : "", key.double_sided ? "_2s" : "");
        }
        mat.name = name;
        mat.double_sided = key.double_sided;
        if (key.semi) {
            mat.alpha_mode = gltf::AlphaMode::Blend;
            mat.base_color[3] = 0.5f;
        }
        json::Writer e;
        e.begin_object();
        e.member("textured", key.textured);
        if (key.textured) {
            e.member("tpage", key.tpage);
            e.member("tpage_x", (key.tpage & 15) * 64);
            e.member("tpage_y", ((key.tpage >> 4) & 1) * 256);
            e.member("tpage_color_mode", (key.tpage >> 7) & 3);
            e.member("clut", key.clut);
            e.member("clut_x", (key.clut & 63) * 16);
            e.member("clut_row", key.clut >> 6);
            const auto it = uses.find({static_cast<std::uint16_t>(key.tpage), static_cast<std::uint16_t>(key.clut)});
            if (it != uses.end() && it->second.texture) {
                const ItemTexture& t = *it->second.texture;
                const std::string png = "textures/" + path_without_extension(t.tim) + ".png";
                const std::string uri = level::relative_uri(glb_path, png);
                e.member("tim", t.tim);
                e.member("tim_width", t.width);
                e.member("tim_height", t.height);
                e.member("tim_page_u", t.u0);
                e.member("tim_page_v", t.v0);
                e.member("palette", t.palette);
                e.member("palette_in_tim", true);
                e.member("png", png);
                auto found = texture_index.find(uri);
                if (found == texture_index.end()) {
                    doc.textures.push_back({uri, true});
                    found = texture_index.emplace(uri, static_cast<int>(doc.textures.size() - 1)).first;
                }
                mat.texture = found->second;
                if (!key.semi) {
                    mat.alpha_mode = gltf::AlphaMode::Mask;  // the PS1 never draws a texel whose word is 0x0000
                }
            }
        }
        e.member("semi_transparent", key.semi);
        // A textured TMD primitive carries its own page word, blend mode included; an untextured one blends with
        // whatever mode the drawer last set, which the file does not say.
        if (key.semi && key.textured) {
            const int abr = (key.tpage >> 5) & 3;
            e.member("abr", abr);
            e.member("blend_mode", std::string(gltf::blend_mode_name(abr)));
        }
        e.end_object();
        mat.extras = e.take();
        doc.materials.push_back(std::move(mat));
        return material_index.emplace(key, static_cast<int>(doc.materials.size() - 1)).first->second;
    };

    ModelStats stats;
    stats.kind = "tmd";
    stats.parts = tmd.objects.size();
    stats.rest_pose_source = "not applicable (TMD objects are placed by the game)";

    gltf::Node root;
    root.name = path_stem(game_path);
    doc.nodes.push_back(root);

    json::Writer side(true);
    side.begin_object();
    side.member("format", "sh-model/1");
    side.key("source").begin_object();
    side.member("path", std::string(game_path));
    side.member("kind", "tmd");
    side.member("size", raw.size());
    char crc[16];
    std::snprintf(crc, sizeof crc, "%08X", crc32(raw.data(), raw.size()));
    side.member("crc32", crc);
    side.member("flags", tmd.flags);
    side.member("lossless", false);
    side.end_object();

    side.key("coordinates").begin_object();
    side.member("units_per_metre", kUnitsPerMetre);
    side.member("transform", "x' = -x*2^scale/256, y' = -y*2^scale/256, z' = z*2^scale/256 (rotation by 180 degrees about Z)");
    side.member("winding", "triangle corner order reversed (PS1 front faces are clockwise)");
    side.member("quad", "strip: triangles (0,1,2) and (1,2,3) of the PS1 corner order");
    side.member("uv", "materials with a tim: (u - tim_page_u + 0.5) / tim_width, (v - tim_page_v + 0.5) / tim_height "
                      "(texels of the TIM picture); others: (u + 0.5) / 256, (v + 0.5) / 256 of the texture page; "
                      "V down, no flip");
    side.member("normals", "Q12, rotated like positions");
    side.member("colors", "COLOR_0 holds the packet colour of untextured and unlit packets; lit textured packets have none");
    side.end_object();

    side.key("textures").begin_array();
    for (const auto& [key, use] : uses) {
        side.begin_object();
        side.member("tpage", key.first);
        side.member("clut", key.second);
        if (use.texture) {
            side.member("tim", use.texture->tim);
            side.member("palette", use.texture->palette);
            if (stats.texture.empty()) {
                stats.texture = use.texture->tim;
            }
        } else {
            side.member("unresolved", use.unresolved);
        }
        side.end_object();
    }
    side.end_array();

    std::set<std::array<int, 2>> referenced;
    side.key("objects").begin_array();
    for (std::size_t oi = 0; oi < tmd.objects.size(); ++oi) {
        const TmdObject& obj = tmd.objects[oi];
        const double scale = std::ldexp(1.0, obj.scale);
        std::map<GroupKey, Group> groups;
        std::size_t object_triangles = 0;
        // The mesh primitives are the groups in key order: each packet's group index is known only once every
        // group of the object is.
        std::set<GroupKey> group_keys;
        for (const TmdPrimitive& p : obj.prims) {
            group_keys.insert(GroupKey{material_for(p), p.has_rgb, p.unlit});
        }

        side.begin_object();
        side.member("index", oi);
        side.member("table_offset", obj.table_offset);
        side.member("scale_exponent", obj.scale);
        side.member("vertex_offset", obj.vertex_offset);
        side.member("normal_offset", obj.normal_offset);
        side.member("primitive_offset", obj.prim_offset);
        {
            json::Writer c;
            c.begin_array();
            for (const auto& v : obj.vertices) {
                c.begin_array().value(v[0]).value(v[1]).value(v[2]).end_array();
            }
            c.end_array();
            side.key("vertices").raw(c.str());
        }
        {
            json::Writer c;
            c.begin_array();
            for (const auto& n : obj.normals) {
                c.begin_array().value(n[0]).value(n[1]).value(n[2]).end_array();
            }
            c.end_array();
            side.key("normals").raw(c.str());
        }

        side.key("primitives").begin_array();
        for (const TmdPrimitive& p : obj.prims) {
            const int material = material_for(p);
            const GroupKey group_key{material, p.has_rgb, p.unlit};
            Group& group = groups[group_key];
            group.prim.material = material;
            const std::size_t first_triangle = group.prim.indices.size() / 3;

            std::array<std::uint32_t, 4> ids{};
            for (int k = 0; k < p.corners; ++k) {
                const auto ku = static_cast<std::size_t>(k);
                const int nidx = p.unlit ? -1 : p.normal[ku];
                const std::array<int, 7> key = {p.vertex[ku], nidx,         p.uv[ku][0], p.uv[ku][1],
                                                p.rgb[ku][0], p.rgb[ku][1], p.rgb[ku][2]};
                const auto found = group.lookup.find(key);
                if (found != group.lookup.end()) {
                    ids[ku] = found->second;
                } else {
                    gltf::Primitive& gp = group.prim;
                    const auto id = static_cast<std::uint32_t>(gp.vertex_count());
                    const auto& v = obj.vertices[p.vertex[ku]];
                    const auto pos = to_gltf_position(v[0] * scale, v[1] * scale, v[2] * scale);
                    gp.positions.insert(gp.positions.end(), pos.begin(), pos.end());
                    if (!p.unlit) {
                        const auto& n = obj.normals[p.normal[ku]];
                        const auto nrm = to_gltf_direction(n[0], n[1], n[2]);
                        gp.normals.insert(gp.normals.end(), nrm.begin(), nrm.end());
                    }
                    if (p.textured) {
                        const TextureUse* use = use_of(p);
                        if (use != nullptr && use->texture) {
                            const ItemTexture& t = *use->texture;
                            gp.texcoords.push_back(static_cast<float>((p.uv[ku][0] - t.u0 + 0.5) / t.width));
                            gp.texcoords.push_back(static_cast<float>((p.uv[ku][1] - t.v0 + 0.5) / t.height));
                        } else {
                            gp.texcoords.push_back(static_cast<float>((p.uv[ku][0] + 0.5) / 256.0));
                            gp.texcoords.push_back(static_cast<float>((p.uv[ku][1] + 0.5) / 256.0));
                        }
                    }
                    if (p.has_rgb) {
                        gp.colors.insert(gp.colors.end(), {p.rgb[ku][0], p.rgb[ku][1], p.rgb[ku][2], 255});
                    }
                    group.lookup.emplace(key, id);
                    ids[ku] = id;
                }
                referenced.insert({static_cast<int>(oi), p.vertex[ku]});
            }
            const auto tris = face_triangles(p.quad);
            for (std::size_t t = 0; t < (p.quad ? 2u : 1u); ++t) {
                for (const int corner : tris[t]) {
                    group.prim.indices.push_back(ids[static_cast<std::size_t>(corner)]);
                }
                ++object_triangles;
            }
            stats.source_primitives += 1;
            stats.source_triangles += p.quad ? 2 : 1;

            json::Writer c;
            c.begin_object();
            c.member("offset", p.offset);
            c.member("mode", p.mode);
            c.member("flag", p.flag);
            c.member("gltf_material", material);
            // The face map (importer 7): the mesh primitive and first triangle of the packet in the glb, and the
            // packet itself, so a fix that addresses packets (items/<Name>.json "close_up_fix") finds its faces.
            c.member("gltf_primitive",
                     static_cast<std::size_t>(std::distance(group_keys.begin(), group_keys.find(group_key))));
            c.member("gltf_triangle", first_triangle);
            const std::size_t packet_bytes =
                p.offset + 1 < raw.size() ? 4u + static_cast<std::size_t>(raw[p.offset + 1]) * 4u : 0u;
            if (packet_bytes > 0 && p.offset + packet_bytes <= raw.size()) {
                c.member("packet", hex_bytes(raw.subspan(p.offset, packet_bytes)));
            }
            c.end_object();
            side.raw(c.str());
        }
        side.end_array();
        stats.source_vertices += obj.vertices.size();

        gltf::Node node;
        node.name = "obj" + std::to_string(oi);
        if (!groups.empty()) {
            gltf::Mesh mesh;
            mesh.name = node.name;
            for (auto& entry : groups) {
                stats.glb_vertices += entry.second.prim.vertex_count();
                stats.glb_triangles += entry.second.prim.indices.size() / 3;
                mesh.primitives.push_back(std::move(entry.second.prim));
            }
            doc.meshes.push_back(std::move(mesh));
            node.mesh = static_cast<int>(doc.meshes.size() - 1);
        }
        doc.nodes.push_back(std::move(node));
        doc.nodes[0].children.push_back(static_cast<int>(doc.nodes.size() - 1));
        side.member("triangles", object_triangles);
        side.member("node", doc.nodes.size() - 1);
        side.end_object();
    }
    side.end_array();

    stats.referenced_vertices = referenced.size();
    stats.part_vertex_refs = referenced.size();
    side.key("totals").begin_object();
    side.member("parts", stats.parts);
    side.member("source_vertices", stats.source_vertices);
    side.member("source_primitives", stats.source_primitives);
    side.member("source_triangles", stats.source_triangles);
    side.member("referenced_vertices", stats.referenced_vertices);
    side.member("glb_vertices", stats.glb_vertices);
    side.end_object();
    side.end_object();

    doc.scene_name = path_stem(game_path);
    auto glb = gltf::write_glb(doc);
    if (!glb) {
        return Out::fail(glb.error());
    }
    ConvertedModel result;
    result.glb = std::move(glb.value());
    result.sidecar_json = side.take() + "\n";
    result.stats = std::move(stats);
    return Out::ok(std::move(result));
}

}  // namespace sh::convert::model
