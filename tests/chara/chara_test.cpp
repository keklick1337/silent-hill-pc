#include "framework/sh_test.h"

#include "engine/chara/chara_library.h"
#include "engine/chara/game/chara_file_table.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include "framework/mod_fixture.h"

namespace fs = std::filesystem;
using sh::chara::CharaDef;
using sh::chara::CharaLibrary;

namespace {

fs::path repo_gamedata() { return fs::path(SH_PROJECT_ROOT) / "gamedata"; }

struct Scratch {
    fs::path root;
    explicit Scratch(const char* name) : root(fs::path(SH_PROJECT_ROOT) / ".tmp" / "chara" / name) {
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

constexpr std::string_view kTwo = R"json({
  "characters": [
    {"id": 1, "enum": "Chara_Harry", "animations": "anim/ANIM/HB_BASE.anim.json",
     "model": "models/CHARA/HERO.glb", "textures": ["textures/CHARA/HERO.png"], "shadow": 0.25,
     "blend_mode": "additive", "camera_anchor": "character", "camera_offset_y": -0.7},
    {"id": 8, "enum": "Chara_GreyChild", "animations": "anim/ANIM/CLD2.anim.json",
     "model": "models/CHARA/CLD3.glb", "textures": ["textures/CHARA/CLD3.png"], "blend_mode": "additive"}
  ]
})json";

bool has_warning(const std::vector<std::string>& warnings, std::string_view part) {
    for (const std::string& w : warnings) {
        if (w.find(part) != std::string::npos) {
            return true;
        }
    }
    return false;
}

}  // namespace

// The repository's own data must load clean and match the original table.
SH_TEST(gamedata_chara_load) {
    std::vector<std::string> warnings;
    const auto library = CharaLibrary::load(repo_gamedata(), {}, warnings);
    SH_CHECK(library);
    for (const std::string& warning : warnings) {
        SH_CHECK(warning.empty());
    }
    SH_CHECK(library.value().check_consistency().empty());
    SH_CHECK(library.value().size() == 45);

    // Spot values of the character files the engine table is built from.
    const CharaDef* harry = library.value().find(1);
    SH_CHECK(harry != nullptr);
    SH_CHECK(harry->id == "silenthill:Harry");
    SH_CHECK(harry->folder == "Harry");
    SH_CHECK(harry->animations == "anim/ANIM/HB_BASE.anim.json");
    SH_CHECK(harry->model == "models/CHARA/HERO.glb");
    SH_CHECK(harry->textures == std::vector<std::string>{"textures/CHARA/HERO.png"});
    SH_CHECK(harry->disc_model.empty() && harry->disc_anim.empty() && harry->disc_texture.empty());
    SH_CHECK(harry->description.starts_with("Harry Mason"));
    SH_CHECK(harry->maps.size() == 43);
    SH_CHECK(harry->blend_mode == "additive");
    SH_CHECK(harry->camera_anchor == "character");
    SH_CHECK(harry->camera_offset_y == -0.7);
    SH_CHECK(harry->shadow == 0.25);

    const CharaDef* romper = library.value().find_by_id("Romper");
    SH_CHECK(romper != nullptr);
    SH_CHECK(romper->camera_anchor == "ground");
    SH_CHECK(romper->shadow == 0.3);
    SH_CHECK(romper->maps == (std::vector<std::string>{"map2_s02", "map4_s02", "map5_s01", "map6_s00"}));

    // Every character says who it is.
    for (const CharaDef& c : library.value().characters()) {
        SH_CHECK(!c.description.empty());
    }

    // ids cover the table densely: 0..44.
    for (int id = 0; id < 45; ++id) {
        SH_CHECK(library.value().find((std::uint8_t)id) != nullptr);
    }
}

SH_TEST(parse_and_lookup) {
    std::vector<std::string> warnings;
    const auto library = CharaLibrary::parse(kTwo, warnings);
    SH_CHECK(library);
    SH_CHECK(warnings.empty());
    SH_CHECK(library.value().size() == 2);
    SH_CHECK(library.value().find(8) != nullptr);
    SH_CHECK(library.value().find(7) == nullptr);
    SH_CHECK(library.value().find_by_id("Harry") == library.value().find(1));
    SH_CHECK(library.value().find_by_id("silenthill:Harry") == library.value().find(1));
    SH_CHECK(library.value().find_id("Chara_GreyChild") == 8 && library.value().find_id("greychild") == 8);
    // A missing optional stays default, a consistency check catches bad enums.
    SH_CHECK(library.value().check_consistency().empty());
}

