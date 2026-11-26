#include "render/scene/item_model.h"

#include "formats/common/json.h"
#include "formats/gltf/cgltf_prefixed.h"
#include "formats/png/sh_png.h"
#include "formats/png/stb_png.h"
#include "render/scene/scene.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>
#include <tuple>

namespace sh::scene {

namespace {

constexpr float kUnitsPerMetre = 256.0f;  // glTF metres back to PS1 units (the converter's model_build.h)

struct DataDeleter {
    void operator()(cgltf_data* d) const { cgltf_free(d); }
};

const cgltf_accessor* attribute(const cgltf_primitive& p, cgltf_attribute_type type) {
    for (cgltf_size i = 0; i < p.attributes_count; ++i)
        if (p.attributes[i].type == type && p.attributes[i].index == 0) return p.attributes[i].data;
    return nullptr;
}

std::optional<json::Value> extras_of(const cgltf_data* data, const cgltf_extras& extras) {
    cgltf_size size = 0;
    if (cgltf_copy_extras_json(data, &extras, nullptr, &size) != cgltf_result_success || size == 0) return std::nullopt;
    std::string text(size, '\0');
    if (cgltf_copy_extras_json(data, &extras, text.data(), &size) != cgltf_result_success) return std::nullopt;
    while (!text.empty() && text.back() == '\0') text.pop_back();
    auto v = json::parse(text);
    if (!v) return std::nullopt;
    return std::move(v.value());
}

int int_of(const json::Value& v, std::string_view key, int fallback) {
    const json::Value* m = v.find(key);
    return (m != nullptr && m->is_number()) ? static_cast<int>(m->number()) : fallback;
}

std::string string_of(const json::Value& v, std::string_view key) {
    const json::Value* m = v.find(key);
    return (m != nullptr && m->is_string()) ? m->string() : std::string();
}

bool bool_of(const json::Value& v, std::string_view key) {
    const json::Value* m = v.find(key);
    return m != nullptr && m->is_bool() && m->boolean();
}

// The importer's material: the packet kind it groups and the TIM picture it found.
struct GlbMaterial {
    ItemMaterial material;
    bool semi = false;
    bool double_sided = false;
    int tim_width = 256;  // texture coordinates are normalised to the picture (or to the page without one)
    int tim_height = 256;
};

GlbMaterial material_of(const cgltf_data* data, const cgltf_material* m) {
    GlbMaterial out;
    if (m == nullptr) return out;
    out.double_sided = m->double_sided != 0;
    const auto e = extras_of(data, m->extras);
    if (!e || e->find("tim") == nullptr) {
        // A mod's model: the picture embedded in the glb, sampled with the file's coordinates.
        const cgltf_texture* tex =
            m->has_pbr_metallic_roughness ? m->pbr_metallic_roughness.base_color_texture.texture : nullptr;
        const cgltf_image* img = tex != nullptr ? tex->image : nullptr;
        const cgltf_buffer_view* view = img != nullptr ? img->buffer_view : nullptr;
        if (view != nullptr && view->buffer != nullptr && view->buffer->data != nullptr) {
            const auto* bytes = static_cast<const unsigned char*>(view->buffer->data) + view->offset;
            int w = 0, h = 0;
            if (unsigned char* px = sh_png_stb_load_rgba(bytes, static_cast<int>(view->size), &w, &h)) {
                out.material.own_rgba =
                    std::make_shared<const std::vector<std::uint8_t>>(px, px + std::size_t(w) * h * 4);
                out.material.own_width = w;
                out.material.own_height = h;
                out.material.textured = true;
                sh_png_stb_free(px);
            }
        }
        if (!e) return out;
    }
    out.material.textured = bool_of(*e, "textured");
    out.semi = bool_of(*e, "semi_transparent");
    out.material.tpage = static_cast<std::uint16_t>(int_of(*e, "tpage", 0));
    out.material.clut = static_cast<std::uint16_t>(int_of(*e, "clut", 0));
    const std::string tim = string_of(*e, "tim");
    if (out.material.textured && !tim.empty()) {
        ItemSheet sheet;
        sheet.tim = tim;
        sheet.palette = int_of(*e, "palette", 0);
        sheet.u0 = int_of(*e, "tim_page_u", 0);
        sheet.v0 = int_of(*e, "tim_page_v", 0);
        out.tim_width = std::max(1, int_of(*e, "tim_width", 256));
        out.tim_height = std::max(1, int_of(*e, "tim_height", 256));
        out.material.sheet = std::move(sheet);
    }
    return out;
}

// A triangle of the glb as read: corners in the file's order.
struct GlbTriangle {
    int material = 0;
    std::array<std::array<float, 3>, 3> position{};
    std::array<std::array<float, 2>, 3> uv{};
    std::array<std::uint8_t, 3> color{128, 128, 128};
};

// Texel of a normalised coordinate (the importer wrote (texel - origin + 0.5) / size).
std::uint8_t texel_of(float t, int size, int origin) {
    return static_cast<std::uint8_t>(std::lround(static_cast<double>(t) * size - 0.5) + origin);
}

std::vector<std::uint8_t> hex_bytes(std::string_view hex) {
    std::vector<std::uint8_t> out;
    auto nibble = [](char c) { return c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10; };
    for (std::size_t i = 0; i + 1 < hex.size(); i += 2)
        out.push_back(static_cast<std::uint8_t>((nibble(hex[i]) << 4) | nibble(hex[i + 1])));
    return out;
}

// A packet the item drawer draws: a lit gouraud triangle, textured (mode 0x34, 0x36 blended) or in its colour.
bool drawn_kind(std::span<const std::uint8_t> packet) {
    if (packet.size() < 4) return false;
    const int ilen = packet[1], flag = packet[2], mode = packet[3];
    if (flag & 1) return false;
    return ((mode & 0xFD) == 0x34 && ilen == 6) || (mode == 0x30 && ilen == 4);
}

// The corners of the packet in the order of glb triangle `t` of it (the converter's face_triangles: the winding is
// reversed, a quad is the strip (0, 1, 2), (1, 2, 3)).
constexpr std::array<std::array<int, 3>, 2> kPacketCorners = {{{0, 2, 1}, {1, 2, 3}}};

}  // namespace

std::string item_glb_path(std::string_view model) {
    if (model.size() > 4 && model.substr(model.size() - 4) == ".glb") return "convert/models/" + std::string(model);
    std::string stem(model);
    if (const std::size_t dot = stem.find_last_of('.'); dot != std::string::npos && stem.find('/', dot) == std::string::npos)
        stem.erase(dot);
    return "convert/models/" + stem + ".glb";
}

std::string item_sidecar_path(std::string_view model) {
    std::string glb = item_glb_path(model);
    return glb.substr(0, glb.size() - 4) + ".model.json";
}

Result<ItemMesh> read_item_mesh(std::span<const std::uint8_t> glb, std::span<const std::uint8_t> sidecar, int object,
                                const ItemPacketFix* fix, std::string_view path) {
    using R = Result<ItemMesh>;
    cgltf_options options{};
    options.type = cgltf_file_type_glb;
    cgltf_data* raw = nullptr;
    if (cgltf_parse(&options, glb.data(), glb.size(), &raw) != cgltf_result_success || raw == nullptr)
        return R::fail(std::string(path) + ": not a glTF binary");
    std::unique_ptr<cgltf_data, DataDeleter> data(raw);
    if (cgltf_load_buffers(&options, raw, nullptr) != cgltf_result_success)
        return R::fail(std::string(path) + ": buffers do not load");

    const std::string node_name = "obj" + std::to_string(object);
    const cgltf_node* node = nullptr;
    for (cgltf_size i = 0; i < raw->nodes_count; ++i)
        if (raw->nodes[i].name != nullptr && node_name == raw->nodes[i].name) node = &raw->nodes[i];
    // A mod's own model: its first node with a mesh.
    for (cgltf_size i = 0; node == nullptr && object == 0 && sidecar.empty() && i < raw->nodes_count; ++i)
        if (raw->nodes[i].mesh != nullptr) node = &raw->nodes[i];
    if (node == nullptr) return R::fail(std::string(path) + ": no node " + node_name);

    // The object's scale exponent: the PS1 drawer used the stored vertices as they are.
    double unscale = 1.0;
    std::optional<json::Value> side;
    const json::Value* side_object = nullptr;
    if (!sidecar.empty()) {
        auto parsed = json::parse(std::string_view(reinterpret_cast<const char*>(sidecar.data()), sidecar.size()));
        if (!parsed) return R::fail(std::string(path) + ": sidecar: " + parsed.error());
        side = std::move(parsed.value());
        if (const json::Value* objects = side->find("objects"); objects != nullptr && objects->is_array())
            for (const json::Value& o : objects->array())
                if (int_of(o, "index", -1) == object) side_object = &o;
        if (side_object != nullptr) unscale = std::ldexp(1.0, -int_of(*side_object, "scale_exponent", 0));
    }

    ItemMesh mesh;
    std::vector<GlbMaterial> materials;
    std::map<const cgltf_material*, int> material_index;
    // Triangles by mesh primitive, in file order.
    std::vector<std::vector<GlbTriangle>> primitives;
    float world[16];
    cgltf_node_transform_world(node, world);
    if (node->mesh != nullptr) {
        for (cgltf_size pi = 0; pi < node->mesh->primitives_count; ++pi) {
            const cgltf_primitive& p = node->mesh->primitives[pi];
            std::vector<GlbTriangle>& tris = primitives.emplace_back();
            if (p.type != cgltf_primitive_type_triangles) continue;
            const cgltf_accessor* pos = attribute(p, cgltf_attribute_type_position);
            if (pos == nullptr) continue;
            const cgltf_accessor* uv = attribute(p, cgltf_attribute_type_texcoord);
            const cgltf_accessor* col = attribute(p, cgltf_attribute_type_color);
            auto [it, added] = material_index.try_emplace(p.material, static_cast<int>(materials.size()));
            if (added) materials.push_back(material_of(raw, p.material));
            const cgltf_size count = p.indices != nullptr ? p.indices->count : pos->count;
            for (cgltf_size i = 0; i + 2 < count; i += 3) {
                GlbTriangle t;
                t.material = it->second;
                for (int k = 0; k < 3; ++k) {
                    const cgltf_size v = p.indices != nullptr ? cgltf_accessor_read_index(p.indices, i + k) : i + k;
                    float lp[3] = {0, 0, 0}, lt[2] = {0, 0}, lc[4] = {1, 1, 1, 1};
                    cgltf_accessor_read_float(pos, v, lp, 3);
                    if (uv != nullptr) cgltf_accessor_read_float(uv, v, lt, 2);
                    if (col != nullptr) cgltf_accessor_read_float(col, v, lc, 4);
                    float wp[3];
                    for (int r = 0; r < 3; ++r)
                        wp[r] = world[0 * 4 + r] * lp[0] + world[1 * 4 + r] * lp[1] + world[2 * 4 + r] * lp[2] + world[12 + r];
                    // The converter's 180 degree turn about Z, undone.
                    t.position[k] = {static_cast<float>(-wp[0] * kUnitsPerMetre * unscale),
                                     static_cast<float>(-wp[1] * kUnitsPerMetre * unscale),
                                     static_cast<float>(wp[2] * kUnitsPerMetre * unscale)};
                    t.uv[k] = {lt[0], lt[1]};
                    if (k == 0 && col != nullptr)
                        for (int c = 0; c < 3; ++c)
                            t.color[static_cast<std::size_t>(c)] =
                                static_cast<std::uint8_t>(std::lround(std::clamp(lc[c], 0.0f, 1.0f) * 255.0f));
                }
                tris.push_back(t);
            }
        }
    }
    for (const GlbMaterial& m : materials) mesh.materials.push_back(m.material);

    // A face from glb triangle `t`, its corners put back in packet order (`corners`: packet corner of each file corner).
    auto face_of = [&](const GlbTriangle& t, const std::array<int, 3>& corners, int first_corner) {
        const GlbMaterial& gm = materials[static_cast<std::size_t>(t.material)];
        ItemFace f;
        f.material = t.material;
        f.semi = gm.semi;
        f.double_sided = gm.double_sided;
        f.color = t.color;
        // File corner k is packet corner corners[k]; packet corners first_corner .. first_corner + 2 become 0..2.
        for (int k = 0; k < 3; ++k) {
            const int pc = corners[static_cast<std::size_t>(k)] - first_corner;
            f.position[static_cast<std::size_t>(pc)] = t.position[static_cast<std::size_t>(k)];
            f.uv[static_cast<std::size_t>(pc)] = t.uv[static_cast<std::size_t>(k)];
            if (gm.material.textured && !gm.material.own_rgba) {
                const int w = gm.material.sheet ? gm.tim_width : 256, h = gm.material.sheet ? gm.tim_height : 256;
                const int u0 = gm.material.sheet ? gm.material.sheet->u0 : 0;
                const int v0 = gm.material.sheet ? gm.material.sheet->v0 : 0;
                f.texel[static_cast<std::size_t>(pc)] = {texel_of(t.uv[static_cast<std::size_t>(k)][0], w, u0),
                                                         texel_of(t.uv[static_cast<std::size_t>(k)][1], h, v0)};
            }
        }
        return f;
    };

    const json::Value* packets = side_object != nullptr ? side_object->find("primitives") : nullptr;
    if (packets == nullptr || !packets->is_array()) {
        // No face map: the glb's triangles as they are, in file order.
        bool own = false;
        for (const auto& tris : primitives)
            for (const GlbTriangle& t : tris) {
                mesh.faces.push_back(face_of(t, kPacketCorners[0], 0));
                const GlbMaterial& gm = materials[static_cast<std::size_t>(t.material)];
                if (!gm.material.textured && gm.semi) mesh.drawable = false;
                own = own || gm.material.own_rgba != nullptr;
            }
        if (own && !mesh.faces.empty()) {
            // A mod's model in metres: centred and scaled so its longest side matches the game's item models.
            constexpr float kItemSize = 440.0f;
            float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
            for (const ItemFace& f : mesh.faces)
                for (const auto& p : f.position)
                    for (int c = 0; c < 3; ++c) {
                        lo[c] = std::min(lo[c], p[static_cast<std::size_t>(c)]);
                        hi[c] = std::max(hi[c], p[static_cast<std::size_t>(c)]);
                    }
            // By its face, not its longest side: the two largest extents' mean, so a thin model fills its slot as
            // the game's items do.
            float ext[3] = {hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2]};
            std::sort(ext, ext + 3);
            const float face = std::sqrt(ext[1] * ext[2]);
            const float k = face > 0.0f ? kItemSize / face : 1.0f;
            // In the file's metres: the faces above are turned 180 degrees about Z (x and y negated).
            const float per_metre = static_cast<float>(kUnitsPerMetre * unscale);
            mesh.own_scale = k * per_metre;
            mesh.own_middle = {-(lo[0] + hi[0]) * 0.5f / per_metre, -(lo[1] + hi[1]) * 0.5f / per_metre,
                               (lo[2] + hi[2]) * 0.5f / per_metre};
            for (ItemFace& f : mesh.faces)
                for (auto& p : f.position)
                    for (int c = 0; c < 3; ++c)
                        p[static_cast<std::size_t>(c)] = (p[static_cast<std::size_t>(c)] - (lo[c] + hi[c]) * 0.5f) * k;
        }
        return R::ok(std::move(mesh));
    }

