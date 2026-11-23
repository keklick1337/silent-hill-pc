// Front end of the primitive renderer (src/render/gpu, RENDER.md section 3): the split key of SplitBuilder,
// the split cap and the near-full flush, page resolution through fake host hooks, and Frame entries drawn through
// frame_draw (2D commands, 3D polygons, scene, mesh and capture splits).

#include "framework/sh_test.h"
#include "render/draw2d/draw2d.h"
#include "render/draw3d/draw3d.h"
#include "render/frame/frame.h"
#include "render/gpu/frame_draw.h"
#include "render/gpu/page_resolve.h"
#include "render/gpu/split_builder.h"
#include "render/gpu/vertex_precision.h"

#include <cstdint>
#include <vector>

namespace gpu = sh::gpu;

namespace {

struct Fixture {
    std::vector<gpu::SplitBatch> flushed;
    gpu::SplitBuilder out{[this](gpu::SplitBatch& b) { flushed.push_back(b); }};

    Fixture() {
        gpu::page_hooks() = gpu::PageHooks{};
        sh::frame::state().set_draw_offset(false, 0, 0);
        out.state().env.clip = gpu::Rect{0, 0, 320, 240};
        out.state().env.draw_on_display = true;
    }
    ~Fixture() { gpu::page_hooks() = gpu::PageHooks{}; }

    // One primitive of `n` vertices under the current state.
    void prim(bool semi, bool textured, gpu::DepthMode depth = gpu::DepthMode::Disabled, std::uint32_t n = 3) {
        out.begin_primitive(semi, 128);
        out.begin_prims(semi, textured, depth);
        out.reserve(n);
    }
    std::size_t splits() { return out.batch().splits.size(); }
};

std::uint16_t page(int format, int blend, int x64) {
    return static_cast<std::uint16_t>((format << 7) | (blend << 5) | x64);
}

}  // namespace

SH_TEST(gpu_split_builder_key_merges_and_splits) {
    Fixture f;
    f.out.state().env.tpage = page(0, 0, 8);
    f.prim(false, true);
    f.prim(false, true);
    SH_CHECK(f.splits() == 1);
    // Mixed program: an untextured prim and another video-memory format share the split.
    f.prim(false, false);
    f.out.state().env.tpage = page(1, 0, 8);
    f.prim(false, true);
    SH_CHECK(f.splits() == 1);
    const gpu::Split& s0 = f.out.batch().splits[0];
    SH_CHECK(s0.mixed && s0.format == gpu::TexFormat::Direct16 && s0.texture.kind == gpu::TextureRef::Kind::Vram);

    // Blend from the page word when semi-transparent.
    f.out.state().env.tpage = page(0, 2, 8);
    f.prim(true, true);
    SH_CHECK(f.splits() == 2 && f.out.batch().splits[1].blend == gpu::Blend::Subtract);
    // Depth mode, mask mode, clip, dfe and label each start a split.
    f.prim(true, true, gpu::DepthMode::WorldFar);
    SH_CHECK(f.splits() == 3 && f.out.batch().splits[2].depth == gpu::DepthMode::WorldFar);
    f.out.state().mask_mode = 1;
    f.prim(true, true, gpu::DepthMode::WorldFar);
    SH_CHECK(f.splits() == 4);
    f.out.state().env.clip = gpu::Rect{0, 0, 320, 224};
    f.prim(true, true, gpu::DepthMode::WorldFar);
    SH_CHECK(f.splits() == 5);
    f.out.state().env.draw_on_display = false;
    f.prim(true, true, gpu::DepthMode::WorldFar);
    SH_CHECK(f.splits() == 6);
    static const char kLabel[] = "hud";
    f.out.state().label = kLabel;
    f.prim(true, true, gpu::DepthMode::WorldFar);
    SH_CHECK(f.splits() == 7 && f.out.batch().splits[6].label == kLabel);

    // A glyph splits textured prims only.
    f.out.begin_primitive(true, 128);
    f.out.state().glyph = true;
    f.out.begin_prims(true, false, gpu::DepthMode::WorldFar);
    SH_CHECK(f.splits() == 7);
    f.out.begin_prims(true, true, gpu::DepthMode::WorldFar);
    SH_CHECK(f.splits() == 8 && f.out.batch().splits[7].glyph);

    // The scene-scratch base layer fades in at the damp: an opaque prim draws ConstantAlpha.
    f.out.begin_primitive(false, 128);
    f.out.state().scratch = true;
    f.out.state().feedback_damp = 0.5f;
    f.out.begin_prims(false, true);
    SH_CHECK(f.out.batch().splits.back().blend == gpu::Blend::ConstantAlpha);
    f.out.state().feedback_damp = 1.f;
    f.out.begin_prims(false, true);
    SH_CHECK(f.out.batch().splits.back().blend == gpu::Blend::None);
}

