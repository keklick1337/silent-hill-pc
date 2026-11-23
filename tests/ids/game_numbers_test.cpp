// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// sh::ids game numbers (engine/ids/game_numbers.h): read by place from the repository gamedata/, they must give
// the original's numbers (the constants of engine/game_state/game_ids.h come from the same enums).

#include "engine/game_state/game_ids.h"
#include "engine/ids/game_numbers.h"
#include "framework/sh_test.h"

#include <string>

namespace gs = sh::game_state;

SH_TEST(game_numbers_items_and_characters_by_place) {
    SH_CHECK(sh::ids::item_number("HealthDrink") == gs::item::kHealthDrink);
    SH_CHECK(sh::ids::item_number("silenthill:Ampoule") == gs::item::kAmpoule);
    SH_CHECK(sh::ids::item_number("LobbyKey") == gs::item::kLobbyKey);
    SH_CHECK(sh::ids::item_number("SewerExitKey") == gs::item::kSewerExitKey);  // after the gap at 86
    SH_CHECK(sh::ids::item_number("KaufmannKey") == gs::item::kKaufmannKey);    // after the gap at 111
    SH_CHECK(sh::ids::item_number("Hammer") == gs::item::kHammer);              // after the gap at 131
    SH_CHECK(sh::ids::item_number("Handgun") == gs::item::kHandgun);
    SH_CHECK(sh::ids::item_number("ShotgunShells") == gs::item::kShotgunShells);
    SH_CHECK(sh::ids::item_number("GasolineTank") == gs::item::kGasolineTank);
    SH_CHECK(sh::ids::item_number("NoSuchItem") == -1);
    SH_CHECK(std::string(sh::ids::item_name(gs::item::kPocketRadio)) == "silenthill:PocketRadio");
    SH_CHECK(sh::ids::item_name(86) == nullptr);

    SH_CHECK(sh::ids::chara_number("None") == gs::chara::kNone);
    SH_CHECK(sh::ids::chara_number("Harry") == gs::chara::kHarry);
    SH_CHECK(sh::ids::chara_number("Cheryl") == gs::chara::kCheryl);
    SH_CHECK(sh::ids::chara_number("Padlock") == gs::chara::kPadlock);

    SH_CHECK(sh::ids::weapon_number("KitchenKnife") == 0);
    SH_CHECK(sh::ids::weapon_number("Stomp") == 9);
    SH_CHECK(sh::ids::weapon_number("Handgun") == 32);
    SH_CHECK(sh::ids::weapon_number("HyperBlaster") == 35);
    SH_CHECK(sh::ids::common_pickup_number("FirstAidKit") == 0);
    SH_CHECK(sh::ids::common_pickup_number("ShotgunShells") == 5);
    SH_CHECK(sh::ids::common_pickup_number("Lighter") == -1);
}

SH_TEST(game_numbers_flags_sounds_pages_by_place) {
    // The original save layout (original_order.json "event_flags") places the base flags.
    SH_CHECK(sh::ids::flag_number("M0S00_OpeningCutsceneSeen") == 1);
    SH_CHECK(sh::ids::flag_number("M7S01_RifleShells") == 27);
    SH_CHECK(sh::ids::flag_number("MapMark_AltHospital3F_LinenRoomBotBlank") == 1622);
    SH_CHECK(sh::ids::flag_number("MapMark_AltHospital3F_VideoSignBlank") == 1643);  // read by the last page's marks
    SH_CHECK(sh::ids::flag_name(1644) == nullptr);
    SH_CHECK(std::string(sh::ids::flag_name(1641)) == "silenthill:MapMark_AltHospital3F_LinenRoomLeftArrows");
    // A name that moved to another flag: old saves still read it as the flag it meant there.
    SH_CHECK(std::string(sh::ids::flag_name(1620)) == "silenthill:MapMark_AltHospital3F_StoreRoomBroken");
    SH_CHECK(sh::ids::flag_name_in_old_save("MapMark_AltHospital3F_LinenRoomLeftArrows") != nullptr &&
             std::string(sh::ids::flag_name_in_old_save("MapMark_AltHospital3F_LinenRoomLeftArrows")) ==
                 "silenthill:MapMark_AltHospital3F_StoreRoomBroken");
    SH_CHECK(sh::ids::flag_name_in_old_save("M7S01_RifleShells") == nullptr);
    // The former placeholders are aliases: found, never written.
    SH_CHECK(sh::ids::flag_number("Flag1") == 1);
    SH_CHECK(sh::ids::flag_number("Flag1622") == 1622);
    SH_CHECK(sh::ids::flag_number("silenthill:Flag1642") == 1642);

    SH_CHECK(sh::ids::sound_number("menu_start_game") == 1281);
    SH_CHECK(sh::ids::sound_number("handgun_shot") == 1283);
    SH_CHECK(sh::ids::sound_number("intro_cafe_voice") == 4108);
    SH_CHECK(sh::ids::sound_number("finale_cybil_walk_in_voice") == 4753);
    // The staff roll music of the endings (menu/staff_roll.json): the original's XA ids 0x12D3..0x12D6.
    SH_CHECK(sh::ids::sound_number("staff_roll_music_good_plus") == 0x12D3);
    SH_CHECK(sh::ids::sound_number("staff_roll_music_bad") == 0x12D6);
    SH_CHECK(sh::ids::sound_number("no_such_sound") == -1);

    SH_CHECK(sh::ids::paper_map_number("OtherPlaces") == 0);
    SH_CHECK(sh::ids::paper_map_number("FogSchool1F") == 6);
    SH_CHECK(sh::ids::paper_map_number("PaperMap24") == 24);
    SH_CHECK(std::string(sh::ids::paper_map_name(23)) == "silenthill:AltHospital3F");
    SH_CHECK(sh::ids::paper_map_name(25) == nullptr);
}
