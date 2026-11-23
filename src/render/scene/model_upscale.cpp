#include "render/scene/model_upscale.h"

#include "render/scene/scene_shaders.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string_view>
#include <unordered_map>

namespace sh::scene {
namespace {

using namespace sh::rhi;

std::atomic<int> g_active{0};

// Normals this close count as one: the converter writes a shared normal once per vertex, so a split is a real one.
constexpr float kSameNormal = 0.9995f;

// std140 ScenePn of scene_pn_data.glsl and scene_pn.tesc.
struct PnBlock {
    std::array<float, 4> level{};
    std::array<std::uint32_t, 4> draw{};
};
static_assert(sizeof(PnBlock) == 32);

constexpr std::string_view kVsBlocks[] = {"SceneVs", "WorldFog", "ScenePn"};
// The nearest view depth an edge's size on screen is taken at (scene_pn.tesc): an edge reaching behind the eye.
constexpr float kAdaptiveNearZ = 16.0f;
constexpr std::uint32_t kMeshTasks = 32;  // triangles per task workgroup (scene_pn_data.glsl kPnTasks)

void log_once(const std::string& what) {
    static std::string last;
    if (what == last) return;
    last = what;
    std::fprintf(stderr, "[SCENE] model upscaling: %s\n", what.c_str());
}

}  // namespace

UpscaleMode upscale_mode_for(int setting, const Caps& caps) {
    if (setting >= 2 && caps.meshShaders && caps.storageBuffers) return UpscaleMode::MeshShaders;
    if (setting >= 1 && caps.tessellation && caps.storageBuffers) return UpscaleMode::Tessellation;
    return UpscaleMode::Off;
}

std::string upscale_missing(UpscaleMode mode, Backend backend, const Caps& caps) {
    if (mode == UpscaleMode::Off) return "";
    if (backend != Backend::Vulkan) return "only the native Vulkan renderer (renderer = vulkan) upscales models";
    if (!caps.tessellation || !caps.storageBuffers) return "the GPU has no tessellation shaders";
    if (mode == UpscaleMode::MeshShaders && !caps.meshShaders)
        return caps.meshShadersMissing.empty() ? std::string("the GPU has no mesh shaders") : caps.meshShadersMissing;
    return "";
}

UpscaleMode upscale_active() { return static_cast<UpscaleMode>(g_active.load(std::memory_order_relaxed)); }

void set_upscale_active(UpscaleMode mode) { g_active.store(static_cast<int>(mode), std::memory_order_relaxed); }

PnWeld pn_weld(std::span<const std::string> keys, std::span<const std::array<float, 3>> normals) {
    PnWeld w;
    const std::size_t n = keys.size();
    w.rep.resize(n);
    w.crease.assign(n, 0);
    std::unordered_map<std::string_view, std::uint32_t> first;
    first.reserve(n);
    for (std::size_t v = 0; v < n; ++v) {
        const auto [it, added] = first.try_emplace(keys[v], static_cast<std::uint32_t>(v));
        w.rep[v] = it->second;
        if (added || v >= normals.size() || it->second >= normals.size()) continue;
        const auto& a = normals[v];
        const auto& b = normals[it->second];
        if (a[0] * b[0] + a[1] * b[1] + a[2] * b[2] < kSameNormal) w.crease[it->second] = 1;
    }
    for (std::size_t v = 0; v < n; ++v) w.crease[v] = w.crease[w.rep[v]];
    return w;
}

void pn_normals(const PnWeld& weld, std::span<const std::array<float, 3>> normals, std::span<const float> xyz,
                std::span<const std::uint32_t> indices, std::vector<std::array<float, 4>>& out, bool geometric) {
    const std::size_t n = weld.rep.size();
    std::vector<std::array<double, 3>> sum(n, {0.0, 0.0, 0.0});
    auto face_normal = [&](std::size_t t, std::array<double, 3>& f) {
        const std::uint32_t i[3] = {indices[t], indices[t + 1], indices[t + 2]};
        if (i[0] >= n || i[1] >= n || i[2] >= n || std::size_t{std::max({i[0], i[1], i[2]})} * 3 + 3 > xyz.size())
            return false;
        double e1[3], e2[3];
        for (std::size_t k = 0; k < 3; ++k) {
            e1[k] = double(xyz[i[1] * 3 + k]) - xyz[i[0] * 3 + k];
            e2[k] = double(xyz[i[2] * 3 + k]) - xyz[i[0] * 3 + k];
        }
        f = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]};
        return f[0] * f[0] + f[1] * f[1] + f[2] * f[2] > 1e-18;
    };
    if (geometric) {
        // Area-weighted face normals, each turned to the side its corners' file normals point to (the file's normals
        // only pick the side: on props they are often set for the light, not for the shape).
        for (std::size_t t = 0; t + 2 < indices.size(); t += 3) {
            std::array<double, 3> f;
            if (!face_normal(t, f)) continue;
            double side = 0;
            for (std::size_t c = 0; c < 3; ++c)
                if (indices[t + c] < normals.size())
                    for (std::size_t k = 0; k < 3; ++k) side += f[k] * normals[indices[t + c]][k];
            const double sign = side < 0 ? -1.0 : 1.0;
            for (std::size_t c = 0; c < 3; ++c)
                for (std::size_t k = 0; k < 3; ++k) sum[weld.rep[indices[t + c]]][k] += sign * f[k];
        }
    } else {
        for (std::size_t v = 0; v < n && v < normals.size(); ++v) {
            auto& s = sum[weld.rep[v]];
            for (std::size_t k = 0; k < 3; ++k) s[k] += normals[v][k];
        }
    }
    out.resize(n);
    for (std::size_t v = 0; v < n; ++v) {
        const auto& s = sum[weld.rep[v]];
        const double len = std::sqrt(s[0] * s[0] + s[1] * s[1] + s[2] * s[2]);
        // Opposite normals at one point (a two-sided sheet): no direction to bend along, so its edges stay straight.
        if (len < 1e-6) {
            out[v] = {0.0f, 0.0f, 1.0f, 1.0f};
            continue;
        }
        out[v] = {static_cast<float>(s[0] / len), static_cast<float>(s[1] / len), static_cast<float>(s[2] / len),
                  static_cast<float>(weld.crease[v])};
    }
    // Folds: the face's own normal against the mean of each corner's position (either winding: two-sided sheets).
    std::vector<std::uint8_t> fold(n, 0);
    for (std::size_t t = 0; t + 2 < indices.size(); t += 3) {
        std::array<double, 3> f;
        if (!face_normal(t, f)) continue;
        const double flen = std::sqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
        for (std::size_t c = 0; c < 3; ++c) {
            const std::uint32_t i = indices[t + c];
            const auto& m = out[i];
            const double d = std::abs(f[0] * m[0] + f[1] * m[1] + f[2] * m[2]) / flen;
            if (d < kUpscaleFoldCos) fold[weld.rep[i]] = 1;
        }
    }
    for (std::size_t v = 0; v < n; ++v)
        if (fold[weld.rep[v]]) out[v][3] = 1.0f;
}

