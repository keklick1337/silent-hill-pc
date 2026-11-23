// SPDX-License-Identifier: GPL-3.0-or-later
//
// Numbers of a mod's things: an item of any group takes a free byte number, a mod's gun
// takes attack rows past the original's table, and what an item is held as comes from the data (its weapon file),
// never from its number; a mod's "pickup_once" item gets a flag of its own.

#include "framework/sh_test.h"

#include "engine/ids/game_numbers.h"
#include "engine/items/item_library.h"
#include "framework/mod_fixture.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;
using sh::items::ItemLibrary;
using sh::items::WeaponAttack;

namespace {

fs::path repo_gamedata() { return fs::path(SH_PROJECT_ROOT) / "gamedata"; }

struct Scratch {
    fs::path root;
    explicit Scratch(const char* name) : root(fs::path(SH_PROJECT_ROOT) / ".tmp" / "mod_numbers" / name) {
        fs::remove_all(root);
        fs::create_directories(root);
    }
    ~Scratch() {
        std::error_code ec;
        fs::remove_all(root, ec);
    }
    void write(const fs::path& relative, std::string_view text) const {
        fs::create_directories(root / relative.parent_path());
        std::ofstream(root / relative, std::ios::binary) << text;
        sh::test::mod_file_written(root / relative);
    }
};

bool has_warning(const std::vector<std::string>& warnings, std::string_view part) {
    for (const std::string& w : warnings) {
        if (w.find(part) != std::string::npos) {
            return true;
        }
    }
    return false;
}

void write_gun_mod(const Scratch& dir) {
    dir.write("mods/arms/mod.json", R"({"id": "arms", "version": "1.0.0", "gameplay": true})");
    dir.write("mods/arms/items/Blaster.json", R"({"group": "guns", "inventory": {"command": "EquipReload"}})");
    dir.write("mods/arms/items/BlasterCells.json", R"({"group": "ammo", "pickup_count": 12})");
    dir.write("mods/arms/items/Lantern.json", R"({"group": "portable", "inventory": {"pickup_once": true}})");
    dir.write("mods/arms/items/Cleaver.json", R"({"group": "melee"})");
    dir.write("mods/arms/weapons/Blaster.json", R"({"class": "gun", "item": "Blaster", "clip": 12,
        "ammo": "BlasterCells", "attacks": [{"input": "tap", "hit_type": 3, "power": 250, "reach_q12": 40960,
        "keyframe_length": 5, "pellets": 1}]})");
    dir.write("mods/arms/weapons/Cleaver.json", R"({"class": "melee", "item": "Cleaver",
        "attacks": [{"input": "tap", "power": 200}]})");
}

}  // namespace

// Items of every group get a number; the mod's gun gets its own attack rows.
SH_TEST(mod_items_numbered_in_every_group) {
    Scratch dir("groups");
    write_gun_mod(dir);
    std::vector<std::string> warnings;
    const auto loaded = ItemLibrary::load(repo_gamedata(), dir.root / "mods", warnings);
    SH_CHECK(loaded);
    const ItemLibrary& lib = loaded.value();
    SH_CHECK(lib.check_consistency().empty());

    // The first free number of each group: 170 after the guns' scene props, 195 after the shells, 227 after the tank,
    // 131 the melee group's gap.
    SH_CHECK(lib.find_item("arms:Blaster")->number == 170);
    SH_CHECK(lib.find_item("arms:BlasterCells")->number == 195);
    SH_CHECK(lib.find_item("arms:Lantern")->number == 227);
    SH_CHECK(lib.find_item("arms:Cleaver")->number == 131);
    SH_CHECK(sh::ids::item_number("arms:Blaster") == 170);
    SH_CHECK(!has_warning(warnings, "no free item number"));

    // The gun: the highest free number of the appended rows, each input one row of its own past the original's 70.
    const sh::items::WeaponDef* gun = lib.find_weapon("arms:Blaster");
    SH_CHECK(gun != nullptr && gun->number == 107);
    SH_CHECK(gun->attack(WeaponAttack::Input::Tap)->index == 107);
    SH_CHECK(gun->attack(WeaponAttack::Input::Tap)->gun_shot);
    SH_CHECK(sh::ids::weapon_number("arms:Blaster") == 107);
    SH_CHECK(lib.clip_capacity(107) == 12);

    // What the item is held as comes from its weapon file.
    SH_CHECK(lib.held_number(170) == 107);
    SH_CHECK(lib.held_item(107) == 170);
    SH_CHECK(lib.held_as_gun(170));
    SH_CHECK(lib.ammo_item_number(170) == 195);
    SH_CHECK(lib.ammo_pickup_count(195) == 12);

    // A melee weapon of a mod takes the next number of the same pool; its rows decode to it through the ids, not by
    // the original's arithmetic (106 + 10 is a melee hold, not a gun).
    const sh::items::WeaponDef* cleaver = lib.find_weapon("arms:Cleaver");
    SH_CHECK(cleaver != nullptr && cleaver->number == 106);
    SH_CHECK(cleaver->attack(WeaponAttack::Input::Tap)->index == 106);
    SH_CHECK(!cleaver->attack(WeaponAttack::Input::Tap)->gun_shot);
    SH_CHECK(!has_warning(warnings, "attack slot"));
    SH_CHECK(lib.held_number(131) == 106);
    SH_CHECK(!lib.held_as_gun(131));
    SH_CHECK(lib.held_number(227) == -1);  // the lantern is no weapon
    SH_CHECK(sh::ids::mod_attack_weapon(116) == 106 && sh::ids::mod_attack_input(116) == 1);
    SH_CHECK(!sh::ids::attack_is_gun(116) && !sh::ids::attack_is_gun(106));
    SH_CHECK(sh::ids::attack_is_gun(107) && sh::ids::attack_weapon(127) == 107);
    // The original's attacks decode as before.
    SH_CHECK(sh::ids::mod_attack_weapon(12) == -1 && sh::ids::attack_weapon(12) == 2);
    SH_CHECK(sh::ids::attack_is_gun(32) && !sh::ids::attack_is_gun(29) && !sh::ids::attack_is_gun(-1));
}

