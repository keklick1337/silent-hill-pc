// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The characters of the game as data: one folder per
// character, gamedata/chara/<Name>/character.json, describes it whole - files, render, sounds
// per named slot (names of the sound registry, gamedata/sounds), numeric parameters, named
// animations - so maps and events never define a character's sounds; the same folder holds
// its Lua (behavior.lua, actions.lua); edited without touching the engine. A mod patches a character with chara/<Name>/character.json (merged key by key)
// or adds one; the old single chara/characters.json and flat chara/<name>.json patches still
// load, with a warning.
//
// Lookups never fail: an absent character, slot or key returns the caller's fallback, which
// is the original hard-coded value - so the shipped data (= the originals) and an empty or
// missing file both behave exactly like the original game.

#pragma once

#include "formats/common/status.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "engine/audio/sound_registry.h"
#include "engine/chara/chara_sfx_slot.h"

namespace sh::chara {

template <typename T>
using NamedValues = std::map<std::string, T, std::less<>>;

// One character, one chara/<Name>/character.json. The folder name is the identifier:
// chara/Harry is silenthill:Harry, a mod's new chara/Ghost is
// <mod folder>:Ghost. `number` is the engine's run-time number (the game's character id for the
// base game, from the numbering the game installs; a new character of a mod gets a free one);
// the data carries none. Files are the converted files of the import, relative to
// gamedata/convert (a mod's convert/ first); empty/null keeps the compiled-in file. Which disc
// file a converted one came from is the import's business (index.tsv, the manifests next to the
// files: ConvertedSources), never the data's.
struct CharacterDef {
    int number = 0;
    std::string id;          // "silenthill:Harry"
    std::string folder;      // "Harry": gamedata/chara/<folder>/
    std::string description;            // who this is and where they appear (English prose)
    std::vector<std::string> maps;      // maps the character appears on ("map0_s00", ...)
    // The group the debug spawn menu files it under: "CommonEnemies", "Bosses", ... (base) or
    // "<mod>:Name" (a mod's own); empty = uncategorised (see CharaLibrary::spawn_category).
    std::string category;
    // The folder whose behavior.lua the type runs when it is not its own: types that run another type's update in
    // every map header ("Stalker" for the Grey Child); empty = its own folder.
    std::string behavior;
    std::string model;                  // "models/CHARA/HERO.glb" (skin + clips)
    std::string animations;             // "anim/ANIM/HB_BASE.anim.json"
    std::vector<std::string> textures;  // "textures/CHARA/HERO.png" first, then its variant pictures
    // The old keys of mods ("anim", "texture", a "model" that is not a .glb): disc paths,
    // read with a warning. A later key of the other form replaces them.
    std::string disc_anim;     // "ANIM/HB_BASE.ANM"
    std::string disc_model;    // "CHARA/HERO.ILM"
    std::string disc_texture;  // "CHARA/HERO.TIM"
    double shadow = 0.0;     // shadow height (Q8 of the original)
    std::string blend_mode;  // "average" | "additive" | "subtractive"
    std::string camera_anchor;  // "character" | "ground" | "camera"
    double camera_offset_y = 0.0;

    NamedValues<int> sounds;               // slot ("footstep", "death", ...) -> e_SfxId
    NamedValues<std::string> sound_names;  // slot -> the registry name the data gives
    // Footstep sound per floor surface (gamedata/world/surfaces.json names, "*" = any other
    // surface) -> e_SfxId; empty = the character steps with Harry's table.
    NamedValues<int> footsteps;
    NamedValues<std::string> footstep_names;  // surface -> the registry name the data gives
    NamedValues<double> params;            // "health_base" -> 210.0 (game units; Q12 on use)
    NamedValues<int> anims;                // "walk_forward" -> original anim index
    // Cutscene states: Harry's e_PlayerState ("states", global) and
    // an NPC's e_<Name>Control ("controls") -- the number sh.event_chara_anim passes.
    NamedValues<int> states;
    NamedValues<int> controls;

