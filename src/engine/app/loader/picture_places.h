// SPDX-License-Identifier: GPL-3.0-or-later
//
// Where the game's fixed pictures land in video memory: one record of named places (src/engine/app/loader/
// picture_places.cpp holds the retail values). Image reads are queued with these places; the screens draw from them.
#pragma once

#include "engine/app/loader/game_files.h"

namespace sh::loader {

struct PicturePlaces {
    ImagePlace font16_atlas;
    ImagePlace konami_logo;
    ImagePlace kcet_logo;
    ImagePlace card_message;
    ImagePlace title;
    ImagePlace paper_map;
    ImagePlace paper_map_markings;
    ImagePlace item_inspection;
    ImagePlace loading_screen;
    ImagePlace controller_buttons;
    ImagePlace brightness_from_title;
    ImagePlace brightness_from_game;
    ImagePlace death_tip;
    ImagePlace health_portrait;
    ImagePlace key_item_textures;
    ImagePlace first_aid_kit_textures;
    // The inventory's model texture sheet (ITEM/TIM07.TIM) and the event models' texture.
    ImagePlace item_model_textures;
    ImagePlace event_model_textures;
    // The effect pictures: blood, the map effects' slot (glass and the like), fire.
    ImagePlace blood_effect;
    ImagePlace effect;
    ImagePlace fire_effect;
    ImagePlace font24_atlas;
};

PicturePlaces& picture_places();

}  // namespace sh::loader
