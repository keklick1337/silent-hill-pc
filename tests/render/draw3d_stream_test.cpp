// The native backend of the world-space effect list (render/draw3d/draw3d_stream): the vertex stream a polygon becomes.

#include "framework/sh_test.h"
#include "render/draw3d/draw3d_stream.h"

#include <vector>

namespace {

namespace d3 = sh::draw3d;
using sh::rhi::PrimVertex;

class Target final : public d3::StreamTarget {
public:
    d3::StreamFrame f;
    d3::PrimFlags prim;
    std::vector<PrimVertex> out;
    std::vector<d3::StreamRun> runs;
    std::uint16_t latched = 0;
    int precise_calls = 0, resolved = 0, projections = 0;
    float last_h = 0.f;
    std::uint8_t alpha = 255;
    bool drop = false;
    float depth_z = -2.f;  // < -1: leave the layer depth

    d3::PrimFlags begin_prim(const d3::Polygon& p) override {
        d3::PrimFlags r = prim;
        r.affine = r.affine || p.affine;
        r.no_flashlight = r.no_flashlight || p.no_flashlight;
        return r;
    }
    void latch_page(std::uint16_t page) override { latched = page; }
    PrimVertex* begin(const d3::StreamRun& run, int vertices) override {
        if (drop) return nullptr;
        if (run.textured) {
            latched = run.page;
            f.page = run.page;
        }
        runs.push_back(run);
        room_.assign(static_cast<std::size_t>(vertices), PrimVertex{});
        return room_.data();
    }
    d3::StreamFrame frame() const override { return f; }
    void precise(PrimVertex&, bool ok) override {
        precise_calls++;
        resolved += ok ? 1 : 0;
    }
    void view_projection(float, float, float h) override {
        projections++;
        last_h = h;
    }
    std::uint8_t depth(PrimVertex* corners, int count, const d3::Polygon&) override {
        if (depth_z >= -1.f)
            for (int i = 0; i < count; i++) corners[i].z = depth_z;
        return alpha;
    }
    void end(const d3::StreamRun&, PrimVertex* triangles, int vertices) override {
        out.insert(out.end(), triangles, triangles + vertices);
    }

private:
    std::vector<PrimVertex> room_;
};

d3::Polygon quad() {
    d3::Polygon p;
    for (int i = 0; i < 4; i++) {
        p.v[i].xy = {static_cast<std::int16_t>((i & 1) * 10), static_cast<std::int16_t>((i >> 1) * 10)};
        p.v[i].color = {static_cast<std::uint8_t>(i * 10), 1, 2};
        p.v[i].uv = {static_cast<std::uint8_t>((i & 1) * 16), static_cast<std::uint8_t>((i >> 1) * 16)};
    }
    return p;
}

}  // namespace

SH_TEST(draw3d_stream_flat_quad_positions_and_order) {
    Target t;
    t.f.offset_x = 160.5f;  // fractional offsets truncate the integer position
    t.f.offset_y = 112.f;
    t.f.depth = 0.5f;
    d3::Polygon p = quad();
    d3::emit_stream(p, t);
    SH_CHECK(t.out.size() == 6);
    const int expect[6] = {0, 1, 2, 1, 2, 3};
    for (int k = 0; k < 6; k++) {
        const int i = expect[k];
        SH_CHECK(t.out[k].x == static_cast<std::int16_t>(static_cast<int>((i & 1) * 10 + 160.5f)));
        SH_CHECK(t.out[k].y == (i >> 1) * 10 + 112);
        // A flat polygon is one colour; no fog.
        SH_CHECK(t.out[k].r == 0 && t.out[k].g == 1 && t.out[k].fog == 0);
        SH_CHECK(t.out[k].z == 0.5f && t.out[k].a == 255);
        SH_CHECK(t.out[k].page == sh::rhi::kPageUntextured && t.out[k].bright == 1 && t.out[k].dither == 0);
        SH_CHECK(t.out[k].ppw == 0.f && t.out[k].ny == 0.f);
    }
    SH_CHECK(t.precise_calls == 0);
}

