// SPDX-License-Identifier: GPL-3.0-or-later
//
// Character numbers past a byte: the library finds a character numbered 300, a
// record keeps it, the map's clip lookup and the animation slots hold it, and an attacker slot past the signed byte
// stays whole.

#include "framework/sh_test.h"

#include "engine/chara/chara_library.h"
#include "engine/entities/character.h"
#include "engine/gameflow/anim_slots.h"
#include "engine/maps/chara_rules.h"

#include <string>
#include <vector>

using sh::chara::CharaLibrary;

SH_TEST(chara_library_finds_numbers_past_a_byte) {
    std::vector<std::string> warnings;
    const auto lib = CharaLibrary::parse(
        R"json({"characters": [{"id": 1, "enum": "Harry"}, {"id": 300, "enum": "Wanderer"}]})json", warnings);
    SH_CHECK(lib);
    if (!lib) {
        return;
    }
    const sh::chara::CharacterDef* def = lib.value().find(300);
    SH_CHECK(def != nullptr && def->number == 300 && def->folder == "Wanderer");
    SH_CHECK(lib.value().find(300 & 0xFF) == nullptr);  // 44 is not 300
    SH_CHECK(lib.value().find_id("300") == 300);
    SH_CHECK(lib.value().character(300).valid());
}

SH_TEST(character_record_keeps_wide_numbers) {
    sh::entities::Character c(3, 300, nullptr);
    SH_CHECK(c.chara_id() == 300);
    c.set_attacker(sh::entities::CharacterRef{201, 0});
    SH_CHECK(c.record().partner == 200);
    c.set_attack_received(140);
    SH_CHECK(c.record().attack_received == 140);
}

SH_TEST(chara_table_lookup_holds_numbers_past_a_byte) {
    sh::maps::CharaTableLookup lookup;
    lookup.set(300, 2);
    lookup.set_fallback(7);
    SH_CHECK(lookup.find(300) == 2);
    SH_CHECK(lookup.find(44) == 7);  // 300 & 0xFF has no table of its own
    lookup.clear();
    SH_CHECK(lookup.find(300) == sh::maps::CharaTableLookup::kNone);
}

SH_TEST(anim_slot_table_grows_with_character_numbers) {
    sh::gameflow::AnimSlotTable t;
    t.slots.assign(4, sh::gameflow::AnimSlot{});
    SH_CHECK(t.chara_slot_of(300) == -1 && t.chara_slot_of(-1) == -1);
    t.set_chara_slot(300, 304);
    SH_CHECK(t.chara_slot_of(300) == 304 && t.chara_slot_of(299) == -1);
    SH_CHECK(!t.has_slot(304));
}
