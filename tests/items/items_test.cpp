#include "framework/sh_test.h"

#include "engine/items/item_library.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string_view>
#include <system_error>
#include <vector>
#include "framework/mod_fixture.h"

namespace fs = std::filesystem;
using sh::items::ItemLibrary;
using sh::items::WeaponAttack;
using sh::items::WeaponDef;

namespace {

fs::path repo_gamedata() { return fs::path(SH_PROJECT_ROOT) / "gamedata"; }

// The old single files (still read, with a warning).
constexpr std::string_view kItems = R"json({
  "items": [
    {"id": 160, "enum": "InvItemId_Handgun", "group": "guns", "name": "Handgun"},
    {"id": 128, "enum": "InvItemId_KitchenKnife", "group": "melee", "name": "Kitchen_knife"},
    {"id": 192, "enum": "InvItemId_HandgunBullets", "group": "ammo", "name": "Handgun_bullets"}
  ]
})json";
constexpr std::string_view kWeapons = R"json({
  "default_pickup_count": 1,
  "ammo_pickup_counts": {"InvItemId_HandgunBullets": 15},
  "weapons": [
    {"id": 0, "enum": "EquippedWeaponId_KitchenKnife", "class": "melee", "item_id": 128,
     "item_enum": "InvItemId_KitchenKnife", "clip": null, "ammo_item_enum": null,
     "attacks": [{"input": "tap", "index": 0, "reach_q12": 1638, "reach_start_q12": 410,
                  "power": 130, "min_hit_box_q12": [2048, 0, 819], "keyframe_start": 3,
                  "keyframe_length": 3}]},
    {"id": 32, "enum": "EquippedWeaponId_Handgun", "class": "gun", "item_id": 160,
     "item_enum": "InvItemId_Handgun", "clip": 15, "ammo_item_enum": "InvItemId_HandgunBullets",
     "attacks": [{"input": "tap", "index": 32, "hit_type": 3, "power": 100, "pellets": 1}]}
  ]
})json";

