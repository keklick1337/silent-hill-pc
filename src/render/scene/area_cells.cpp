#include "render/scene/area_cells.h"

#include "formats/common/json.h"
#include "formats/gltf/cgltf_prefixed.h"
#include "formats/gltf/ps1_blend.h"
#include "formats/png/sh_png.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <tuple>

namespace sh::scene {
namespace {

// glTF metres (the converter's 180 degree turn about Z, level_flat.h) back to PS1 axes and units.
constexpr float kUnitsPerMetre = 256.0f;
constexpr int kCellUnits = 40 * 256;

struct DataDeleter {
    void operator()(cgltf_data* d) const { cgltf_free(d); }
};

const cgltf_accessor* attribute(const cgltf_primitive& p, cgltf_attribute_type type) {
    for (cgltf_size i = 0; i < p.attributes_count; ++i)
        if (p.attributes[i].type == type && p.attributes[i].index == 0) return p.attributes[i].data;
    return nullptr;
}

std::string extras_json(const cgltf_data* data, const cgltf_extras& extras) {
    cgltf_size size = 0;
    if (cgltf_copy_extras_json(data, &extras, nullptr, &size) != cgltf_result_success || size == 0) return {};
    std::string text(size, '\0');
    if (cgltf_copy_extras_json(data, &extras, text.data(), &size) != cgltf_result_success) return {};
    while (!text.empty() && text.back() == '\0') text.pop_back();
    return text;
}

std::optional<json::Value> parse_extras(const cgltf_data* data, const cgltf_extras& extras) {
    const std::string text = extras_json(data, extras);
    if (text.empty()) return std::nullopt;
    auto v = json::parse(text);
    if (!v) return std::nullopt;
    return std::move(v.value());
}

int int_member(const json::Value& v, std::string_view key, int fallback) {
    const json::Value* m = v.find(key);
    return (m != nullptr && m->is_number()) ? static_cast<int>(m->number()) : fallback;
}

CellMaterial material_of(const cgltf_data* data, const cgltf_material* m) {
    CellMaterial out;
    if (m == nullptr) return out;
    if (auto e = parse_extras(data, m->extras)) {
        if (const json::Value* png = e->find("png"); png != nullptr && png->is_string() && !png->string().empty()) {
            out.texture = "convert/" + png->string();
            out.texel_center_uv = true;
        }
        const json::Value* tim = e->find("tim");
        const json::Value* own = e->find("material");
        if (out.texture.empty() && tim != nullptr && tim->is_string() && tim->string().empty() && own != nullptr &&
            own->is_string() && !own->string().empty()) {
            out.unpictured = own->string();
        }
        if (const json::Value* st = e->find("semi_transparent"); st != nullptr && st->is_bool())
            out.semi_transparent = st->boolean();
        if (const json::Value* abr = e->find("abr"); abr != nullptr && abr->is_number() && abr->number() >= 0 &&
                                                         abr->number() <= 3) {
            out.blend = blend_of_abr(static_cast<int>(abr->number()));
        } else if (const json::Value* name = e->find("blend_mode"); name != nullptr && name->is_string()) {
            if (const int a = gltf::abr_of_blend_mode(name->string()); a >= 0) out.blend = blend_of_abr(a);
        }
        out.palette = int_member(*e, "palette", -1);
    }
    // A material without the importer's extras (a modder's own file): its image, relative to maps/areas/.
    if (out.texture.empty() && m->has_pbr_metallic_roughness) {
        const cgltf_texture* t = m->pbr_metallic_roughness.base_color_texture.texture;
        if (t != nullptr && t->image != nullptr && t->image->uri != nullptr) {
            std::string uri = t->image->uri;
            while (uri.rfind("../", 0) == 0) uri.erase(0, 3);
            out.texture = uri;
        }
    }
    if (out.texture.empty() == false && m->alpha_mode == cgltf_alpha_mode_blend) out.semi_transparent = true;
    return out;
}

// The triangles of one material while a mesh is read: three indices each, the model each came from (all its
// instances count as one), the model's ordering slot (-1 unknown) and the triangle's overlay layer (CellRange::layer).
struct MaterialTriangles {
    std::vector<std::uint32_t> idx;
    std::vector<int> model;
    std::vector<int> slot;
    std::vector<std::uint8_t> layer;
};

struct MeshBuild {
    std::map<const cgltf_material*, int> material_index;
    std::map<int, MaterialTriangles> by_material;
    std::map<const cgltf_mesh*, int> models;
    std::map<const cgltf_mesh*, int> order_slots;
};

// The ordering-table slot the game sorted a model's polygons from, by the model's group (`ot_group` of the importer,
// the LM model flags): the nearer slot is drawn later, over the others at the same depth.
int order_slot_of_group(int group) {
    constexpr int kSlots[] = {2, 0, 4, 33, 66, 99};
    return (group >= 0 && group < 6) ? kSlots[group] : -1;
}

// Appends every primitive of `node` and its children, transformed by the node's world matrix.
void append_node(const cgltf_data* data, const cgltf_node* node, CellMesh& cell, MeshBuild& build) {
    std::map<const cgltf_material*, int>& material_index = build.material_index;
    if (node->mesh != nullptr) {
        const int model_id = build.models.try_emplace(node->mesh, static_cast<int>(build.models.size())).first->second;
        const auto slot_it = build.order_slots.find(node->mesh);
        const int slot = slot_it != build.order_slots.end() ? slot_it->second : -1;
        float world[16];
        cgltf_node_transform_world(node, world);
        // Normals: the node matrices of the importer are rotations and translations, so the 3x3 turns them too.
        for (cgltf_size pi = 0; pi < node->mesh->primitives_count; ++pi) {
            const cgltf_primitive& p = node->mesh->primitives[pi];
            if (p.type != cgltf_primitive_type_triangles) continue;
            const cgltf_accessor* pos = attribute(p, cgltf_attribute_type_position);
            if (pos == nullptr || pos->count == 0) continue;
            const cgltf_accessor* nrm = attribute(p, cgltf_attribute_type_normal);
            const cgltf_accessor* uv = attribute(p, cgltf_attribute_type_texcoord);

            auto [it, added] = material_index.try_emplace(p.material, static_cast<int>(cell.materials.size()));
            if (added) cell.materials.push_back(material_of(data, p.material));
            const int mat = it->second;

            const std::uint32_t base = static_cast<std::uint32_t>(cell.vertices.size());
            const float ox = static_cast<float>(cell.cell_x * kCellUnits);
            const float oz = static_cast<float>(cell.cell_z * kCellUnits);
            for (cgltf_size i = 0; i < pos->count; ++i) {
                float lp[3] = {0, 0, 0}, ln[3] = {0, 0, -1}, lt[2] = {0, 0};
                cgltf_accessor_read_float(pos, i, lp, 3);
                if (nrm != nullptr) cgltf_accessor_read_float(nrm, i, ln, 3);
                if (uv != nullptr) cgltf_accessor_read_float(uv, i, lt, 2);
                float wp[3], wn[3];
                for (int r = 0; r < 3; ++r) {
                    wp[r] = world[0 * 4 + r] * lp[0] + world[1 * 4 + r] * lp[1] + world[2 * 4 + r] * lp[2] + world[12 + r];
                    wn[r] = world[0 * 4 + r] * ln[0] + world[1 * 4 + r] * ln[1] + world[2 * 4 + r] * ln[2];
                }
                const float len = std::sqrt(wn[0] * wn[0] + wn[1] * wn[1] + wn[2] * wn[2]);
                if (len > 1e-6f)
                    for (float& c : wn) c /= len;
                CellVertex v;
                v.x = -wp[0] * kUnitsPerMetre - ox;
                v.y = -wp[1] * kUnitsPerMetre;
                v.z = wp[2] * kUnitsPerMetre - oz;
                // The file's outward normal, turned like positions and negated: the inward one of the PS1 data.
                v.nx = wn[0];
                v.ny = wn[1];
                v.nz = -wn[2];
                v.u = lt[0];
                v.v = lt[1];
                cell.vertices.push_back(v);
            }
            MaterialTriangles& tris = build.by_material[mat];
            std::vector<std::uint32_t>& out = tris.idx;
            if (p.indices != nullptr) {
                for (cgltf_size i = 0; i + 2 < p.indices->count; i += 3)
                    for (int k = 0; k < 3; ++k)
                        out.push_back(base + static_cast<std::uint32_t>(cgltf_accessor_read_index(p.indices, i + k)));
            } else {
                for (cgltf_size i = 0; i + 2 < pos->count; i += 3)
                    for (int k = 0; k < 3; ++k) out.push_back(base + static_cast<std::uint32_t>(i + k));
            }
            tris.model.resize(out.size() / 3, model_id);
            tris.slot.resize(out.size() / 3, slot);
            tris.layer.resize(out.size() / 3, 0);
        }
    }
    for (cgltf_size c = 0; c < node->children_count; ++c) append_node(data, node->children[c], cell, build);
}

// Finds the triangles that lie on other triangles of the mesh (road markings on the asphalt, posters on a wall,
// painted shadows) and gives them a layer above what they cover: the depth buffer alone does not order them
// (RENDER.md section 6.6). Two surfaces facing the same way, sharing a place and no farther apart than kStackGap are
// a stack, and the one in front is the upper one. In one plane the upper one is the surface whose model the game
// sorted from a nearer ordering slot; models of one slot, or without a known one, are told apart by size: the smaller
// surface is the one laid on top (were it under, nobody would have seen it). Size is the area of the triangle's
// model and material on that plane, then of its connected patch.
void layer_overlays(const CellMesh& cell, std::map<int, MaterialTriangles>& by_material) {
    constexpr double kSamePlane = 0.5;  // plane distances this close are one plane (the data is in whole units)
    // Posters and signs hang up to a dozen units off their wall. Farther apart, the gap is larger than what the
    // rasterised depth of a surface can be off by at any distance one looks at it from.
    constexpr double kStackGap = 24.0;
    constexpr double kTouch = 0.25;     // sharing a border is not sharing a place
    struct Tri {
        MaterialTriangles* owner = nullptr;
        std::size_t index = 0;
        int material = 0;
        int model = 0;
        int slot = -1;
        std::array<std::array<double, 3>, 3> p{};
        std::array<double, 3> n{};  // out of the front face
        double d = 0;
        double area = 0;
        std::array<std::array<double, 2>, 3> q{};  // in the plane
        std::array<double, 4> box{};
        int patch = 0;
    };
    // By facing: the two sides of a two-sided wall are not layers of each other.
    using Facing = std::array<std::int64_t, 3>;
    std::map<Facing, std::vector<Tri>> facings;
    for (auto& [mat, tris] : by_material) {
        for (std::size_t t = 0; t < tris.layer.size(); ++t) {
            Tri tri;
            tri.owner = &tris;
            tri.index = t;
            tri.material = mat;
            tri.model = tris.model[t];
            tri.slot = tris.slot[t];
            for (std::size_t k = 0; k < 3; ++k) {
                const CellVertex& v = cell.vertices[tris.idx[t * 3 + k]];
                tri.p[k] = {v.x, v.y, v.z};
            }
            const auto& a = tri.p[0];
            double e1[3], e2[3];
            for (std::size_t k = 0; k < 3; ++k) {
                e1[k] = tri.p[1][k] - a[k];
                e2[k] = tri.p[2][k] - a[k];
            }
            const double c[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2],
                                 e1[0] * e2[1] - e1[1] * e2[0]};
            const double len = std::sqrt(c[0] * c[0] + c[1] * c[1] + c[2] * c[2]);
            if (len < 1e-3) continue;
            tri.n = {c[0] / len, c[1] / len, c[2] / len};
            tri.d = tri.n[0] * a[0] + tri.n[1] * a[1] + tri.n[2] * a[2];
            tri.area = len * 0.5;
            facings[{std::llround(tri.n[0] * 64.0), std::llround(tri.n[1] * 64.0), std::llround(tri.n[2] * 64.0)}]
                .push_back(tri);
        }
    }

