// The engine frame (render/frame): entries placed by order key are drawn slot by slot from the top down, latest
// first inside a slot, per layer and display buffer; a linked inset is drawn inside world slot 0.

#include "framework/sh_test.h"
#include "render/frame/frame.h"
#include "render/gpu/frame_draw.h"

#include <vector>

namespace f = sh::frame;

namespace {

std::vector<std::uint32_t> take_ids(f::Frame& fr, f::Layer layer, int buffer) {
    std::vector<std::uint32_t> got;
    fr.take_layer(layer, buffer, [](std::uint32_t, bool) {}, [&](const f::Entry& e) { got.push_back(e.id); });
    return got;
}

}  // namespace

SH_TEST(frame_slots_draw_top_down_latest_first) {
    f::Frame fr;
    fr.submit(f::Kind::Draw2D, 1, {f::Layer::World, 5}, 0);
    fr.submit(f::Kind::Draw3D, 2, {f::Layer::World, 5}, 0);
    fr.submit(f::Kind::Draw2D, 3, {f::Layer::World, 900}, 0);
    fr.submit(f::Kind::Draw2D, 4, {f::Layer::World, 0}, 0);
    fr.submit(f::Kind::Draw2D, 5, {f::Layer::Overlay, 3}, 0);
    const std::vector<std::uint32_t> want = {3, 2, 1, 4};
    SH_CHECK(take_ids(fr, f::Layer::World, 0) == want);
    SH_CHECK(take_ids(fr, f::Layer::World, 0).empty());  // consumed
    SH_CHECK(fr.pending() == 1);
    SH_CHECK(take_ids(fr, f::Layer::Overlay, 0) == std::vector<std::uint32_t>{5});
}

SH_TEST(frame_buffers_keep_their_own_lists) {
    f::Frame fr;
    fr.submit(f::Kind::Draw2D, 1, {f::Layer::World, 2}, 0);
    fr.submit(f::Kind::Draw2D, 2, {f::Layer::World, 2}, 1);
    SH_CHECK(take_ids(fr, f::Layer::World, 1) == std::vector<std::uint32_t>{2});
    SH_CHECK(take_ids(fr, f::Layer::World, 0) == std::vector<std::uint32_t>{1});
}

SH_TEST(frame_slot_walk_visits_every_slot_and_clamps_past_the_top) {
    f::Frame fr;
    fr.submit(f::Kind::Draw2D, 1, {f::Layer::Overlay, 99}, 0);  // past the 16 overlay slots: the top slot
    std::vector<std::uint32_t> slots;
    std::uint32_t entry_slot = 0;
    fr.take_layer(
        f::Layer::Overlay, 0, [&](std::uint32_t s, bool) { slots.push_back(s); },
        [&](const f::Entry&) { entry_slot = slots.back(); });
    SH_CHECK(slots.size() == 16 && slots.front() == 15 && slots.back() == 0);
    SH_CHECK(entry_slot == 15);
}

SH_TEST(frame_inset_draws_inside_world_slot_zero) {
    f::Frame fr;
    fr.submit(f::Kind::Draw2D, 1, {f::Layer::World, 0}, 0);  // before the link: drawn after the inset
    fr.submit(f::Kind::Draw2D, 2, {f::Layer::World, 1}, 0);
    fr.link_inset(0);
    fr.submit(f::Kind::Scene, 3, {f::Layer::Inset, 4}, 0);
    fr.submit(f::Kind::Scene, 4, {f::Layer::Inset, 0}, 0);
    fr.submit(f::Kind::Draw2D, 5, {f::Layer::World, 0}, 0);  // after the link: drawn before the inset
    std::vector<std::uint32_t> got;
    int inset_slots = 0;
    fr.take_layer(
        f::Layer::World, 0, [&](std::uint32_t, bool inset) { inset_slots += inset ? 1 : 0; },
        [&](const f::Entry& e) { got.push_back(e.id); });
    SH_CHECK((got == std::vector<std::uint32_t>{2, 5, 3, 4, 1}));
    SH_CHECK(inset_slots == 512);
}

SH_TEST(frame_inset_waits_for_a_link_and_a_clear_unlinks_it) {
    f::Frame fr;
    fr.submit(f::Kind::Scene, 1, {f::Layer::Inset, 0}, 0);
    fr.submit(f::Kind::Draw2D, 2, {f::Layer::World, 3}, 0);
    SH_CHECK(take_ids(fr, f::Layer::World, 0) == std::vector<std::uint32_t>{2});
    fr.link_inset(0);
    fr.clear(f::Layer::World, 0);
    SH_CHECK(take_ids(fr, f::Layer::World, 0).empty());
    fr.link_inset(0);
    SH_CHECK(take_ids(fr, f::Layer::World, 0) == std::vector<std::uint32_t>{1});
}

SH_TEST(frame_drops_stale_entries_after_several_presents) {
    f::Frame fr;
    fr.submit(f::Kind::Draw2D, 7, {f::Layer::World, 1}, 0);
    for (int i = 0; i < 3; i++) fr.end_frame();
    SH_CHECK(fr.pending() == 1);
    for (int i = 0; i < 3; i++) fr.end_frame();
    SH_CHECK(fr.pending() == 0);
}

SH_TEST(frame_keyed_submit_uses_the_buffer_source) {
    f::set_buffer_source([] { return 1; });
    f::submit_keyed(f::Kind::Draw2D, 9, {f::Layer::Overlay, 2});
    f::set_buffer_source(nullptr);
    SH_CHECK(take_ids(f::frame(), f::Layer::Overlay, 1) == std::vector<std::uint32_t>{9});
}

