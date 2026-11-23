// The health screen cues (engine/screens/health_cues.h): the quick-heal pulse and the low-health glow.
#include "engine/screens/health_cues.h"
#include "framework/sh_test.h"

#include <variant>

namespace {

namespace d2 = sh::draw2d;

SH_TEST(heal_flash_fades_out_and_stops) {
    sh::screens::HealFlash f;
    SH_CHECK(f.update(68, 320, 240).count == 0);
    f.start();
    sh::screens::CueCommands c = f.update(0, 320, 240);
    SH_CHECK(c.count == 2);
    const d2::Fill* tile = std::get_if<d2::Fill>(&c.list[0]);
    SH_CHECK(tile != nullptr && tile->translucent && tile->color.g == 96 && tile->w == 1280 && tile->h == 480);
    SH_CHECK(tile->at.x == -320 && tile->at.y == -240);
    const d2::DrawMode* mode = std::get_if<d2::DrawMode>(&c.list[1]);
    SH_CHECK(mode != nullptr && mode->dither && mode->page.blend == d2::Blend::Add);
    c = f.update(5000, 320, 240);  // the last frame still draws, at 0
    SH_CHECK(c.count == 2 && std::get<d2::Fill>(c.list[0]).color.g == 0);
    SH_CHECK(f.update(68, 320, 240).count == 0);
}

SH_TEST(low_health_glow_gates_and_bands) {
    sh::screens::LowHealthGlow g;
    sh::screens::LowHealthGlowInput in;
    in.enabled = true;
    in.gameplay = true;
    in.health = 4096 * 20;
    in.frame_time = 68;
    SH_CHECK(g.update(in).count == 0);  // 20 points: no glow
    in.health = 4096 * 5;
    in.cutscene = true;
    SH_CHECK(g.update(in).count == 0 && g.phase() == 0);
    in.cutscene = false;
    const sh::screens::CueCommands c = g.update(in);
    SH_CHECK(c.count == 5 && g.phase() == 68);
    const d2::Polygon& top = std::get<d2::Polygon>(c.list[0]);
    SH_CHECK(top.gouraud && top.shade.translucent && top.xy[0].x == -160 && top.xy[0].y == -120);
    SH_CHECK(top.xy[2].y == -120 + 32 && top.color[0].r > 0 && top.color[2].r == 0);
    const d2::Polygon& right = std::get<d2::Polygon>(c.list[3]);
    SH_CHECK(right.xy[0].x == 160 - 32);
    SH_CHECK(std::holds_alternative<d2::DrawMode>(c.list[4]));
}

SH_TEST(low_health_glow_half_width_follows_the_window) {
    SH_CHECK(sh::screens::glow_half_width(320, 240, false, 1920, 1080, 1.0f, 0.76f) == 160);
    SH_CHECK(sh::screens::glow_half_width(320, 240, true, 1920, 0, 1.0f, 0.76f) == 160);
    // 120 * 16/9 / 0.76 = 280.7
    SH_CHECK(sh::screens::glow_half_width(320, 240, true, 1920, 1080, 1.0f, 0.76f) == 281);
    SH_CHECK(sh::screens::glow_half_width(320, 240, true, 10000, 1000, 1.0f, 0.76f) == 512);
    SH_CHECK(sh::screens::glow_half_width(320, 240, true, 100, 1000, 1.0f, 0.76f) == 160);
}

}  // namespace
