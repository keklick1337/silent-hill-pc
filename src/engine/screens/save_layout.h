// SPDX-License-Identifier: GPL-3.0-or-later
// Layout of the PC save/load screen (320x224 text space), its text colours and text keys as data:
// gamedata/menu/save_screen.json, mod override by file name, built-in default when the file is
// missing or refused. Omitted keys keep the built-in value.
#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "formats/common/status.h"

namespace sh::screens {

// Colours of the game's menu font, by palette slot ("white", "light_grey", ...).
struct SaveScreenColors {
    int heading = 7, message = 7, hint = 6;
    int no_saves = 7, new_save = 7, row = 7, row_next_fear = 0, damaged = 1, difficulty = 6;
    int question = 7, answer = 7;
    int info_label = 7, info_value = 7, date = 6;
};

// Text keys (strings_*.json) the screen shows.
struct SaveScreenTexts {
    std::string heading_save = "ui.save.heading_save", heading_load = "ui.save.heading_load";
    std::string no_saves = "ui.save.no_saves", new_save = "ui.save.new_save", damaged = "ui.save.damaged";
    std::string difficulty_easy = "ui.save.difficulty.easy", difficulty_normal = "ui.save.difficulty.normal",
                difficulty_hard = "ui.save.difficulty.hard";
    std::string confirm_save = "ui.save.confirm_save", confirm_overwrite = "ui.save.confirm_overwrite",
                confirm_delete = "ui.save.confirm_delete";
    std::string yes = "ui.save.yes", no = "ui.save.no";
    std::string loading = "ui.save.loading", load_failed = "ui.save.load_failed";
    std::string deleted = "ui.save.deleted", delete_failed = "ui.save.delete_failed";
    std::string saved = "ui.save.saved", save_failed = "ui.save.save_failed";
    std::string new_save_info = "ui.save.new_save_info";
    std::string data = "ui.save.data", time = "ui.save.time", save_count = "ui.save.save_count";
    std::string delete_hint = "ui.save.delete_hint";
};

struct SaveScreenLayout {
    int visible_rows = 5;
    int heading_y = 14;
    int center_x = 160;
    int list_x = 24, list_y = 34, list_w = 272, list_extra_h = 12;
    int row_y = 40, row_pitch = 20, row_hit_dy = 2;
    int empty_row = 2;
    int row_text_x = 40, row_text_right = 284;
    int select_inset_x = 6, select_inset_y = 3, select_glow = 48, select_glow_idle = 16;
    int arrow_x = 308, arrow_up_dy = 4, arrow_down_dy = 8;
    int hint_y = 150;
    int info_y = 172, info_h = 40;
    int info_line1 = 178, info_line2 = 196;
    int yes_cx = 118, no_cx = 202;
    int answer_hit_half_w = 28, answer_hit_top = 3, answer_hit_bottom = 17;
    int answer_frame_w = 48, answer_frame_h = 18, answer_frame_dy = 2, answer_frame_glow = 40;
    int info_label_x = 40, info_value_x = 92, info_time_x = 150;
    int number_digits = 4, save_count_max = 999;
    int message_ticks = 60, refresh_ticks = 60;

    SaveScreenColors colors;
    SaveScreenTexts texts;
    // Save point names in e_SaveLocationId order; an id out of range shows the first.
    std::vector<std::string> location_keys = {
        "ui.save.location.anywhere",     "ui.save.location.cafe",         "ui.save.location.bus",
        "ui.save.location.store",        "ui.save.location.infirmary",    "ui.save.location.doghouse",
        "ui.save.location.gordon",       "ui.save.location.church",       "ui.save.location.garage",
        "ui.save.location.police",       "ui.save.location.reception",    "ui.save.location.room_302",
        "ui.save.location.directors",    "ui.save.location.jewelry_shop", "ui.save.location.pool_hall",
        "ui.save.location.antique_shop", "ui.save.location.theme_park",   "ui.save.location.boat",
        "ui.save.location.bridge",       "ui.save.location.motel",        "ui.save.location.lighthouse",
        "ui.save.location.sewer",        "ui.save.location.nowhere",      "ui.save.location.childs_room",
        "ui.save.location.next_fear",
    };
};

// Names of the menu font's palette slots (the game's text colours).
struct MenuTextColorName {
    const char* name;
    int slot;
};
inline constexpr MenuTextColorName kMenuTextColors[] = {
    {"gold", 0}, {"dark_grey", 1}, {"green", 2}, {"red", 4}, {"light_grey", 6}, {"white", 7},
};

// Palette slot of a menu font colour name, -1 if unknown.
int menu_text_color(std::string_view name);

Result<SaveScreenLayout> parse_save_layout(std::string_view json_text);
// `source` (optional) receives the file read. Mods are searched newest-first under mods_dir.
Result<SaveScreenLayout> load_save_layout(const std::filesystem::path& gamedata_dir,
                                          const std::filesystem::path& mods_dir, std::string* source);

}  // namespace sh::screens