SH_TEST(parse_and_consistency_errors) {
    std::vector<std::string> warnings;
    SH_CHECK(!CharaLibrary::parse("{", warnings));
    SH_CHECK(!CharaLibrary::parse(R"json({"entries": []})json", warnings));  // wrong key
    SH_CHECK(!CharaLibrary::parse(R"json({"characters": [{"enum": "Chara_X"}]})json", warnings));  // no id

    const auto bad_blend = CharaLibrary::parse(R"json({
        "characters": [{"id": 1, "blend_mode": "sparkly"}]})json",
                                                warnings);
    SH_CHECK(bad_blend);
    SH_CHECK(!bad_blend.value().check_consistency().empty());
    const auto bad_path = CharaLibrary::parse(R"json({
        "characters": [{"id": 1, "model": "HERO"}]})json",
                                              warnings);
    SH_CHECK(bad_path);
    SH_CHECK(!bad_path.value().check_consistency().empty());
    const auto bad_converted = CharaLibrary::parse(R"json({
        "characters": [{"id": 1, "model": "/abs/HERO.glb", "animations": "anim/HERO.ANM",
                        "textures": ["../HERO.png"]}]})json",
                                                   warnings);
    SH_CHECK(bad_converted);
    SH_CHECK(bad_converted.value().check_consistency().size() == 3);
    SH_CHECK(!CharaLibrary::parse(R"json({"characters": [{"id": 1, "textures": [1]}]})json", warnings));
}

// "spawn": the first behaviour step, what the character is and what "auto" runs off its maps.
SH_TEST(chara_spawn_section) {
    std::vector<std::string> warnings;
    const auto library = CharaLibrary::parse(R"json({"characters": [
        {"id": 9, "enum": "Chara_Mumbler", "spawn": {"state": 3, "kind": "enemy", "auto": "full", "note": "n"}},
        {"id": 26, "enum": "Chara_Cybil", "spawn": {"kind": "actor", "auto": "fallback"}},
        {"id": 13, "enum": "Chara_Chicken"}]})json",
                                             warnings);
    SH_CHECK(library);
    const CharaDef* mumbler = library.value().find(9);
    SH_CHECK(mumbler->spawn_state == 3 && mumbler->spawn_kind == "enemy" && mumbler->spawn_auto == "full");
    SH_CHECK(mumbler->spawn_note == "n");
    SH_CHECK(library.value().find(26)->spawn_state == -1 && library.value().find(26)->spawn_auto == "fallback");
    SH_CHECK(library.value().find(13)->spawn_kind.empty() && library.value().find(13)->spawn_state == -1);
    SH_CHECK(library.value().check_consistency().empty());

    const auto bad = CharaLibrary::parse(R"json({"characters": [{"id": 9, "spawn": {"kind": "ghost", "auto": "half"}}]})json",
                                         warnings);
    SH_CHECK(bad);
    SH_CHECK(bad.value().check_consistency().size() == 2);
    SH_CHECK(!CharaLibrary::parse(R"json({"characters": [{"id": 9, "spawn": {"state": -2}}]})json", warnings));

    // The base data: every spawnable character says how a spawn starts it and Mumbler is not a corpse.
    const auto base = CharaLibrary::load(repo_gamedata(), {}, warnings);
    SH_CHECK(base);
    for (int id = 2; id <= 43; ++id) {
        const CharaDef* c = base.value().find(static_cast<std::uint8_t>(id));
        SH_CHECK(c != nullptr && !c->spawn_kind.empty() && !c->spawn_auto.empty() && c->spawn_state >= 0);
    }
    SH_CHECK(base.value().find_by_id("Mumbler")->spawn_state == 3);
    // The prologue's copy of the stalker behaviour has only the alley chase (step 6).
    for (const char* name : {"Stalker", "GreyChild", "Mumbler"}) {
        const CharaDef* c = base.value().find_by_id(name);
        SH_CHECK(c != nullptr && c->spawn_map_states.count("map0_s00") == 1 && c->spawn_map_states.at("map0_s00") == 6);
    }
    // The two placeholders of the original are not spawnable.
    SH_CHECK(!base.value().find_by_id("DummyNurse")->pooled() && !base.value().find_by_id("DummyDoctor")->pooled());

    // "spawn.map_state" and "preview" parse; a bad value or an unknown preview key fails the file.
    const auto staged = CharaLibrary::parse(
        R"json({"characters": [{"id": 9, "spawn": {"state": 3, "map_state": {"map0_s00": 6}},
                                "preview": {"rotate": [-90, 0, 12.5], "zoom": 1.5, "lift": 0.1}}]})json",
        warnings);
    SH_CHECK(staged);
    if (staged) {
        const CharaDef* c = staged.value().find(9);
        SH_CHECK(c->spawn_state == 3 && c->spawn_map_states.at("map0_s00") == 6);
        SH_CHECK(c->preview_rotate[0] == -90.0 && c->preview_rotate[1] == 0.0 && c->preview_rotate[2] == 12.5);
        SH_CHECK(c->preview_zoom == 1.5 && c->preview_lift == 0.1);
    }
    SH_CHECK(!CharaLibrary::parse(R"json({"characters": [{"id": 9, "spawn": {"map_state": {"map0_s00": 300}}}]})json",
                                  warnings));
    SH_CHECK(!CharaLibrary::parse(R"json({"characters": [{"id": 9, "preview": {"rotate": [1, 2]}}]})json", warnings));
    SH_CHECK(!CharaLibrary::parse(R"json({"characters": [{"id": 9, "preview": {"scale": 2}}]})json", warnings));
}

