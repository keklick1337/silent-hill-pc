#include "framework/sh_test.h"
#include "render/scene/model_upscale.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace sc = sh::scene;

namespace {

std::string key_of(float x, float y, float z) {
    const float p[3] = {x, y, z};
    return std::string(reinterpret_cast<const char*>(p), sizeof p);
}

}  // namespace

SH_TEST(model_upscale_weld_marks_split_normals_as_crease) {
    // Two vertices at one point with one normal (a UV seam), two at another with different normals (a hard edge).
    const std::vector<std::string> keys = {key_of(0, 0, 0), key_of(0, 0, 0), key_of(1, 0, 0), key_of(1, 0, 0)};
    const std::vector<std::array<float, 3>> normals = {{0, 0, 1}, {0, 0, 1}, {0, 0, 1}, {1, 0, 0}};
    const sc::PnWeld w = sc::pn_weld(keys, normals);
    SH_CHECK(w.rep[1] == 0 && w.rep[3] == 2);
    SH_CHECK(w.crease[0] == 0 && w.crease[1] == 0);
    SH_CHECK(w.crease[2] == 1 && w.crease[3] == 1);
}

SH_TEST(model_upscale_patch_normal_is_shared_by_a_welded_position) {
    const std::vector<std::string> keys = {key_of(0, 0, 0), key_of(0, 0, 0), key_of(1, 0, 0)};
    const std::vector<std::array<float, 3>> normals = {{0, 0, 1}, {0, 0.1f, 1}, {0, 0, 1}};
    const std::vector<float> xyz = {0, 0, 0, 0, 0, 0, 1, 0, 0};
    std::vector<std::array<float, 4>> out;
    sc::pn_normals(sc::pn_weld(keys, normals), normals, xyz, {}, out);
    SH_CHECK(out[0] == out[1]);
    SH_CHECK(std::abs(out[2][2] - 1.0f) < 1e-6f);
}

SH_TEST(model_upscale_right_angle_fold_is_a_crease) {
    // Two faces meeting at 90 degrees around the edge (0,0,0)-(0,1,0), one shared normal along the diagonal.
    const std::vector<float> xyz = {0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0, 1};
    std::vector<std::string> keys;
    for (int v = 0; v < 4; ++v) keys.push_back(key_of(xyz[v * 3], xyz[v * 3 + 1], xyz[v * 3 + 2]));
    const float d = std::sqrt(0.5f);
    const std::vector<std::array<float, 3>> normals = {{d, 0, d}, {d, 0, d}, {0, 0, 1}, {1, 0, 0}};
    const std::vector<std::uint32_t> tris = {0, 1, 2, 0, 3, 1};
    std::vector<std::array<float, 4>> out;
    sc::pn_normals(sc::pn_weld(keys, normals), normals, xyz, tris, out, true);
    SH_CHECK(out[0][3] == 1.0f && out[1][3] == 1.0f);
}

SH_TEST(model_upscale_levels_follow_screen_size_and_budget) {
    const std::vector<sc::PnModel> models = {{30.0, 400.0, 100}, {30.0, 4000.0, 100}, {30.0, 50.0, 100}};
    const std::vector<int> free = sc::pn_levels(models, 256.0, 6.0, 1u << 30);
    SH_CHECK(free[0] > free[1]);
    SH_CHECK(free[1] >= 1);
    SH_CHECK(free[2] == sc::kUpscaleMaxLevel);
    const std::vector<int> tight = sc::pn_levels(models, 256.0, 6.0, 3000);
    std::uint64_t total = 0;
    for (std::size_t i = 0; i < models.size(); ++i) total += models[i].triangles * std::uint64_t(tight[i] * tight[i]);
    SH_CHECK(total <= 3000);
}

SH_TEST(model_upscale_mode_follows_device_caps) {
    sh::rhi::Caps caps;
    SH_CHECK(sc::upscale_mode_for(2, caps) == sc::UpscaleMode::Off);
    caps.storageBuffers = caps.tessellation = true;
    SH_CHECK(sc::upscale_mode_for(2, caps) == sc::UpscaleMode::Tessellation);
    caps.meshShaders = true;
    SH_CHECK(sc::upscale_mode_for(2, caps) == sc::UpscaleMode::MeshShaders);
    SH_CHECK(sc::upscale_mode_for(0, caps) == sc::UpscaleMode::Off);
    SH_CHECK(!sc::upscale_missing(sc::UpscaleMode::Tessellation, sh::rhi::Backend::Gl33, caps).empty());
}

