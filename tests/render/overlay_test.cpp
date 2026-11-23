// The engine overlay (render/overlay): spaces, batching by texture and scissor, line quads, the texture store.

#include "framework/sh_test.h"
#include "render/overlay/overlay.h"

#include <cmath>
#include <cstdint>
#include <vector>

namespace {

namespace ov = sh::overlay;

ov::Frame frame_800x600() {
    ov::Frame f;
    f.target_w = 800;
    f.target_h = 600;
    f.picture = {100.f, 50.f, 600.f, 500.f};  // pillarboxed and letterboxed
    return f;
}

bool near(float a, float b) { return std::fabs(a - b) < 1e-4f; }

}  // namespace

SH_TEST(overlay_quad_spaces) {
    ov::DrawList list;
    list.reset(frame_800x600());
    list.quad(ov::Space::Picture, {10.f, 20.f, 30.f, 40.f}, {1.f, 0.f, 0.f, 1.f});
    list.quad(ov::Space::Target, {10.f, 20.f, 30.f, 40.f}, {1.f, 0.f, 0.f, 1.f});
    SH_CHECK(list.vertices().size() == 12);
    // Two triangles: TL, TR, BL, BL, TR, BR.
    const ov::Vertex& tl = list.vertices()[0];
    const ov::Vertex& br = list.vertices()[5];
    SH_CHECK(near(tl.x, 110.f) && near(tl.y, 70.f));
    SH_CHECK(near(br.x, 140.f) && near(br.y, 110.f));
    SH_CHECK(near(tl.u, 0.f) && near(tl.v, 0.f) && near(br.u, 1.f) && near(br.v, 1.f));
    const ov::Vertex& tl2 = list.vertices()[6];
    SH_CHECK(near(tl2.x, 10.f) && near(tl2.y, 20.f));
    SH_CHECK(tl.rgba[0] == 255 && tl.rgba[1] == 0 && tl.rgba[2] == 0 && tl.rgba[3] == 255);
    // Same texture (none), no scissor: one batch.
    SH_CHECK(list.batches().size() == 1);
    SH_CHECK(list.batches()[0].first == 0 && list.batches()[0].count == 12);
}

SH_TEST(overlay_batches_split_on_texture_and_scissor) {
    ov::DrawList list;
    list.reset(frame_800x600());
    const ov::Color white;
    list.rect(ov::Space::Picture, {0.f, 0.f, 10.f, 10.f}, white);
    list.quad(ov::Space::Picture, {0.f, 0.f, 10.f, 10.f}, white, 7);
    list.quad(ov::Space::Picture, {20.f, 0.f, 10.f, 10.f}, white, 7);  // merges with the previous
    list.set_scissor(ov::Space::Picture, {0.5f, 1.5f, 10.f, 10.f});
    list.quad(ov::Space::Picture, {0.f, 0.f, 10.f, 10.f}, white, 7);    // same texture, new clip
    list.set_scissor(ov::Space::Picture, {0.5f, 1.5f, 10.f, 10.f});
    list.rect(ov::Space::Picture, {0.f, 0.f, 10.f, 10.f}, white);       // same clip, other texture
    list.clear_scissor();
    list.rect(ov::Space::Picture, {0.f, 0.f, 10.f, 10.f}, white);
    const auto& b = list.batches();
    SH_CHECK(b.size() == 5);
    SH_CHECK(b[0].texture == 0 && b[0].count == 6 && !b[0].scissored);
    SH_CHECK(b[1].texture == 7 && b[1].count == 12 && b[1].first == 6);
    SH_CHECK(b[2].texture == 7 && b[2].scissored);
    // Outward to whole pixels, then onto the target.
    SH_CHECK(b[2].scissor == (ov::PixelRect{100, 51, 11, 11}));
    SH_CHECK(b[3].texture == 0 && b[3].scissored && b[3].scissor == b[2].scissor);
    SH_CHECK(b[4].texture == 0 && !b[4].scissored);
    std::uint32_t total = 0;
    for (const auto& x : b) total += x.count;
    SH_CHECK(total == list.vertices().size());
}

SH_TEST(overlay_scissor_clamps_and_drops_empty) {
    ov::DrawList list;
    list.reset(frame_800x600());
    list.set_scissor(ov::Space::Target, {-50.f, 590.f, 100.f, 100.f});
    list.rect(ov::Space::Target, {0.f, 0.f, 5.f, 5.f}, {});
    SH_CHECK(list.batches().size() == 1);
    SH_CHECK(list.batches()[0].scissor == (ov::PixelRect{0, 590, 50, 10}));
    list.set_scissor(ov::Space::Target, {900.f, 0.f, 10.f, 10.f});  // off the target
    list.rect(ov::Space::Target, {0.f, 0.f, 5.f, 5.f}, {});
    SH_CHECK(list.batches().size() == 1);  // clipped away entirely
}