SH_TEST(draw3d_stream_gouraud_textured_quad) {
    Target t;
    t.f.page = 0;
    d3::Polygon p = quad();
    p.gouraud = true;
    p.textured = true;
    p.page = d3::TexturePage::from_word(0x022C);  // dither bit in the page word
    p.palette = d3::Palette::from_word(0x0123);
    for (int i = 0; i < 4; i++) p.v[i].fog = static_cast<std::uint8_t>(i + 5);
    t.alpha = 77;
    d3::emit_stream(p, t);
    SH_CHECK(t.out.size() == 6 && t.latched == 0x022C);
    const int expect[6] = {0, 1, 2, 1, 2, 3};
    for (int k = 0; k < 6; k++) {
        const int i = expect[k];
        SH_CHECK(t.out[k].r == i * 10);
        SH_CHECK(t.out[k].fog == i + 5);
        SH_CHECK(t.out[k].u == (i & 1) * 16 && t.out[k].v == (i >> 1) * 16);
        SH_CHECK(t.out[k].a == 77 && t.out[k].bright == 2 && t.out[k].dither == 1);
        SH_CHECK(t.out[k].clut == 0x0123);
        // The far edges are exclusive: the box ends one texel short.
        SH_CHECK(t.out[k].ulo == 0 && t.out[k].uhi == 15 && t.out[k].vhi == 15);
    }
}

SH_TEST(draw3d_stream_raw_texture_ignores_colour) {
    Target t;
    d3::Polygon p = quad();
    p.textured = true;
    p.shade.raw_texture = true;
    d3::emit_stream(p, t);
    SH_CHECK(t.out.size() == 6 && t.out[0].r == 128 && t.out[3].b == 128);
}

SH_TEST(draw3d_stream_surface_decal_is_marked_for_the_decal_program) {
    Target t;
    t.f.precise = true;
    d3::Polygon p = quad();
    p.textured = true;
    p.gouraud = true;
    p.shade = {true, false};
    for (int i = 0; i < 4; i++) {
        p.v[i].precise = d3::Precise{static_cast<float>(p.v[i].xy.x), static_cast<float>(p.v[i].xy.y), 100.f + i};
        p.v[i].fog = static_cast<std::uint8_t>(20 + i);
    }
    d3::emit_stream(p, t);
    SH_CHECK(t.out.size() == 6);
    for (const PrimVertex& v : t.out) {
        // Its corners keep their own view depth (what the renderer tests against the scene) and their fog.
        SH_CHECK(v.geom3d == 0.f && v.ppw >= 100.f && v.fog >= 20);
    }

    Target m;
    m.f.precise = true;
    p.surface_decal = true;
    d3::emit_stream(p, m);
    SH_CHECK(m.out.size() == 6);
    for (const PrimVertex& v : m.out) {
        SH_CHECK(v.geom3d == sh::rhi::kGeomSurfaceDecal && v.ppw >= 100.f && v.fog >= 20);
    }
}

SH_TEST(draw3d_stream_precise_corners) {
    Target t;
    t.f.precise = true;
    t.f.offset_x = 10.f;
    t.f.offset_y = 20.f;
    d3::Polygon p = quad();
    for (int i = 0; i < 4; i++) p.v[i].precise = d3::Precise{p.v[i].xy.x + 0.25f, p.v[i].xy.y + 0.5f, 100.f + i};
    d3::emit_stream(p, t);
    SH_CHECK(t.out.size() == 6);
    const int expect[6] = {0, 1, 2, 1, 2, 3};
    for (int k = 0; k < 6; k++) {
        const int i = expect[k];
        SH_CHECK(t.out[k].ppx == (i & 1) * 10 + 0.25f + 10.f);
        SH_CHECK(t.out[k].ppy == (i >> 1) * 10 + 0.5f + 20.f);
        SH_CHECK(t.out[k].ppw == 100.f + i);
    }
    SH_CHECK(t.precise_calls == 4 && t.resolved == 4);

    // One corner without a projection drops the whole polygon to affine.
    Target m;
    m.f.precise = true;
    p.v[2].precise.reset();
    d3::emit_stream(p, m);
    for (const PrimVertex& v : m.out) SH_CHECK(v.ppw == 0.f);
    SH_CHECK(m.resolved == 3);
    SH_CHECK(m.out[2].ppx == static_cast<float>(m.out[2].x));

    // Past the guard band a corner is affine too.
    Target g;
    g.f.precise = true;
    g.f.edge_max = 50.f;
    p.v[2].precise = d3::Precise{60.f, 0.f, 10.f};
    d3::emit_stream(p, g);
    SH_CHECK(g.resolved == 3);
    for (const PrimVertex& v : g.out) SH_CHECK(v.ppw == 0.f);

    // An affine polygon never resolves.
    Target a;
    a.f.precise = true;
    p.v[2].precise = d3::Precise{1.f, 1.f, 10.f};
    p.affine = true;
    d3::emit_stream(p, a);
    SH_CHECK(a.precise_calls == 0);
    for (const PrimVertex& v : a.out) SH_CHECK(v.ppw == 0.f && v.ppx == static_cast<float>(v.x));
}