    // "spawn": how a spawn outside the map's own spawn rows (console, debug menu, a script's
    // create) starts the character. spawn_state is the first behaviour step (-1 = the
    // engine's default, 3); spawn_kind what it is: "enemy", "boss" (its arena and cutscenes belong
    // to one map), "actor" (story character), "prop"; spawn_auto what the "auto" AI mode runs on a
    // map that does not run the type itself: "full" (its own behaviour, the map data it lacks from
    // its Lua fallbacks), "fallback" (the generic chara/fallback_ai.lua) or "none" (a posed statue);
    // empty = "full" with a behaviour, else "fallback".
    int spawn_state = -1;
    std::string spawn_kind;
    std::string spawn_auto;
    std::string spawn_note;  // one line for the spawn list (why it is limited)
    // "spawn.map_state": the first behaviour step on a map whose copy of the behaviour starts it from another
    // one ({"map0_s00": 6}: the alley chase is the only chase the prologue's stalker code has); beats spawn_state.
    NamedValues<int> spawn_map_states;
    // "preview": how the spawn menu's turntable shows the bind pose of the glb. rotate (degrees about x, y, z, in
    // that order) stands a model up whose bind pose lies or leans; zoom scales it in its panel (1 = fit); lift
    // moves it up the panel by that fraction of the panel's height.
    std::array<double, 3> preview_rotate{0.0, 0.0, 0.0};
    double preview_zoom = 1.0;
    double preview_lift = 0.0;
    // Harry's reaction clips its attacks need (names or groups of gamedata/chara/Harry/reactions.json):
    // loaded from one of its maps when it is spawned on a map that lacks them.
    std::vector<std::string> harry_reactions;
    // The clip tables the type plays from, the first its default: a table of gamedata/tables/anim ("cybil") or a
    // clip file of gamedata/chara ("AirScreamer/clips.json").
    std::vector<std::string> clip_tables;
    // "portrait": the picture this character shows as the player's portrait on the inventory screen, a path below
    // the game data ("convert/textures/ITEM/HEROPIC2.png"); the player's type names it, so it can be swapped.
    std::string portrait;
    // "as_player": how the type is worn by the player (sh.player.set_character): "model", a glb or a derive manifest
    // below convert/models/ bound to the player's skeleton; "portrait", a picture of a mod that stands in for the
    // player's portrait. Empty: the type cannot be worn.
    std::string player_model;
    std::string player_portrait;

    // Per-type traits. The defaults are
    // what a type the original never named got, so a mod's character only sets what differs.
    //
    // "blood": the blood a hit spills, a name of gamedata/effects/blood.json ("None": no blood). The library
    // resolves it to the effect's colour kind (blood_kind, -1 for none) after every patch.
    std::string blood = "Default";
    int blood_kind = kDefaultBloodKind;
    // "blood_at_zero_health": a hit that leaves health at exactly 0 still bleeds (the player); else a body at 0 or
    // below spills no more blood.
    bool blood_at_zero_health = false;
    // "blood_strength": >= 0 replaces the strength the weapon's row gives every hit.
    int blood_strength = -1;
    // "fresh_hit_blood_strength": >= 0 replaces it for a hit while the type is not already recoiling;
    // "fresh_hit_blood_strength_scatter" for such a hit of a scatter shot (a gun ray of several pellets).
    int fresh_hit_blood_strength = -1;
    int fresh_hit_blood_strength_scatter = -1;
    // "blood_follows_body": the sprays take an effect anchor that moves with the body (false: they stay put).
    bool blood_follows_body = true;
    // "blood_depth_bias": the sprays' depth bias (further back the higher).
    int blood_depth_bias = kDefaultBloodDepthBias;
    // "impact_sound": the sound a melee hit on it makes instead of the weapon's impact sound; empty: the weapon's.
    std::string impact_sound;
    // "is_player": the character the player controls: aims with the aim timer, rumbles the pad when firing, counts
    // in the ranking statistics, and its attack result is the hit count (an NPC's: whether the player was hit).
    bool is_player = false;
    // "enemy": a common enemy of the original's enemy range (its distance steadies a single-ray gun's aim).
    bool enemy = false;
    // "is_boss": a live one makes the fight a boss fight (quick save refuses while one lives).
    bool is_boss = false;
    // "acid_attack": the attack Harry receives when its acid strikes him: a row name of config/attacks.json
    // ("FloatstingerAcid", resolved where the table is loaded) or the row number (0: it spits none).
    std::string acid_attack_name;
    int acid_attack = 0;
    // "single_clip_table": any clip table index a behaviour attaches gives its first table.
    bool single_clip_table = false;
    // "state_layout": the folder whose state.json lays out its behaviour variables when it has none of its own;
    // empty: its "behavior" folder.
    std::string state_layout;
    // "pool": the global character pool loads it, so it can be spawned on any map; unset: whether it has "spawn".
    std::optional<bool> pool;
    bool has_spawn = false;  // a "spawn" object was given
    // "pool_files": disc files the pool loads for it instead of its file table row (empty: the row's).
    std::string pool_model, pool_texture, pool_anim;

    bool pooled() const { return pool.value_or(has_spawn); }
    // The folder whose state.json the type uses when it has none.
    const std::string& state_layout_folder() const { return state_layout.empty() ? behavior : state_layout; }