// The old disc-path keys of older mods still load, with a warning; the converted keys win
// when given later, and the other way round.
SH_TEST(old_file_keys_warn) {
    std::vector<std::string> warnings;
    auto library = CharaLibrary::parse(R"json({"characters": [
        {"id": 1, "anim": "ANIM/HB_BASE.ANM", "model": "CHARA/HERO.ILM", "texture": "CHARA/HERO.TIM"}]})json",
                                       warnings);
    SH_CHECK(library);
    SH_CHECK(warnings.size() == 3);
    SH_CHECK(has_warning(warnings, "\"anim\" is the old disc-path key"));
    const CharaDef* harry = library.value().find(1);
    SH_CHECK(harry->disc_anim == "ANIM/HB_BASE.ANM" && harry->disc_model == "CHARA/HERO.ILM" &&
             harry->disc_texture == "CHARA/HERO.TIM");
    SH_CHECK(harry->model.empty() && harry->animations.empty() && harry->textures.empty());
    SH_CHECK(library.value().check_consistency().empty());

    warnings.clear();
    auto patched = library.value().apply_patch(R"json({"id": 1, "model": "models/CHARA/BD2.glb"})json", "p",
                                               {}, &warnings);
    SH_CHECK(patched && warnings.empty());
    harry = library.value().find(1);
    SH_CHECK(harry->model == "models/CHARA/BD2.glb" && harry->disc_model.empty());
    SH_CHECK(harry->disc_anim == "ANIM/HB_BASE.ANM");  // untouched
    patched = library.value().apply_patch(R"json({"id": 1, "texture": "CHARA/BD2.TIM"})json", "p", {}, &warnings);
    SH_CHECK(patched && has_warning(warnings, "p: \"texture\""));
}

// Converted file -> the disc file it came from: index.tsv, else the manifest next to the file.
SH_TEST(converted_sources) {
    const auto index = sh::chara::ConvertedSources::from_index(
        "path\tsize\tcrc32\tsource\tformat\tconverted\n"
        "CHARA/HERO.ILM\t14592\t2D30905C\tSILENT:id=1422\tilm\tmodels/CHARA/HERO.glb;models/CHARA/HERO.model.json\n"
        "CHARA/HERO.TIM\t25088\t663A8D4E\tSILENT:id=1423\ttim\ttextures/CHARA/HERO.png;textures/CHARA/HERO.p01.png\n"
        "SYSTEM.CNF\t68\t1F1A87CC\tISO:file\tcnf\t\n");
    SH_CHECK(index.size() == 4);
    SH_CHECK(index.disc_path("models/CHARA/HERO.glb") == "CHARA/HERO.ILM");
    SH_CHECK(index.disc_path("MODELS/chara/hero.GLB") == "CHARA/HERO.ILM");
    SH_CHECK(index.disc_path("textures/CHARA/HERO.p01.png") == "CHARA/HERO.TIM");
    SH_CHECK(index.disc_path("models/CHARA/NONE.glb").empty());
    SH_CHECK(sh::chara::ConvertedSources::from_index("garbage").size() == 0);

    Scratch dir("sources");
    dir.write("gd/convert/models/CHARA/NEW.model.json", R"json({"source": {"path": "CHARA/NEW.ILM"}})json");
    dir.write("gd/convert/anim/ANIM/NEW.anim.json", R"json({"source": {"path": "ANIM/NEW.ANM"}})json");
    dir.write("gd/convert/textures/CHARA/NEW.tim.json", R"json({"game_path": "CHARA/NEW.TIM"})json");
    const auto sources = sh::chara::ConvertedSources::load(dir.root / "gd");  // no index.tsv
    SH_CHECK(sources.disc_path("models/CHARA/NEW.glb") == "CHARA/NEW.ILM");
    SH_CHECK(sources.disc_path("anim/ANIM/NEW.anim.json") == "ANIM/NEW.ANM");
    SH_CHECK(sources.disc_path("textures/CHARA/NEW.png") == "CHARA/NEW.TIM");
    SH_CHECK(sources.disc_path("textures/CHARA/NEW.p12.png") == "CHARA/NEW.TIM");
    SH_CHECK(sources.disc_path("textures/CHARA/NEW.alt1.png") == "CHARA/NEW.TIM");
    SH_CHECK(sources.disc_path("textures/CHARA/OTHER.png").empty());
}