SH_TEST(draw3d_stream_view_space) {
    Target t;
    t.f.view_space = true;
    d3::Polygon p = quad();
    p.v[1].view = d3::ViewPosition{1.f, 2.f, 300.f, false, 0.5f, 160.f, 120.f, 256.f};
    d3::emit_stream(p, t);
    SH_CHECK(t.projections == 1 && t.last_h == 256.f);
    // Vertex 1 is corner 1 of both triangles.
    SH_CHECK(t.out[1].vsz == 300.f && t.out[1].ny == 1.f && t.out[1].nx == 1.f && t.out[1].nz == 0.5f);
    SH_CHECK(t.out[3].vsz == 300.f);
    SH_CHECK(t.out[0].ny == 0.f);
    // A known corner marks the whole polygon as scene geometry.
    for (const PrimVertex& v : t.out) SH_CHECK(v.geom3d == 1.f);

    // Kept out of the flashlight: every corner reads as untracked.
    Target n;
    n.f.view_space = true;
    p.no_flashlight = true;
    d3::emit_stream(p, n);
    for (const PrimVertex& v : n.out) SH_CHECK(v.vsz == -1.f);

    // Every corner far away: no view space needed.
    Target far;
    far.f.view_space = true;
    far.f.precise = true;
    p.no_flashlight = false;
    for (int i = 0; i < 4; i++) p.v[i].precise = d3::Precise{0.f, 0.f, 20000.f};
    d3::emit_stream(p, far);
    SH_CHECK(far.projections == 0 && far.out[1].ny == 0.f);
}

SH_TEST(draw3d_stream_near_clip_keeps_precise) {
    Target t;
    t.f.precise = true;
    t.f.view_space = true;
    t.f.near_clip = true;
    t.f.near_z = 16.f;
    d3::Polygon p = quad();
    for (int i = 0; i < 4; i++) p.v[i].view = d3::ViewPosition{0.f, 0.f, i == 3 ? 4.f : 100.f, true, 0.f, 0.f, 0.f, 1.f};
    for (int i = 0; i < 3; i++) p.v[i].precise = d3::Precise{0.f, 0.f, 100.f};
    d3::emit_stream(p, t);
    // The corner behind the eye has no projection, but the clip needs the others intact.
    SH_CHECK(t.out[0].ppw == 100.f && t.out[5].ppw == 0.f);
}

SH_TEST(draw3d_stream_triangle_and_null_triangle) {
    Target t;
    d3::Polygon p = quad();
    p.count = 3;
    p.gouraud = true;
    d3::emit_stream(p, t);
    SH_CHECK(t.out.size() == 3 && t.out[2].r == 20 && t.out[0].dither == 1);

    Target n;
    p.gouraud = false;
    p.textured = true;
    p.page = d3::TexturePage::from_word(0x1F);
    for (int i = 0; i < 3; i++) p.v[i].xy = {-1, -1};
    d3::emit_stream(p, n);
    SH_CHECK(n.latched == 0x1F && n.out.empty() && n.runs.empty());
}

SH_TEST(draw3d_stream_target_depth_and_skip) {
    Target t;
    t.depth_z = 0.75f;
    d3::emit_stream(quad(), t);
    for (const PrimVertex& v : t.out) SH_CHECK(v.z == 0.75f);
    Target s;
    s.drop = true;
    d3::emit_stream(quad(), s);
    SH_CHECK(s.out.empty());
}