SH_TEST(overlay_skips_invisible_and_degenerate) {
    ov::DrawList list;
    list.reset(frame_800x600());
    list.rect(ov::Space::Picture, {0.f, 0.f, 10.f, 10.f}, {1.f, 1.f, 1.f, 0.f});
    list.rect(ov::Space::Picture, {0.f, 0.f, 0.f, 10.f}, {});
    list.line(ov::Space::Picture, 5.f, 5.f, 5.f, 5.f, 2.f, {});
    std::vector<ov::Vertex> two(2);
    list.triangles(ov::Space::Picture, two, 0);
    SH_CHECK(list.empty());
    SH_CHECK(list.vertices().empty());
}

SH_TEST(overlay_line_is_a_quad_across_the_segment) {
    ov::DrawList list;
    list.reset(frame_800x600());
    list.line(ov::Space::Target, 10.f, 20.f, 110.f, 20.f, 2.f, {0.f, 1.f, 0.f, 1.f});
    SH_CHECK(list.vertices().size() == 6);
    float minY = 1e9f, maxY = -1e9f, minX = 1e9f, maxX = -1e9f;
    for (const auto& v : list.vertices()) {
        minY = std::fmin(minY, v.y);
        maxY = std::fmax(maxY, v.y);
        minX = std::fmin(minX, v.x);
        maxX = std::fmax(maxX, v.x);
        SH_CHECK(v.rgba[1] == 255 && v.rgba[0] == 0);
    }
    SH_CHECK(near(minY, 19.f) && near(maxY, 21.f));
    SH_CHECK(near(minX, 10.f) && near(maxX, 110.f));
}

SH_TEST(overlay_triangles_move_with_the_picture) {
    ov::DrawList list;
    list.reset(frame_800x600());
    std::vector<ov::Vertex> tri(4);  // the fourth is a remainder and is dropped
    tri[1].x = 10.f;
    tri[2].y = 10.f;
    list.triangles(ov::Space::Picture, tri, 3);
    SH_CHECK(list.vertices().size() == 3);
    SH_CHECK(near(list.vertices()[0].x, 100.f) && near(list.vertices()[0].y, 50.f));
    SH_CHECK(near(list.vertices()[1].x, 110.f) && near(list.vertices()[2].y, 60.f));
    SH_CHECK(list.batches()[0].texture == 3);
}

SH_TEST(overlay_color_pack_rounds_and_clamps) {
    std::uint8_t c[4];
    ov::pack({0.5f, 2.f, -1.f, 100.f / 255.f}, c);
    SH_CHECK(c[0] == 128 && c[1] == 255 && c[2] == 0 && c[3] == 100);
}

SH_TEST(overlay_texture_store_versions_and_deferred_destroy) {
    ov::TextureStore store;
    const std::uint8_t px[4] = {1, 2, 3, 4};
    const ov::TextureId a = store.create(1, 1, ov::Format::Rgba8, ov::Filter::Nearest, px);
    const ov::TextureId b = store.create(2, 2, ov::Format::Alpha8, ov::Filter::Linear, nullptr);
    SH_CHECK(a != 0 && b != 0 && a != b);
    SH_CHECK(store.find(a)->pixels.size() == 4 && store.find(a)->pixels[2] == 3);
    SH_CHECK(store.find(b)->pixels.size() == 4 && store.find(b)->pixels[0] == 0);
    const std::uint64_t v0 = store.find(a)->version;
    const std::uint8_t px2[8] = {9, 9, 9, 9, 8, 8, 8, 8};
    store.update(a, 2, 1, px2);
    SH_CHECK(store.find(a)->version > v0);
    SH_CHECK(store.find(a)->w == 2 && store.find(a)->pixels.size() == 8);
    store.destroy(a);
    SH_CHECK(store.find(a) != nullptr);  // still there for this frame's quads
    store.collect();
    SH_CHECK(store.find(a) == nullptr);
    SH_CHECK(store.size() == 1);
    // Ids are never reused.
    const ov::TextureId c = store.create(1, 1, ov::Format::Rgba8, ov::Filter::Nearest, px);
    SH_CHECK(c != a && c != b);
    SH_CHECK(store.create(0, 4, ov::Format::Rgba8, ov::Filter::Nearest, nullptr) == 0);
}

SH_TEST(overlay_begin_frame_resets_and_collects) {
    ov::Overlay o;
    o.begin_frame(frame_800x600());
    SH_CHECK(near(o.width(), 600.f) && near(o.height(), 500.f));
    const ov::TextureId t = o.textures().create(1, 1, ov::Format::Rgba8, ov::Filter::Nearest, nullptr);
    o.quad(ov::Space::Picture, {0.f, 0.f, 1.f, 1.f}, {}, t);
    o.textures().destroy(t);
    SH_CHECK(!o.list().empty());
    ov::Frame other;
    other.target_w = 320;
    other.target_h = 240;
    other.picture = {0.f, 0.f, 320.f, 240.f};
    o.begin_frame(other);
    SH_CHECK(o.list().empty());
    SH_CHECK(o.textures().find(t) == nullptr);
    SH_CHECK(o.frame().target_w == 320 && o.frame().valid());
}
