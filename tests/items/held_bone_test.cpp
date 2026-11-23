// SPDX-License-Identifier: GPL-3.0-or-later
// The weapons' "held_bone" and the item <-> weapon <-> ammo links of the shipped data against the
// original's arithmetic.

#include "framework/sh_test.h"

#include "engine/items/item_library.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using sh::items::ItemLibrary;

namespace {

fs::path repo_gamedata() { return fs::path(SH_PROJECT_ROOT) / "gamedata"; }

// The original's switch over the weapon attack (Gfx_PlayerHeldItemAttach): melee weapons on
// MODEL_BONE(2, 2), the three guns on (3, 1), everything else on (1, 0).
std::array<std::uint8_t, 2> original_bone(int attack)
{
    switch (attack) {
    case 0: case 1: case 2: case 4: case 5: case 6: case 7:
        return {2, 2};
    case 32: case 33: case 34:
        return {3, 1};
    default:
        return {1, 0};
    }
}

}  // namespace

SH_TEST(gamedata_held_bone_matches_original_switch) {
    std::vector<std::string> warnings;
    const auto library = ItemLibrary::load(repo_gamedata(), warnings);
    SH_CHECK(library);
    for (int attack = 0; attack < 256; attack++)
        SH_CHECK(library.value().held_bone(attack) == original_bone(attack));
    SH_CHECK(library.value().held_bone(-1) == sh::items::kDefaultHeldBone);
}

// The weapon of an equipped item is item + 128 in a byte in the original, the ammo of a gun
// item + 32 (INV_WEAPON_AMMO_ID), and only gun ammo sits in group 6 (numbers 192..223).
SH_TEST(gamedata_weapon_and_ammo_numbers_match_original_arithmetic) {
    std::vector<std::string> warnings;
    const auto loaded = ItemLibrary::load(repo_gamedata(), warnings);
    SH_CHECK(loaded);
    const ItemLibrary& library = loaded.value();
    int linked = 0;
    for (const sh::items::ItemDef& item : library.items()) {
        SH_CHECK(item.number != 0);
        const sh::items::WeaponDef* weapon = library.weapon_of_item(item.number);
        if (weapon != nullptr) {
            SH_CHECK(weapon->number == ((item.number + 128) & 0xFF));
            linked++;
            const sh::items::ItemDef* ammo = library.ammo_item_of(*weapon);
            if (ammo != nullptr)
                SH_CHECK(ammo->number == item.number + 32 && (ammo->number >> 5) == 6);
            else
                // A gun+32 that is no item can never be in a slot: the original's match fails too.
                SH_CHECK(library.find_item(static_cast<std::uint16_t>(item.number + 32)) == nullptr ||
                         ((item.number + 32) >> 5) != 6);
        }
        if ((item.number >> 5) == 6) {
            // Every item of the ammo group is some gun's ammo.
            SH_CHECK(item.group == "ammo");
            const sh::items::WeaponDef* gun = library.weapon_of_item(static_cast<std::uint16_t>(item.number - 32));
            SH_CHECK(gun != nullptr && library.ammo_item_of(*gun) == &item);
        }
    }
    SH_CHECK(linked == 11);  // seven melee weapons, four guns
    SH_CHECK(library.weapon_of_item(0) == nullptr);
}

SH_TEST(held_bone_field_parse) {
    std::vector<std::string> warnings;
    const char* items = R"({"items": [{"id": 128, "enum": "InvItemId_KitchenKnife", "group": "melee"}]})";
    const auto ok = ItemLibrary::parse(
        items, R"({"weapons": [{"id": 0, "class": "melee", "item_id": 128, "held_bone": [2, 3]}]})", warnings);
    SH_CHECK(ok);
    SH_CHECK((ok.value().weapons().front().held_bone == std::array<std::uint8_t, 2>{2, 3}));
    const auto bad = ItemLibrary::parse(
        items, R"({"weapons": [{"id": 0, "class": "melee", "item_id": 128, "held_bone": [2]}]})", warnings);
    SH_CHECK(!bad);
}

// "muzzle_light": the three guns of the base game have one, melee weapons none; a bad value is an error.
SH_TEST(gamedata_guns_have_muzzle_lights) {
    std::vector<std::string> warnings;
    const auto loaded = ItemLibrary::load(repo_gamedata(), warnings);
    SH_CHECK(loaded);
    if (!loaded) return;
    for (const char* gun : {"Handgun", "Shotgun", "HuntingRifle"}) {
        const sh::items::WeaponDef* w = loaded.value().find_weapon(gun);
        SH_CHECK(w != nullptr && w->muzzle_light && w->muzzle_light->intensity > 0.0f);
    }
    const sh::items::WeaponDef* pipe = loaded.value().find_weapon("SteelPipe");
    SH_CHECK(pipe != nullptr && !pipe->muzzle_light);

    const char* items = R"({"items": [{"id": 128, "enum": "InvItemId_KitchenKnife", "group": "melee"}]})";
    const auto ok = ItemLibrary::parse(
        items, R"({"weapons": [{"id": 0, "class": "melee", "item_id": 128, "muzzle_light": {"range": 3}}]})", warnings);
    SH_CHECK(ok && ok.value().weapons().front().muzzle_light &&
             ok.value().weapons().front().muzzle_light->range == 3.0f);
    const auto bad = ItemLibrary::parse(
        items, R"({"weapons": [{"id": 0, "class": "melee", "item_id": 128, "muzzle_light": {"fade": 2}}]})", warnings);
    SH_CHECK(!bad);
}