SH_TEST(frame_offscreen_area_pairs_become_a_capture) {
    f::Frame fr;
    // Drawn top down: the area that sends the scene offscreen (slot 9), the scene, the area that returns (slot 2).
    fr.submit(f::Kind::Area, 0, {f::Layer::World, 9}, 0, nullptr, {512, 0, 320, 240});
    fr.submit(f::Kind::Scene, 1, {f::Layer::World, 5}, 0);
    fr.submit(f::Kind::Area, 0, {f::Layer::World, 2}, 0, nullptr, {0, 0, 320, 240});
    fr.submit(f::Kind::Area, 0, {f::Layer::World, 1}, 0, nullptr, {0, 0, 320, 240});  // no scene open: stays
    std::vector<int> redirects;
    static std::vector<int>* s_out = nullptr;
    s_out = &redirects;
    sh::gpu::resolve_scene_captures(fr, 0, [](int x, int y, int w, int h) { s_out->insert(s_out->end(), {x, y, w, h}); });
    SH_CHECK((redirects == std::vector<int>{512, 0, 320, 240}));
    std::vector<f::Kind> kinds;
    f::Rect capture;
    bool stepped = false;
    fr.take_layer(f::Layer::World, 0, [](std::uint32_t, bool) {}, [&](const f::Entry& e) {
        kinds.push_back(e.kind);
        if (e.kind == f::Kind::Capture) {
            capture = e.rect;
            stepped = e.steps_down;
        }
    });
    SH_CHECK((kinds == std::vector<f::Kind>{f::Kind::Area, f::Kind::Scene, f::Kind::Capture, f::Kind::Area}));
    SH_CHECK(capture.x == 512 && capture.y == 0 && capture.w == 320 && capture.h == 240 && stepped);
}

SH_TEST(frame_slot_depth_matches_the_bucket_scales) {
    // Painter scale: the top slot is the far plane, the bottom one the near plane, linear in between.
    SH_CHECK(sh::gpu::slot_depth(0, 16, 0) == -1.f);
    SH_CHECK(sh::gpu::slot_depth(15, 16, 0) == 1.f);
    SH_CHECK(sh::gpu::slot_depth(40, 16, 0) == 1.f);  // past the bottom (an inset) stays at the near plane
    // Linear view-Z scale: slot b covers view Z ~ b << shift.
    const float z = sh::gpu::slot_depth(2047, 2048, 4);  // slot 0
    SH_CHECK(z > 0.99f && z <= 1.f);
    SH_CHECK(sh::gpu::slot_depth(2047 + 100, 2048, 4) == z);
}

// A frame presented between two game updates draws the last presented frame's entries again.
SH_TEST(frame_redraw_last_presents_the_same_lists_again) {
    f::Frame fr;
    fr.submit(f::Kind::Draw2D, 1, {f::Layer::World, 5}, 0);
    fr.submit(f::Kind::Draw2D, 2, {f::Layer::World, 9}, 0);
    fr.submit(f::Kind::Draw2D, 3, {f::Layer::Overlay, 1}, 0);
    const std::vector<std::uint32_t> world = {2, 1};
    SH_CHECK(take_ids(fr, f::Layer::World, 0) == world);
    SH_CHECK(take_ids(fr, f::Layer::Overlay, 0) == std::vector<std::uint32_t>{3});
    fr.end_frame();
    // Any number of frames in a row, in the same order; the entries do not age away while they are redrawn.
    for (int i = 0; i < 12; ++i) {
        SH_CHECK(fr.redraw_last() == 3);
        SH_CHECK(take_ids(fr, f::Layer::World, 0) == world);
        SH_CHECK(take_ids(fr, f::Layer::Overlay, 0) == std::vector<std::uint32_t>{3});
        fr.end_frame();
    }
    // The next update's lists replace them.
    fr.submit(f::Kind::Draw2D, 4, {f::Layer::World, 5}, 0);
    SH_CHECK(take_ids(fr, f::Layer::World, 0) == std::vector<std::uint32_t>{4});
    fr.end_frame();
    SH_CHECK(fr.redraw_last() == 1);
    SH_CHECK(take_ids(fr, f::Layer::World, 0) == std::vector<std::uint32_t>{4});
    SH_CHECK(take_ids(fr, f::Layer::Overlay, 0).empty());
    // Entries not drawn in the last frame are not brought back.
    fr.end_frame();
    fr.end_frame();
    SH_CHECK(fr.redraw_last() == 0);
    SH_CHECK(fr.pending() == 0);
}

// An update that is not presented leaves nothing for the next one to draw; what the last present drew stays.
SH_TEST(frame_discard_pending_drops_undrawn_lists) {
    f::Frame fr;
    fr.submit(f::Kind::Draw2D, 1, {f::Layer::World, 5}, 0);
    SH_CHECK(take_ids(fr, f::Layer::World, 0) == std::vector<std::uint32_t>{1});
    fr.end_frame();
    fr.submit(f::Kind::Draw2D, 2, {f::Layer::World, 5}, 0);
    fr.submit(f::Kind::Draw2D, 3, {f::Layer::Overlay, 5}, 0);
    fr.discard_pending();
    SH_CHECK(fr.pending() == 0);
    fr.submit(f::Kind::Draw2D, 4, {f::Layer::World, 5}, 0);
    SH_CHECK(take_ids(fr, f::Layer::World, 0) == std::vector<std::uint32_t>{4});
    SH_CHECK(take_ids(fr, f::Layer::Overlay, 0).empty());
}