    static constexpr int kDefaultBloodKind = 16;
    static constexpr int kDefaultBloodDepthBias = 32;

    bool operator==(const CharacterDef&) const = default;
};
using CharaDef = CharacterDef;  // the name before the Character class

// Read-only view of one character with the fallback semantics; a default-constructed
// Character (unknown number) answers every lookup with the fallback.
class Character {
public:
    Character() = default;
    explicit Character(const CharacterDef* def) : def_(def) {}

    bool valid() const { return def_ != nullptr; }
    const CharacterDef* def() const { return def_; }

    int sfx(std::string_view slot, int fallback = -1) const;
    double param(std::string_view name, double fallback) const;
    // Q12 of a param the way the original's Q12(x) literal is: (s32)((float)x * 4096.0f).
    std::int32_t param_q12(std::string_view name, std::int32_t fallback) const;
    std::int32_t param_int(std::string_view name, std::int32_t fallback) const;
    int anim(std::string_view name, int fallback = -1) const;
    // Cutscene state machines: the player's e_PlayerState ("states"), an NPC's control state
    // ("controls"). Same fallback semantics as anim().
    int state(std::string_view name, int fallback = -1) const;
    int control(std::string_view name, int fallback = -1) const;
    // The footstep sound for `surface`: its entry, else the "*" entry, else `fallback`.
    int footstep(std::string_view surface, int fallback = -1) const;
    bool has_footsteps() const { return def_ != nullptr && !def_->footsteps.empty(); }

private:
    const CharacterDef* def_ = nullptr;
};

struct LoadSource {
    std::string mod_id;          // the mod whose characters.json replaced the base set
    bool mod_gameplay = false;
    std::vector<std::string> patches;  // per-character patch files applied, in order
    bool legacy = false;         // some data came in the old single-file / flat-patch layout
};

// The C-safe slot enum by name ("footstep", ...); nullptr past the enum.
const char* sfx_slot_name(Pc_CharaSfxSlot slot);

// Q12 of a game-unit value exactly like the original Q12(x) macro on a float literal.
std::int32_t to_q12(double value);

// Converted file of the import -> the disc file it was converted from ("models/CHARA/HERO.glb"
// -> "CHARA/HERO.ILM"): the "converted" column of gamedata/index.tsv, else the manifest next to
// the file (<stem>.model.json "source.path", an .anim.json's own "source.path", <stem>.tim.json
// "game_path" for a texture or one of its variant pictures) under each convert root in turn.
class ConvertedSources {
public:
    // `convert_roots`: extra convert/ directories to search for manifests, highest priority
    // first (mods); gamedata/convert is searched last. A missing index.tsv is not an error.
    static ConvertedSources load(const std::filesystem::path& gamedata_dir,
                                 std::vector<std::filesystem::path> convert_roots = {});
    // From the text of an index.tsv (tests).
    static ConvertedSources from_index(std::string_view index_tsv);

    // The disc path, or empty when no manifest names one.
    std::string disc_path(std::string_view converted) const;
    std::size_t size() const { return by_converted_.size(); }

private:
    std::map<std::string, std::string, std::less<>> by_converted_;  // lower-cased converted path
    std::vector<std::filesystem::path> roots_;
};

// Whether a mod's chara/<Name>/character.json patch touches only how the character looks ("model", "textures",
// "shadow", "blend_mode", "description", "_notes"): such a mod needs no "gameplay": true in its mod.json.
bool patch_changes_look_only(std::string_view patch_text);

// The glb, below convert/models/ ("CHARA/HERO.glb", "my_mod/HERO.glb"), a character type is drawn from.
// `data_model` is its character.json "model" and `data_file` the disc file that model stood for when the file table
// was built; `current_file` and `current_disc_path` are the model file the type loads now. While the type still
// loads the file its data names, the glb is the data's own -- so a mod's character.json can point a character of the
// game at any glb of any active mod, as long as the manifest next to that glb names the same skeleton file. A type
// the game retargeted to another file (a region's model, the pool's beta model) is drawn from that file's glb.
// Empty: no glb is known.
std::string drawn_model_glb(std::string_view data_model, int data_file, int current_file,
                            std::string_view current_disc_path);

// How the engine numbers characters at run time: the full id -> a non-negative number, or -1 when
// none can be given (the engine installs one, src/engine/ids/game_numbers.cpp).
using CharaNumbering = std::function<int(std::string_view id)>;

class CharaLibrary {
public:
    // The numbering every load() uses.
    static void set_default_numbering(CharaNumbering numbering);
    static const CharaNumbering& default_numbering();