    // The faces in packet order through the importer's face map, and the packets for a fix.
    struct PacketFaces {
        std::size_t first_face = 0;
        int faces = 0;
        std::size_t offset = 0;  // into `bytes`
        std::size_t size = 0;
        bool quad = false;
    };
    std::vector<PacketFaces> map;
    std::vector<std::uint8_t> bytes;
    bool recorded = true;
    for (const json::Value& p : packets->array()) {
        const int prim = int_of(p, "gltf_primitive", -1);
        const int first = int_of(p, "gltf_triangle", -1);
        const std::vector<std::uint8_t> packet = hex_bytes(string_of(p, "packet"));
        const int mode = int_of(p, "mode", packet.size() > 3 ? packet[3] : 0);
        const bool quad = (mode & 0x08) != 0;
        const int count = quad ? 2 : 1;
        if (prim < 0 || first < 0 || static_cast<std::size_t>(prim) >= primitives.size() ||
            static_cast<std::size_t>(first + count) > primitives[static_cast<std::size_t>(prim)].size()) {
            return R::fail(std::string(path) + ": " + node_name + ": the face map does not match the glb");
        }
        if (packet.empty()) recorded = false;
        if (!drawn_kind(packet)) mesh.drawable = false;
        PacketFaces pf;
        pf.first_face = mesh.faces.size();
        pf.faces = count;
        pf.offset = bytes.size();
        pf.size = packet.size();
        pf.quad = quad;
        bytes.insert(bytes.end(), packet.begin(), packet.end());
        for (int t = 0; t < count; ++t) {
            const std::array<int, 3>& corners = kPacketCorners[static_cast<std::size_t>(t)];
            mesh.faces.push_back(face_of(primitives[static_cast<std::size_t>(prim)][static_cast<std::size_t>(first + t)],
                                         corners, t));
        }
        map.push_back(pf);
    }