    for (auto& [facing, list] : facings) {
        if (list.size() < 2) continue;
        // Plane axes: drop the normal's dominant axis.
        const std::array<double, 3>& n = list.front().n;
        const int drop = (std::abs(n[0]) >= std::abs(n[1]) && std::abs(n[0]) >= std::abs(n[2])) ? 0
                         : (std::abs(n[1]) >= std::abs(n[2]) ? 1 : 2);
        const std::size_t ax0 = static_cast<std::size_t>((drop + 1) % 3), ax1 = static_cast<std::size_t>((drop + 2) % 3);
        for (Tri& t : list) {
            t.box = {1e30, 1e30, -1e30, -1e30};
            for (std::size_t k = 0; k < 3; ++k) {
                t.q[k] = {t.p[k][ax0], t.p[k][ax1]};
                t.box[0] = std::min(t.box[0], t.q[k][0]);
                t.box[1] = std::min(t.box[1], t.q[k][1]);
                t.box[2] = std::max(t.box[2], t.q[k][0]);
                t.box[3] = std::max(t.box[3], t.q[k][1]);
            }
        }
        // Seen along the normal, the triangles cover a common area: no edge of either separates them.
        auto overlap = [&](const Tri& a, const Tri& b) {
            if (a.box[0] > b.box[2] - kTouch || b.box[0] > a.box[2] - kTouch || a.box[1] > b.box[3] - kTouch ||
                b.box[1] > a.box[3] - kTouch)
                return false;
            for (const Tri* t : {&a, &b}) {
                for (std::size_t k = 0; k < 3; ++k) {
                    const auto& p0 = t->q[k];
                    const auto& p1 = t->q[(k + 1) % 3];
                    double ex = p1[0] - p0[0], ey = p1[1] - p0[1];
                    const double el = std::sqrt(ex * ex + ey * ey);
                    if (el < 1e-9) continue;
                    ex /= el;
                    ey /= el;
                    double lo[2] = {1e30, 1e30}, hi[2] = {-1e30, -1e30};
                    int side = 0;
                    for (const Tri* s : {&a, &b}) {
                        for (std::size_t j = 0; j < 3; ++j) {
                            const double v = (s->q[j][0] - p0[0]) * -ey + (s->q[j][1] - p0[1]) * ex;
                            lo[side] = std::min(lo[side], v);
                            hi[side] = std::max(hi[side], v);
                        }
                        ++side;
                    }
                    if (hi[0] - lo[1] < kTouch || hi[1] - lo[0] < kTouch) return false;
                }
            }
            return true;
        };
        auto same_plane = [&](const Tri& a, const Tri& b) { return std::abs(a.d - b.d) <= kSamePlane; };
        auto same_surface = [&](const Tri& a, const Tri& b) {
            return a.model == b.model && a.material == b.material && same_plane(a, b);
        };

        // Patches: triangles of one model and material joined along whole edges.
        std::vector<int> parent(list.size());
        for (std::size_t i = 0; i < list.size(); ++i) parent[i] = static_cast<int>(i);
        auto find = [&](int i) {
            while (parent[static_cast<std::size_t>(i)] != i) {
                parent[static_cast<std::size_t>(i)] = parent[static_cast<std::size_t>(parent[static_cast<std::size_t>(i)])];
                i = parent[static_cast<std::size_t>(i)];
            }
            return i;
        };
        using Point = std::array<std::int64_t, 2>;
        std::map<std::pair<Point, Point>, std::vector<int>> edges;
        for (std::size_t i = 0; i < list.size(); ++i) {
            for (std::size_t k = 0; k < 3; ++k) {
                Point a = {std::llround(list[i].q[k][0] * 16.0), std::llround(list[i].q[k][1] * 16.0)};
                Point b = {std::llround(list[i].q[(k + 1) % 3][0] * 16.0), std::llround(list[i].q[(k + 1) % 3][1] * 16.0)};
                if (b < a) std::swap(a, b);
                edges[{a, b}].push_back(static_cast<int>(i));
            }
        }
        for (const auto& [edge, users] : edges) {
            for (std::size_t i = 0; i < users.size(); ++i) {
                for (std::size_t j = i + 1; j < users.size(); ++j) {
                    const Tri& a = list[static_cast<std::size_t>(users[i])];
                    const Tri& b = list[static_cast<std::size_t>(users[j])];
                    // Neighbours lie on the two sides of their edge; a repeated instance lies on itself.
                    if (!same_surface(a, b) || overlap(a, b)) continue;
                    parent[static_cast<std::size_t>(find(users[i]))] = find(users[j]);
                }
            }
        }
        std::map<int, double> patch_area;
        std::map<std::tuple<int, int, std::int64_t>, double> surface_area;
        for (std::size_t i = 0; i < list.size(); ++i) {
            Tri& t = list[i];
            t.patch = find(static_cast<int>(i));
            patch_area[t.patch] += t.area;
            surface_area[{t.model, t.material, std::llround(t.d)}] += t.area;
        }
        // Larger first; the patch id only makes the order total.
        using Rank = std::tuple<double, double, int>;
        std::map<int, Rank> rank;
        for (const Tri& t : list)
            rank[t.patch] = {surface_area[{t.model, t.material, std::llround(t.d)}], patch_area[t.patch], -t.patch};
        auto lies_under = [&](const Tri& a, const Tri& b) {
            if (!same_plane(a, b)) return a.d < b.d;
            if (a.slot >= 0 && b.slot >= 0 && a.slot != b.slot) return a.slot > b.slot;
            return rank[a.patch] > rank[b.patch];
        };

        // Triangle -> the triangles it lies on. Only the triangles of a surface that cover something are lifted.
        std::map<int, std::vector<int>> under;
        std::vector<std::size_t> order(list.size());
        for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
        std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) { return list[a].box[0] < list[b].box[0]; });
        for (std::size_t oi = 0; oi < order.size(); ++oi) {
            const Tri& a = list[order[oi]];
            for (std::size_t oj = oi + 1; oj < order.size(); ++oj) {
                const Tri& b = list[order[oj]];
                if (b.box[0] > a.box[2] - kTouch) break;
                // One model's material over itself is the same surface twice (cells repeat instances in place).
                if (a.patch == b.patch || std::abs(a.d - b.d) > kStackGap || same_surface(a, b) || !overlap(a, b)) continue;
                const bool a_under = lies_under(a, b);
                std::vector<int>& u = under[static_cast<int>(a_under ? order[oj] : order[oi])];
                const int lower = static_cast<int>(a_under ? order[oi] : order[oj]);
                if (std::find(u.begin(), u.end(), lower) == u.end()) u.push_back(lower);
            }
        }
        if (under.empty()) continue;
        // Slots and sizes together are not one order, so the layers settle by passes; a loop of surfaces each lying
        // on the next ends at the top layer.
        std::map<int, int> layer;
        for (int pass = 0; pass <= kMaxOverlayLayer; ++pass) {
            bool changed = false;
            for (const auto& [upper, lowers] : under) {
                int l = 0;
                for (int lower : lowers) {
                    const auto it = layer.find(lower);
                    l = std::max(l, (it != layer.end() ? it->second : 0) + 1);
                }
                l = std::min(l, kMaxOverlayLayer);
                int& now = layer[upper];
                changed = changed || now != l;
                now = l;
            }
            if (!changed) break;
        }
        for (const Tri& t : list)
            if (const auto it = layer.find(static_cast<int>(&t - list.data())); it != layer.end())
                t.owner->layer[t.index] = static_cast<std::uint8_t>(it->second);
    }
}