namespace {

struct Mesh {
    std::vector<float> xyz;
    std::vector<std::uint32_t> tris;
    std::uint32_t add(float x, float y, float z) {
        xyz.insert(xyz.end(), {x, y, z});
        return static_cast<std::uint32_t>(xyz.size() / 3 - 1);
    }
    void tri(std::uint32_t a, std::uint32_t b, std::uint32_t c) { tris.insert(tris.end(), {a, b, c}); }
};

// An octagonal barrel, closed by two fans, wound alike all round; separate vertices per face as the maps have them.
Mesh barrel() {
    Mesh m;
    const float r = 100.0f, h = 400.0f;
    auto at = [&](int i, float y) {
        const double a = 6.283185307179586 * (i % 8) / 8;
        return std::array<float, 3>{static_cast<float>(r * std::cos(a)), y, static_cast<float>(r * std::sin(a))};
    };
    auto v = [&](const std::array<float, 3>& p) { return m.add(p[0], p[1], p[2]); };
    for (int i = 0; i < 8; ++i) {
        const auto a0 = at(i, 0), a1 = at(i + 1, 0), b0 = at(i, h), b1 = at(i + 1, h);
        m.tri(v(a0), v(a1), v(b1));
        m.tri(v(a0), v(b1), v(b0));
        m.tri(m.add(0, 0, 0), v(a1), v(a0));
        m.tri(m.add(0, h, 0), v(b0), v(b1));
    }
    return m;
}

// Every edge's control points, seen from each face that has the edge, are one point.
bool faces_meet(const Mesh& m, const sc::PnSurface& s) {
    std::map<std::array<float, 6>, std::array<float, 3>> seen;
    for (std::size_t t = 0; t < m.tris.size() / 3; ++t) {
        for (std::size_t k = 0; k < 3; ++k) {
            const std::uint32_t a = m.tris[t * 3 + k], b = m.tris[t * 3 + (k + 1) % 3];
            const auto& off_a = s.corners[(t * 3 + k) * 2];
            const auto& off_b = s.corners[(t * 3 + (k + 1) % 3) * 2 + 1];
            const std::array<float, 3> pa = {m.xyz[a * 3], m.xyz[a * 3 + 1], m.xyz[a * 3 + 2]};
            const std::array<float, 3> pb = {m.xyz[b * 3], m.xyz[b * 3 + 1], m.xyz[b * 3 + 2]};
            const std::pair<std::array<float, 6>, std::array<float, 3>> ends[2] = {
                {{pa[0], pa[1], pa[2], pb[0], pb[1], pb[2]}, {pa[0] + off_a[0], pa[1] + off_a[1], pa[2] + off_a[2]}},
                {{pb[0], pb[1], pb[2], pa[0], pa[1], pa[2]}, {pb[0] + off_b[0], pb[1] + off_b[1], pb[2] + off_b[2]}}};
            for (const auto& [key, point] : ends) {
                auto [it, added] = seen.try_emplace(key, point);
                if (!added && it->second != point) return false;
            }
        }
    }
    return true;
}

}  // namespace

SH_TEST(model_upscale_map_barrel_rounds_and_its_caps_stay_flat) {
    const Mesh m = barrel();
    const sc::PnSurface s = sc::pn_surface(m.xyz, m.tris, {});
    SH_CHECK(s.curved_count == m.tris.size() / 3);
    SH_CHECK(faces_meet(m, s));
    // A cap's control points stay in its plane; a side's rim bulges outwards.
    for (std::size_t t = 0; t < 8; ++t) {
        for (std::size_t c = 0; c < 6; ++c) {
            SH_CHECK(std::abs(s.corners[(t * 4 + 2) * 6 + c][1]) < 1e-3f);
            SH_CHECK(std::abs(s.corners[(t * 4 + 3) * 6 + c][1]) < 1e-3f);
        }
    }
    const auto& rim = s.corners[0];  // side 0, corner a0, towards a1
    const float mid_x = 100.0f + rim[0], mid_z = rim[2];
    SH_CHECK(mid_x * mid_x + mid_z * mid_z > 100.0f * 100.0f);
}

SH_TEST(model_upscale_map_box_and_plane_stay_plain) {
    Mesh plane;
    for (int z = 0; z < 4; ++z)
        for (int x = 0; x < 4; ++x) {
            const float fx = x * 256.0f, fz = z * 256.0f;
            const std::uint32_t a = plane.add(fx, 0, fz), b = plane.add(fx + 256, 0, fz),
                                c = plane.add(fx + 256, 0, fz + 256), d = plane.add(fx, 0, fz + 256);
            plane.tri(a, b, c);
            plane.tri(a, c, d);
        }
    SH_CHECK(sc::pn_surface(plane.xyz, plane.tris, {}).curved_count == 0);

    Mesh box;
    const float p[8][3] = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}, {0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}};
    const int faces[6][4] = {{0, 3, 2, 1}, {4, 5, 6, 7}, {0, 1, 5, 4}, {2, 3, 7, 6}, {1, 2, 6, 5}, {0, 4, 7, 3}};
    for (const auto& f : faces) {
        std::uint32_t q[4];
        for (int k = 0; k < 4; ++k) q[k] = box.add(p[f[k]][0] * 300, p[f[k]][1] * 300, p[f[k]][2] * 300);
        box.tri(q[0], q[1], q[2]);
        box.tri(q[0], q[2], q[3]);
    }
    SH_CHECK(sc::pn_surface(box.xyz, box.tris, {}).curved_count == 0);
}

SH_TEST(model_upscale_map_pinned_triangles_stay_plain) {
    const Mesh m = barrel();
    const std::vector<std::uint8_t> flags(m.tris.size() / 3, sc::kPnPinned);
    SH_CHECK(sc::pn_surface(m.xyz, m.tris, flags).curved_count == 0);
}