    if (fix != nullptr && *fix && recorded) {
        const std::vector<std::uint8_t> before = bytes;
        const json::Value* vertices = side_object->find("vertices");
        const auto vertex_count =
            static_cast<std::uint32_t>(vertices != nullptr && vertices->is_array() ? vertices->array().size() : 0);
        if ((*fix)(std::span<std::uint8_t>(bytes), vertex_count, static_cast<std::uint32_t>(map.size()))) {
            for (const PacketFaces& pf : map) {
                const std::uint8_t* was = &before[pf.offset];
                const std::uint8_t* now = &bytes[pf.offset];
                if (pf.size < 4) continue;
                for (int t = 0; t < pf.faces; ++t) {
                    ItemFace& f = mesh.faces[pf.first_face + static_cast<std::size_t>(t)];
                    if (was[3] != now[3]) f.semi = (now[3] & 2) != 0;
                    // A textured packet's corner texels: u, v at 4, 8, 12 (and 16 for a quad).
                    if ((now[3] & 4) == 0 || pf.size < (pf.quad ? 20u : 16u)) continue;
                    for (int c = 0; c < 3; ++c) {
                        const int pc = c + t;
                        f.texel[static_cast<std::size_t>(c)] = {now[4 + pc * 4], now[5 + pc * 4]};
                    }
                }
            }
        }
    }
    return R::ok(std::move(mesh));
}