SH_TEST(gpu_split_builder_override_and_formats) {
    Fixture f;
    f.out.state().env.tpage = page(0, 0, 8);
    gpu::PrimState& st = f.out.state();
    st.override_texture = gpu::TextureRef{gpu::TextureRef::Kind::Override, 7, 0};
    st.override_info.width = 64;
    st.override_info.hires_w = 256;
    f.prim(false, true);
    const gpu::Split& s0 = f.out.batch().splits[0];
    SH_CHECK(!s0.mixed && s0.format == gpu::TexFormat::Rgba32 && s0.texture.id == 7);
    SH_CHECK(s0.override_info.width == 64 && s0.override_info.hires_w == 256);
    // Another page origin of the same override texture must not share the offset.
    st.override_info.offset_x = 64;
    f.prim(false, true);
    SH_CHECK(f.splits() == 2);
    // A different normal map with the same colour texture splits too.
    st.override_texture.normal_id = 9;
    f.prim(false, true);
    SH_CHECK(f.splits() == 3);
    // Untextured prims never sample the override.
    f.prim(false, false);
    SH_CHECK(f.out.batch().splits.back().texture.kind == gpu::TextureRef::Kind::Vram);

    // Without the mixed program every format keys separately and untextured prims use the white texture.
    gpu::SplitBuilder plain([](gpu::SplitBatch&) {});
    plain.set_mixed_program(false);
    plain.state().env.tpage = page(0, 0, 8);
    plain.begin_prims(false, true);
    plain.state().env.tpage = page(1, 0, 8);
    plain.begin_prims(false, true);
    plain.begin_prims(false, false);
    SH_CHECK(plain.batch().splits.size() == 3);
    SH_CHECK(plain.batch().splits[0].format == gpu::TexFormat::Clut4);
    SH_CHECK(plain.batch().splits[1].format == gpu::TexFormat::Clut8);
    SH_CHECK(plain.batch().splits[2].texture.kind == gpu::TextureRef::Kind::White);
}

SH_TEST(gpu_split_builder_cap_flush_and_steps) {
    Fixture f;
    // Alternating blends: one split per prim until the cap, then the batch is drawn and building goes on.
    for (std::uint32_t i = 0; i <= gpu::SplitBuilder::kMaxSplits; ++i) {
        f.out.state().env.tpage = page(2, static_cast<int>(i & 1), 8);
        f.prim(true, false);
    }
    SH_CHECK(f.flushed.size() == 1);
    SH_CHECK(f.flushed[0].splits.size() == gpu::SplitBuilder::kMaxSplits);
    SH_CHECK(f.flushed[0].splits.back().count == 3);
    SH_CHECK(f.splits() == 1 && f.out.batch().splits[0].first == 0);
    SH_CHECK(f.out.stats().cap_flushes == 1);

    // Near full: the batch is drawn before the next primitive.
    f.out.flush();
    f.flushed.clear();
    f.prim(false, false, gpu::DepthMode::Disabled, gpu::SplitBuilder::kMaxVertices - 10);
    f.out.flush_if_near_full(8);
    SH_CHECK(f.flushed.empty());
    f.out.flush_if_near_full(24);
    SH_CHECK(f.flushed.size() == 1 && f.flushed[0].splits[0].count == gpu::SplitBuilder::kMaxVertices - 10);
    SH_CHECK(f.out.vertex_count() == 0 && f.splits() == 0);

    // append across a full batch continues the same split in the next one.
    f.flushed.clear();
    f.out.state().env.tpage = page(2, 1, 8);
    f.prim(true, false, gpu::DepthMode::Disabled, gpu::SplitBuilder::kMaxVertices - 3);
    std::vector<gpu::Vertex> six(6);
    six[0].x = 42;
    f.out.append(six);
    SH_CHECK(f.flushed.size() == 1);
    SH_CHECK(f.splits() == 1 && f.out.batch().splits[0].blend == gpu::Blend::Add);
    SH_CHECK(f.out.vertex_count() == 6 && f.out.batch().vertices[0].x == 42);

    // Scene and capture splits; prims after them open a new split.
    f.out.add_scene(-1, 3.f, 4.f);
    f.out.add_capture(gpu::Rect{512, 0, 320, 240});
    f.prim(true, false);
    const auto& s = f.out.batch().splits;
    SH_CHECK(s.size() == 4);
    SH_CHECK(s[1].kind == gpu::Split::Kind::Scene && s[1].scene_item == -1 && s[1].scene_ofs_y == 4.f);
    SH_CHECK(s[2].kind == gpu::Split::Kind::Capture && s[2].capture == (gpu::Rect{512, 0, 320, 240}));
    SH_CHECK(s[3].kind == gpu::Split::Kind::Prims && s[3].first == 6);
    f.out.flush();
    SH_CHECK(f.flushed.back().splits[0].count == 6);

    // discard drops the batch without drawing it.
    f.prim(false, false);
    const std::size_t before = f.flushed.size();
    f.out.discard();
    f.out.flush();
    SH_CHECK(f.flushed.size() == before && f.out.vertex_count() == 0);
}