// Which glb a character is drawn from: the one its character.json names while the type loads that model's skeleton
// file (a mod's "model": "models/<mod>/HERO.glb" with a manifest naming CHARA/HERO.ILM), else the loaded file's own.
SH_TEST(drawn_model_glb_follows_the_data) {
    using sh::chara::drawn_model_glb;
    // The base data: the import's glb either way.
    SH_CHECK(drawn_model_glb("models/CHARA/HERO.glb", 1422, 1422, "CHARA/HERO.ILM") == "CHARA/HERO.glb");
    // A mod's character.json points the character at a glb of another mod with the same skeleton file.
    SH_CHECK(drawn_model_glb("models/cj_model/HERO.glb", 1422, 1422, "CHARA/HERO.ILM") == "cj_model/HERO.glb");
    SH_CHECK(drawn_model_glb("models/cj_model/hero.GLB", 1422, 1422, "CHARA/HERO.ILM") == "cj_model/hero.GLB");
    // The game moved the type to another file (a region's model, the pool's beta model): that file's glb.
    SH_CHECK(drawn_model_glb("models/cj_model/CLD3.glb", 1410, 1411, "CHARA/CLD4.ILM") == "CHARA/CLD4.glb");
    SH_CHECK(drawn_model_glb("models/CHARA/DUMMY.glb", 1415, 2040, "TEST/PRS2.ILM") == "TEST/PRS2.glb");
    // Data without a converted model (an old disc path, nothing): the loaded file's glb.
    SH_CHECK(drawn_model_glb("CHARA/HERO.ILM", 1422, 1422, "CHARA/HERO.ILM") == "CHARA/HERO.glb");
    SH_CHECK(drawn_model_glb("", -1, 1422, "CHARA/HERO.ILM") == "CHARA/HERO.glb");
    SH_CHECK(drawn_model_glb("models/x/A.glb", -1, -1, "").empty());  // the manifest named no file of the disc
    SH_CHECK(drawn_model_glb("", -1, 7, "DIR.NAME/FILE") == "DIR.NAME/FILE.glb");

    // The manifest next to a mod's glb is what ties it to the skeleton file, whichever mod holds it.
    Scratch dir("drawn");
    dir.write("gd/index.tsv", "path\tsize\tcrc32\tsource\tformat\tconverted\n"
                              "CHARA/HERO.ILM\t1\t0\tSILENT:id=1\tilm\tmodels/CHARA/HERO.glb\n");
    dir.write("mods/cj_model/convert/models/cj_model/HERO.model.json", R"json({"source": {"path": "CHARA/HERO.ILM"}})json");
    const auto sources = sh::chara::ConvertedSources::load(dir.root / "gd", {dir.root / "mods/cj_model/convert"});
    SH_CHECK(sources.disc_path("models/cj_model/HERO.glb") == sources.disc_path("models/CHARA/HERO.glb"));
}

