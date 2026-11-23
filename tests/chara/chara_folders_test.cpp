#include "framework/sh_test.h"

#include "engine/audio/sound_registry.h"
#include "engine/chara/chara_actions.h"
#include "engine/chara/chara_library.h"
#include "engine/script/script_host.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include "framework/mod_fixture.h"

namespace fs = std::filesystem;
using sh::audio::SoundRegistry;
using sh::chara::CharaActions;
using sh::chara::CharaLibrary;

namespace {

fs::path repo_gamedata() { return fs::path(SH_PROJECT_ROOT) / "gamedata"; }

struct Scratch {
    fs::path root;
    explicit Scratch(const char* name) : root(fs::path(SH_PROJECT_ROOT) / ".tmp" / "chara_folders" / name) {
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

constexpr std::string_view kSounds = R"json({"sounds": {
    "footstep_concrete": {"id": 1311, "enum": "Sfx_FootstepConcrete", "banks": ["BASE"], "files": ["audio/vab/BASE/005.wav"]},
    "footstep_grass": {"id": 1312},
    "harry_footstep_ground_8": {"id": 1330},
    "groaner_death": {"id": 1414}
}})json";

constexpr std::string_view kHarry = R"json({"model": "CHARA/HERO.ILM",
    "sounds": {"footstep": "footstep_concrete", "footstep_grass": "footstep_grass"}})json";
constexpr std::string_view kGroaner = R"json({"model": "CHARA/DOG.ILM",
    "sounds": {"death": "groaner_death"}, "params": {"health_base": 210.0}})json";

}  // namespace

// The shipped registry: readable names for the ids the code plays.
SH_TEST(sound_registry_shipped) {
    std::vector<std::string> warnings;
    const auto registry = SoundRegistry::load(repo_gamedata(), {}, warnings);
    SH_CHECK(registry);
    SH_CHECK(warnings.empty());
    SH_CHECK(registry.value().size() > 400);
    SH_CHECK(registry.value().id("footstep_concrete") == 1311);   // Sfx_FootstepConcrete
    // The file names sounds only: the numbers are the game's sound numbers.
    {
        std::ifstream in(repo_gamedata() / "sounds" / "sounds.json");
        const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        SH_CHECK(text.find("\"id\":") == std::string::npos && text.find("\"enum\":") == std::string::npos);
    }
    SH_CHECK(registry.value().id("1311") == 1311);
    SH_CHECK(registry.value().id("door_locked") == 1344);
    SH_CHECK(registry.value().id("groaner_death") == 1414);
    SH_CHECK(registry.value().id("no_such_sound") == -1);
    SH_CHECK(registry.value().name(1311) != nullptr && *registry.value().name(1311) == "footstep_concrete");
    const sh::audio::SoundInfo* info = registry.value().find("footstep_concrete");
    SH_CHECK(info != nullptr && info->banks == std::vector<std::string>{"BASE"});
}

// Every character sound of the shipped data is a registry name that gives the original id.
SH_TEST(chara_sounds_by_name_shipped) {
    std::vector<std::string> warnings;
    const auto library = CharaLibrary::load(repo_gamedata(), {}, warnings);
    SH_CHECK(library);
    SH_CHECK(warnings.empty());
    const sh::chara::CharacterDef* harry = library.value().find(1);
    SH_CHECK(harry != nullptr && harry->folder == "Harry");
    SH_CHECK(harry->footstep_names.at("concrete") == "footstep_concrete");
    SH_CHECK(harry->footsteps.at("concrete") == 1311);
    for (const sh::chara::CharacterDef& c : library.value().characters()) {
        SH_CHECK(c.sound_names.size() == c.sounds.size());  // no numeric sounds left in the data
        SH_CHECK(c.footstep_names.size() == c.footsteps.size());
    }
    SH_CHECK(!library.value().script_file(1, "actions.lua").empty());  // Harry's example hook
    SH_CHECK(library.value().script_file(4, "actions.lua").empty());
}