// A scratch directory with write(); removes itself on destruction.
struct Scratch {
    fs::path root;
    explicit Scratch(const char* name)
        : root(fs::path(SH_PROJECT_ROOT) / ".tmp" / "items" / name) {
        fs::remove_all(root);
        fs::create_directories(root);
    }
    ~Scratch() { std::error_code ec; fs::remove_all(root, ec); }
    void write(const fs::path& relative, std::string_view text) const {
        fs::create_directories(root / relative.parent_path());
        std::ofstream out(root / relative, std::ios::binary);
        out << text;
        out.close();
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

}  // namespace

// The repository's own data must load clean: it is what every build ships.
SH_TEST(gamedata_items_load) {
    std::vector<std::string> warnings;
    const auto library = ItemLibrary::load(repo_gamedata(), warnings);
    SH_CHECK(library);
    for (const std::string& warning : warnings) {
        SH_CHECK(warning.empty());
    }
    SH_CHECK(library.value().check_consistency().empty());
    SH_CHECK(library.value().item_count() == 81);
    SH_CHECK(library.value().weapon_count() == 14);

    // Names are the identifiers; the numbers are the original's (the game's numbering).
    const sh::items::ItemDef* handgun = library.value().find_item("Handgun");
    SH_CHECK(handgun != nullptr);
    SH_CHECK(handgun->id == "silenthill:Handgun");
    SH_CHECK(handgun->number == 160);
    SH_CHECK(handgun->group == "guns");
    SH_CHECK(library.value().find_item(160) == handgun);
    SH_CHECK(library.value().find_item("silenthill:Handgun") == handgun);
    SH_CHECK(library.value().find_item("Nothing") == nullptr);
    for (const sh::items::ItemDef& item : library.value().items()) {
        SH_CHECK(item.number != 0);
    }
    for (const WeaponDef& weapon : library.value().weapons()) {
        SH_CHECK(weapon.number >= 0);
    }

    const WeaponDef* gun = library.value().find_weapon("Handgun");
    SH_CHECK(gun != nullptr);
    SH_CHECK(gun == library.value().find_weapon(32));
    SH_CHECK(gun->is_gun());
    SH_CHECK(gun->item == "silenthill:Handgun");
    SH_CHECK(gun->clip.has_value() && *gun->clip == 15);
    const WeaponAttack* shot = gun->attack(WeaponAttack::Input::Tap);
    SH_CHECK(shot != nullptr);
    SH_CHECK(shot->index == 32);
    SH_CHECK(shot->power == 100);
    SH_CHECK(gun->attack(WeaponAttack::Input::Multitap)->index == 52);

    // Melee: the kitchen knife hit window from attack table row 0.
    const WeaponDef* knife = library.value().find_weapon(0);
    SH_CHECK(knife != nullptr && knife->id == "silenthill:KitchenKnife");
    SH_CHECK(!knife->is_gun());
    SH_CHECK(!knife->clip.has_value());
    const WeaponAttack* slash = knife->attack(WeaponAttack::Input::Tap);
    SH_CHECK(slash != nullptr);
    SH_CHECK(slash->reach_q12 == 1638);        // Q12(0.4)
    SH_CHECK(slash->reach_start_q12 == 410);   // Q12(0.1)
    SH_CHECK(slash->power == 130);             // 0x0082
    SH_CHECK(slash->hit_type == WeaponAttack::kHitLightSweep);
    // Bytes 0, 8, 0, Chara_GreyChild (8), 0x33, 0x03 of the C initializer as three u16 sizes.
    SH_CHECK(slash->min_hit_box_q12(0) == 2048 && slash->min_hit_box_q12(1) == 2048);
    SH_CHECK(slash->min_hit_box_q12(2) == 819);
    SH_CHECK(slash->keyframe_start == 3 && slash->keyframe_length == 3);
    SH_CHECK(slash->hurt_sound == -1 && slash->blood_splatter == 2);

    // Kick and stomp are weapons carried as no item.
    SH_CHECK(library.value().find_weapon("Kick") != nullptr && library.value().find_weapon("Kick")->item.empty());
    SH_CHECK(library.value().find_weapon("Kick")->number == 8);

    // The shotgun's shot: 12 pellets, the ray view of the same six bytes.
    const WeaponAttack* blast = library.value().find_weapon(34)->attack(WeaponAttack::Input::Tap);
    SH_CHECK(blast != nullptr && blast->hit_type == WeaponAttack::kHitRay);
    SH_CHECK(blast->pellets() == 12 && blast->spread_q12() == 170);
    SH_CHECK(blast->spread_sectors() == 12 && blast->spread_stride() == 5);

    // Ammo pickups: 15 per handgun bullet pickup, the default otherwise.
    const sh::items::ItemDef* bullets = library.value().find_item("HandgunBullets");
    SH_CHECK(bullets != nullptr && bullets->number == 192);
    SH_CHECK(library.value().ammo_pickup_count(bullets->number) == 15);
    SH_CHECK(library.value().ammo_pickup_count(34 /* Ampoule */) == 1);
    SH_CHECK(library.value().ammo_item_of(*gun) == bullets);

    // Every weapon's item exists.
    for (const WeaponDef& weapon : library.value().weapons()) {
        if (!weapon.item.empty()) {
            SH_CHECK(library.value().find_item(weapon.item) != nullptr);
        }
    }
}

// The files name nothing by number: no "id", "enum", "index".
SH_TEST(gamedata_items_have_no_numbers) {
    for (const char* dir : {"items", "weapons"}) {
        for (const auto& entry : fs::directory_iterator(repo_gamedata() / dir)) {
            std::ifstream in(entry.path(), std::ios::binary);
            const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            SH_CHECK(text.find("\"id\"") == std::string::npos);
            SH_CHECK(text.find("\"enum\"") == std::string::npos);
            SH_CHECK(text.find("\"index\"") == std::string::npos);
            SH_CHECK(text.find("_id\"") == std::string::npos);
        }
    }
    SH_CHECK(!fs::exists(repo_gamedata() / "items" / "items.json"));
    SH_CHECK(!fs::exists(repo_gamedata() / "items" / "weapons.json"));
}

SH_TEST(parse_and_lookup) {
    std::vector<std::string> warnings;
    const auto library = ItemLibrary::parse(kItems, kWeapons, warnings);
    SH_CHECK(library);
    SH_CHECK(warnings.empty());
    SH_CHECK(library.value().item_count() == 3);
    SH_CHECK(library.value().find_item(160) != nullptr);
    SH_CHECK(library.value().find_item(99) == nullptr);
    SH_CHECK(library.value().find_item("Handgun") == library.value().find_item(160));
    SH_CHECK(library.value().find_item(192)->pickup_count == 15);
    SH_CHECK(library.value().find_weapon(0) != nullptr);
    SH_CHECK(library.value().find_weapon(0)->item == "silenthill:KitchenKnife");
    SH_CHECK(library.value().find_weapon(32)->ammo == "silenthill:HandgunBullets");
    SH_CHECK(library.value().find_weapon(9) == nullptr);
    SH_CHECK(library.value().check_consistency().empty());
}

// Files from before the members were named (the unnamed per-byte keys) load without those keys, with
// one warning for the whole file.
SH_TEST(parse_legacy_keys) {
    constexpr std::string_view kOld = R"json({
  "weapons": [
    {"id": 0, "class": "melee", "item_id": 128, "attacks": [
      {"input": "tap", "index": 0, "power": 130, "chara": 8, "keyframe_start": 3, "keyframe_length": 3},
      {"input": "hold", "index": 10, "power": 120, "chara": 4}]}
  ]
})json";
    std::vector<std::string> warnings;
    const auto library = ItemLibrary::parse(kItems, kOld, warnings);
    SH_CHECK(library);
    SH_CHECK(warnings.size() == 1);
    SH_CHECK(!warnings.empty() && warnings[0].find("old unnamed keys") != std::string::npos);
    const WeaponAttack* tap = library.value().find_weapon(0)->attack(WeaponAttack::Input::Tap);
    SH_CHECK(tap->power == 130 && tap->keyframe_start == 3);
    SH_CHECK((tap->shape == std::array<std::uint8_t, 6>{}));
}