// A character patch of the look alone needs no "gameplay": true; anything else does.
SH_TEST(look_only_character_patch) {
    using sh::chara::patch_changes_look_only;
    SH_CHECK(patch_changes_look_only(R"json({"model": "models/cj_model/DOG.glb"})json"));
    SH_CHECK(patch_changes_look_only(R"json({"description": "x", "_note": 1, "textures": ["textures/a.png"], "shadow": 0.5})json"));
    SH_CHECK(!patch_changes_look_only(R"json({"model": "models/a.glb", "params": {"health_base": 1.0}})json"));
    SH_CHECK(!patch_changes_look_only(R"json({"animations": "anim/ANIM/DOG.anim.json"})json"));
    SH_CHECK(!patch_changes_look_only("[1, 2]"));
    SH_CHECK(!patch_changes_look_only("{broken"));

    Scratch dir("look");
    dir.write("base/chara/Harry/character.json", R"json({"model": "models/CHARA/HERO.glb", "params": {"a": 1.0}})json");
    dir.write("mods/skin/mod.json", R"({"id": "skin", "version": "1.0.0"})");
    dir.write("mods/skin/chara/Harry/character.json", R"json({"model": "models/skin/HERO.glb"})json");
    dir.write("mods/rules/mod.json", R"({"id": "rules", "version": "1.0.0"})");
    dir.write("mods/rules/chara/Harry/character.json", R"json({"params": {"a": 2.0}})json");
    std::vector<std::string> warnings;
    auto library = CharaLibrary::load(dir.root / "base", dir.root / "mods", warnings);
    SH_CHECK(library);
    SH_CHECK(!has_warning(warnings, "mod skin patches character data"));
    SH_CHECK(has_warning(warnings, "mod rules patches character data"));
    if (library) {
        const auto* harry = library.value().find_by_id("Harry");
        SH_CHECK(harry != nullptr && harry->model == "models/skin/HERO.glb");
    }
}

SH_TEST(mod_replaces_characters_file) {
    Scratch dir("mod");
    dir.write("base/chara/characters.json", kTwo);
    dir.write("mods/plain/mod.json", R"({"id": "plain", "version": "1.0.0"})");
    dir.write("mods/plain/chara/characters.json", R"json({
        "characters": [{"id": 1, "enum": "Chara_Harry", "model": "CHARA/BD2.ILM"}]})json");

    // The mod's file replaces the base one whole; without "gameplay": true there is a warning.
    std::vector<std::string> warnings;
    sh::chara::LoadSource source;
    auto library = CharaLibrary::load(dir.root / "base", dir.root / "mods", warnings, &source);
    SH_CHECK(library);
    SH_CHECK(source.mod_id == "plain");
    SH_CHECK(!source.mod_gameplay);
    SH_CHECK(has_warning(warnings, "\"gameplay\": true"));
    SH_CHECK(library.value().size() == 1);
    SH_CHECK(library.value().find(1)->disc_model == "CHARA/BD2.ILM");
    SH_CHECK(has_warning(warnings, "old disc-path key"));

    dir.write("mods/plain/mod.json", R"({"id": "plain", "version": "1.0.0", "gameplay": true})");
    warnings.clear();
    library = CharaLibrary::load(dir.root / "base", dir.root / "mods", warnings, &source);
    SH_CHECK(library);
    SH_CHECK(!has_warning(warnings, "\"gameplay\": true"));

    // No mod file anywhere: the base loads.
    dir.write("mods/plain/mod.json", R"({"id": "plain", "version": "1.0.0"})");
    fs::remove(dir.root / "mods" / "plain" / "chara" / "characters.json");
    warnings.clear();
    source = {};
    library = CharaLibrary::load(dir.root / "base", dir.root / "mods", warnings, &source);
    SH_CHECK(library);
    SH_CHECK(source.mod_id.empty());
    SH_CHECK(library.value().size() == 2);
}

