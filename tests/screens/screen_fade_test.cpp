// The screen fade, the cutscene bars, the puzzle cursor and the outline box (src/engine/screens/screen_fade.h,
// cursor_shapes.h), worked out by hand from the specification.

#include "engine/screens/cursor_shapes.h"
#include "engine/screens/screen_fade.h"

#include "framework/sh_test.h"

#include <variant>
#include <vector>

namespace {

namespace sc = sh::screens;
namespace d2 = sh::draw2d;

class FadeHost final : public sc::ScreenFadeHost {
public:
    std::int32_t st = 1, step = 0, dt = 68;
    int queued = 0, buf = 0;
    std::vector<d2::Command> out;
    std::int32_t status() override { return st; }
    void set_status(std::int32_t s) override { st = s; }
    std::int32_t timestep() override { return step; }
    void set_timestep(std::int32_t s) override { step = s; }
    int file_queue_length() override { return queued; }
    std::int32_t frame_time() override { return dt; }
    int buffer() override { return buf; }
    void submit(const d2::Command& c) override { out.push_back(c); }
};

class BarsHost final : public sc::CutsceneBordersHost {
public:
    std::int32_t st = 1, dt = 68;
    bool paused = false, active = true;
    int buf = 0, proj = -1;
    std::vector<d2::Command> out;
    std::int32_t state() override { return st; }
    void set_state(std::int32_t s) override { st = s; }
    bool music_paused() override { return paused; }
    std::int32_t frame_time() override { return dt; }
    int buffer() override { return buf; }
    bool cutscene_active() override { return active; }
    void clear_cutscene_active() override { active = false; }
    int screen_height() override { return 224; }
    int half_width() override { return 170; }
    int bar_outer() override { return 112; }
    int bar_inner() override { return 96; }
    void set_event_camera_rate(std::int32_t) override {}
    void set_projection(int v) override { proj = v; }
    void submit(const d2::Command& c) override { out.push_back(c); }
};

}  // namespace

SH_TEST(screen_fade_out_paced_by_file_queue) {
    sc::ScreenFade fade;
    FadeHost h;
    h.st = 2;  // fade out from 0, black
    h.queued = 2;
    h.dt = 68;
    fade.update(h);
    // step 3.0 / (2 + 1) = 1.0 per second -> 68 * 4096 >> 12 = 68.
    SH_CHECK(h.st == 3);
    SH_CHECK(fade.progress() == 68);
    SH_CHECK(h.out.size() == 2);
    const auto& tile = std::get<d2::Fill>(h.out[0]);
    SH_CHECK(tile.color.r == 68 >> 4 && tile.translucent && tile.at.x == -320 && tile.w == 1280);
    SH_CHECK(std::get<d2::DrawMode>(h.out[1]).page.word() == 64);
}

SH_TEST(screen_fade_white_page_and_finish) {
    sc::ScreenFade fade;
    FadeHost h;
    h.st = 2 | sc::kFadeWhiteBit;
    h.step = 4096 * 100;  // finishes in one frame
    fade.update(h);
    SH_CHECK(h.st == (4 | sc::kFadeWhiteBit));  // fade out done: next frame clears the custom step
    SH_CHECK(fade.progress() == 4095);
    SH_CHECK(std::get<d2::DrawMode>(h.out[1]).page.word() == 32);
}

SH_TEST(screen_fade_in_resets_and_idles) {
    sc::ScreenFade fade;
    FadeHost h;
    h.st = 6;  // fade in start
    h.step = 4096 * 100;
    fade.update(h);
    SH_CHECK(h.st == 7 && fade.progress() == 4095);
    fade.update(h);
    SH_CHECK(h.st == 0 && fade.progress() == 0);
    h.out.clear();
    fade.update(h);  // Reset: custom step cleared, idle, nothing drawn
    SH_CHECK(h.st == 1 && h.step == 0 && h.out.empty());
    SH_CHECK(fade.visible_progress() == 4096);
}