// Folders, mod patches by folder, rebinding a sound name, the old layout with a warning.
SH_TEST(chara_folders_and_mods) {
    Scratch dir("mods");
    dir.write("base/sounds/sounds.json", kSounds);
    dir.write("base/chara/Harry/character.json", kHarry);
    dir.write("base/chara/Groaner/character.json", kGroaner);
    // A mod: Harry's footsteps by name, by folder name only (no id/enum in the patch).
    dir.write("mods/steps/mod.json", R"({"id": "steps", "version": "1.0.0", "priority": 10, "scripts": true})");
    dir.write("mods/steps/chara/Harry/character.json", R"json({"sounds": {"footstep": "harry_footstep_ground_8"}})json");
    dir.write("mods/steps/chara/Harry/actions.lua", "-- the mod's copy\n");
    // Another mod rebinds a registry name: every user of groaner_death follows.
    dir.write("mods/rebind/mod.json", R"({"id": "rebind", "version": "1.0.0", "priority": 20, "gameplay": true})");
    dir.write("mods/rebind/sounds/groaner.json", R"json({"sounds": {"groaner_death": {"id": 1415}, "my_step": 1312}})json");
    dir.write("mods/rebind/chara/Groaner/character.json", R"json({"sounds": {"run": "my_step", "bad": "no_such_sound"}})json");
    // Its Lua does not run: mod.json does not say "scripts": true.
    dir.write("mods/rebind/chara/Groaner/actions.lua", "-- never run\n");

    std::vector<std::string> warnings;
    sh::chara::LoadSource source;
    auto library = CharaLibrary::load(dir.root / "base", dir.root / "mods", warnings, &source);
    SH_CHECK(library);
    SH_CHECK(!source.legacy);
    SH_CHECK(library.value().sfx(1, "footstep") == 1330);   // the mod's patch
    SH_CHECK(library.value().sfx(1, "footstep_grass") == 1312);
    SH_CHECK(library.value().sfx(4, "death") == 1415);      // rebound name
    SH_CHECK(library.value().sfx(4, "run") == -1);          // the bad patch changed nothing
    SH_CHECK(has_warning(warnings, "no_such_sound") && has_warning(warnings, "patch skipped"));
    SH_CHECK(has_warning(warnings, "mod steps patches character data (Harry/character.json)"));
    SH_CHECK(library.value().folders(1).size() == 2);
    SH_CHECK(library.value().script_file(1, "actions.lua") == dir.root / "mods/steps/chara/Harry/actions.lua");
    SH_CHECK(library.value().script_file(4, "actions.lua").empty());
    SH_CHECK(library.value().folders(4).size() == 2);  // its JSON still applies
    SH_CHECK(has_warning(warnings, "mod rebind has Lua files but mod.json does not say \"scripts\": true"));

    // A new character of a mod is <mod folder>:<Name>, numbered after the original ones; a
    // character.json of the old layout ("enum") still patches, with a warning.
    dir.write("mods/steps/chara/Ghost/character.json", R"json({"model": "CHARA/GHOST.ILM"})json");
    dir.write("mods/steps/chara/Groaner/character.json", R"json({"enum": "Chara_Groaner", "params": {"health_base": 300.0}})json");
    warnings.clear();
    library = CharaLibrary::load(dir.root / "base", dir.root / "mods", warnings, &source);
    SH_CHECK(library);
    const sh::chara::CharacterDef* ghost = library.value().find_by_id("steps:Ghost");
    SH_CHECK(ghost != nullptr && ghost->number >= 45 && ghost->folder == "Ghost");
    SH_CHECK(library.value().find_by_id("Ghost") == ghost);
    SH_CHECK(library.value().find_id("steps:Ghost") == ghost->number);
    SH_CHECK(library.value().find(4)->params.at("health_base") == 300.0);
    SH_CHECK(has_warning(warnings, "old character layout"));

    // The old single file still loads (no folders), with a warning.
    Scratch old("legacy");
    old.write("base/sounds/sounds.json", kSounds);
    old.write("base/chara/characters.json", R"json({"characters": [
        {"id": 1, "enum": "Chara_Harry", "sounds": {"footstep": 1311, "footstep_grass": "footstep_grass"}}]})json");
    old.write("mods/flat/mod.json", R"({"id": "flat", "version": "1.0.0", "gameplay": true})");
    old.write("mods/flat/chara/harry.json", R"json({"enum": "Chara_Harry", "sounds": {"footstep": "harry_footstep_ground_8"}})json");
    warnings.clear();
    source = {};
    library = CharaLibrary::load(old.root / "base", old.root / "mods", warnings, &source);
    SH_CHECK(library);
    SH_CHECK(source.legacy);
    SH_CHECK(has_warning(warnings, "old character layout"));
    SH_CHECK(library.value().sfx(1, "footstep") == 1330);
    SH_CHECK(library.value().find(1)->sound_names.at("footstep_grass") == "footstep_grass");
}

