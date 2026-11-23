// sh::gpu::precision: per-kind depth, near-plane clipping, weld, the PS1 size rule and the lighting side channel.

#include "framework/sh_test.h"
#include "render/gpu/vertex_precision.h"

#include <cmath>
#include <cstdint>

namespace {

namespace pr = sh::gpu::precision;
using sh::gpu::Vertex;

// Fresh state with a given SZ reference (0 = none).
void reset(std::uint32_t sz_reference) {
    pr::settings() = {};
    pr::counters() = {};
    pr::begin_polygon(false, 0.f, 0.f);
    pr::roll_sz_max();
    pr::note_sz(sz_reference);
    pr::roll_sz_max();
}

pr::DepthIn triangle_depth(pr::DepthKind kind, std::uint32_t a, std::uint32_t b, std::uint32_t c) {
    pr::DepthIn in;
    in.found = true;
    in.kind = kind;
    in.sz[1] = a;
    in.sz[2] = b;
    in.sz[3] = c;
    return in;
}

Vertex view_vertex(float vsx, float vsy, float vsz, std::uint8_t u, float ppw) {
    Vertex v;
    v.vsx = vsx;
    v.vsy = vsy;
    v.vsz = vsz;
    v.ny = 1.f;
    v.u = u;
    v.ppx = vsx;
    v.ppy = vsy;
    v.ppw = ppw;
    return v;
}

}  // namespace

SH_TEST(gpu_precision_sz_normaliser) {
    reset(0);
    SH_CHECK(pr::item_sz_max() == 1.0f);
    pr::note_sz(500);
    pr::note_sz(300);
    SH_CHECK(pr::sz_max_prev() == 0);
    pr::begin_frame();
    SH_CHECK(pr::sz_max_prev() == 500);
    SH_CHECK(pr::item_sz_max() == 500.0f);
    // The scene's scale is the constant one with either vertex path.
    SH_CHECK(pr::sz_max() == pr::kPgxpDepthSzMax);
    pr::settings().pgxp = true;
    SH_CHECK(pr::sz_max() == pr::kPgxpDepthSzMax);
    SH_CHECK(pr::ndc_from_view_z(0.f) == 1.0f);
    SH_CHECK(pr::ndc_from_view_z(1e9f) == -1.0f);
}

SH_TEST(gpu_precision_depth_legacy_flat) {
    // The integer path is on the scene's constant scale, whatever last frame's reference was.
    reset(1000);
    Vertex v[3];
    const std::uint8_t a = pr::apply_depth(v, triangle_depth(pr::DepthKind::Exact, 100, 200, 300), false);
    SH_CHECK(a == 255);
    const float z = pr::ndc_from_view_z((100.f + 200.f + 300.f) * (1.0f / 3.0f));
    for (const Vertex& c : v) {
        SH_CHECK(c.z == z);
        SH_CHECK(c.depthMark == 0);
    }

    // A depth the projection only left behind keeps the bucket depth.
    reset(0);
    Vertex w[3];
    w[0].z = w[1].z = w[2].z = 0.25f;
    pr::apply_depth(w, triangle_depth(pr::DepthKind::None, 100, 200, 300), false);
    SH_CHECK(w[0].z == 0.25f);

    // Not found: untouched, opaque.
    reset(1000);
    pr::DepthIn none;
    none.alpha = 7;
    SH_CHECK(pr::apply_depth(w, none, false) == 255);
    SH_CHECK(w[0].z == 0.25f);
}