std::shared_ptr<const ItemMesh> find_item_mesh(std::string_view model, int object, std::string_view fix_name,
                                               const ItemPacketFix& fix) {
    struct Key {
        std::string model;
        int object;
        std::string fix;
        bool operator<(const Key& o) const { return std::tie(model, object, fix) < std::tie(o.model, o.object, o.fix); }
    };
    static std::map<Key, std::shared_ptr<const ItemMesh>> s_meshes;
    static std::uint32_t s_generation = asset_generation();
    if (s_generation != asset_generation()) {
        s_meshes.clear();
        s_generation = asset_generation();
    }
    Key key{std::string(model), object, std::string(fix_name)};
    if (auto it = s_meshes.find(key); it != s_meshes.end()) return it->second;

    std::shared_ptr<const ItemMesh> out;
    const std::string glb_path = item_glb_path(model);
    const std::optional<std::vector<std::uint8_t>> glb = read_scene_file(glb_path);
    if (!glb) {
        log(1, "[SCENE] " + glb_path + ": not found; the item model is not drawn");
    } else {
        const std::optional<std::vector<std::uint8_t>> side = read_scene_file(item_sidecar_path(model));
        const std::span<const std::uint8_t> side_bytes =
            side ? std::span<const std::uint8_t>(*side) : std::span<const std::uint8_t>();
        auto mesh = read_item_mesh(*glb, side_bytes, object, fix ? &fix : nullptr, glb_path);
        if (mesh)
            out = std::make_shared<const ItemMesh>(std::move(mesh.value()));
        else
            log(1, "[SCENE] " + mesh.error());
    }
    s_meshes.emplace(std::move(key), out);
    return out;
}

