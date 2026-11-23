// Framebuffer feedback geometry: capture rects, display buffers, sample selection.

#include "framework/sh_test.h"
#include "render/feedback/feedback_geometry.h"

namespace fb = sh::render::feedback;

SH_TEST(feedback_area_falls_back_to_viewport_until_recorded) {
    fb::AreaInputs in;
    in.presentViewport = {10, 20, 640, 480};
    const fb::Area a = fb::areaFor(in);
    SH_CHECK(!a.recorded);
    SH_CHECK(a.rect.x0 == 10.0f && a.rect.x1 == 650.0f && a.rect.y1 == 500.0f);
    fb::IRect r;
    SH_CHECK(!fb::areaPixelRect(a, &r));
}

SH_TEST(feedback_exact_capture_is_whole_pixels_and_clipped) {
    fb::AreaInputs in;
    in.worldArea = {-4.0f, 0.0f, 636.0f, 448.0f};
    in.worldAreaValid = true;
    in.presentViewport = {0, 0, 640, 448};
    const fb::Area a = fb::areaFor(in);
    fb::IRect r;
    SH_CHECK(fb::areaPixelRect(a, &r) && r.w == 640 && r.h == 448);
    const fb::CaptureBlit b = fb::captureBlit(a, r.w, r.h, true);
    SH_CHECK(b.valid && b.src.x0 == 0.0f && b.dx0 == 4 && b.dx1 == 640);
}

SH_TEST(feedback_display_buffers_stand_down_on_overlapping_upload) {
    fb::DisplayBuffers d;
    d.set(0, 32, 0, 256, 320, 300);  // h clamps to the 224-row gap
    SH_CHECK(d.buffer(0).h == 224);
    SH_CHECK(!d.noteUpload(0, 0, 320, 32));
    SH_CHECK(d.noteUpload(10, 40, 4, 4));
    int frames = 0;
    while (d.consumeSuppress()) ++frames;
    SH_CHECK(frames == 4);
}

SH_TEST(feedback_sample_selects_same_position_copy) {
    fb::DisplayBuffers d;
    d.set(0, 32, 0, 256, 320, 224);
    fb::SampleQuery q;
    q.pageX = 0; q.pageY = 32; q.w = 320; q.h = 224; q.dstX = 0; q.dstY = 0;
    fb::SampleResult r = fb::selectSample(q, true, false, {}, true, d);
    SH_CHECK(r.kind == fb::SampleKind::Screen && r.mode == 1 && r.natW == 320);
    q.dstX = 5;
    SH_CHECK(fb::selectSample(q, true, false, {}, true, d).mode == 2);
    SH_CHECK(fb::selectSample(q, true, false, {}, false, d).kind == fb::SampleKind::None);
}