// Gives every triangle of an importer material its UV box. Vertices are shared between polygons whose boxes differ
// (two wall quads meeting at u = 32), so a vertex is copied when a second box needs it.
void stamp_uv_boxes(CellMesh& cell, std::map<int, MaterialTriangles>& by_material) {
    using Box = std::array<float, 4>;
    std::map<std::uint32_t, std::vector<std::pair<Box, std::uint32_t>>> copies;
    for (auto& [mat, tris] : by_material) {
        std::vector<std::uint32_t>& idx = tris.idx;
        if (!cell.materials[static_cast<std::size_t>(mat)].texel_center_uv) continue;
        for (std::size_t t = 0; t + 2 < idx.size(); t += 3) {
            Box box = {1e30f, 1e30f, -1e30f, -1e30f};
            for (int k = 0; k < 3; ++k) {
                const CellVertex& v = cell.vertices[idx[t + k]];
                box[0] = std::min(box[0], v.u);
                box[1] = std::min(box[1], v.v);
                box[2] = std::max(box[2], v.u);
                box[3] = std::max(box[3], v.v);
            }
            for (int k = 0; k < 3; ++k) {
                const std::uint32_t src = idx[t + k];
                auto& list = copies[src];
                std::uint32_t use = src;
                bool found = false;
                for (const auto& [b, i] : list)
                    if (b == box) {
                        use = i;
                        found = true;
                        break;
                    }
                if (!found) {
                    if (!list.empty()) {
                        use = static_cast<std::uint32_t>(cell.vertices.size());
                        const CellVertex copy = cell.vertices[src];
                        cell.vertices.push_back(copy);
                    }
                    CellVertex& v = cell.vertices[use];
                    v.box_u0 = box[0];
                    v.box_v0 = box[1];
                    v.box_u1 = box[2];
                    v.box_v1 = box[3];
                    list.emplace_back(box, use);
                }
                idx[t + k] = use;
            }
        }
    }
}