SH_TEST(screen_fade_frame_time_clamp) {
    SH_CHECK(sc::fade_frame_time(0) == 4096 / 60);
    SH_CHECK(sc::fade_frame_time(-3) == 4096 / 60);
    SH_CHECK(sc::fade_frame_time(1000) == 4096 / 30);
    SH_CHECK(sc::fade_frame_time(50) == 50);
}

SH_TEST(cutscene_bars_fade_in_and_projection) {
    sc::CutsceneBorders bars;
    BarsHost h;
    h.st = 18;
    h.active = false;
    h.dt = 2048;
    bars.update(h);
    SH_CHECK(h.st == 19 && bars.shade() == 2048);
    // Two bars of this buffer and the page change.
    SH_CHECK(h.out.size() == 3);
    const auto& top = std::get<d2::Polygon>(h.out[0]);
    SH_CHECK(top.xy[0].x == -170 && top.xy[0].y == -112 && top.xy[3].y == -96);
    SH_CHECK(top.color[0].r == 2048 >> 4 && top.color[2].r == 2048 >> 5);
    SH_CHECK(std::get<d2::Polygon>(h.out[1]).xy[0].y == 112);
    SH_CHECK(h.proj == 224 + (((377 - 224) * 2048) >> 12));
    bars.update(h);
    SH_CHECK(h.st == 20 && bars.shade() == 4095);  // full: the inner edge goes to 255
    SH_CHECK(std::get<d2::Polygon>(h.out[3]).color[2].r == 255);
}

SH_TEST(cutscene_bars_reset_clears_cutscene) {
    sc::CutsceneBorders bars;
    BarsHost h;
    h.st = 0;
    bars.update(h);
    SH_CHECK(h.st == 1 && !h.active && h.out.empty());
    h.st = 21;
    h.paused = true;
    bars.update(h);
    SH_CHECK(h.out.empty());
}

SH_TEST(puzzle_cursor_quad_corners_and_texels) {
    sc::PuzzleCursor c;
    c.x = 10;
    c.y = -4;
    c.half_w = 8;
    c.half_h = 6;
    c.u = 250;  // u + w carries into v
    c.v = 64;
    c.w = 16;
    c.h = 16;
    c.shade = 100;
    c.clut_x = 192;
    c.clut_y = 0;
    c.page = 0x17;
    const d2::Polygon p = sc::puzzle_cursor_quad(c);
    SH_CHECK(p.textured && p.count == 4);
    SH_CHECK(p.xy[0].x == 2 && p.xy[0].y == -10 && p.xy[3].x == 18 && p.xy[3].y == 2);
    SH_CHECK(p.uv[0].u == 250 && p.uv[0].v == 64);
    SH_CHECK(p.uv[1].u == static_cast<std::uint8_t>(266) && p.uv[1].v == 65);
    SH_CHECK(p.palette.word() == 12);
    SH_CHECK(p.page.word() == 0x17);
    SH_CHECK(p.color[0].r == 100 && p.color[0].g == 100 && p.color[0].b == 100);
}

SH_TEST(outline_box_rings) {
    const auto lines = sc::outline_box_lines(-4096, {0, 0, 10, 10}, {20, 30, 40, 50});
    // Fully at `to`: x 20..60, y 30..80; first ring brightest.
    SH_CHECK(lines[0].xy[0].x == 20 && lines[0].xy[0].y == 30 && lines[0].xy[2].x == 60 && lines[0].xy[2].y == 80);
    SH_CHECK(lines[1].xy[0].y == 29 && lines[1].xy[2].y == 81);
    SH_CHECK(lines[0].color[0].b == 0x80 && lines[4].color[0].b == 0x80 - 64 / 5);
    // Animated: at t 0.5 the first ring has arrived, the last (delayed 0.4) is at blend 0.2.
    const auto anim = sc::outline_box_lines(2048, {0, 0, 0, 0}, {100, 0, 0, 0});
    SH_CHECK(anim[0].xy[0].x == 100);
    SH_CHECK(anim[16].xy[0].x == 20);
}
