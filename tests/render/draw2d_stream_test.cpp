// The native backend of the 2D draw list (render/draw2d/draw2d_stream): the vertex stream a command becomes.

#include "framework/sh_test.h"
#include "render/draw2d/draw2d_stream.h"

#include <vector>

namespace {

namespace d2 = sh::draw2d;
using sh::rhi::PrimVertex;

class Target final : public d2::StreamTarget {
public:
    d2::StreamFrame f;
    std::vector<PrimVertex> out;
    std::vector<d2::StreamRun> runs;
    int shapes = 0;
    bool drop = false;

    d2::StreamFrame frame() const override { return f; }
    void latch_page(std::uint16_t page) override { f.page = page; }
    void latch_mode(std::uint16_t page, bool dither) override {
        f.page = page;
        f.dither = dither;
    }
    void latch_area(const d2::DrawArea&) override {}
    void latch_mask(const d2::Mask&) override {}
    PrimVertex* begin(const d2::StreamRun& run, int vertices) override {
        if (drop) return nullptr;
        runs.push_back(run);
        room_.assign(static_cast<std::size_t>(vertices), PrimVertex{});
        return room_.data();
    }
    void shape(const d2::StreamRun&, PrimVertex*, int) override { shapes++; }
    void end(int vertices) override { out.insert(out.end(), room_.begin(), room_.begin() + vertices); }

private:
    std::vector<PrimVertex> room_;
};

}  // namespace

SH_TEST(draw2d_stream_sprite_corners_and_texels) {
    Target t;
    t.f.offset_x = 100;
    t.f.offset_y = 50;
    t.f.depth = 0.25f;
    t.f.page = 0x10A;  // 4-bit page at column 10, row 0, blend 0, with bit 8 set
    d2::Sprite s;
    s.at = {-10, 4};
    s.w = 16;
    s.h = 8;
    s.uv = {240, 8};  // u + w == 256: the far edge lands on the page edge
    s.palette = d2::Palette::from_word(0x7FD2);
    d2::emit_stream(s, t);
    SH_CHECK(t.out.size() == 6);
    SH_CHECK(t.shapes == 1);
    // TL, BL, TR / BL, TR, BR
    SH_CHECK(t.out[0].x == 90 && t.out[0].y == 54);
    SH_CHECK(t.out[1].x == 90 && t.out[1].y == 62);
    SH_CHECK(t.out[2].x == 106 && t.out[2].y == 54);
    SH_CHECK(t.out[5].x == 106 && t.out[5].y == 62);
    SH_CHECK(t.out[0].z == 0.25f);
    // 256 does not fit: 255 plus one texel of offset on the far corners.
    SH_CHECK(t.out[5].u == 255 && t.out[5].tcx == 2 && t.out[0].tcx == 0);
    SH_CHECK(t.out[0].uhi == 255 && t.out[0].vhi == 15);
    SH_CHECK(t.out[0].bright == 2 && t.out[0].clut == static_cast<std::int16_t>(0x7FD2));
    SH_CHECK(t.out[0].page == ((0x10A & 0x1F) | (((0x10A >> 7) & 3) << 5)));
    SH_CHECK(t.out[0].r == 128 && t.out[0].a == 255);
}

SH_TEST(draw2d_stream_raw_sprite_ignores_colour) {
    Target t;
    d2::Sprite s;
    s.w = s.h = 4;
    s.color = {10, 20, 30};
    s.shade.raw_texture = true;
    d2::emit_stream(s, t);
    SH_CHECK(t.out.size() == 6 && t.out[0].r == 128 && t.out[0].g == 128);
}

SH_TEST(draw2d_stream_gouraud_quad_fog_and_order) {
    Target t;
    d2::Polygon p;
    p.gouraud = true;
    for (int i = 0; i < 4; i++) {
        p.xy[i] = {static_cast<std::int16_t>((i & 1) * 10), static_cast<std::int16_t>((i >> 1) * 10)};
        p.color[i] = {static_cast<std::uint8_t>(i * 10), 0, 0};
        p.fog.amount[i] = static_cast<std::uint8_t>(i + 1);
    }
    d2::emit_stream(p, t);
    SH_CHECK(t.out.size() == 6);
    // Triangles (0 1 2) and (1 2 3) of the hardware order.
    const int expect[6] = {0, 1, 2, 1, 2, 3};
    for (int k = 0; k < 6; k++) SH_CHECK(t.out[k].r == expect[k] * 10);
    // Vertex 0 takes vertex 1's fog.
    SH_CHECK(t.out[0].fog == 2 && t.out[1].fog == 2 && t.out[2].fog == 3 && t.out[5].fog == 4);
    SH_CHECK(t.out[0].dither == 1 && t.out[0].bright == 1);
}

SH_TEST(draw2d_stream_textured_polygon_latches_page) {
    Target t;
    d2::Polygon p;
    p.count = 3;
    p.textured = true;
    p.page = d2::TexturePage::from_word(0x0205);  // dither bit set in the page word
    for (int i = 0; i < 3; i++) p.xy[i] = {static_cast<std::int16_t>(i), 0};
    d2::emit_stream(p, t);
    SH_CHECK(t.f.page == 0x0205);
    SH_CHECK(t.out.size() == 3 && t.out[0].dither == 1);
    // The SDK's null triangle changes the page and draws nothing.
    Target n;
    for (int i = 0; i < 3; i++) p.xy[i] = {-1, -1};
    d2::emit_stream(p, n);
    SH_CHECK(n.f.page == 0x0205 && n.out.empty() && n.runs.empty());
}

SH_TEST(draw2d_stream_size_rule) {
    Target t;
    d2::Polygon p;
    p.xy[0] = {0, 0};
    p.xy[1] = {1100, 0};
    p.xy[2] = {0, 10};
    p.xy[3] = {1100, 10};
    d2::emit_stream(p, t);
    SH_CHECK(t.out.empty());
    t.f.size_rule = false;
    d2::emit_stream(p, t);
    SH_CHECK(t.out.size() == 6);
}

SH_TEST(draw2d_stream_lines) {
    Target t;
    d2::Line l;
    l.xy[0] = {10, 5};
    l.xy[1] = {0, 5};  // right to left: drawn left to right
    l.color[0] = {200, 0, 0};
    d2::emit_stream(l, t);
    SH_CHECK(t.out.size() == 6);
    SH_CHECK(t.out[0].x == 0 && t.out[0].y == 5);
    SH_CHECK(t.out[5].x == 11 && t.out[5].y == 6);
    Target poly;
    l.count = 4;
    l.xy[2] = {0, 20};
    l.xy[3] = {5, 20};
    d2::emit_stream(l, poly);
    SH_CHECK(poly.out.size() == 18 && poly.shapes == 3 && poly.runs.size() == 1);
}

SH_TEST(draw2d_stream_draw_mode_and_drop) {
    Target t;
    d2::DrawMode m;
    m.page = d2::TexturePage::from_word(0x0A05);  // bits past 9 are not latched; bit 9 is dither
    d2::emit_stream(m, t);
    SH_CHECK(t.f.page == 0x005 && t.f.dither);
    t.drop = true;
    d2::Fill f;
    f.w = f.h = 4;
    d2::emit_stream(f, t);
    SH_CHECK(t.out.empty());
}
