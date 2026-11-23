#include "render/scene/surface_query.h"

#include "render/scene/area_cells.h"
#include "render/scene/scene.h"

#include <algorithm>
#include <cmath>

namespace sh::scene {
namespace {

constexpr double kCellUnits = 40.0 * 256.0;

SurfaceVec sub(const SurfaceVec& a, const SurfaceVec& b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
double dot(const SurfaceVec& a, const SurfaceVec& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
SurfaceVec cross(const SurfaceVec& a, const SurfaceVec& b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}

// A mesh of the world with the transform of its own space: world = rot * local + trans.
struct Placed {
    const CellMesh* mesh = nullptr;
    std::array<double, 9> rot{1, 0, 0, 0, 1, 0, 0, 0, 1};
    SurfaceVec trans{};

    SurfaceVec to_local(const SurfaceVec& w) const {
        const SurfaceVec d = sub(w, trans);
        return {rot[0] * d[0] + rot[3] * d[1] + rot[6] * d[2], rot[1] * d[0] + rot[4] * d[1] + rot[7] * d[2],
                rot[2] * d[0] + rot[5] * d[1] + rot[8] * d[2]};
    }
    SurfaceVec to_world(const SurfaceVec& l) const {
        return {rot[0] * l[0] + rot[1] * l[1] + rot[2] * l[2] + trans[0],
                rot[3] * l[0] + rot[4] * l[1] + rot[5] * l[2] + trans[1],
                rot[6] * l[0] + rot[7] * l[1] + rot[8] * l[2] + trans[2]};
    }
};

std::vector<Placed> placed_meshes(std::vector<std::shared_ptr<const CellMesh>>& keep) {
    std::vector<Placed> out;
    for (const CellRef& ref : placed_cells()) {
        std::shared_ptr<const CellMesh> mesh = find_cell(ref.stem);
        if (!mesh) continue;
        Placed p;
        p.mesh = mesh.get();
        p.trans = {ref.cell_x * kCellUnits, 0.0, ref.cell_z * kCellUnits};
        out.push_back(p);
        keep.push_back(std::move(mesh));
    }
    for (const ObjectRef& o : placed_objects()) {
        if (!o.mesh) continue;
        Placed p;
        p.mesh = o.mesh.get();
        p.rot = o.rot;
        p.trans = o.trans;
        out.push_back(p);
    }
    return out;
}

SurfaceVec position(const CellVertex& v) { return {v.x, v.y, v.z}; }

// Whether the material shows a solid texel at (u, v).
bool solid_texel(const CellMaterial& m, double u, double v) {
    if (m.texture.empty()) return true;
    const std::shared_ptr<const SceneTexture> tex = find_texture(m.texture, m.palette);
    if (!tex || tex->width <= 0 || tex->height <= 0) return true;
    const int x = std::clamp(static_cast<int>(std::floor(u * tex->width)), 0, tex->width - 1);
    const int y = std::clamp(static_cast<int>(std::floor(v * tex->height)), 0, tex->height - 1);
    const std::uint8_t alpha = tex->rgba[(static_cast<std::size_t>(y) * tex->width + x) * 4 + 3];
    if (alpha == 0) return false;
    return !(m.semi_transparent && alpha < 192);
}

}  // namespace

std::optional<SurfaceHit> solid_surface_on_segment(const SurfaceVec& from, const SurfaceVec& to, double patch_radius) {
    std::vector<std::shared_ptr<const CellMesh>> keep;
    const std::vector<Placed> meshes = placed_meshes(keep);

    double best = 2.0;
    SurfaceHit hit;
    for (const Placed& placed : meshes) {
        const CellMesh& mesh = *placed.mesh;
        const SurfaceVec o = placed.to_local(from);
        const SurfaceVec dir = sub(placed.to_local(to), o);
        for (const CellRange& r : mesh.ranges) {
            const CellMaterial& material = mesh.materials[static_cast<std::size_t>(r.material)];
            for (std::uint32_t t = r.first; t + 2 < r.first + r.count; t += 3) {
                const CellVertex& va = mesh.vertices[mesh.indices[t]];
                const CellVertex& vb = mesh.vertices[mesh.indices[t + 1]];
                const CellVertex& vc = mesh.vertices[mesh.indices[t + 2]];
                const SurfaceVec a = position(va);
                const SurfaceVec e1 = sub(position(vb), a);
                const SurfaceVec e2 = sub(position(vc), a);
                const SurfaceVec p = cross(dir, e2);
                const double det = dot(e1, p);
                if (std::abs(det) < 1e-9) continue;
                const SurfaceVec s = sub(o, a);
                const double bu = dot(s, p) / det;
                if (bu < 0.0 || bu > 1.0) continue;
                const SurfaceVec q = cross(s, e1);
                const double bv = dot(dir, q) / det;
                if (bv < 0.0 || bu + bv > 1.0) continue;
                const double along = dot(e2, q) / det;
                if (along < 0.0 || along > 1.0 || along >= best) continue;
                const double w = 1.0 - bu - bv;
                if (!solid_texel(material, w * va.u + bu * vb.u + bv * vc.u, w * va.v + bu * vb.v + bv * vc.v)) continue;
                best = along;
                SurfaceVec n = cross(e1, e2);
                const double len = std::sqrt(dot(n, n));
                if (len < 1e-9) continue;
                for (double& c : n) c /= len;
                if (dot(n, dir) > 0.0)
                    for (double& c : n) c = -c;
                // Rotations only: the local normal turns like a direction.
                const SurfaceVec zero = placed.to_world({0, 0, 0});
                hit.normal = sub(placed.to_world(n), zero);
                hit.point = {from[0] + (to[0] - from[0]) * along, from[1] + (to[1] - from[1]) * along,
                             from[2] + (to[2] - from[2]) * along};
            }
        }
    }
    if (best > 1.0) return std::nullopt;

    constexpr double kSamePlane = 1.0;
    constexpr std::size_t kMaxPatch = 96;
    for (const Placed& placed : meshes) {
        const CellMesh& mesh = *placed.mesh;
        const SurfaceVec centre = placed.to_local(hit.point);
        const SurfaceVec zero = placed.to_local({0, 0, 0});
        const SurfaceVec n = sub(placed.to_local(hit.normal), zero);
        for (const CellRange& r : mesh.ranges) {
            for (std::uint32_t t = r.first; t + 2 < r.first + r.count && hit.patch.size() < kMaxPatch; t += 3) {
                std::array<SurfaceVec, 3> tri;
                bool on_plane = true;
                SurfaceVec lo{1e30, 1e30, 1e30}, hi{-1e30, -1e30, -1e30};
                for (std::size_t k = 0; k < 3 && on_plane; ++k) {
                    tri[k] = position(mesh.vertices[mesh.indices[t + k]]);
                    on_plane = std::abs(dot(sub(tri[k], centre), n)) <= kSamePlane;
                    for (std::size_t c = 0; c < 3; ++c) {
                        lo[c] = std::min(lo[c], tri[k][c]);
                        hi[c] = std::max(hi[c], tri[k][c]);
                    }
                }
                if (!on_plane) continue;
                bool near = true;
                for (std::size_t c = 0; c < 3; ++c)
                    near = near && lo[c] <= centre[c] + patch_radius && hi[c] >= centre[c] - patch_radius;
                if (!near) continue;
                for (SurfaceVec& v : tri) v = placed.to_world(v);
                hit.patch.push_back(tri);
            }
        }
    }
    return hit;
}

}  // namespace sh::scene