Result<std::vector<std::uint8_t>> item_sheet_rgba(std::span<const std::uint8_t> indexed_png,
                                                  std::span<const std::uint8_t> tim_json, int palette, int& width,
                                                  int& height) {
    using R = Result<std::vector<std::uint8_t>>;
    auto image = png::decode(indexed_png);
    if (!image) return R::fail(image.error());
    if (image.value().color_type != png::ColorType::Indexed) return R::fail("the picture is not indexed");
    auto doc = json::parse(std::string_view(reinterpret_cast<const char*>(tim_json.data()), tim_json.size()));
    if (!doc) return R::fail(doc.error());
    const json::Value* pals = doc.value().find("palettes");
    if (pals == nullptr || !pals->is_array() || palette < 0 || static_cast<std::size_t>(palette) >= pals->array().size())
        return R::fail("no CLUT row " + std::to_string(palette));
    std::vector<std::uint16_t> words;
    for (const json::Value& w : pals->array()[static_cast<std::size_t>(palette)].array())
        words.push_back(w.is_string() ? static_cast<std::uint16_t>(std::strtoul(w.string().c_str(), nullptr, 16)) : 0);

    const png::Image& im = image.value();
    width = static_cast<int>(im.width);
    height = static_cast<int>(im.height);
    // The PS1 prim shader reads a 5-bit channel as c * 8 / 256; in 8 bits of 255 that is c * 8 * 255 / 256.
    auto channel = [](int v) { return static_cast<std::uint8_t>((v * 8 * 255 + 128) / 256); };
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(width) * height * 4, 0);
    for (std::size_t i = 0; i < static_cast<std::size_t>(width) * height; ++i) {
        const std::uint8_t index = im.pixels[i];
        const std::uint16_t c = index < words.size() ? words[index] : 0;
        std::uint8_t* o = &rgba[i * 4];
        o[0] = channel(c & 31);
        o[1] = channel((c >> 5) & 31);
        o[2] = channel((c >> 10) & 31);
        o[3] = c == 0 ? 0 : ((c & 0x8000) ? 128 : 255);
    }
    return R::ok(std::move(rgba));
}

