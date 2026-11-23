// SPDX-License-Identifier: GPL-3.0-or-later
//
// The console converters keep the console's byte layout for values the engine holds wider (
// section 4): a value that fits is written as before, one past the console's range is written as all ones and
// counted, and a read gives the console type's value sign- or zero-extended.

#include "framework/sh_test.h"

#include "engine/entities/console_character.h"
#include "engine/save/console_fields.h"
#include "engine/save/console_records.h"

#include <cstdint>
#include <vector>

namespace console = sh::save::console;

SH_TEST(console_narrow_writes_fitting_values_and_marks_the_rest) {
    std::vector<std::uint8_t> bytes(8, 0);
    const console::FieldWriter w(bytes);
    w.narrow<std::int8_t>(0, std::int32_t{-1});
    w.narrow<std::int8_t>(1, std::int32_t{100});
    w.narrow<std::int8_t>(2, std::int32_t{200});   // a target slot past the signed byte
    w.narrow<std::uint8_t>(3, std::int32_t{300});  // an item past the byte
    w.narrow<std::uint8_t>(4, std::int32_t{255});
    SH_CHECK(bytes[0] == 0xFF && bytes[1] == 100 && bytes[2] == 0xFF && bytes[3] == 0xFF && bytes[4] == 0xFF);
    SH_CHECK(w.narrowed() == 2);
    const console::FieldReader r(bytes);
    std::int32_t v = 0;
    r.narrow<std::int8_t>(2, v);
    SH_CHECK(v == -1);
    r.narrow<std::uint8_t>(3, v);
    SH_CHECK(v == 255);
    r.narrow<std::int8_t>(1, v);
    SH_CHECK(v == 100);
}

SH_TEST(console_save_record_keeps_its_layout_with_wide_values) {
    sh::game::SaveRecord record{};
    for (sh::game::SaveItem& item : record.items) {
        item = {0xFF, 0, 0, 0xFF};
    }
    record.items[0] = {0x80, 200, 2, 7};
    record.items[1] = {300, 400, 1, 3};  // a mod's item and a stack past the byte
    record.map = 12;
    record.room = 5;
    record.equipped_weapon = 0x80;
    record.paper_map = 3;
    const std::vector<std::uint8_t> bytes = console::encode_save_record(record);
    SH_CHECK(bytes.size() == console::kSaveRecordBytes);
    SH_CHECK(bytes[0] == 0x80 && bytes[1] == 200 && bytes[2] == 2 && bytes[3] == 7);
    SH_CHECK(bytes[4] == 0xFF && bytes[5] == 0xFF && bytes[6] == 1 && bytes[7] == 3);
    SH_CHECK(bytes[0xA4] == 12 && bytes[0xA5] == 5 && bytes[0xA9] == 3 && bytes[0xAA] == 0x80);
    const sh::game::SaveRecord back = console::decode_save_record(bytes);
    SH_CHECK(back.items[0] == record.items[0]);
    SH_CHECK(back.items[1].item == 0xFF && back.items[1].count == 255);  // the console cannot hold them
    SH_CHECK(back.map == 12 && back.room == 5 && back.equipped_weapon == 0x80 && back.paper_map == 3);
    // A record that fits survives the round trip whole.
    record.items[1] = {0x22, 9, 1, 3};
    SH_CHECK(console::decode_save_record(console::encode_save_record(record)) == record);
}

SH_TEST(console_character_record_narrows_wide_numbers) {
    sh::entities::CharacterRecord c{};
    c.model.chara_id = 300;  // a mod's character
    c.partner = 200;         // an NPC slot past the signed byte
    c.attack_received = 12;
    c.attack.attack = 140;   // a mod weapon's row
    std::vector<std::uint8_t> bytes(sh::entities::console::kCharacterBytes, 0);
    const console::FieldWriter w(bytes);
    sh::entities::console::visit_character(w, 0, c);
    SH_CHECK(bytes[0x00] == 0xFF && bytes[0x40] == 0xFF && bytes[0x41] == 12 && bytes[0x46] == 140);
    SH_CHECK(w.narrowed() == 2);
    sh::entities::CharacterRecord back{};
    sh::entities::console::visit_character(console::FieldReader(bytes), 0, back);
    SH_CHECK(back.model.chara_id == -1 && back.partner == -1 && back.attack_received == 12 && back.attack.attack == 140);
}