// Appends the seam lines of `cell` (CellMesh::seams). Edges are matched by position, a 1/16 unit grid absorbing
// the float noise of the instance transforms, so copies of a vertex (another normal or UV) still close an edge.
void add_seams(CellMesh& cell) {
    using Key = std::array<std::int64_t, 3>;
    auto key = [&](std::uint32_t i) {
        const CellVertex& v = cell.vertices[i];
        return Key{std::llround(v.x * 16.0f), std::llround(v.y * 16.0f), std::llround(v.z * 16.0f)};
    };
    std::map<std::pair<Key, Key>, int> uses;
    auto edge = [&](std::uint32_t a, std::uint32_t b) {
        Key ka = key(a), kb = key(b);
        if (kb < ka) std::swap(ka, kb);
        return std::make_pair(ka, kb);
    };
    for (const CellRange& r : cell.ranges) {
        if (cell.materials[static_cast<std::size_t>(r.material)].semi_transparent) continue;
        for (std::uint32_t t = r.first; t + 2 < r.first + r.count; t += 3)
            for (int k = 0; k < 3; ++k) ++uses[edge(cell.indices[t + k], cell.indices[t + (k + 1) % 3])];
    }
    for (std::size_t ri = 0; ri < cell.ranges.size(); ++ri) {
        const CellRange r = cell.ranges[ri];
        if (cell.materials[static_cast<std::size_t>(r.material)].semi_transparent) continue;
        CellRange s;
        s.material = r.material;
        s.first = static_cast<std::uint32_t>(cell.indices.size());
        for (std::uint32_t t = r.first; t + 2 < r.first + r.count; t += 3)
            for (int k = 0; k < 3; ++k) {
                const std::uint32_t a = cell.indices[t + k], b = cell.indices[t + (k + 1) % 3];
                if (uses[edge(a, b)] != 1) continue;
                cell.indices.push_back(a);
                cell.indices.push_back(b);
            }
        s.count = static_cast<std::uint32_t>(cell.indices.size()) - s.first;
        if (s.count > 0) cell.seams.push_back(s);
    }
}