namespace {

struct ItemTriangle {
    PrelitVertex v[3];
    std::uint64_t texture = 0;
    Blend blend = Blend::Opaque;
    float depth = 0;  // mean view z
};

// The scene texture of a sheet picture: registered on first use, keyed by (TIM, row). 0 when it cannot be read.
struct SheetTexture {
    std::uint64_t key = 0;
    int width = 0, height = 0;
};

SheetTexture sheet_texture(const std::string& tim, int palette) {
    static std::map<std::pair<std::string, int>, SheetTexture> s_textures;
    // The store forgot the posed meshes' textures (`mods reload`): they are registered again.
    static std::uint32_t s_generation = asset_generation();
    if (s_generation != asset_generation()) {
        s_textures.clear();
        s_generation = asset_generation();
    }
    const auto id = std::make_pair(tim, palette);
    if (auto it = s_textures.find(id); it != s_textures.end()) return it->second;
    SheetTexture out;
    std::string stem = tim;
    if (const std::size_t dot = stem.find_last_of('.'); dot != std::string::npos) stem.erase(dot);
    const std::optional<std::vector<std::uint8_t>> png = read_scene_file("convert/indexed/" + stem + ".png");
    const std::optional<std::vector<std::uint8_t>> desc = read_scene_file("convert/textures/" + stem + ".tim.json");
    if (!png || !desc) {
        log(1, "[SCENE] item sheet " + tim + ": the import's picture is missing; the models on it are not drawn");
    } else if (auto rgba = item_sheet_rgba(*png, *desc, palette, out.width, out.height); !rgba) {
        log(1, "[SCENE] item sheet " + tim + " row " + std::to_string(palette) + ": " + rgba.error());
    } else {
        std::uint64_t hash = 1469598103934665603ull;
        auto mix = [&hash](std::uint64_t v) { hash = (hash ^ v) * 1099511628211ull; };
        for (char c : tim) mix(static_cast<unsigned char>(c));
        mix(static_cast<std::uint64_t>(palette));
        out.key = hash | (1ull << 63);  // never 0; character texture keys keep the top bit clear
        add_prelit_texture(out.key, out.width, out.height, rgba.value().data());
    }
    s_textures.emplace(id, out);
    return out;
}

// The OT bucket the PS1 drawer gives a triangle: its sort depth (a quarter of the mean view z of its corners, the
// GTE's average with the triangle factor 0x155 / 4096 over the three z) shifted down by the sort shift. Farther
// triangles land in higher buckets, which are drawn first. (Not the depth cue of the corners: it falls with distance,
// would draw the near models of the carousel first, and a farther model would cover a nearer one's translucent shell.)
int sort_bucket(const PrelitVertex (&v)[3], int shift) {
    constexpr std::int64_t kTriangleAverage = 0x155;
    std::int64_t sum = 0;
    for (const PrelitVertex& c : v) sum += std::clamp<std::int64_t>(static_cast<std::int64_t>(c.z), 0, 65535);
    const std::int64_t otz = std::min<std::int64_t>((kTriangleAverage * sum) >> 12, 0xFFFF);
    return static_cast<int>(otz >> shift);
}

}  // namespace