SH_TEST(gpu_precision_depth_world) {
    reset(0);
    pr::settings().pgxp = true;
    Vertex q[4];
    pr::DepthIn in;
    in.found = true;
    in.kind = pr::DepthKind::Flat;
    in.alpha = 128;
    in.sz[0] = 1000;
    in.sz[1] = 2000;
    in.sz[2] = 3000;
    in.sz[3] = 4000;
    SH_CHECK(pr::apply_depth(q, in, true) == 128);
    const float z = pr::ndc_from_view_z((1000.f + 2000.f + 4000.f + 3000.f) * 0.25f + 64.f);
    for (const Vertex& c : q) {
        SH_CHECK(c.z == z);
        SH_CHECK(c.depthMark == 1);
    }

    // Translucent world keeps the flat depth without the per-vertex mark; debug bit 4 forces opaque.
    Vertex t[4];
    in.semi = true;
    pr::settings().debug = 4;
    SH_CHECK(pr::apply_depth(t, in, true) == 255);
    SH_CHECK(t[0].z == z && t[0].depthMark == 0);

    // Auto-captured FIFO depth keeps the bucket seed; so does a disabled world depth.
    Vertex u[3];
    u[0].z = u[1].z = u[2].z = 0.5f;
    pr::apply_depth(u, triangle_depth(pr::DepthKind::None, 100, 200, 300), false);
    SH_CHECK(u[0].z == 0.5f);
    pr::settings().world_depth = false;
    pr::apply_depth(u, triangle_depth(pr::DepthKind::Flat, 100, 200, 300), false);
    SH_CHECK(u[0].z == 0.5f);

    // Exact: same scale, no far bias.
    pr::apply_depth(u, triangle_depth(pr::DepthKind::Exact, 100, 200, 300), false);
    SH_CHECK(u[1].z == pr::ndc_from_view_z((100.f + 200.f + 300.f) * (1.0f / 3.0f)));
    SH_CHECK(u[1].depthMark == 0);
}

SH_TEST(gpu_precision_depth_actor) {
    reset(4000);
    pr::settings().pgxp = true;
    Vertex q[4];
    pr::DepthIn in;
    in.found = true;
    in.kind = pr::DepthKind::Actor;
    in.sz[0] = 1000;
    in.sz[1] = 2000;
    in.sz[2] = 3000;
    in.sz[3] = 4000;
    pr::apply_depth(q, in, true);
    // Stream corners 2 and 3 are hardware corners 3 and 2.
    SH_CHECK(q[0].z == pr::ndc_from_view_z(1000.f));
    SH_CHECK(q[2].z == pr::ndc_from_view_z(4000.f));
    SH_CHECK(q[3].z == pr::ndc_from_view_z(3000.f));
    SH_CHECK(q[1].depthMark == 2);

    // PGXP off: the same scale, so a character orders against the scene's world the same way.
    pr::settings().pgxp = false;
    Vertex t[3];
    pr::apply_depth(t, triangle_depth(pr::DepthKind::Actor, 1000, 2000, 3000), false);
    SH_CHECK(t[1].z == pr::ndc_from_view_z(2000.f));
}

SH_TEST(gpu_precision_depth_item) {
    reset(800);
    pr::settings().force_item_depth = true;
    pr::settings().pgxp = true;
    Vertex t[3];
    pr::apply_depth(t, triangle_depth(pr::DepthKind::Exact, 100, 400, 2000), false);
    const float inv = 2.0f / 800.f;
    SH_CHECK(t[0].z == 1.0f - 100.f * inv);
    SH_CHECK(t[1].z == 1.0f - 400.f * inv);
    SH_CHECK(t[2].z == -1.0f);

    // Non-exact in the item pass: flat self-normalised average.
    Vertex f[3];
    pr::apply_depth(f, triangle_depth(pr::DepthKind::Flat, 100, 200, 300), false);
    SH_CHECK(f[0].z == 1.0f - 2.0f * ((100.f + 200.f + 300.f) * (1.0f / 3.0f)) * (1.0f / 800.f));
    SH_CHECK(pr::counters().item_non_exact == 1);
}