namespace {

int g_feedback_notes = 0;
bool g_feedback_semi = false;

std::uint32_t fake_lookup(int tpage, int clut, int* w, int* h, int* ox, int* oy, int* hw, int* hh) {
    // An override for page column 9, and for the virtual palette row 600 of column 10.
    const bool col9 = (tpage & 0xF) == 9;
    const bool virt = (tpage & 0xF) == 10 && (clut >> 6) == 600;
    if (!col9 && !virt) return 0;
    if (w) *w = 128;
    if (h) *h = 64;
    if (ox) *ox = 32;
    if (oy) *oy = 0;
    if (hw) *hw = 512;
    if (hh) *hh = 256;
    return col9 ? 11u : 12u;
}

}  // namespace

SH_TEST(gpu_page_resolve_hooks) {
    Fixture f;
    gpu::PageHooks& h = gpu::page_hooks();
    h.lookup_override = &fake_lookup;
    h.glyph_test = [](int tpage, int) { return (tpage & 0xF) == 3 ? 1 : 0; };
    h.note_feedback_reader = [](bool semi, int) {
        ++g_feedback_notes;
        g_feedback_semi = semi;
    };
    h.scratch_covers = [](int x, int y) { return x == 512 && y == 256; };
    g_feedback_notes = 0;

    const std::uint16_t bad_clut = static_cast<std::uint16_t>(600 << 6);
    // A palette row past 511 that no override claims draws nothing; a direct page reads no palette.
    SH_CHECK(gpu::clut_drops(page(1, 0, 4), bad_clut, "TEST"));
    SH_CHECK(!gpu::clut_drops(page(2, 0, 4), bad_clut, "TEST"));
    SH_CHECK(!gpu::resolve_page(f.out, page(0, 0, 4), bad_clut, "TEST"));
    SH_CHECK(gpu::resolve_page(f.out, page(0, 0, 10), bad_clut, "TEST"));
    SH_CHECK(f.out.state().override_texture.id == 12);

    // Override lookup fills the footprint.
    f.out.begin_primitive(false, 128);
    SH_CHECK(gpu::resolve_page(f.out, page(0, 0, 9), 0, "TEST"));
    const gpu::PrimState& st = f.out.state();
    SH_CHECK(st.override_texture.kind == gpu::TextureRef::Kind::Override && st.override_texture.id == 11);
    SH_CHECK(st.override_info.width == 128 && st.override_info.offset_x == 32 && st.override_info.hires_w == 512);
    // A miss clears it again.
    SH_CHECK(gpu::resolve_page(f.out, page(0, 0, 8), 0, "TEST"));
    SH_CHECK(st.override_texture.kind == gpu::TextureRef::Kind::White && st.override_info.width == 0);

    // Glyph test.
    SH_CHECK(gpu::resolve_page(f.out, page(0, 0, 3), 0, "TEST") && st.glyph);

    // Display-buffer band: a feedback reader, announced with its blend.
    f.out.begin_primitive(true, 127);
    SH_CHECK(gpu::resolve_page(f.out, page(2, 1, 2), 0, "TEST"));
    SH_CHECK(st.feedback && !st.scratch && g_feedback_notes == 1 && g_feedback_semi);
    // The live scene scratch at (512, 256).
    f.out.begin_primitive(false, 128);
    SH_CHECK(gpu::resolve_page(f.out, page(2, 0, 8 | 16), 0, "TEST"));
    SH_CHECK(st.scratch && !st.feedback && g_feedback_notes == 1);
    // An 8-bit page over the band is a texture, not feedback.
    f.out.begin_primitive(false, 128);
    SH_CHECK(gpu::resolve_page(f.out, page(1, 0, 2), 0, "TEST") && !st.feedback);
}