// The base game's items keep the original's in-hand numbers.
SH_TEST(base_items_held_as_the_original) {
    std::vector<std::string> warnings;
    const auto loaded = ItemLibrary::load(repo_gamedata(), warnings);
    SH_CHECK(loaded);
    const ItemLibrary& lib = loaded.value();
    SH_CHECK(lib.held_number(128) == 0);    // kitchen knife
    SH_CHECK(lib.held_number(130) == 2);    // rock drill
    SH_CHECK(lib.held_number(160) == 32);   // handgun
    SH_CHECK(lib.held_number(163) == 35);   // hyper blaster
    SH_CHECK(lib.held_number(164) == 36);   // the cutscene phone: no weapon file, the original's number
    SH_CHECK(lib.held_number(224) == -1);   // the flashlight is not held as a weapon
    SH_CHECK(lib.held_number(32) == -1);    // health drink
    SH_CHECK(lib.held_item(32) == 160);
    SH_CHECK(lib.held_item(36) == 164);
    SH_CHECK(lib.held_item(8) == -1);       // the kick is carried as nothing
    SH_CHECK(lib.held_as_gun(160) && lib.held_as_gun(164) && !lib.held_as_gun(128));
    SH_CHECK(lib.ammo_item_number(160) == 192);
    SH_CHECK(lib.ammo_item_number(163) == -1);  // no ammo for the blaster
    SH_CHECK(lib.ammo_item_number(128) == -1);
}

// A full group: the next items take free numbers anywhere.
SH_TEST(mod_items_overflow_their_group) {
    Scratch dir("overflow");
    dir.write("mods/many/mod.json", R"({"id": "many", "version": "1.0.0", "gameplay": true})");
    // The health group has 29 free numbers (35..63).
    for (int i = 0; i < 31; ++i) {
        dir.write("mods/many/items/Tonic" + std::to_string(100 + i) + ".json", R"({"group": "health"})");
    }
    std::vector<std::string> warnings;
    const auto loaded = ItemLibrary::load(repo_gamedata(), dir.root / "mods", warnings);
    SH_CHECK(loaded);
    const ItemLibrary& lib = loaded.value();
    SH_CHECK(lib.find_item("many:Tonic100")->number == 35);
    SH_CHECK(lib.find_item("many:Tonic128")->number == 63);
    SH_CHECK(lib.find_item("many:Tonic129")->number == 1);  // the first free byte: the empty "none" group
    SH_CHECK(lib.find_item("many:Tonic130")->number == 2);
    SH_CHECK(lib.check_consistency().empty());
    SH_CHECK(warnings.empty());
}

// A mod's pickup_once item gets a flag of its own, with a save bit like any mod flag.
SH_TEST(mod_pickup_once_item_gets_a_flag) {
    Scratch dir("once");
    write_gun_mod(dir);
    sh::ids::configure(repo_gamedata(), dir.root / "mods");
    const std::string flag = sh::ids::pickup_once_flag_name("arms:Lantern");
    SH_CHECK(flag == "arms:Lantern_PickedOnce");
    const int bit = sh::ids::flag_number(flag.c_str());
    SH_CHECK(bit > 0);
    SH_CHECK(sh::ids::flag_name(bit) != nullptr && sh::ids::flag_name(bit) == flag);
    // Only items that ask for it: the gun has none.
    SH_CHECK(sh::ids::flag_number(sh::ids::pickup_once_flag_name("arms:Blaster").c_str()) == -1);
    sh::ids::configure({}, {});
}