// actions.lua hooks: nil keeps the data's sound, a name or an id replaces it; on_sound is the
// fallback hook; a script error keeps the data's sound.
SH_TEST(chara_action_hooks) {
    Scratch dir("hooks");
    dir.write("base/sounds/sounds.json", kSounds);
    dir.write("base/chara/Harry/character.json", kHarry);
    dir.write("base/chara/Groaner/character.json", kGroaner);
    dir.write("base/chara/Harry/actions.lua", R"lua(
        function on_footstep(e)
            assert(e.character == "Harry" and e.chara_id == 1)
            if e.surface == "grass" then return "footstep_concrete" end
            if e.surface == "metal" then return 1330 end
            if e.surface == "ground_9" then return "no_such_sound" end
            return nil
        end
        function on_sound(e)
            if e.slot == "body_fall" then error("boom") end
            return e.id + 1
        end
    )lua");
    std::vector<std::string> warnings;
    const auto library = CharaLibrary::load(dir.root / "base", {}, warnings);
    SH_CHECK(library);
    sh::script::HostConfig config;
    config.gamedata_dir = dir.root / "base";
    std::vector<std::string> log;
    static std::vector<std::string>* s_log;
    s_log = &log;
    config.log = [](int level, const std::string& message) { s_log->push_back(std::to_string(level) + "|" + message); };
    CharaActions actions(library.value(), config);

    SH_CHECK(actions.sound(1, "footstep", 1311) == 1311);        // nil: the data's sound
    SH_CHECK(actions.sound(1, "footstep_grass", 1312) == 1311);  // by name
    SH_CHECK(actions.sound(1, "footstep_metal", 1332) == 1330);  // by id
    SH_CHECK(actions.sound(1, "footstep_ground_9", 1331) == 1331);  // unknown name: kept, logged
    SH_CHECK(actions.sound(1, "idle", 1400) == 1401);            // on_sound
    SH_CHECK(actions.sound(1, "body_fall", 1317) == 1317);       // error: kept, logged
    SH_CHECK(actions.sound(4, "death", 1414) == 1414);           // no actions.lua
    SH_CHECK(log.size() == 2);

    SH_CHECK(std::string(CharaActions::hook_for_slot("footstep_ground_2")) == "on_footstep");
    SH_CHECK(std::string(CharaActions::hook_for_slot("damage_type_2")) == "on_damage");
    SH_CHECK(std::string(CharaActions::hook_for_slot("death")) == "on_death");
    SH_CHECK(std::string(CharaActions::hook_for_slot("bite_attack")) == "on_attack");
    SH_CHECK(std::string(CharaActions::hook_for_slot("idle")) == "on_sound");
    SH_CHECK(CharaActions::footstep_surface("footstep") == "default");
}

// Scripts name sounds: sh.sound.id / name / exists work without a game (the registry is data).
SH_TEST(lua_sound_names) {
    Scratch dir("lua");
    dir.write("sounds/sounds.json", kSounds);
    dir.write("scripts/game.lua", R"lua(
        function on_boot()
            assert(sh.sound.id("footstep_concrete") == 1311)
            assert(sh.sound.id(1312) == 1312)
            assert(sh.sound.name(1414) == "groaner_death")
            assert(sh.sound.name(9) == nil)
            assert(sh.sound.exists("footstep_grass") and not sh.sound.exists("nope"))
            assert(not pcall(sh.sound.id, "nope"))
        end
    )lua");
    sh::script::HostConfig config;
    config.gamedata_dir = dir.root;
    std::vector<std::string> log;
    static std::vector<std::string>* s_log;
    s_log = &log;
    config.log = [](int level, const std::string& message) { s_log->push_back(std::to_string(level) + "|" + message); };
    sh::script::Host host(config);
    SH_CHECK(host.load().value());
    host.on_boot();
    for (const std::string& line : log) {
        SH_CHECK(line.empty());
    }
}