// Overlay layers, UV boxes, then the triangles by material and layer, opaque ranges first: the semi-transparent ones
// blend over them, and within each kind a layer follows what it lies on.
void finish_mesh(CellMesh& cell, MeshBuild& build) {
    std::map<int, MaterialTriangles>& by_material = build.by_material;
    layer_overlays(cell, by_material);
    stamp_uv_boxes(cell, by_material);
    for (int pass = 0; pass < 2; ++pass) {
        for (int layer = 0; layer <= kMaxOverlayLayer; ++layer) {
            for (auto& [mat, tris] : by_material) {
                if (cell.materials[static_cast<std::size_t>(mat)].semi_transparent != (pass == 1)) continue;
                CellRange r;
                r.material = mat;
                r.layer = layer;
                r.first = static_cast<std::uint32_t>(cell.indices.size());
                for (std::size_t t = 0; t < tris.layer.size(); ++t) {
                    if (tris.layer[t] != layer) continue;
                    const auto at = tris.idx.begin() + static_cast<std::ptrdiff_t>(t * 3);
                    cell.indices.insert(cell.indices.end(), at, at + 3);
                }
                r.count = static_cast<std::uint32_t>(cell.indices.size()) - r.first;
                if (r.count > 0) cell.ranges.push_back(r);
            }
        }
    }
}

