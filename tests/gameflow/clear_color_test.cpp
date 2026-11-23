// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The end-of-frame clear colour, the pickup anchor and the dither switch (engine/gameflow/clear_color.h): every
// input combination against the recorded table, and the rules in words.

#include <string>

#include "framework/golden.h"
#include "framework/sh_test.h"

#include "engine/gameflow/clear_color.h"

using namespace sh::gameflow;

namespace {

constexpr const char* kTable = "gameflow/clear_color.txt";

const char* name_of(ClearColor c) { return c == ClearColor::Keep ? "keep" : c == ClearColor::Black ? "black" : "fog"; }

ClearColorIn foggy_world() {
    ClearColorIn in;
    in.in_game = true;
    in.fog_enabled = true;
    in.world_drawn = true;
    return in;
}

}  // namespace

SH_TEST(clear_color_every_combination_as_recorded) {
    const auto rows = sh::test::golden_rows(kTable, "clear");
    SH_CHECK(rows.size() == 768);
    for (const auto& r : rows) {
        ClearColorIn in;
        in.map_screen_active = r.flag(0);
        in.present_last_frame = r.flag(1);
        in.in_game = r.flag(2);
        in.inventory_screen = r.flag(3);
        in.cutscene_active = r.flag(4);
        in.alessa_shop_scene = r.flag(5);
        in.game_over_state = r.flag(6);
        in.world_drawn = r.flag(7);
        in.fog_enabled = r.flag(8);
        in.bg2d_held = r.flag(9);
        const ClearColorOut o = clear_color_choose(in);
        const bool same = r.str(10) == name_of(o.color) && o.set_fog_uniform == r.flag(11);
        SH_CHECK(same);
        if (!same) {
            sh::test::golden_mismatch(kTable, r, std::string(name_of(o.color)) + " " + std::to_string(o.set_fog_uniform));
        }
    }
    const auto anchors = sh::test::golden_rows(kTable, "anchor");
    SH_CHECK(anchors.size() == 32);
    for (const auto& r : anchors) {
        SH_CHECK(pickup_zero_anchor(r.flag(0), r.flag(1), r.flag(2), r.flag(3), r.flag(4)) == r.flag(5));
    }
    const auto dither = sh::test::golden_rows(kTable, "dither");
    SH_CHECK(dither.size() == 2);
    for (const auto& r : dither) {
        SH_CHECK(dither_suppressed(r.flag(0)) == r.flag(1));
    }
}

// A foggy world clears to the fog colour while the world (or its frozen frame) is on screen, and to black under a
// held 2D background or when nothing of the world is shown; either way the renderer's fog takes the world's colour.
SH_TEST(clear_color_fog_follows_the_world) {
    ClearColorIn in = foggy_world();
    ClearColorOut o = clear_color_choose(in);
    SH_CHECK(o.color == ClearColor::Fog && o.set_fog_uniform);
    in.world_drawn = false;
    in.present_last_frame = true;
    SH_CHECK(clear_color_choose(in).color == ClearColor::Fog);
    in.present_last_frame = false;
    o = clear_color_choose(in);
    SH_CHECK(o.color == ClearColor::Black && o.set_fog_uniform);
    in = foggy_world();
    in.bg2d_held = true;
    SH_CHECK(clear_color_choose(in).color == ClearColor::Black);
    in = foggy_world();
    in.fog_enabled = false;
    o = clear_color_choose(in);
    SH_CHECK(o.color == ClearColor::Keep && !o.set_fog_uniform);
}

// Screens that are not the world clear to black: the paper map (unless a frozen frame shows behind it), the
// inventory, a game over whose world is not drawn, and the cutscene of the black void map.
SH_TEST(clear_color_black_screens) {
    ClearColorIn in = foggy_world();
    in.map_screen_active = true;
    ClearColorOut o = clear_color_choose(in);
    SH_CHECK(o.color == ClearColor::Black && !o.set_fog_uniform);
    in.present_last_frame = true;
    SH_CHECK(clear_color_choose(in).color == ClearColor::Fog);

    in = ClearColorIn{};
    in.inventory_screen = true;
    in.fog_enabled = true;
    o = clear_color_choose(in);
    SH_CHECK(o.color == ClearColor::Black && !o.set_fog_uniform);

    in = foggy_world();
    in.game_over_state = true;
    SH_CHECK(clear_color_choose(in).color == ClearColor::Fog);
    in.world_drawn = false;
    o = clear_color_choose(in);
    SH_CHECK(o.color == ClearColor::Black && !o.set_fog_uniform);

    in = foggy_world();
    in.cutscene_active = true;
    in.alessa_shop_scene = true;
    o = clear_color_choose(in);
    SH_CHECK(o.color == ClearColor::Black && o.set_fog_uniform);
    in.cutscene_active = false;
    SH_CHECK(clear_color_choose(in).color == ClearColor::Fog);
}

// Outside the game and the inventory the colour is left alone.
SH_TEST(clear_color_kept_outside_the_game) {
    ClearColorIn in;
    in.fog_enabled = in.world_drawn = in.cutscene_active = in.alessa_shop_scene = in.game_over_state = true;
    const ClearColorOut o = clear_color_choose(in);
    SH_CHECK(o.color == ClearColor::Keep && !o.set_fog_uniform);
}

// A pickup shown over a world that is not drawn sits on the screen centre, unless a cutscene frames the picture;
// dithering is on only in game.
SH_TEST(clear_color_pickup_anchor_and_dither) {
    SH_CHECK(pickup_zero_anchor(true, true, false, false, false));
    SH_CHECK(!pickup_zero_anchor(false, true, false, false, false));
    SH_CHECK(!pickup_zero_anchor(true, false, false, false, false));
    SH_CHECK(!pickup_zero_anchor(true, true, true, false, false));
    SH_CHECK(!pickup_zero_anchor(true, true, false, true, false));
    SH_CHECK(!pickup_zero_anchor(true, true, false, false, true));
    SH_CHECK(!dither_suppressed(true) && dither_suppressed(false));
}