SH_TEST(sounds_and_params) {
    Scratch dir("sounds");

    dir.write("base/chara/characters.json", R"json({
  "characters": [
    {"id": 1, "enum": "Chara_Harry", "anim": "ANIM/HB_BASE.ANM", "model": "CHARA/HERO.ILM",
     "sounds": {"footstep": 1311, "footstep_grass": 1312, "footstep_metal": 1332}},
    {"id": 4, "enum": "Chara_Groaner", "model": "CHARA/GRN.ILM",
     "sounds": {"footstep": 1413, "death": 1414, "damage": 1409, "idle": 1406},
     "params": {"health_max": 280.0, "scale": 1.35, "rand": 100},
     "anims": {"walk_forward": 17}}
  ]
})json");

    std::vector<std::string> warnings;
    auto library = CharaLibrary::load(dir.root / "base", dir.root / "mods", warnings);
    SH_CHECK(library);
    SH_CHECK(library.value().check_consistency().empty());

    // The enum slots map onto the named ones.
    SH_CHECK(library.value().sfx(1, PC_CHARA_SFX_FOOTSTEP) == 1311);
    SH_CHECK(library.value().sfx(1, PC_CHARA_SFX_FOOTSTEP_GRASS) == 1312);
    SH_CHECK(library.value().sfx(1, PC_CHARA_SFX_FOOTSTEP_METAL) == 1332);
    SH_CHECK(library.value().sfx(1, PC_CHARA_SFX_DEATH) == -1);  // left out: unset
    SH_CHECK(library.value().sfx(4, PC_CHARA_SFX_DEATH) == 1414);
    SH_CHECK(library.value().sfx(4, "idle") == 1406);

    // Character: every lookup has the caller's fallback.
    const sh::chara::Character groaner = library.value().character(4);
    SH_CHECK(groaner.valid());
    SH_CHECK(groaner.sfx("damage", 7) == 1409);
    SH_CHECK(groaner.sfx("roar", 7) == 7);
    SH_CHECK(groaner.param("health_max", -1.0) == 280.0);
    SH_CHECK(groaner.param("move_speed", -1.0) == -1.0);
    SH_CHECK(groaner.param_q12("health_max", 0) == 280 * 4096);
    // Q12(1.35f) of the original: float multiply, truncation -> 5529.
    SH_CHECK(groaner.param_q12("scale", 0) == 5529);
    SH_CHECK(groaner.param_int("rand", 0) == 100);
    SH_CHECK(groaner.anim("walk_forward") == 17);
    SH_CHECK(groaner.anim("fly", 3) == 3);

    // Unknown character or bad slot: no value, the caller keeps its original.
    SH_CHECK(library.value().sfx(99, PC_CHARA_SFX_FOOTSTEP) == -1);
    SH_CHECK(library.value().sfx(1, (Pc_CharaSfxSlot)99) == -1);
    SH_CHECK(!library.value().character(99).valid());
    SH_CHECK(library.value().character(99).sfx("footstep", 1311) == 1311);
    SH_CHECK(library.value().character(99).param_q12("health", 5) == 5);
}

// The shipped data equals the original's hard-coded values; these spot checks pin a few.
SH_TEST(gamedata_chara_original_values) {
    std::vector<std::string> warnings;
    const auto library = CharaLibrary::load(repo_gamedata(), {}, warnings);
    SH_CHECK(library);
    const sh::chara::Character harry = library.value().character(1);
    SH_CHECK(harry.footstep("concrete") == 1311);   // Sfx_FootstepConcrete (materials 0/1/7)
    SH_CHECK(harry.footstep("grass") == 1312);      // Sfx_FootstepGrass (3)
    SH_CHECK(harry.footstep("metal_grating") == 1332);  // Sfx_FootstepMetal (6/10/11)
    SH_CHECK(harry.footstep("wood") == 1330);       // material 8
    SH_CHECK(harry.footstep("no_such_surface") == 1311);  // "*"
    SH_CHECK(harry.sfx("damage") == 1326);
    SH_CHECK(harry.anim("walk_forward") == 2);      // HarryAnim_WalkForward

    const sh::chara::Character groaner = library.value().character(4);
    SH_CHECK(groaner.sfx("footstep") == 1413);      // Sfx_GroanerFootstep
    SH_CHECK(groaner.sfx("damage") == 1409);
    SH_CHECK(groaner.sfx("death") == 1414);
    SH_CHECK(groaner.param_q12("health_base", 0) == 210 * 4096);
    SH_CHECK(groaner.param_q12("health_bonus_max", 0) == 70 * 4096);
    SH_CHECK(groaner.anim("jump_attack") == 10);    // GroanerAnim_JumpAttack

    SH_CHECK(library.value().character(12).param_q12("health_bonus_max", 0) == 460800);  // Romper Q12(112.5f)
    SH_CHECK(library.value().character(10).param_q12("health_hard_scale", 0) == 5529);   // Q12(1.35f)
    SH_CHECK(library.value().character(28).sfx("footstep") == 1353);                     // Sfx_CherylFootstep
    // The Air Screamer code is shared by the Night Flutter: both entries, own health bonus.
    SH_CHECK(library.value().character(2).param_int("health_rand", 0) == 100);
    SH_CHECK(library.value().character(3).param_int("health_rand", 0) == 300);
}