SH_TEST(gpu_precision_near_clip_one_behind) {
    reset(0);
    pr::settings().pgxp = true;
    pr::settings().near_z = 16.f;
    pr::set_polygon_projection(160.f, 120.f, 256.f);
    Vertex v[12];
    v[0] = view_vertex(0.f, 0.f, 100.f, 0, 100.f);
    v[1] = view_vertex(10.f, 0.f, 100.f, 0, 100.f);
    v[2] = view_vertex(0.f, 10.f, -100.f, 200, 0.f);
    SH_CHECK(pr::near_clip_eligible(v, 3));
    const int n = pr::emit_polygon(v, 3);
    SH_CHECK(n == 6);
    SH_CHECK(pr::counters().clipped == 1);
    int on_plane = 0;
    for (int i = 0; i < n; i++) {
        SH_CHECK(v[i].vsz >= 16.f);
        SH_CHECK(v[i].ppw > 0.f);
        if (v[i].vsz == 16.f) {
            on_plane++;
            SH_CHECK(v[i].u == 84);  // 200 * 0.42 (edge 1-2) and 200 - 200 * 0.58 (edge 2-0), rounded
            SH_CHECK(v[i].ppy == 120.f + v[i].vsy * (256.f / 16.f) + 0.f);
        }
    }
    SH_CHECK(on_plane == 3);  // the two clip vertices, the first of them twice in the fan
}

SH_TEST(gpu_precision_near_clip_two_behind) {
    reset(0);
    pr::settings().pgxp = true;
    pr::set_polygon_projection(0.f, 0.f, 256.f);
    Vertex v[12];
    v[0] = view_vertex(0.f, 0.f, 100.f, 0, 100.f);
    v[1] = view_vertex(10.f, 0.f, -50.f, 0, 0.f);
    v[2] = view_vertex(0.f, 10.f, -50.f, 0, 0.f);
    SH_CHECK(pr::emit_polygon(v, 3) == 3);
    SH_CHECK(v[1].vsz == 16.f && v[2].vsz == 16.f);

    // No room in the stream: forced affine, unchanged.
    v[0] = view_vertex(0.f, 0.f, 100.f, 0, 100.f);
    v[1] = view_vertex(10.f, 0.f, -50.f, 0, 0.f);
    v[2] = view_vertex(0.f, 10.f, -50.f, 0, 0.f);
    SH_CHECK(pr::emit_polygon(v, 3, false) == 3);
    SH_CHECK(v[0].ppw == 0.f && v[1].vsz == -50.f);

    // A clip vertex past the guard band abandons the clip.
    pr::settings().edge_max = 100.f;
    v[0] = view_vertex(0.f, 0.f, 100.f, 0, 100.f);
    v[1] = view_vertex(1000.f, 0.f, -50.f, 0, 0.f);
    v[2] = view_vertex(0.f, 10.f, -50.f, 0, 0.f);
    SH_CHECK(pr::emit_polygon(v, 3) == 3);
    SH_CHECK(v[0].ppw == 0.f);

    // Not eligible: affine polygon, a corner without view space, all in front.
    v[0] = view_vertex(0.f, 0.f, 100.f, 0, 100.f);
    v[1] = view_vertex(10.f, 0.f, -50.f, 0, 0.f);
    v[2] = view_vertex(0.f, 10.f, -50.f, 0, 0.f);
    pr::begin_polygon(true, 0.f, 0.f);
    SH_CHECK(!pr::near_clip_eligible(v, 3));
    pr::begin_polygon(false, 0.f, 0.f);
    v[2].ny = 0.f;
    SH_CHECK(!pr::near_clip_eligible(v, 3));
    v[2].ny = 1.f;
    v[1].vsz = v[2].vsz = 50.f;
    SH_CHECK(!pr::near_clip_eligible(v, 3));
}