// The shipped weapons hold the same bytes as the original table.
SH_TEST(gamedata_weapons_match_original_bytes) {
    std::vector<std::string> warnings;
    const auto library = ItemLibrary::load(repo_gamedata(), warnings);
    SH_CHECK(library);
    // Row 1 (steel pipe tap): 102, 6, 0, Chara_GreyChild (8), 0xCC, 0x04.
    const WeaponAttack* pipe = library.value().find_weapon(1)->attack(WeaponAttack::Input::Tap);
    SH_CHECK(pipe != nullptr);
    SH_CHECK((pipe->shape == std::array<std::uint8_t, 6>{102, 6, 0, 8, 0xCC, 0x04}));
    SH_CHECK(pipe->knockback_q12 == 4096 && pipe->hit_type == WeaponAttack::kHitHeavySweep);
    // Row 32 (handgun tap): 0, 0, 0, 1, 0x44, 0x00 -- one ray, spread 68.
    const WeaponAttack* shot = library.value().find_weapon(32)->attack(WeaponAttack::Input::Tap);
    SH_CHECK((shot->shape == std::array<std::uint8_t, 6>{0, 0, 0, 1, 0x44, 0}));
    SH_CHECK(shot->reach_q12 == 49152);
}

SH_TEST(parse_errors) {
    std::vector<std::string> warnings;
    SH_CHECK(!ItemLibrary::parse("{", kWeapons, warnings));
    SH_CHECK(!ItemLibrary::parse(kItems, "{}", warnings));                  // no weapons array
    SH_CHECK(!ItemLibrary::parse(R"({"items": [{"group": "x"}]})", kWeapons, warnings));  // no id
    SH_CHECK(!ItemLibrary::parse(R"({"items": [{}]})", kWeapons, warnings));
    // An unknown attack input names the place in the file.
    const auto bad = ItemLibrary::parse(kItems, R"json({
        "weapons": [{"id": 0, "class": "melee", "attacks": [{"index": 0, "input": "combo"}]}]})json",
                                        warnings);
    SH_CHECK(!bad);
    SH_CHECK(bad.error().find("combo") != std::string::npos);
}