namespace {

// An edge whose control points lie within this share of its length of the straight ones is kept one piece.
constexpr double kStraightEps = 5e-4;
// A fold between kUpscaleSmoothCos and this (cos 75 degrees) is smooth too where both faces are long strips along it,
// narrower than kStripShare of its length and than kStripWidth: the sides of a barrel, a pillar or a tree trunk, not
// the chamfer around a flat panel or a roof's ridge.
constexpr double kStripFoldCos = 0.2588;
constexpr double kStripShare = 0.75;
constexpr double kStripWidth = 192.0;  // view units (0.75 m)
// A corner whose smooth normal leans more than this from a face of the edge (cos 50 degrees) is a tip (a cone's
// apex): the edge takes the mean of its own two faces there, so the tip stays sharp.
constexpr double kTipCos = 0.643;
// A patch whose control points leave its plane by more than this bends (view units); a decal on it pins it.
constexpr double kBendReach = 0.25;
constexpr double kDecalReach = 4.0;  // view units: the overlay's own lift and the importer's rounding

using V3 = std::array<double, 3>;
V3 sub(const V3& a, const V3& b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
double dot(const V3& a, const V3& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
V3 cross(const V3& a, const V3& b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
V3 scale(const V3& a, double s) { return {a[0] * s, a[1] * s, a[2] * s}; }

struct PosKey {
    std::uint32_t b[3];
    bool operator==(const PosKey& o) const { return b[0] == o.b[0] && b[1] == o.b[1] && b[2] == o.b[2]; }
};
struct PosHash {
    std::size_t operator()(const PosKey& k) const {
        return (std::size_t{k.b[0]} * 0x9E3779B97F4A7C15ull) ^ (std::size_t{k.b[1]} * 0xC2B2AE3D27D4EB4Full) ^
               (std::size_t{k.b[2]} * 0x165667B19E3779F9ull);
    }
};

std::uint32_t uf_find(std::vector<std::uint32_t>& parent, std::uint32_t x) {
    while (parent[x] != x) {
        parent[x] = parent[parent[x]];
        x = parent[x];
    }
    return x;
}

std::array<float, 4> to_float(const V3& v, float w) {
    return {static_cast<float>(v[0]), static_cast<float>(v[1]), static_cast<float>(v[2]), w};
}

}  // namespace

PnSurface pn_surface(std::span<const float> xyz, std::span<const std::uint32_t> indices,
                     std::span<const std::uint8_t> flags) {
    PnSurface out;
    const std::size_t tris = indices.size() / 3;
    const std::size_t verts = xyz.size() / 3;
    out.curved.assign(tris, 0);
    out.corners.assign(tris * 6, {0.0f, 0.0f, 0.0f, 0.0f});
    // Positions welded by their bits; -0 and 0 are one point.
    std::vector<std::uint32_t> pos(verts);
    {
        std::unordered_map<PosKey, std::uint32_t, PosHash> ids;
        ids.reserve(verts);
        for (std::size_t v = 0; v < verts; ++v) {
            PosKey k;
            for (int c = 0; c < 3; ++c) {
                float f = xyz[v * 3 + static_cast<std::size_t>(c)];
                if (f == 0.0f) f = 0.0f;
                std::memcpy(&k.b[c], &f, 4);
            }
            pos[v] = ids.try_emplace(k, static_cast<std::uint32_t>(ids.size())).first->second;
        }
    }
    auto point = [&](std::uint32_t v) -> V3 { return {xyz[v * 3], xyz[v * 3 + 1], xyz[v * 3 + 2]}; };
    auto corner_point = [&](std::size_t t, std::size_t k) { return point(indices[t * 3 + k % 3]); };
    auto corner_pos = [&](std::size_t t, std::size_t k) { return pos[indices[t * 3 + k % 3]]; };

    std::vector<V3> face(tris);
    std::vector<std::array<double, 3>> angle(tris);
    std::vector<double> area2(tris, 0.0);  // twice the area
    std::vector<std::uint8_t> valid(tris, 0);
    for (std::size_t t = 0; t < tris; ++t) {
        const std::uint32_t* ix = &indices[t * 3];
        if (ix[0] >= verts || ix[1] >= verts || ix[2] >= verts) continue;
        const V3 p[3] = {point(ix[0]), point(ix[1]), point(ix[2])};
        const V3 n = cross(sub(p[1], p[0]), sub(p[2], p[0]));
        const double len = std::sqrt(dot(n, n));
        if (len < 1e-9) continue;
        face[t] = scale(n, 1.0 / len);
        area2[t] = len;
        valid[t] = 1;
        for (std::size_t k = 0; k < 3; ++k) {
            const V3 u = sub(p[(k + 1) % 3], p[k]);
            const V3 w = sub(p[(k + 2) % 3], p[k]);
            const double lu = std::sqrt(dot(u, u)), lw = std::sqrt(dot(w, w));
            const double d = lu > 0 && lw > 0 ? dot(u, w) / (lu * lw) : 1.0;
            angle[t][k] = std::acos(std::clamp(d, -1.0, 1.0));
        }
    }
    // Edge k of triangle t runs from its corner k to corner k + 1; edges are keyed by their two welded ends.
    std::unordered_map<std::uint64_t, std::vector<std::uint32_t>> edges;
    edges.reserve(tris * 3);
    for (std::size_t t = 0; t < tris; ++t) {
        if (!valid[t]) continue;
        for (std::size_t k = 0; k < 3; ++k) {
            const std::uint32_t a = corner_pos(t, k), b = corner_pos(t, k + 1);
            const std::uint64_t key = (std::uint64_t{std::min(a, b)} << 32) | std::max(a, b);
            edges[key].push_back(static_cast<std::uint32_t>(t * 3 + k));
        }
    }
    std::vector<std::uint8_t> pinned(tris, 0);
    for (std::size_t t = 0; t < tris && t < flags.size(); ++t) pinned[t] = flags[t] & (kPnPinned | kPnOverlay);

    // Two faces that run one edge in opposite directions: the same side of both is the outside.
    auto opposite = [&](std::uint32_t ea, std::uint32_t eb) {
        const std::size_t ta = ea / 3, tb = eb / 3, ka = ea % 3, kb = eb % 3;
        return ta != tb && corner_pos(ta, ka) == corner_pos(tb, kb + 1) && corner_pos(ta, ka + 1) == corner_pos(tb, kb);
    };
    // A face is a strip along its edge k: narrow across it.
    auto strip = [&](std::size_t t, std::size_t k) {
        const V3 e = sub(corner_point(t, k + 1), corner_point(t, k));
        const double len = std::sqrt(dot(e, e));
        const double width = len > 0 ? area2[t] / len : 0.0;
        return width <= kStripWidth && width <= kStripShare * len;
    };

    std::vector<V3> normal(tris * 3);  // per corner: the mean of its smooth group
    std::vector<std::uint8_t> bends(tris, 0);
    for (int pass = 0; pass < 4; ++pass) {
        std::vector<std::uint32_t> parent(tris * 3);
        for (std::size_t i = 0; i < parent.size(); ++i) parent[i] = static_cast<std::uint32_t>(i);
        std::vector<std::uint8_t> joined(tris * 3, 0);
        for (const auto& [key, list] : edges) {
            if (list.size() != 2 || !opposite(list[0], list[1])) continue;
            const std::uint32_t ea = list[0], eb = list[1];
            const std::size_t ta = ea / 3, tb = eb / 3, ka = ea % 3, kb = eb % 3;
            if (pinned[ta] || pinned[tb]) continue;
            const double c = dot(face[ta], face[tb]);
            const bool strips = c >= kStripFoldCos && strip(ta, ka) && strip(tb, kb);
            if (c < kUpscaleSmoothCos && !strips) continue;
            joined[ea] = joined[eb] = 1;
            auto join = [&](std::size_t x, std::size_t y) {
                const std::uint32_t rx = uf_find(parent, static_cast<std::uint32_t>(x));
                const std::uint32_t ry = uf_find(parent, static_cast<std::uint32_t>(y));
                if (rx != ry) parent[ry] = rx;
            };
            join(ta * 3 + ka, tb * 3 + (kb + 1) % 3);
            join(ta * 3 + (ka + 1) % 3, tb * 3 + kb);
        }
        // Weighed by the corner's angle and the face's squared area: a large face keeps its plane against the small
        // bevels around it, faces of one size (a pillar's sides) share alike.
        std::vector<V3> sum(tris * 3, {0.0, 0.0, 0.0});
        for (std::size_t c = 0; c < tris * 3; ++c) {
            const std::size_t t = c / 3;
            if (!valid[t]) continue;
            V3& s = sum[uf_find(parent, static_cast<std::uint32_t>(c))];
            const double w = angle[t][c % 3] * area2[t] * area2[t];
            for (std::size_t k = 0; k < 3; ++k) s[k] += face[t][k] * w;
        }
        for (std::size_t c = 0; c < tris * 3; ++c) {
            const V3& s = sum[uf_find(parent, static_cast<std::uint32_t>(c))];
            const double len = std::sqrt(dot(s, s));
            normal[c] = len > 1e-30 ? scale(s, 1.0 / len) : face[c / 3];
        }

        // Every edge once: its control points next to either end, written to the corners of all its faces, so the
        // faces of an edge always share its curve.
        std::fill(out.curved.begin(), out.curved.end(), 0);
        for (const auto& [key, list] : edges) {
            const std::uint32_t e0 = list[0];
            const std::size_t t0 = e0 / 3, k0 = e0 % 3;
            const V3 p = corner_point(t0, k0), q = corner_point(t0, k0 + 1);
            const V3 e = sub(q, p);
            const double len = std::sqrt(dot(e, e));
            V3 off_p = scale(e, 1.0 / 3.0), off_q = scale(e, -1.0 / 3.0);
            const bool pair = list.size() == 2 && opposite(list[0], list[1]) && !pinned[t0] && !pinned[list[1] / 3];
            if (pair && joined[e0]) {
                // A smooth edge: the point-normal construction with the group's normal at either end.
                const std::size_t t1 = list[1] / 3;
                V3 own = {face[t0][0] + face[t1][0], face[t0][1] + face[t1][1], face[t0][2] + face[t1][2]};
                own = scale(own, 1.0 / std::sqrt(std::max(dot(own, own), 1e-30)));
                auto end_normal = [&](const V3& n) {
                    return dot(n, face[t0]) < kTipCos || dot(n, face[t1]) < kTipCos ? own : n;
                };
                const V3 np = end_normal(normal[t0 * 3 + k0]);
                const V3 nq = end_normal(normal[t0 * 3 + (k0 + 1) % 3]);
                off_p = scale(sub(e, scale(np, dot(e, np))), 1.0 / 3.0);
                off_q = scale(sub(scale(e, -1.0), scale(nq, -dot(e, nq))), 1.0 / 3.0);
            } else if (pair) {
                // A hard edge: at either end the curve runs along both faces' tangent planes, so a pillar's rim
                // rounds with its sides while its cap stays in its plane.
                const std::size_t t1 = list[1] / 3, k1 = list[1] % 3;
                auto tangent_end = [&](const V3& na, const V3& nb, const V3& d, V3& off) {
                    V3 t = cross(na, nb);
                    const double tl = std::sqrt(dot(t, t));
                    if (tl < 1e-6) return;
                    t = scale(t, 1.0 / tl);
                    const double along = dot(d, t);
                    // A tangent far off the edge would fold the curve back on itself.
                    if (std::abs(along) < 0.5 * len) return;
                    off = scale(t, along / 3.0);
                };
                tangent_end(normal[t0 * 3 + k0], normal[t1 * 3 + (k1 + 1) % 3], e, off_p);
                tangent_end(normal[t0 * 3 + (k0 + 1) % 3], normal[t1 * 3 + k1], scale(e, -1.0), off_q);
            }
            const V3 dp = sub(off_p, scale(e, 1.0 / 3.0));
            const V3 dq = sub(off_q, scale(e, -1.0 / 3.0));
            const double tol = kStraightEps * len;
            const bool straight = dot(dp, dp) <= tol * tol && dot(dq, dq) <= tol * tol;
            if (straight) {
                off_p = scale(e, 1.0 / 3.0);
                off_q = scale(e, -1.0 / 3.0);
            }
            for (const std::uint32_t entry : list) {
                const std::size_t t = entry / 3, k = entry % 3;
                const bool forward = corner_pos(t, k) == corner_pos(t0, k0);  // the face runs it from p to q
                out.corners[(t * 3 + k) * 2] = to_float(forward ? off_p : off_q, straight ? 2.0f : 0.0f);
                out.corners[(t * 3 + (k + 1) % 3) * 2 + 1] = to_float(forward ? off_q : off_p, 0.0f);
                if (!straight) out.curved[t] = 1;
            }
        }
        // A degenerate triangle is in no edge list: straight control points.
        for (std::size_t t = 0; t < tris; ++t) {
            if (valid[t]) continue;
            for (std::size_t k = 0; k < 3; ++k) {
                const V3 e = sub(corner_point(t, k + 1), corner_point(t, k));
                out.corners[(t * 3 + k) * 2] = to_float(scale(e, 1.0 / 3.0), 2.0f);
                out.corners[(t * 3 + (k + 1) % 3) * 2 + 1] = to_float(scale(e, -1.0 / 3.0), 0.0f);
            }
        }
        for (std::size_t t = 0; t < tris; ++t) {
            bends[t] = 0;
            if (!out.curved[t]) continue;
            for (std::size_t c = 0; c < 6; ++c) {
                const auto& o = out.corners[t * 6 + c];
                if (std::abs(o[0] * face[t][0] + o[1] * face[t][1] + o[2] * face[t][2]) > kBendReach) bends[t] = 1;
            }
        }

        // A decal lying on a bending patch would sink under its bend: such a patch is pinned and the edges made again.
        bool more = false;
        for (std::size_t o = 0; o < tris && o < flags.size(); ++o) {
            if (!(flags[o] & kPnOverlay) || !valid[o]) continue;
            V3 lo{1e30, 1e30, 1e30}, hi{-1e30, -1e30, -1e30};
            for (std::size_t k = 0; k < 3; ++k) {
                const V3 v = corner_point(o, k);
                for (std::size_t c = 0; c < 3; ++c) {
                    lo[c] = std::min(lo[c], v[c]);
                    hi[c] = std::max(hi[c], v[c]);
                }
            }
            for (std::size_t t = 0; t < tris; ++t) {
                if (!bends[t] || pinned[t]) continue;
                bool overlaps = true;
                for (std::size_t c = 0; c < 3 && overlaps; ++c) {
                    double tlo = 1e30, thi = -1e30;
                    for (std::size_t k = 0; k < 3; ++k) {
                        tlo = std::min(tlo, corner_point(t, k)[c]);
                        thi = std::max(thi, corner_point(t, k)[c]);
                    }
                    overlaps = lo[c] <= thi + kDecalReach && hi[c] >= tlo - kDecalReach;
                }
                if (!overlaps) continue;
                const V3 base = corner_point(t, 0);
                bool near_plane = true;
                for (std::size_t k = 0; k < 3; ++k)
                    near_plane = near_plane && std::abs(dot(sub(corner_point(o, k), base), face[t])) <= kDecalReach;
                if (!near_plane) continue;
                pinned[t] = 1;
                more = true;
            }
        }
        if (!more) break;
    }
    out.curved_count = 0;
    for (std::size_t t = 0; t < tris; ++t) out.curved_count += out.curved[t];
    return out;
}

std::vector<int> pn_levels(std::span<const PnModel> models, double h, double px_per_unit, std::uint64_t budget) {
    std::vector<int> level(models.size(), 1);
    std::uint64_t total = 0;
    for (std::size_t i = 0; i < models.size(); ++i) {
        const PnModel& m = models[i];
        if (m.triangles == 0 || m.mean_edge <= 0 || m.nearest_z <= 0 || h <= 0 || px_per_unit <= 0) continue;
        const double px = m.mean_edge * h / m.nearest_z * px_per_unit;
        level[i] = static_cast<int>(
            std::clamp(std::ceil(px / kUpscaleTargetPixels), 1.0, static_cast<double>(kUpscaleMaxLevel)));
        total += std::uint64_t{m.triangles} * static_cast<std::uint64_t>(level[i] * level[i]);
    }
    while (total > budget) {
        std::size_t top = 0;
        for (std::size_t i = 1; i < level.size(); ++i)
            if (level[i] > level[top]) top = i;
        if (level.empty() || level[top] <= 1) break;
        const auto t = std::uint64_t{models[top].triangles};
        total -= t * static_cast<std::uint64_t>(level[top] * level[top]);
        --level[top];
        total += t * static_cast<std::uint64_t>(level[top] * level[top]);
    }
    return level;
}

ModelUpscaler::ModelUpscaler(Device& device) : device_(device) {}

ModelUpscaler::~ModelUpscaler() {
    for (const Cached& c : pipelines_)
        if (c.pipeline) device_.destroy(c.pipeline);
    for (ShaderH s : {vert_, tesc_, tese_, task_, mesh_})
        if (s) device_.destroy(s);
}

UpscaleMode ModelUpscaler::begin_frame(int setting) {
    UpscaleMode want = upscale_mode_for(setting, device_.caps());
    if (want != UpscaleMode::Off && device_.backend() != Backend::Vulkan) want = UpscaleMode::Off;
    auto make = [&](ShaderStage stage, const embedded::Shader& src, std::string_view name, std::uint32_t storage,
                    ShaderH& out) {
        ShaderDesc d;
        d.stage = stage;
        d.glsl = src.glsl;
        d.spirv = src.spirv;
        d.uniformBlockNames = kVsBlocks;
        d.storageBuffers = storage;
        d.name = name;
        auto s = device_.createShader(d);
        if (!s) {
            log_once(std::string(name) + ": " + s.error());
            return false;
        }
        out = s.value();
        return true;
    };
    if (want != UpscaleMode::Off && !tried_tess_) {
        tried_tess_ = true;
        const bool ok = make(ShaderStage::Vertex, shaders::sceneVertPn(), "scene.vert pn", 1, vert_) &&
                        make(ShaderStage::TessControl, shaders::scenePnTesc(), "scene_pn.tesc", 0, tesc_) &&
                        make(ShaderStage::TessEval, shaders::scenePnTese(), "scene_pn.tese", 0, tese_);
        if (!ok) vert_ = tesc_ = tese_ = {};
    }
    if (want == UpscaleMode::MeshShaders && !tried_mesh_) {
        tried_mesh_ = true;
        const bool ok = make(ShaderStage::Task, shaders::scenePnTask(), "scene_pn.task", 3, task_) &&
                        make(ShaderStage::Mesh, shaders::scenePnMesh(), "scene_pn.mesh", 3, mesh_);
        if (!ok) task_ = mesh_ = {};
    }
    if (want == UpscaleMode::MeshShaders && !mesh_) want = UpscaleMode::Tessellation;
    if (want == UpscaleMode::Tessellation && !tese_) want = UpscaleMode::Off;
    mode_ = want;
    set_upscale_active(mode_);
    return mode_;
}

PipelineH ModelUpscaler::pipeline(int slot, const PipelineDesc& base, bool tessellation) {
    if (mode_ == UpscaleMode::Off) return {};
    const UpscaleMode mode = tessellation ? UpscaleMode::Tessellation : mode_;
    if (mode == UpscaleMode::Tessellation && !tese_) return {};
    for (const Cached& c : pipelines_)
        if (c.slot == slot && c.mode == mode) return c.pipeline;
    PipelineDesc p = base;
    if (mode == UpscaleMode::MeshShaders) {
        p.taskShader = task_;
        p.meshShader = mesh_;
        p.name = "scene upscaled (mesh)";
    } else {
        p.vertexShader = vert_;
        p.tessControlShader = tesc_;
        p.tessEvalShader = tese_;
        p.patchControlPoints = 3;
        p.name = "scene upscaled (tessellation)";
    }
    auto made = device_.createPipeline(p);
    if (!made) log_once(std::string(p.name) + ": " + made.error());
    pipelines_.push_back({slot, mode, made ? made.value() : PipelineH{}});
    return pipelines_.back().pipeline;
}

void ModelUpscaler::draw(RenderPass& pass, const Draw& d) {
    if (d.count < 3 || !d.patch_normals) return;
    PnBlock block;
    block.level = {static_cast<float>(std::clamp(d.level, 1, kUpscaleMaxLevel)), 0.0f, 0.0f, 0.0f};
    if (d.adaptive) block.level = {block.level[0], kAdaptiveNearZ, d.px_scale, 1.0f / kUpscaleTargetPixels};
    block.draw = {d.first, d.count / 3, 0, 0};
    pass.pushUniforms(ShaderStage::Vertex, 2, std::as_bytes(std::span(&block, 1)));
    pass.bindStorageBuffer(0, d.patch_normals, 0);
    if (mode_ == UpscaleMode::MeshShaders && !d.adaptive) {
        pass.bindStorageBuffer(1, d.vertices, 0);
        pass.bindStorageBuffer(2, d.indices, 0);
        pass.beginTimedSection("upscale mesh");
        pass.drawMeshTasks((d.count / 3 + kMeshTasks - 1) / kMeshTasks);
    } else {
        pass.beginTimedSection(d.adaptive ? "upscale map" : "upscale tess");
        pass.drawIndexed(d.count, d.first, 0);
    }
    pass.endTimedSection();
}

}  // namespace sh::scene
