// SPDX-License-Identifier: GPL-3.0-or-later
//
// Numbers past a byte: a mod's items go on past 254 (255 stays the empty slot) and a
// mod's weapons past the 17 numbers the console's signed byte left them, without a cap.

#include "framework/sh_test.h"

#include "engine/ids/game_numbers.h"
#include "engine/items/item_library.h"
#include "framework/mod_fixture.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
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
    explicit Scratch(const char* name) : root(fs::path(SH_PROJECT_ROOT) / ".tmp" / "wide_numbers" / name) {
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

bool has_warning_text(const std::vector<std::string>& warnings, std::string_view part) {
    for (const std::string& w : warnings) {
        if (w.find(part) != std::string::npos) {
            return true;
        }
    }
    return false;
}

}  // namespace

// More items than the byte numbers left: the rest number from 256 on, and 255 (the empty slot) is never given.
SH_TEST(mod_items_number_past_a_byte) {
    Scratch dir("items");
    dir.write("mods/hoard/mod.json", R"({"id": "hoard", "version": "1.0.0", "gameplay": true})");
    constexpr int kItems = 300;
    for (int i = 0; i < kItems; ++i) {
        dir.write("mods/hoard/items/Trinket" + std::to_string(1000 + i) + ".json", R"({"group": "portable"})");
    }
    sh::ids::configure(repo_gamedata(), dir.root / "mods");
    std::vector<std::string> warnings;
    const auto loaded = ItemLibrary::load(repo_gamedata(), dir.root / "mods", warnings);
    SH_CHECK(loaded);
    const ItemLibrary& lib = loaded.value();
    std::set<int> numbers;
    int highest = 0;
    for (int i = 0; i < kItems; ++i) {
        const sh::items::ItemDef* item = lib.find_item("hoard:Trinket" + std::to_string(1000 + i));
        SH_CHECK(item != nullptr);
        if (item == nullptr) {
            continue;
        }
        SH_CHECK(item->number > 0 && item->number != 0xFF);
        numbers.insert(item->number);
        highest = std::max(highest, item->number);
        SH_CHECK(lib.find_item(item->number) == item);
    }
    SH_CHECK(static_cast<int>(numbers.size()) == kItems);
    SH_CHECK(highest > 0xFF);
    SH_CHECK(numbers.count(256) == 1);  // the first number past the byte
    SH_CHECK(lib.check_consistency().empty());
    SH_CHECK(warnings.empty());
    sh::ids::configure({}, {});
}

// More mod weapons than the 17 numbers below 128: the next take numbers from 130 on, rows number + 10 * input.
SH_TEST(mod_weapons_number_past_the_signed_byte) {
    Scratch dir("weapons");
    dir.write("mods/forge/mod.json", R"({"id": "forge", "version": "1.0.0", "gameplay": true})");
    constexpr int kWeapons = 20;
    for (int i = 0; i < kWeapons; ++i) {
        const std::string name = "Blade" + std::to_string(10 + i);
        dir.write("mods/forge/items/" + name + ".json", R"({"group": "melee"})");
        dir.write("mods/forge/weapons/" + name + ".json", R"({"class": "melee", "item": ")" + name + R"(",
            "attacks": [{"input": "tap", "power": 100}, {"input": "hold", "power": 150}]})");
    }
    sh::ids::configure(repo_gamedata(), dir.root / "mods");
    std::vector<std::string> warnings;
    const auto loaded = ItemLibrary::load(repo_gamedata(), dir.root / "mods", warnings);
    SH_CHECK(loaded);
    const ItemLibrary& lib = loaded.value();
    std::set<int> numbers;
    for (int i = 0; i < kWeapons; ++i) {
        const sh::items::WeaponDef* w = lib.find_weapon("forge:Blade" + std::to_string(10 + i));
        SH_CHECK(w != nullptr && w->number > 0);
        if (w == nullptr) {
            continue;
        }
        numbers.insert(w->number);
        for (const WeaponAttack& a : w->attacks) {
            SH_CHECK(a.index == w->number + static_cast<int>(a.input) * 10);
        }
        // The engine decodes every row of a mod's weapon by lookup, past the byte too.
        SH_CHECK(sh::ids::mod_attack_weapon(w->number) == w->number);
        SH_CHECK(sh::ids::mod_attack_weapon(w->number + 10) == w->number);
        SH_CHECK(!sh::ids::attack_is_gun(w->number + 10));
        SH_CHECK(lib.held_item(w->number) == lib.find_item(w->item)->number);
    }
    SH_CHECK(static_cast<int>(numbers.size()) == kWeapons);
    SH_CHECK(*numbers.rbegin() >= 130);
    SH_CHECK(!has_warning_text(warnings, "every attack slot"));
    sh::ids::configure({}, {});
}