SH_TEST(consistency_problems) {
    std::vector<std::string> warnings;
    // A weapon attack whose index disagrees with its input (hand-edited old data).
    const auto wrong_index = ItemLibrary::parse(kItems, R"({
        "weapons": [{"id": 0, "class": "melee", "item_id": 128,
                     "attacks": [{"input": "tap", "index": 7}]}]})",
                                       warnings);
    SH_CHECK(wrong_index);
    SH_CHECK(!wrong_index.value().check_consistency().empty());
    // A weapon carried as an item the table does not have.
    const auto unknown_item = ItemLibrary::parse(kItems, R"({
        "weapons": [{"id": 0, "class": "melee", "item_enum": "InvItemId_Missing", "attacks": []}]})",
                                        warnings);
    SH_CHECK(unknown_item);
    SH_CHECK(!unknown_item.value().check_consistency().empty());
    // The same number twice.
    const auto duplicate = ItemLibrary::parse(R"({
        "items": [{"id": 32, "enum": "InvItemId_A"}, {"id": 32, "enum": "InvItemId_B"}]})",
                                              kWeapons, warnings);
    SH_CHECK(duplicate);
    SH_CHECK(!duplicate.value().check_consistency().empty());
}

namespace {

void write_base(const Scratch& dir) {
    dir.write("base/items/Handgun.json", R"({"group": "guns"})");
    dir.write("base/items/KitchenKnife.json", R"({"group": "melee"})");
    dir.write("base/items/HandgunBullets.json", R"({"group": "ammo", "pickup_count": 15})");
    dir.write("base/weapons/KitchenKnife.json", R"json({"class": "melee", "item": "KitchenKnife",
        "attacks": [{"input": "tap", "power": 130}]})json");
    dir.write("base/weapons/Handgun.json", R"json({"class": "gun", "item": "Handgun", "clip": 15,
        "ammo": "HandgunBullets", "attacks": [{"input": "tap", "hit_type": 3, "power": 100, "pellets": 1}]})json");
}

}  // namespace