SH_TEST(find_id_names) {
    std::vector<std::string> warnings;
    const auto library = CharaLibrary::load(repo_gamedata(), {}, warnings);
    SH_CHECK(library);
    SH_CHECK(library.value().find_id("Chara_Cheryl") == 28);
    SH_CHECK(library.value().find_id("Cheryl") == 28);
    SH_CHECK(library.value().find_id("cheryl") == 28);
    SH_CHECK(library.value().find_id("hanged_scratcher") == 10);
    SH_CHECK(library.value().find_id("28") == 28);
    SH_CHECK(library.value().find_id("300") == -1);
    SH_CHECK(library.value().find_id("Nobody") == -1);
    SH_CHECK(library.value().find_id("") == -1);
}

// Cutscene states: Harry's e_PlayerState ("states"), an NPC's e_<Name>Control ("controls");
// a mod patch merges key by key like "anims".
SH_TEST(cutscene_states) {
    std::vector<std::string> warnings;
    auto library = CharaLibrary::load(repo_gamedata(), {}, warnings);
    SH_CHECK(library);
    for (const std::string& warning : warnings) {
        SH_CHECK(warning.empty());
    }

    const sh::chara::Character harry = library.value().character(1);  // Chara_Harry
    SH_CHECK(harry.state("pickup_crouch", -1) == 59);
    SH_CHECK(harry.state("pickup_stand_up", -1) == 60);
    SH_CHECK(harry.state("walk_forward", -1) == 53);
    SH_CHECK(harry.state("motorcycle_key_take", -1) == 187);
    SH_CHECK(harry.state("no_such_state", 77) == 77);   // unknown name: the fallback
    SH_CHECK(harry.control("anything", 5) == 5);        // Harry has no controls

    const sh::chara::Character kaufmann = library.value().character(library.value().find_id("Kaufmann"));
    SH_CHECK(kaufmann.valid());
    SH_CHECK(kaufmann.control("burst_in", -1) == 16);
    SH_CHECK(kaufmann.control("control_16", -1) == -1);  // a hand name replaces the placeholder
    SH_CHECK(kaufmann.control("no_such_control", 9) == 9);
    SH_CHECK(library.value().check_consistency().empty());

    // A mod patch overrides one state of a base that has them.
    Scratch dir("states");
    dir.write("base/chara/characters.json", R"json({
  "characters": [
    {"id": 1, "enum": "Chara_Harry", "model": "models/CHARA/HERO.glb",
     "states": {"pickup_crouch": 59, "walk_forward": 53}}
  ]
})json");
    dir.write("mods/a/mod.json", R"({"id": "a", "version": "1.0.0", "gameplay": true})");
    dir.write("mods/a/chara/harry.json", R"json({
        "enum": "Chara_Harry", "states": {"pickup_crouch": 61}, "controls": {"idle": 2}})json");
    warnings.clear();
    library = CharaLibrary::load(dir.root / "base", dir.root / "mods", warnings);
    SH_CHECK(library);
    const sh::chara::Character patched = library.value().character(1);
    SH_CHECK(patched.state("pickup_crouch", -1) == 61);   // the mod's number wins
    SH_CHECK(patched.state("walk_forward", -1) == 53);    // an untouched key keeps the base
    SH_CHECK(patched.control("idle", -1) == 2);
}