std::string upper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return s;
}

bool ends_with(std::string_view s, std::string_view tail) {
    return s.size() >= tail.size() && s.substr(s.size() - tail.size()) == tail;
}

constexpr std::string_view kModelsSuffix = "/models";

struct Parsed {
    std::unique_ptr<cgltf_data, DataDeleter> data;
    const cgltf_scene* scene = nullptr;
};

Result<Parsed> parse_glb(std::span<const std::uint8_t> glb, std::string_view path) {
    using R = Result<Parsed>;
    cgltf_options options{};
    options.type = cgltf_file_type_glb;
    cgltf_data* raw = nullptr;
    if (cgltf_parse(&options, glb.data(), glb.size(), &raw) != cgltf_result_success || raw == nullptr)
        return R::fail(std::string(path) + ": not a glTF binary");
    Parsed p;
    p.data.reset(raw);
    if (cgltf_load_buffers(&options, p.data.get(), nullptr) != cgltf_result_success)
        return R::fail(std::string(path) + ": buffers do not load");
    p.scene = raw->scene != nullptr ? raw->scene : (raw->scenes_count > 0 ? &raw->scenes[0] : nullptr);
    return R::ok(std::move(p));
}

// The models under `group` (direct children), keyed "<ROOT>/<NAME>" and "<ROOT>/#<model_index>".
void add_library(const cgltf_data* data, const cgltf_node* group, const std::string& root,
                 std::vector<LibraryModel>& out) {
    for (cgltf_size c = 0; c < group->children_count; ++c) {
        const cgltf_node* node = group->children[c];
        if (node->name == nullptr || node->mesh == nullptr) continue;
        LibraryModel m;
        m.name = root + "/" + upper(node->name);
        auto extras = parse_extras(data, node->extras);
        if (extras) {
            m.ot_group = int_member(*extras, "ot_group", 0);
            const int index = int_member(*extras, "model_index", -1);
            if (index >= 0) m.index_name = root + "/#" + std::to_string(index);
        }
        m.mesh.stem = m.name;
        MeshBuild build;
        append_node(data, node, m.mesh, build);
        finish_mesh(m.mesh, build);
        out.push_back(std::move(m));
    }
}

}  // namespace

Result<std::vector<CellMesh>> read_area_cells(std::span<const std::uint8_t> glb, std::string_view area_path) {
    using R = Result<std::vector<CellMesh>>;
    auto parsed = parse_glb(glb, area_path);
    if (!parsed) return R::fail(parsed.error());
    const cgltf_data* data = parsed.value().data.get();
    const cgltf_scene* scene = parsed.value().scene;

    std::vector<CellMesh> cells;
    if (scene == nullptr) return R::ok(std::move(cells));
    for (cgltf_size ni = 0; ni < scene->nodes_count; ++ni) {
        const cgltf_node* root = scene->nodes[ni];
        auto extras = parse_extras(data, root->extras);
        // Cell roots carry their grid square; libraries (BG_ITEM, MGR, ...) have none and are not cells.
        if (!extras || extras->find("cell_x") == nullptr || root->name == nullptr) continue;
        CellMesh cell;
        cell.stem = upper(root->name);
        cell.cell_x = int_member(*extras, "cell_x", 0);
        cell.cell_z = int_member(*extras, "cell_z", 0);
        MeshBuild build;
        // The cell's instances share their meshes with its model library, which names each model's group.
        for (cgltf_size c = 0; c < root->children_count; ++c) {
            const cgltf_node* group = root->children[c];
            if (group->name == nullptr || !ends_with(group->name, kModelsSuffix)) continue;
            for (cgltf_size m = 0; m < group->children_count; ++m) {
                const cgltf_node* model = group->children[m];
                if (model->mesh == nullptr) continue;
                if (auto model_extras = parse_extras(data, model->extras))
                    build.order_slots[model->mesh] = order_slot_of_group(int_member(*model_extras, "ot_group", -1));
            }
        }
        for (cgltf_size c = 0; c < root->children_count; ++c) {
            // The cell's model library is drawn by name (objects), not as part of the cell.
            const cgltf_node* child = root->children[c];
            if (child->name != nullptr && ends_with(child->name, kModelsSuffix)) continue;
            append_node(data, child, cell, build);
        }
        finish_mesh(cell, build);
        add_seams(cell);
        cells.push_back(std::move(cell));
    }
    return R::ok(std::move(cells));
}

