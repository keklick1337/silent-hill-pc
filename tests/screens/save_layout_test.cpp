// SPDX-License-Identifier: GPL-3.0-or-later
// gamedata/menu/save_screen.json equals the built-in layout, and bad files are refused.

#include <filesystem>
#include <string>

#include "engine/screens/save_layout.h"
#include "framework/sh_test.h"

using namespace sh::screens;

SH_TEST(save_layout_json_matches_defaults) {
    const auto loaded = load_save_layout(std::filesystem::path(SH_PROJECT_ROOT) / "gamedata", {}, nullptr);
    SH_CHECK(static_cast<bool>(loaded));
    const SaveScreenLayout d;
    const SaveScreenLayout& a = loaded.value();
    SH_CHECK(a.visible_rows == d.visible_rows && a.list_y == d.list_y && a.row_pitch == d.row_pitch);
    SH_CHECK(a.row_text_right == d.row_text_right && a.info_line2 == d.info_line2 && a.no_cx == d.no_cx);
    SH_CHECK(a.info_time_x == d.info_time_x && a.refresh_ticks == d.refresh_ticks);
}

SH_TEST(save_layout_json_matches_original_constants) {
    const auto loaded = load_save_layout(std::filesystem::path(SH_PROJECT_ROOT) / "gamedata", {}, nullptr);
    SH_CHECK(static_cast<bool>(loaded));
    const SaveScreenLayout& a = loaded.value();
    // The built-in layout's literals.
    SH_CHECK(a.center_x == 160 && a.list_extra_h == 12 && a.info_h == 40 && a.empty_row == 2);
    SH_CHECK(a.row_hit_dy == 2 && a.select_inset_x == 6 && a.select_inset_y == 3);
    SH_CHECK(a.select_glow == 48 && a.select_glow_idle == 16 && a.arrow_up_dy == 4 && a.arrow_down_dy == 8);
    SH_CHECK(a.answer_hit_half_w == 28 && a.answer_hit_top == 3 && a.answer_hit_bottom == 17);
    SH_CHECK(a.answer_frame_w == 48 && a.answer_frame_w / 2 == 24 && a.answer_frame_h == 18);
    SH_CHECK(a.answer_frame_dy == 2 && a.answer_frame_glow == 40 && a.number_digits == 4 && a.save_count_max == 999);
    // Colours: kWhite 7, kLightGrey 6, kDarkGrey 1, kGold 0.
    const SaveScreenColors& c = a.colors;
    SH_CHECK(c.heading == 7 && c.message == 7 && c.hint == 6 && c.no_saves == 7 && c.new_save == 7);
    SH_CHECK(c.row == 7 && c.row_next_fear == 0 && c.damaged == 1 && c.difficulty == 6);
    SH_CHECK(c.question == 7 && c.answer == 7 && c.info_label == 7 && c.info_value == 7 && c.date == 6);
    const SaveScreenTexts& t = a.texts;
    SH_CHECK(t.heading_save == "ui.save.heading_save" && t.heading_load == "ui.save.heading_load");
    SH_CHECK(t.no_saves == "ui.save.no_saves" && t.new_save == "ui.save.new_save" && t.damaged == "ui.save.damaged");
    SH_CHECK(t.difficulty_easy == "ui.save.difficulty.easy" && t.difficulty_normal == "ui.save.difficulty.normal" &&
             t.difficulty_hard == "ui.save.difficulty.hard");
    SH_CHECK(t.confirm_save == "ui.save.confirm_save" && t.confirm_overwrite == "ui.save.confirm_overwrite" &&
             t.confirm_delete == "ui.save.confirm_delete");
    SH_CHECK(t.yes == "ui.save.yes" && t.no == "ui.save.no" && t.loading == "ui.save.loading" &&
             t.load_failed == "ui.save.load_failed");
    SH_CHECK(t.deleted == "ui.save.deleted" && t.delete_failed == "ui.save.delete_failed" &&
             t.saved == "ui.save.saved" && t.save_failed == "ui.save.save_failed");
    SH_CHECK(t.new_save_info == "ui.save.new_save_info" && t.data == "ui.save.data" && t.time == "ui.save.time" &&
             t.save_count == "ui.save.save_count" && t.delete_hint == "ui.save.delete_hint");
    static const char* const kLocations[] = {
        "anywhere", "cafe",         "bus",       "store",        "infirmary",  "doghouse",   "gordon",
        "church",   "garage",       "police",    "reception",    "room_302",   "directors",  "jewelry_shop",
        "pool_hall", "antique_shop", "theme_park", "boat",       "bridge",     "motel",      "lighthouse",
        "sewer",    "nowhere",      "childs_room", "next_fear",
    };
    SH_CHECK(a.location_keys.size() == 25);
    for (std::size_t i = 0; i < 25 && i < a.location_keys.size(); ++i) {
        SH_CHECK(a.location_keys[i] == std::string("ui.save.location.") + kLocations[i]);
    }
    // The built-in default equals the file.
    const SaveScreenLayout d;
    SH_CHECK(d.location_keys == a.location_keys && d.texts.delete_hint == t.delete_hint && d.colors.date == c.date);
    SH_CHECK(d.answer_frame_glow == a.answer_frame_glow && d.center_x == a.center_x);
}