    // Every <gamedata_dir>/chara/<Name>/character.json (the old chara/characters.json when
    // there are no folders; a mod's chara/characters.json still replaces the set whole), then
    // the patches: base dir, then mods by ascending priority so the highest-priority mod has
    // the last word - per mod the flat chara/<name>.json files, then chara/<Name>/character.json.
    // Sound names resolve through the sound registry of the same directories.
    static Result<CharaLibrary> load(const std::filesystem::path& gamedata_dir,
                                     const std::filesystem::path& mods_dir,
                                     std::vector<std::string>& warnings, LoadSource* source = nullptr);

    // From the text of the old single file {"characters": [...]} (tests, tools). Sound names
    // need `sounds`; without a registry only numeric sound ids parse.
    static Result<CharaLibrary> parse(std::string_view json, std::vector<std::string>& warnings,
                                      const audio::SoundRegistry* sounds = nullptr);

    // One patch file's text: a character object (or {"characters": [...]}) merged over the
    // character `target` (a full id, "silenthill:Harry"); with no target, the entry's old
    // "id"/"enum" keys name it (a new "id" adds a character). sounds/params/anims merge key by
    // key, null removes a key. A sound is a registry name or an e_SfxId. The old file keys go to
    // `warnings` (when given).
    Result<int> apply_patch(std::string_view json, std::string_view origin, std::string_view target = {},
                            std::vector<std::string>* warnings = nullptr);

    // The sound names the library resolves with (empty for parse() without a registry).
    const audio::SoundRegistry& sound_registry() const { return sounds_; }
    void set_sound_registry(audio::SoundRegistry sounds) { sounds_ = std::move(sounds); }

    // A file of the character's folder ("actions.lua", "behavior.lua"): the highest-priority
    // mod's copy, else the base one; empty when none exists. A .lua file counts only from
    // a mod that may run scripts (mods.h, scripts_allowed).
    std::filesystem::path script_file(int id, std::string_view file) const;
    // Every copy of it the same rule allows, base first, then mods by ascending priority (layered Lua).
    std::vector<std::filesystem::path> script_files(int id, std::string_view file) const;
    // A JSON file of the character's folder ("part_variants.json", "state.json") with every mod's copy merged
    // over the base one in load order (engine/mods/data_merge.h). Fails when no folder has it; `source` gets
    // the file (or the merged files) for messages; merge warnings go to `warnings`.
    Result<std::string> data_file(int id, std::string_view file, std::string* source,
                                  std::vector<std::string>& warnings) const;
    // The folders holding the character's files, base first, then mods by ascending priority.
    std::vector<std::filesystem::path> folders(int id) const;

    std::size_t size() const { return characters_.size(); }
    const std::vector<CharacterDef>& characters() const { return characters_; }
    const CharacterDef* find(int number) const;
    // By id: "silenthill:Groaner" or "Groaner" (short names); nullptr when unknown.
    const CharacterDef* find_by_id(std::string_view id) const;
    // The number of "silenthill:Groaner", "Groaner", "groaner", the old "Chara_Groaner" or "4";
    // -1 when unknown.
    int find_id(std::string_view name) const;

    Character character(int id) const { return Character(find(id)); }

    // Sound id for the slot, or -1 when the character/key is not set (the caller then
    // keeps the original hard-coded id).
    int sfx(int id, Pc_CharaSfxSlot slot) const;
    int sfx(int id, std::string_view slot) const { return character(id).sfx(slot); }

    // The spawn-menu category of a character: its `category`; when empty, "Mods" for a character of
    // a mod (id prefix other than silenthill) and "Other" for a base one.
    static std::string spawn_category(const CharacterDef& def);
    // The categories in use, in the order of kSpawnCategoryOrder (base ones first, then the
    // rest alphabetically, "Mods" last).
    std::vector<std::string> spawn_categories() const;

    std::vector<std::string> check_consistency() const;

    // Resolves every character's "blood" through the text of gamedata/effects/blood.json ({"kinds": {name:
    // {"kind": n}}}); an unknown name keeps the default kind with a warning. load() does this itself.
    void resolve_blood(std::string_view blood_json, std::string_view origin, std::vector<std::string>& warnings);

private:
    void sort();
    std::vector<CharacterDef> characters_;  // sorted by number
    audio::SoundRegistry sounds_;
    std::map<int, std::vector<std::filesystem::path>> folders_;
    // Folders of mods without "scripts": true: script_file skips their .lua files.
    std::vector<std::filesystem::path> lua_blocked_;
};

}  // namespace sh::chara