Result<std::vector<LibraryModel>> read_glb_models(std::span<const std::uint8_t> glb, std::string_view path) {
    using R = Result<std::vector<LibraryModel>>;
    auto parsed = parse_glb(glb, path);
    if (!parsed) return R::fail(parsed.error());
    const cgltf_data* data = parsed.value().data.get();
    const cgltf_scene* scene = parsed.value().scene;
    std::vector<LibraryModel> out;
    if (scene == nullptr) return R::ok(std::move(out));
    const auto asset = parse_extras(data, data->asset.extras);
    for (cgltf_size ni = 0; ni < scene->nodes_count; ++ni) {
        const cgltf_node* root = scene->nodes[ni];
        if (root->name == nullptr) continue;
        auto extras = parse_extras(data, root->extras);
        // Only the importer's roots: a cell (its "/models" group) or a library (a "source" file); the map nodes of
        // the area's scenes are neither. The glb of an item model (a TMD) names its source on the asset.
        const bool sourced = extras && extras->find("source") != nullptr;
        if (!sourced && !(asset && asset->find("source") != nullptr)) continue;
        const std::string root_name = upper(root->name);
        if (sourced && extras->find("cell_x") != nullptr) {
            for (cgltf_size c = 0; c < root->children_count; ++c) {
                const cgltf_node* child = root->children[c];
                if (child->name != nullptr && ends_with(child->name, kModelsSuffix)) add_library(data, child, root_name, out);
            }
        } else {
            add_library(data, root, root_name, out);
        }
    }
    return R::ok(std::move(out));
}

std::vector<SceneTexture> mip_levels(const SceneTexture& t) {
    std::vector<SceneTexture> out;
    out.reserve(32);
    const SceneTexture* prev = &t;
    while (prev->width > 1 || prev->height > 1) {
        const int w = std::max(prev->width / 2, 1), h = std::max(prev->height / 2, 1);
        SceneTexture next;
        next.width = w;
        next.height = h;
        next.rgba.resize(static_cast<std::size_t>(w) * h * 4);
        const auto texel = [prev](int x, int y) {
            x = std::min(x, prev->width - 1);
            y = std::min(y, prev->height - 1);
            return &prev->rgba[(static_cast<std::size_t>(y) * prev->width + x) * 4];
        };
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                int sum[3] = {0, 0, 0}, n = 0, stp = 0;
                for (int k = 0; k < 4; ++k) {
                    const std::uint8_t* s = texel(x * 2 + (k & 1), y * 2 + (k >> 1));
                    if (s[3] == 0) continue;
                    for (int c = 0; c < 3; ++c) sum[c] += s[c];
                    ++n;
                    if (s[3] < 192) ++stp;
                }
                std::uint8_t* o = &next.rgba[(static_cast<std::size_t>(y) * w + x) * 4];
                if (n == 0) {
                    const std::uint8_t* s = texel(x * 2, y * 2);
                    for (int c = 0; c < 4; ++c) o[c] = s[c];
                    continue;
                }
                for (int c = 0; c < 3; ++c) o[c] = static_cast<std::uint8_t>(sum[c] / n);
                // Coverage by majority, as the shader's filter decides it: a cut-out keeps its edge at every level.
                o[3] = n < 2 ? 0 : (stp * 2 > n ? 128 : 255);
            }
        }
        out.push_back(std::move(next));
        prev = &out.back();
    }
    return out;
}

SceneTexture fit_texture(const SceneTexture& t, int max_side) {
    if (max_side <= 0 || (t.width <= max_side && t.height <= max_side) || t.width <= 0 || t.height <= 0) return t;
    const double scale = static_cast<double>(max_side) / std::max(t.width, t.height);
    SceneTexture out;
    out.width = std::clamp(static_cast<int>(t.width * scale), 1, max_side);
    out.height = std::clamp(static_cast<int>(t.height * scale), 1, max_side);
    out.rgba.resize(static_cast<std::size_t>(out.width) * out.height * 4);
    for (int y = 0; y < out.height; ++y) {
        const int y0 = static_cast<int>(static_cast<std::int64_t>(y) * t.height / out.height);
        const int y1 = std::max(y0 + 1, static_cast<int>(static_cast<std::int64_t>(y + 1) * t.height / out.height));
        for (int x = 0; x < out.width; ++x) {
            const int x0 = static_cast<int>(static_cast<std::int64_t>(x) * t.width / out.width);
            const int x1 = std::max(x0 + 1, static_cast<int>(static_cast<std::int64_t>(x + 1) * t.width / out.width));
            // Colour from the visible texels only, so a cut-out edge does not darken; alpha by majority class.
            std::uint64_t sum[3] = {0, 0, 0}, alpha = 0, visible = 0, n = 0;
            for (int sy = y0; sy < y1; ++sy)
                for (int sx = x0; sx < x1; ++sx) {
                    const std::uint8_t* s = &t.rgba[(static_cast<std::size_t>(sy) * t.width + sx) * 4];
                    alpha += s[3];
                    ++n;
                    if (s[3] == 0) continue;
                    sum[0] += s[0];
                    sum[1] += s[1];
                    sum[2] += s[2];
                    ++visible;
                }
            std::uint8_t* o = &out.rgba[(static_cast<std::size_t>(y) * out.width + x) * 4];
            if (visible == 0) {
                const std::uint8_t* s = &t.rgba[(static_cast<std::size_t>(y0) * t.width + x0) * 4];
                std::copy(s, s + 3, o);
            } else {
                for (int c = 0; c < 3; ++c) o[c] = static_cast<std::uint8_t>(sum[c] / visible);
            }
            const std::uint64_t a = alpha / n;
            o[3] = a < 64 ? 0 : (a < 192 ? 128 : 255);
        }
    }
    return out;
}