// The spawn-menu category: every base character has one, a mod's own falls into Mods unless it names one.
SH_TEST(chara_spawn_categories) {
    std::vector<std::string> warnings;
    const auto shipped = CharaLibrary::load(repo_gamedata(), {}, warnings);
    SH_CHECK(shipped);
    for (const sh::chara::CharacterDef& c : shipped.value().characters())
        SH_CHECK(!c.category.empty());  // the data names it, no silent "Other" default
    SH_CHECK(shipped.value().find_by_id("Groaner")->category == "CommonEnemies");
    SH_CHECK(shipped.value().find_by_id("Wormhead")->category == "OtherworldEnemies");
    SH_CHECK(shipped.value().find_by_id("SplitHead")->category == "Bosses");
    SH_CHECK(shipped.value().find_by_id("Cybil")->category == "StoryCharacters");
    const std::vector<std::string> order = shipped.value().spawn_categories();
    SH_CHECK((order == std::vector<std::string>{"CommonEnemies", "OtherworldEnemies", "Bosses", "StoryCharacters",
                                                "Animals", "Other"}));

    Scratch dir("category");
    dir.write("base/sounds/sounds.json", kSounds);
    dir.write("base/chara/Harry/character.json", kHarry);
    dir.write("base/chara/Groaner/character.json", R"json({"category": "CommonEnemies"})json");
    dir.write("mods/ghosts/mod.json", R"({"id": "ghosts", "version": "1.0.0", "gameplay": true})");
    dir.write("mods/ghosts/chara/Ghost/character.json", R"json({"model": "CHARA/GHOST.ILM"})json");
    dir.write("mods/ghosts/chara/Boss/character.json", R"json({"category": "Bosses"})json");
    warnings.clear();
    const auto lib = CharaLibrary::load(dir.root / "base", dir.root / "mods", warnings);
    SH_CHECK(lib);
    SH_CHECK(CharaLibrary::spawn_category(*lib.value().find_by_id("ghosts:Ghost")) == "Mods");
    SH_CHECK(CharaLibrary::spawn_category(*lib.value().find_by_id("ghosts:Boss")) == "Bosses");
    SH_CHECK(CharaLibrary::spawn_category(*lib.value().find_by_id("Harry")) == "Other");
    const std::vector<std::string> cats = lib.value().spawn_categories();
    SH_CHECK(!cats.empty() && cats.back() == "Mods");
}

// A mod's new character names its own converted files; the manifests next to them in the mod's convert/ say which
// disc files carry them (the file table loads those, the engine draws the mod's glb and samples its animation).
SH_TEST(chara_mod_character_files) {
    Scratch dir("mod_files");
    dir.write("base/sounds/sounds.json", kSounds);
    dir.write("base/chara/Harry/character.json", kHarry);
    dir.write("mods/ghouls/mod.json", R"({"id": "ghouls", "version": "1.0.0", "gameplay": true})");
    dir.write("mods/ghouls/chara/Ghoul/character.json", R"json({"model": "models/CHARA/GHOUL.glb",
        "animations": "anim/ANIM/GHOUL.anim.json", "textures": ["textures/CHARA/GHOUL.png"],
        "spawn": {"state": 3, "kind": "enemy", "auto": "fallback"}})json");
    dir.write("mods/ghouls/convert/models/CHARA/GHOUL.model.json",
              R"json({"format": "sh-model/1", "source": {"path": "CHARA/DOG.ILM", "kind": "ilm"}})json");
    dir.write("mods/ghouls/convert/anim/ANIM/GHOUL.anim.json",
              R"json({"format": "sh-anim/1", "source": {"path": "ANIM/DOG.ANM"}})json");
    dir.write("mods/ghouls/convert/textures/CHARA/GHOUL.tim.json", R"json({"game_path": "CHARA/DOG.TIM"})json");

    std::vector<std::string> warnings;
    const auto lib = CharaLibrary::load(dir.root / "base", dir.root / "mods", warnings);
    SH_CHECK(lib);
    const sh::chara::CharacterDef* ghoul = lib.value().find_by_id("ghouls:Ghoul");
    SH_CHECK(ghoul != nullptr);
    if (ghoul == nullptr) {
        return;
    }
    SH_CHECK(ghoul->number >= 45 && ghoul->number < 256);
    SH_CHECK(ghoul->pooled());  // its "spawn" block puts it in the character pool
    SH_CHECK(ghoul->model == "models/CHARA/GHOUL.glb");

    // Without the mod's convert/ the manifests are unknown; with it they give the carriers.
    const auto base_only = sh::chara::ConvertedSources::load(dir.root / "base");
    SH_CHECK(base_only.disc_path(ghoul->model).empty());
    const auto sources = sh::chara::ConvertedSources::load(dir.root / "base", {dir.root / "mods/ghouls/convert"});
    SH_CHECK(sources.disc_path(ghoul->model) == "CHARA/DOG.ILM");
    SH_CHECK(sources.disc_path(ghoul->animations) == "ANIM/DOG.ANM");
    SH_CHECK(sources.disc_path(ghoul->textures.front()) == "CHARA/DOG.TIM");
}