ItemModelDraw add_item_model(const ItemMesh& model, const ItemModelPose& pose, const ItemProjection& projection,
                             int dim, int shift, int bucket_count, const ItemSheetResolver& resolve) {
    ItemModelDraw out;
    if (!model.drawable) return out;

    // Each material's picture: the import's, else the one the game holds on that page now.
    struct Bound {
        SheetTexture texture;
        int u0 = 0, v0 = 0;
        int abr = 0;
    };
    std::vector<Bound> bound(model.materials.size());
    for (std::size_t i = 0; i < model.materials.size(); ++i) {
        const ItemMaterial& m = model.materials[i];
        if (!m.textured) continue;
        if (m.own_rgba) {
            // The model's own picture: registered once, keyed by the picture's address.
            static std::map<const void*, SheetTexture> s_own;
            static std::uint32_t s_own_generation = asset_generation();
            if (s_own_generation != asset_generation()) {
                s_own.clear();
                s_own_generation = asset_generation();
            }
            auto [it, added] = s_own.try_emplace(m.own_rgba.get());
            if (added) {
                it->second.width = m.own_width;
                it->second.height = m.own_height;
                it->second.key = (reinterpret_cast<std::uintptr_t>(m.own_rgba.get()) * 1099511628211ull) | (1ull << 63);
                add_prelit_texture(it->second.key, m.own_width, m.own_height, m.own_rgba->data());
            }
            bound[i].texture = it->second;
            continue;
        }
        std::optional<ItemSheet> sheet = m.sheet;
        if (!sheet && resolve) sheet = resolve(m.tpage, m.clut);
        if (!sheet) return out;
        bound[i].texture = sheet_texture(sheet->tim, sheet->palette);
        if (bound[i].texture.key == 0) return out;
        bound[i].u0 = sheet->u0;
        bound[i].v0 = sheet->v0;
        bound[i].abr = (m.tpage >> 5) & 3;
    }

    const float h = projection.h;
    const float ofs_x = projection.ofs_x, ofs_y = projection.ofs_y;
    auto shade = [dim](int c) { return static_cast<std::uint8_t>(std::clamp((c * dim) >> 8, 0, 255)); };

    static std::vector<ItemTriangle> tris;
    tris.clear();
    std::int64_t bucket_sum = 0;
    for (const ItemFace& f : model.faces) {
        const ItemMaterial& m = model.materials[static_cast<std::size_t>(f.material)];
        const Bound& b = bound[static_cast<std::size_t>(f.material)];
        ItemTriangle t;
        double sx[3], sy[3];
        bool visible = true;
        for (int k = 0; k < 3; k++) {
            const auto& p = f.position[static_cast<std::size_t>(k)];
            double v[3];
            for (int r = 0; r < 3; r++)
                v[r] = static_cast<double>(pose.t[r]) +
                       (pose.m[r][0] * static_cast<double>(p[0]) + pose.m[r][1] * static_cast<double>(p[1]) +
                        pose.m[r][2] * static_cast<double>(p[2])) /
                           4096.0;
            if (v[2] <= 0.0) visible = false;
            sx[k] = ofs_x + h * v[0] / std::max(v[2], 1.0);
            sy[k] = ofs_y + h * v[1] / std::max(v[2], 1.0);
            t.v[k].x = static_cast<float>(v[0]);
            t.v[k].y = static_cast<float>(v[1]);
            t.v[k].z = static_cast<float>(v[2]);
        }
        const bool double_sided = m.textured && f.double_sided;
        const double nclip = (sx[1] - sx[0]) * (sy[2] - sy[0]) - (sx[2] - sx[0]) * (sy[1] - sy[0]);
        if (!visible || !(double_sided || nclip > 0.0)) continue;
        if (m.textured && m.own_rgba) {
            t.texture = b.texture.key;
            float bu0 = 1, bv0 = 1, bu1 = 0, bv1 = 0;
            for (const auto& uv : f.uv) {
                bu0 = std::min(bu0, uv[0]), bv0 = std::min(bv0, uv[1]), bu1 = std::max(bu1, uv[0]),
                bv1 = std::max(bv1, uv[1]);
            }
            for (int k = 0; k < 3; k++) {
                PrelitVertex& o = t.v[k];
                o.u = f.uv[static_cast<std::size_t>(k)][0];
                o.v = f.uv[static_cast<std::size_t>(k)][1];
                o.r = o.g = o.b = shade(0x80);
                o.box_u0 = bu0;
                o.box_v0 = bv0;
                o.box_u1 = bu1;
                o.box_v1 = bv1;
            }
        } else if (m.textured) {
            t.texture = b.texture.key;
            if (f.semi) t.blend = blend_of_abr(b.abr);
            const float w = static_cast<float>(b.texture.width), hh = static_cast<float>(b.texture.height);
            int u[3], vv[3];
            for (int k = 0; k < 3; k++) {
                u[k] = f.texel[static_cast<std::size_t>(k)][0] - b.u0;
                vv[k] = f.texel[static_cast<std::size_t>(k)][1] - b.v0;
            }
            const float bu0 = static_cast<float>(std::min({u[0], u[1], u[2]}) + 0.5) / w;
            const float bv0 = static_cast<float>(std::min({vv[0], vv[1], vv[2]}) + 0.5) / hh;
            const float bu1 = static_cast<float>(std::max({u[0], u[1], u[2]}) + 0.5) / w;
            const float bv1 = static_cast<float>(std::max({vv[0], vv[1], vv[2]}) + 0.5) / hh;
            for (int k = 0; k < 3; k++) {
                PrelitVertex& o = t.v[k];
                o.u = static_cast<float>(u[k] + 0.5) / w;
                o.v = static_cast<float>(vv[k] + 0.5) / hh;
                o.r = o.g = o.b = shade(0x80);
                o.box_u0 = bu0;
                o.box_v0 = bv0;
                o.box_u1 = bu1;
                o.box_v1 = bv1;
            }
        } else {
            for (PrelitVertex& o : t.v) {
                o.r = shade(f.color[0]);
                o.g = shade(f.color[1]);
                o.b = shade(f.color[2]);
            }
        }
        t.depth = (t.v[0].z + t.v[1].z + t.v[2].z) / 3.0f;
        bucket_sum += sort_bucket(t.v, shift);
        tris.push_back(t);
    }
    out.drawn = true;
    if (tris.empty()) return out;

    // Opaque first, then the semi-transparent triangles back to front without depth writes, as the PS1 path's item
    // pass drew them: bucket order cannot separate a shell from what is inside it.
    std::stable_sort(tris.begin(), tris.end(), [](const ItemTriangle& a, const ItemTriangle& b) {
        const bool ta = a.blend != Blend::Opaque, tb = b.blend != Blend::Opaque;
        if (ta != tb) return !ta;
        if (!ta) return a.texture < b.texture;
        return a.depth > b.depth;
    });
    Frame& frame = current_frame();
    for (std::size_t i = 0; i < tris.size();) {
        std::size_t end = i;
        while (end < tris.size() && tris[end].texture == tris[i].texture && tris[end].blend == tris[i].blend) end++;
        PrelitMesh m;
        m.texture_key = tris[i].texture;
        m.blend = tris[i].blend;
        m.fade = 1.0f;
        m.casts_shadow = false;
        m.isolated = true;
        m.h = h;
        m.geom_ofs_x = ofs_x;
        m.geom_ofs_y = ofs_y;
        for (std::size_t j = i; j < end; j++) {
            for (const PrelitVertex& v : tris[j].v) {
                m.indices.push_back(static_cast<std::uint32_t>(m.vertices.size()));
                m.vertices.push_back(v);
            }
        }
        frame.prelit.push_back(std::move(m));
        out.meshes.push_back(static_cast<int>(frame.prelit.size()) - 1);
        i = end;
    }
    out.bucket = std::clamp(static_cast<int>(bucket_sum / static_cast<std::int64_t>(tris.size())), 1, bucket_count - 1);
    return out;
}

}  // namespace sh::scene