std::string tim_json_path(std::string_view png_path) {
    std::string p(png_path);
    const std::size_t slash = p.find_last_of('/');
    const std::size_t dot = p.find('.', slash == std::string::npos ? 0 : slash + 1);
    if (dot == std::string::npos) return {};
    return p.substr(0, dot) + ".tim.json";
}

Result<SceneTexture> decode_scene_texture(std::span<const std::uint8_t> png, std::span<const std::uint8_t> tim_json,
                                          int palette) {
    using R = Result<SceneTexture>;
    SceneTexture out;
    // The STP bit of each palette entry, from the TIM description: the PNG shows colours only.
    std::vector<bool> stp;
    if (!tim_json.empty() && palette >= 0) {
        auto doc = json::parse(std::string_view(reinterpret_cast<const char*>(tim_json.data()), tim_json.size()));
        if (doc) {
            const json::Value* pals = doc.value().find("palettes");
            if (pals != nullptr && pals->is_array() && static_cast<std::size_t>(palette) < pals->array().size()) {
                for (const json::Value& w : pals->array()[static_cast<std::size_t>(palette)].array()) {
                    unsigned word = 0;
                    if (w.is_string()) word = static_cast<unsigned>(std::strtoul(w.string().c_str(), nullptr, 16));
                    stp.push_back((word & 0x8000u) != 0);
                }
            }
        }
    }
    auto indexed = png::decode(png);
    if (indexed && indexed.value().color_type == png::ColorType::Indexed) {
        const png::Image& im = indexed.value();
        out.width = static_cast<int>(im.width);
        out.height = static_cast<int>(im.height);
        out.rgba.resize(static_cast<std::size_t>(im.width) * im.height * 4);
        for (std::size_t i = 0; i < static_cast<std::size_t>(im.width) * im.height; ++i) {
            const std::uint8_t idx = im.pixels[i];
            const std::size_t pe = static_cast<std::size_t>(idx) * 3;
            std::uint8_t* o = &out.rgba[i * 4];
            if (pe + 2 < im.palette.size()) {
                o[0] = im.palette[pe];
                o[1] = im.palette[pe + 1];
                o[2] = im.palette[pe + 2];
            }
            const std::uint8_t a = idx < im.palette_alpha.size() ? im.palette_alpha[idx] : 255;
            o[3] = a == 0 ? 0 : ((idx < stp.size() && stp[idx]) ? 128 : 255);
        }
    } else {
        auto rgba = png::decode_rgba8(png);
        if (!rgba) return R::fail(rgba.error());
        out.width = static_cast<int>(rgba.value().width);
        out.height = static_cast<int>(rgba.value().height);
        out.rgba = std::move(rgba.value().pixels);
        // Baked pictures carry three alpha classes (0 cut-out, 128 STP texel, 255 solid); snap filtered or edited
        // values back to the nearest class so the semi-transparent split matches the indexed path.
        for (std::size_t i = 3; i < out.rgba.size(); i += 4)
            out.rgba[i] = out.rgba[i] < 64 ? 0 : (out.rgba[i] < 192 ? 128 : 255);
    }
    // Transparent texels take the colour of a solid neighbour, so linear filtering does not pull black into the
    // edges of cut-outs (the PS1 path weights its filter taps by coverage for the same reason).
    std::vector<std::uint8_t> src = out.rgba;
    const int w = out.width, h = out.height;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            std::uint8_t* o = &out.rgba[(static_cast<std::size_t>(y) * w + x) * 4];
            if (o[3] != 0) continue;
            int sum[3] = {0, 0, 0}, n = 0;
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx) {
                    const int sx = x + dx, sy = y + dy;
                    if (sx < 0 || sy < 0 || sx >= w || sy >= h) continue;
                    const std::uint8_t* s = &src[(static_cast<std::size_t>(sy) * w + sx) * 4];
                    if (s[3] == 0) continue;
                    sum[0] += s[0];
                    sum[1] += s[1];
                    sum[2] += s[2];
                    ++n;
                }
            if (n > 0)
                for (int c = 0; c < 3; ++c) o[c] = static_cast<std::uint8_t>(sum[c] / n);
        }
    }
    return R::ok(std::move(out));
}

}  // namespace sh::scene
