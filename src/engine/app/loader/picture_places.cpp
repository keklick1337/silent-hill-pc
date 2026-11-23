// SPDX-License-Identifier: GPL-3.0-or-later
//
// Where the game's fixed pictures land in video memory (sh::loader::ImagePlace: texture page, u/v, palette cell).
// Values from the retail tables.

#include "engine/maps/map_globals.h"
#include "engine/app/loader/picture_places.h"

using sh::loader::ImagePlace;

namespace sh::loader {

PicturePlaces& picture_places() {
    static PicturePlaces places = {
        .font16_atlas = {{0, 16}, 0, 240, 304, 511},
        .konami_logo = {{0, 12}, 0, 0, 0, 0},
        .kcet_logo = {{0, 14}, 0, 0, 0, 1},
        .card_message = {{0, 28}, 0, 0, 0, 2},
        .title = {{1, 13}, 32, 0, 224, 15},
        .paper_map = {{1, 5}, 0, 16, 224, 15},
        .paper_map_markings = {{0, 7}, 32, 0, 480, 255},
        .item_inspection = {{1, 5}, 0, 16, 224, 14},
        .loading_screen = {{0, 5}, 0, 16, 288, 0},
        .controller_buttons = {{0, 7}, 32, 0, 464, 0},
        .brightness_from_title = {{0, 28}, 0, 16, 288, 0},
        .brightness_from_game = {{0, 30}, 0, 16, 288, 0},
        .death_tip = {{0, 30}, 0, 16, 288, 0},
        .health_portrait = {{1, 31}, 12, 103, 224, 13},
        .key_item_textures = {{0, 14}, 0, 0, 160, 0},
        .first_aid_kit_textures = {{0, 15}, 0, 0, 176, 0},
        .item_model_textures = {{0, 13}, 32, 0, 240, 0},
        .event_model_textures = {{0, 5}, 0, 128, 240, 8},
        .blood_effect = {{0, 11}, 0, 0, 304, 0},
        .effect = {{0, 13}, 0, 0, 224, 0},
        .fire_effect = {{0, 13}, 0, 128, 224, 8},
        .font24_atlas = {{0, 31}, 0, 0, 960, 496},
    };
    return places;
}

}  // namespace sh::loader

extern "C" {

// Two lists the character drawer walks up to an entry whose palette_y is -1 (the second starts at entry 2), kept in
// one block as the retail data was.
ImagePlace g_CharaExtraImageLists[] = {
    {{0, 0}, 35, 0, 35, 3},          {{0, 0}, 0, 0, 0, -1},        {{0, 0}, 15, 0, -53, 10},
    {{0, 0}, 76, 0, 140, 2},         {{168, 0}, 246, 255, 7, 13},  {{153, 0}, 94, 0, 35, 13},
    {{205, 255}, 122, 0, 56, 12},    {{168, 0}, 246, 255, -7, 15}, {{153, 0}, 94, 0, -38, 15},
    {{51, 0}, 125, 0, -56, 14},      {{0, 0}, 0, 0, 0, -1},
};

}  // extern "C"

namespace {

// The map variables name the effect and item inspection picture places (map_vars "effect_tex_img",
// "item_inspection_img").
[[maybe_unused]] const bool g_map_globals_bound = [] {
    sh::maps::map_globals().bind("effect_tex_img", &sh::loader::picture_places().effect);
    sh::maps::map_globals().bind("item_inspection_img", &sh::loader::picture_places().item_inspection);
    return true;
}();

}  // namespace