// One file per thing; a mod patches base things by name and adds its own under its namespace.
SH_TEST(mod_items_by_name) {
    Scratch dir("mod-names");
    write_base(dir);
    dir.write("mods/plain/mod.json", R"({"id": "plain", "version": "1.0.0"})");

    std::vector<std::string> warnings;
    sh::items::LoadSource source;
    auto library = ItemLibrary::load(dir.root / "base", dir.root / "mods", warnings, &source);
    SH_CHECK(library);
    SH_CHECK(warnings.empty());
    SH_CHECK(source.mod_id.empty());
    SH_CHECK(library.value().find_item("Handgun")->number == 160);
    SH_CHECK(library.value().find_weapon("Handgun")->number == 32);
    SH_CHECK(library.value().find_weapon("Handgun")->attack(WeaponAttack::Input::Tap)->index == 32);

    // A patch of a base item (one field), a new item of the mod, a patch of a weapon's attack.
    dir.write("mods/plain/items/HandgunBullets.json", R"({"pickup_count": 30})");
    dir.write("mods/plain/items/Crowbar.json", R"({"group": "puzzle"})");
    dir.write("mods/plain/weapons/Handgun.json", R"({"attacks": [{"input": "tap", "power": 150}]})");
    dir.write("mods/plain/items/other/Thing.json", R"({"group": "keys"})");  // no other:Thing to change
    warnings.clear();
    source = {};
    library = ItemLibrary::load(dir.root / "base", dir.root / "mods", warnings, &source);
    SH_CHECK(library);
    SH_CHECK(source.mod_id == "plain");
    SH_CHECK(has_warning(warnings, "\"gameplay\": true"));
    SH_CHECK(has_warning(warnings, "other:Thing"));
    const auto& lib = library.value();
    SH_CHECK(lib.find_item("HandgunBullets")->pickup_count == 30);
    SH_CHECK(lib.find_item("HandgunBullets")->group == "ammo");
    const sh::items::ItemDef* crowbar = lib.find_item("plain:Crowbar");
    SH_CHECK(crowbar != nullptr);
    SH_CHECK(lib.find_item("Crowbar") == crowbar);  // the one namespace that has it
    SH_CHECK(crowbar->number == 111);  // the only free puzzle number of the old inventory
    SH_CHECK(lib.find_item(111) == crowbar);
    const WeaponAttack* tap = lib.find_weapon("Handgun")->attack(WeaponAttack::Input::Tap);
    SH_CHECK(tap->power == 150 && tap->hit_type == 3 && tap->pellets() == 1);  // the rest stays
    SH_CHECK(lib.check_consistency().empty());

    // A second mod writes the first one's item by its full name.
    dir.write("mods/other/mod.json", R"({"id": "other", "version": "1.0.0", "priority": 5, "gameplay": true})");
    dir.write("mods/other/items/plain/Crowbar.json", R"({"model": "models/ITEM/CROWBAR.glb"})");
    warnings.clear();
    library = ItemLibrary::load(dir.root / "base", dir.root / "mods", warnings, &source);
    SH_CHECK(library);
    SH_CHECK(library.value().find_item("plain:Crowbar")->model_file == "models/ITEM/CROWBAR.glb");

    // Numbers in a file of the new layout are ignored with a warning.
    dir.write("mods/plain/items/Crowbar.json", R"({"id": 200, "group": "puzzle"})");
    warnings.clear();
    library = ItemLibrary::load(dir.root / "base", dir.root / "mods", warnings, &source);
    SH_CHECK(library);
    SH_CHECK(has_warning(warnings, "\"id\" is ignored"));
    SH_CHECK(library.value().find_item("plain:Crowbar")->number == 111);
}

// The old single files of a mod still replace the set, with a warning.
SH_TEST(mod_replaces_items_file) {
    Scratch dir("mod-layers");
    write_base(dir);
    dir.write("mods/plain/mod.json", R"({"id": "plain", "version": "1.0.0", "gameplay": true})");
    dir.write("mods/plain/items/items.json", R"json({
        "items": [{"id": 160, "enum": "InvItemId_Handgun", "group": "guns"}]})json");
    std::vector<std::string> warnings;
    sh::items::LoadSource source;
    auto library = ItemLibrary::load(dir.root / "base", dir.root / "mods", warnings, &source);
    SH_CHECK(library);
    SH_CHECK(source.legacy);
    SH_CHECK(has_warning(warnings, "old single-file layout"));
    SH_CHECK(!has_warning(warnings, "\"gameplay\": true"));
    SH_CHECK(library.value().item_count() == 1);
    SH_CHECK(library.value().weapon_count() == 2);  // weapons are still the base's
    SH_CHECK(!ItemLibrary::load(dir.root / "nope", dir.root / "mods", warnings));

    // An old base (items/items.json + weapons.json, no folders) loads with a warning.
    Scratch old("old-base");
    old.write("base/items/items.json", kItems);
    old.write("base/items/weapons.json", kWeapons);
    warnings.clear();
    library = ItemLibrary::load(old.root / "base", warnings);
    SH_CHECK(library);
    SH_CHECK(has_warning(warnings, "old single-file layout"));
    SH_CHECK(library.value().find_item("Handgun")->number == 160);
}