// A mod patches one character (chara/<name>.json) instead of copying the whole file.
SH_TEST(mod_patches_one_character) {
    Scratch dir("patch");
    dir.write("base/chara/characters.json", kTwo);
    dir.write("mods/a/mod.json", R"({"id": "a", "version": "1.0.0", "priority": 10, "gameplay": true})");
    dir.write("mods/a/chara/harry.json", R"json({
        "enum": "Chara_Harry", "sounds": {"footstep": 1500, "death": 1501}, "params": {"health": 50}})json");
    dir.write("mods/b/mod.json", R"({"id": "b", "version": "1.0.0", "priority": 20, "gameplay": true})");
    dir.write("mods/b/chara/harry.json", R"json({
        "id": 1, "sounds": {"footstep": 1600, "death": null}, "model": "CHARA/HERO2.ILM"})json");
    dir.write("mods/b/chara/new.json", R"json({"characters": [
        {"id": 60, "enum": "Chara_Custom", "model": "CHARA/CUSTOM.ILM", "sounds": {"idle": 1700}}]})json");
    dir.write("mods/b/chara/broken.json", R"json({"enum": "Chara_Nobody", "sounds": {"x": 1}})json");

    std::vector<std::string> warnings;
    sh::chara::LoadSource source;
    auto library = CharaLibrary::load(dir.root / "base", dir.root / "mods", warnings, &source);
    SH_CHECK(library);
    SH_CHECK(source.mod_id.empty());  // nobody replaced characters.json
    SH_CHECK(source.patches.size() == 3);
    SH_CHECK(has_warning(warnings, "Chara_Nobody") && has_warning(warnings, "patch skipped"));
    SH_CHECK(!has_warning(warnings, "\"gameplay\": true"));

    const CharaDef* harry = library.value().find(1);
    SH_CHECK(harry != nullptr);
    SH_CHECK(harry->disc_model == "CHARA/HERO2.ILM");  // b (higher priority) wins
    SH_CHECK(harry->model.empty());
    SH_CHECK(harry->textures.front() == "textures/CHARA/HERO.png");  // an untouched field keeps the base
    SH_CHECK(library.value().sfx(1, "footstep") == 1600);
    SH_CHECK(library.value().sfx(1, "death") == -1);  // b removed a's slot again
    SH_CHECK(library.value().character(1).param("health", 0.0) == 50.0);
    SH_CHECK(library.value().size() == 3);
    SH_CHECK(library.value().sfx(60, "idle") == 1700);
    SH_CHECK(library.value().find_id("Custom") == 60);
    SH_CHECK(library.value().check_consistency().empty());

    // Without "gameplay": true a patch still applies but is reported.
    dir.write("mods/a/mod.json", R"({"id": "a", "version": "1.0.0", "priority": 10})");
    warnings.clear();
    library = CharaLibrary::load(dir.root / "base", dir.root / "mods", warnings);
    SH_CHECK(library);
    SH_CHECK(has_warning(warnings, "mod a patches character data"));
}

SH_TEST(bad_named_values) {
    std::vector<std::string> warnings;
    SH_CHECK(!CharaLibrary::parse(R"json({"characters": [{"id": 1, "sounds": {"footstep": "loud"}}]})json", warnings));
    SH_CHECK(!CharaLibrary::parse(R"json({"characters": [{"id": 1, "sounds": {"footstep": 1.5}}]})json", warnings));
    SH_CHECK(!CharaLibrary::parse(R"json({"characters": [{"id": 1, "anims": [1, 2]}]})json", warnings));
    SH_CHECK(!CharaLibrary::parse(R"json({"characters": [{"id": -1}]})json", warnings));
    // Character numbers are int: one past a byte is a valid number.
    SH_CHECK(CharaLibrary::parse(R"json({"characters": [{"id": 300}]})json", warnings));
    const auto odd = CharaLibrary::parse(R"json({"characters": [
        {"id": 1, "sounds": {"Foot Step": 1311, "death": -5}, "anims": {"walk": -1}}]})json", warnings);
    SH_CHECK(odd);
    SH_CHECK(odd.value().check_consistency().size() == 3);
}

// The character file rows keep a value as the original row's bit fields do (shadow 10 bits, camera Y offset 14).
SH_TEST(chara_file_row_bit_fields) {
    using sh::chara::game::wrap_signed_bits;
    SH_CHECK(wrap_signed_bits(64, 10) == 64);
    SH_CHECK(wrap_signed_bits(-588, 14) == -588);
    SH_CHECK(wrap_signed_bits(8191, 14) == 8191);
    SH_CHECK(wrap_signed_bits(8192, 14) == -8192);
    SH_CHECK(wrap_signed_bits(-513, 10) == 511);
    for (int v = -70000; v <= 70000; ++v) {
        const struct { int f : 14; } bits = {v};
        SH_CHECK(wrap_signed_bits(v, 14) == bits.f);
    }
}

// The keys of a character worn by the player (sh.player.set_character) are its look: no "gameplay" needed.
SH_TEST(chara_as_player_keys_are_look_only) {
    SH_CHECK(sh::chara::patch_changes_look_only(R"({"as_player": {"model": "models/m/A.glb"}, "portrait": "x.png"})"));
    SH_CHECK(!sh::chara::patch_changes_look_only(R"({"as_player": {}, "params": {}})"));
}