SH_TEST(gpu_precision_weld) {
    reset(0);
    float x = 10.2f, y = 20.1f, w = 100.f;
    pr::weld(&x, &y, &w);  // off by default: nothing stored
    pr::settings().weld_px = 1.f;
    float x2 = 10.4f, y2 = 20.3f, w2 = 101.f;
    pr::weld(&x2, &y2, &w2);
    SH_CHECK(x2 == 10.4f);
    pr::weld(&x, &y, &w);  // snaps onto the stored corner
    SH_CHECK(x == 10.4f && y == 20.3f && w == 101.f);
    float x3 = 10.5f, y3 = 20.3f, w3 = 200.f;  // another surface: depth too different
    pr::weld(&x3, &y3, &w3);
    SH_CHECK(x3 == 10.5f && w3 == 200.f);
    pr::bump_generation();  // last frame's corners do not weld
    float x4 = 10.45f, y4 = 20.3f, w4 = 101.f;
    pr::weld(&x4, &y4, &w4);
    SH_CHECK(x4 == 10.45f);
}

SH_TEST(gpu_precision_size_rule) {
    reset(0);
    Vertex v[6];
    v[0].x = 0;    v[0].y = 0;
    v[1].x = 1100; v[1].y = 0;
    v[2].x = 0;    v[2].y = 10;
    v[3].x = 0;    v[3].y = 0;
    v[4].x = 100;  v[4].y = 0;
    v[5].x = 0;    v[5].y = 511;
    SH_CHECK(pr::oversized(&v[0]));
    SH_CHECK(!pr::oversized(&v[3]));
    SH_CHECK(pr::emit_polygon(v, 6) == 3);
    SH_CHECK(v[1].x == 100 && v[2].y == 511);
    SH_CHECK(pr::counters().oversize == 1);

    // Wider scale, a precise polygon, the whole-map far view: kept.
    v[1].x = 1100; v[2].y = 10;
    pr::settings().poly_size_wide_scale = 1.2f;
    SH_CHECK(!pr::oversized(&v[0]));
    pr::settings().poly_size_wide_scale = 1.f;
    v[0].ppw = 1.f;
    SH_CHECK(pr::emit_polygon(v, 3) == 3);
    v[0].ppw = 0.f;
    pr::settings().whole_map_far = true;
    SH_CHECK(pr::emit_polygon(v, 3) == 3);
}

SH_TEST(gpu_precision_lit_side_channel) {
    reset(0);
    pr::settings().pixel_lit_active = true;
    pr::lit_fill({}, 0, 0, 0, nullptr);  // rewind the cursor
    pr::LitRun run;
    run.textured = run.gouraud = true;
    run.light = 2;
    for (int i = 0; i < 12; i++) run.normals[i] = static_cast<float>(i);
    Vertex v[12];
    for (Vertex& c : v) c.ny = 0.f;
    SH_CHECK(pr::end_polygon(v, 6, 4, run) == 6);
    const float* lit = pr::lit_finish(12);
    for (int i = 0; i < 4 * 4; i++) SH_CHECK(lit[i] == 0.f);  // untouched lead-in zeroed
    // Stream vertex 4 + 3 is corner 1 (quad map 0 1 2 1 2 3).
    SH_CHECK(lit[(4 + 3) * 4 + 0] == 3.f && lit[(4 + 3) * 4 + 2] == 5.f && lit[(4 + 3) * 4 + 3] == 2.f);
    SH_CHECK(lit[(4 + 5) * 4 + 0] == 9.f);
    for (int i = 10 * 4; i < 12 * 4; i++) SH_CHECK(lit[i] == 0.f);

    // No normals: zeroed up to the emitted end.
    pr::LitRun flat = run;
    flat.light = 0;
    SH_CHECK(pr::end_polygon(v, 3, 12, flat) == 3);
    lit = pr::lit_finish(15);
    for (int i = 12 * 4; i < 15 * 4; i++) SH_CHECK(lit[i] == 0.f);

    // A size-culled polygon does not line up with its corners: no normals.
    Vertex big[3];
    big[1].x = 2000;
    pr::lit_fill(run.normals, 1, 0, 0, nullptr);  // rewind the cursor
    SH_CHECK(pr::end_polygon(big, 3, 0, run) == 0);
    lit = pr::lit_finish(3);
    for (int i = 0; i < 12; i++) SH_CHECK(lit[i] == 0.f);
}