SH_TEST(gpu_frame_draw_2d_commands) {
    Fixture f;
    namespace d2 = sh::draw2d;
    gpu::FrameDrawContext ctx;
    ctx.layer_depth = 0.5f;
    static int s_redirects = 0;
    s_redirects = 0;
    ctx.scene_redirect = [](int, int, int, int) { ++s_redirects; };

    f.out.state().disp.disp = gpu::Rect{0, 0, 320, 240};
    f.out.state().env.ofs_x = 10;
    f.out.state().env.ofs_y = 20;
    gpu::publish_draw_env(f.out.state());

    auto draw = [&](const d2::Command& c) {
        sh::frame::Entry e;
        e.kind = sh::frame::Kind::Draw2D;
        e.id = d2::frame_commands().record(c);
        gpu::draw_frame_entry(f.out, e, ctx);
    };

    // Draw mode, then a translucent fill and a sprite on the latched page.
    d2::DrawMode mode;
    mode.page = d2::TexturePage::from_word(page(2, 1, 8));
    draw(mode);
    SH_CHECK(f.out.state().env.tpage == page(2, 1, 8));
    d2::Fill fill;
    fill.at = {1, 2};
    fill.w = 30;
    fill.h = 40;
    fill.color = {200, 100, 50};
    fill.translucent = true;
    draw(fill);
    d2::Sprite sprite;
    sprite.at = {100, 50};
    sprite.w = 16;
    sprite.h = 8;
    sprite.uv = {32, 64};
    draw(sprite);
    const gpu::SplitBatch& b = f.out.batch();
    SH_CHECK(b.vertices.size() == 12);
    SH_CHECK(b.splits.size() == 2);
    SH_CHECK(b.splits[0].blend == gpu::Blend::Add && b.splits[0].count == 6);
    SH_CHECK(b.splits[1].blend == gpu::Blend::None);
    SH_CHECK(b.vertices[0].x == 11 && b.vertices[0].y == 22 && b.vertices[0].z == 0.5f);
    SH_CHECK(b.vertices[0].r == 200 && b.vertices[0].page == sh::rhi::kPageUntextured);
    SH_CHECK(b.vertices[6].x == 110 && b.vertices[6].u == 32 && b.vertices[6].bright == 2);

    // A small area right of the display buffers routes drawing offscreen and drops the offset; a big one is a
    // scene capture.
    d2::DrawArea area;
    area.x = 832;
    area.y = 224;
    area.w = 32;
    area.h = 32;
    draw(area);
    SH_CHECK(f.out.state().env.clip == (gpu::Rect{832, 224, 32, 32}));
    SH_CHECK(!f.out.state().env.draw_on_display && sh::frame::state().draw_offset_x == 0.f);
    area = d2::DrawArea{512, 0, 320, 240};
    draw(area);
    SH_CHECK(f.out.state().env.draw_on_display && s_redirects == 1);

    // A dropped palette draws nothing and leaves the stream as it was.
    sprite.palette = d2::Palette::from_word(static_cast<std::uint16_t>(700 << 6));
    mode.page = d2::TexturePage::from_word(page(0, 0, 8));
    draw(mode);
    draw(sprite);
    SH_CHECK(f.out.vertex_count() == 12);

    // Scene and capture entries.
    sh::frame::Entry e;
    e.kind = sh::frame::Kind::Scene;
    e.id = 3;
    gpu::draw_frame_entry(f.out, e, ctx);
    e.kind = sh::frame::Kind::Capture;
    e.rect = sh::frame::Rect{640, 0, 64, 64};
    gpu::draw_frame_entry(f.out, e, ctx);
    const auto& s = f.out.batch().splits;
    SH_CHECK(s.size() == 4);
    SH_CHECK(s[2].kind == gpu::Split::Kind::Scene && s[2].scene_item == 2);
    SH_CHECK(s[3].kind == gpu::Split::Kind::Capture && s[3].capture == (gpu::Rect{640, 0, 64, 64}));
    sh::frame::state().set_draw_offset(false, 0, 0);
}

SH_TEST(gpu_frame_draw_3d_polygon) {
    Fixture f;
    namespace d3 = sh::draw3d;
    gpu::FrameDrawContext ctx;
    ctx.layer_depth = -0.25f;
    d3::Polygon p;
    p.count = 3;
    p.textured = true;
    p.page = sh::draw2d::TexturePage::from_word(page(1, 0, 9));
    p.v[0].xy = {10, 10};
    p.v[1].xy = {20, 10};
    p.v[2].xy = {10, 20};
    p.depth = d3::Depth{};
    p.depth->source = d3::DepthSource::WorldMesh;
    p.depth->far = true;
    sh::frame::Entry e;
    e.kind = sh::frame::Kind::Draw3D;
    e.id = d3::frame_commands().record(p);
    gpu::draw_frame_entry(f.out, e, ctx);
    // The polygon latches its page; without PGXP world polygons keep the plain depth mode.
    SH_CHECK(f.out.state().env.tpage == page(1, 0, 9));
    SH_CHECK(f.out.vertex_count() == 3);
    SH_CHECK(f.out.batch().splits.size() == 1 && f.out.batch().splits[0].depth == gpu::DepthMode::Disabled);
    // With PGXP the far band of the whole-town view is drawn on its true depth.
    ctx.pgxp = true;
    e.id = d3::frame_commands().record(p);
    gpu::draw_frame_entry(f.out, e, ctx);
    SH_CHECK(f.out.batch().splits.back().depth == gpu::DepthMode::WorldFar);
}