SH_TEST(save_layout_refuses_bad_files) {
    SH_CHECK(!parse_save_layout("{\"nope\": 1}"));
    SH_CHECK(!parse_save_layout("{\"row_pitch\": 0}"));
    SH_CHECK(!parse_save_layout("{\"row_pitch\": 1.5}"));
    SH_CHECK(parse_save_layout("{\"row_pitch\": 24}").value().row_pitch == 24);
    SH_CHECK(!parse_save_layout("{\"colors\": {\"row\": \"purple\"}}"));
    SH_CHECK(!parse_save_layout("{\"colors\": {\"nope\": \"white\"}}"));
    SH_CHECK(!parse_save_layout("{\"colors\": [\"white\"]}"));
    SH_CHECK(!parse_save_layout("{\"texts\": {\"yes\": \"\"}}"));
    SH_CHECK(!parse_save_layout("{\"texts\": {\"maybe\": \"ui.save.yes\"}}"));
    SH_CHECK(!parse_save_layout("{\"location_keys\": []}"));
    SH_CHECK(!parse_save_layout("{\"location_keys\": [\"ok\", 3]}"));
    const auto ok = parse_save_layout(
        "{\"colors\": {\"row\": \"red\", \"_x\": 1}, \"texts\": {\"yes\": \"my.yes\"}, \"location_keys\": [\"a\"]}");
    SH_CHECK(static_cast<bool>(ok));
    SH_CHECK(ok.value().colors.row == 4 && ok.value().colors.heading == 7 && ok.value().texts.yes == "my.yes");
    SH_CHECK(ok.value().texts.no == "ui.save.no" && ok.value().location_keys.size() == 1);
}

SH_TEST(save_layout_delete_hint_by_device) {
    const SaveScreenTexts t;
    SH_CHECK(save_hint_device(false, false) == SaveHintDevice::Keys);
    SH_CHECK(save_hint_device(false, true) == SaveHintDevice::Touch);
    // A pad in use names its button even on a phone.
    SH_CHECK(save_hint_device(true, true) == SaveHintDevice::Pad);
    SH_CHECK(save_hint_device(true, false) == SaveHintDevice::Pad);
    SH_CHECK(delete_hint_key(t, SaveHintDevice::Keys) == "ui.save.delete_hint");
    SH_CHECK(delete_hint_key(t, SaveHintDevice::Touch) == "ui.save.delete_hint_touch");
    SH_CHECK(delete_hint_key(t, SaveHintDevice::Pad) == "ui.save.delete_hint_pad");
    const auto loaded = load_save_layout(std::filesystem::path(SH_PROJECT_ROOT) / "gamedata", {}, nullptr);
    SH_CHECK(static_cast<bool>(loaded));
    SH_CHECK(loaded.value().texts.delete_hint_touch == t.delete_hint_touch &&
             loaded.value().texts.delete_hint_pad == t.delete_hint_pad);
}

SH_TEST(save_layout_split_at_button) {
    const ButtonSplit a = split_at_button("{button}: delete");
    SH_CHECK(a.found && a.before.empty() && a.after == ": delete");
    const ButtonSplit b = split_at_button("Löschen mit {button}");
    SH_CHECK(b.found && b.before == "Löschen mit " && b.after.empty());
    const ButtonSplit c = split_at_button("Select: delete");
    SH_CHECK(!c.found && c.before == "Select: delete" && c.after.empty());
}

#include "engine/screens/warning_layout.h"

SH_TEST(warning_layout_json_matches_defaults) {
    const auto loaded = load_warning_layout(std::filesystem::path(SH_PROJECT_ROOT) / "gamedata", {}, nullptr);
    SH_CHECK(static_cast<bool>(loaded));
    const WarningLayout d;
    const WarningLayout& a = loaded.value();
    SH_CHECK(a.fade_step == d.fade_step && a.hold_frames == d.hold_frames && a.quad_h == d.quad_h);
    SH_CHECK(a.quads.size() == d.quads.size() && a.quads[0].u0 == 64 && a.quads[2].x == 64);
    // The original constants: x {-320,-192,64}, w {128,256,256}, u0 {64,0,0}, u1 128, y -240 h 480, page 13..15, v 224.
    const int xs[3] = {-320, -192, 64}, ws[3] = {128, 256, 256}, u0s[3] = {64, 0, 0};
    for (int i = 0; i < 3; ++i) {
        SH_CHECK(a.quads[i].x == xs[i] && a.quads[i].w == ws[i] && a.quads[i].u0 == u0s[i] && a.quads[i].u1 == 128);
    }
    SH_CHECK(a.quad_y == -240 && a.first_page == 13 && a.tex_height == 224 && a.black_frames == 3);
    SH_CHECK(!parse_warning_layout("{\"nope\": 1}"));
    SH_CHECK(!parse_warning_layout("{\"fade_step\": 0}"));
    SH_CHECK(!parse_warning_layout("{\"quads\": []}"));
}
