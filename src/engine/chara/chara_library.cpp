// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/chara/chara_library.h"

#include "engine/ids/ids.h"
#include "engine/mods/data_merge.h"
#include "engine/mods/mods.h"
#include "formats/common/json.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace sh::chara {

namespace {

using json::Value;

[[noreturn]] void fail(std::string message) { throw std::runtime_error(std::move(message)); }

std::string where(const Value& value, std::string_view what) {
    return std::to_string(value.line()) + ", column " + std::to_string(value.column()) + ": " +
           std::string(what);
}

double opt_number(const Value& parent, std::string_view key, double fallback) {
    const Value* field = parent.find(key);
    if (!field || field->is_null()) {
        return fallback;
    }
    if (!field->is_number()) {
        fail(where(*field, "\"" + std::string(key) + "\" must be a number or null"));
    }
    return field->number();
}

// A file field: absent keeps `current`, null clears it (NO_VALUE), a string sets it.
void merge_string(const Value& parent, std::string_view key, std::string& current) {
    const Value* field = parent.find(key);
    if (!field) {
        return;
    }
    if (field->is_null()) {
        current.clear();
    } else if (field->is_string()) {
        current = field->string();
    } else {
        fail(where(*field, "\"" + std::string(key) + "\" must be a string or null"));
    }
}

void merge_number(const Value& parent, std::string_view key, double& current) {
    if (parent.find(key)) {
        current = opt_number(parent, key, current);
    }
}

// An integer trait: absent keeps `current`, null resets it to `reset`.
void merge_int(const Value& parent, std::string_view key, int& current, int reset) {
    const Value* field = parent.find(key);
    if (!field) {
        return;
    }
    if (field->is_null()) {
        current = reset;
    } else if (field->is_integer()) {
        current = static_cast<int>(field->integer());
    } else {
        fail(where(*field, "\"" + std::string(key) + "\" must be an integer or null"));
    }
}

// A true/false trait: absent keeps `current`, null resets it to `reset`.
void merge_bool(const Value& parent, std::string_view key, bool& current, bool reset) {
    const Value* field = parent.find(key);
    if (!field) {
        return;
    }
    if (field->is_null()) {
        current = reset;
    } else if (field->is_bool()) {
        current = field->boolean();
    } else {
        fail(where(*field, "\"" + std::string(key) + "\" must be true, false or null"));
    }
}

// "sounds"/"params"/"anims": an object of name -> number; null as a value removes the name
// (a patch drops a slot back to the original), null as the whole object clears all.
template <typename T>
void merge_named(const Value& entry, std::string_view key, NamedValues<T>& out) {
    const Value* object = entry.find(key);
    if (!object) {
        return;
    }
    if (object->is_null()) {
        out.clear();
        return;
    }
    if (!object->is_object()) {
        fail(where(*object, "\"" + std::string(key) + "\" must be an object or null"));
    }
    for (const auto& [name, value] : object->members()) {
        if (!name.empty() && name.front() == '_') {
            continue;  // "_comment"
        }
        if (value.is_null()) {
            out.erase(name);
            continue;
        }
        if (!value.is_number()) {
            fail(where(value, "\"" + std::string(key) + "." + name + "\" must be a number or null"));
        }
        if constexpr (std::is_integral_v<T>) {
            if (!value.is_integer()) {
                fail(where(value, "\"" + std::string(key) + "." + name + "\" must be an integer"));
            }
            out[name] = static_cast<T>(value.integer());
        } else {
            out[name] = static_cast<T>(value.number());
        }
    }
}

// "sounds" (slot -> sound) and "footsteps" (surface -> sound): a registry name
// ("footstep_concrete") or an e_SfxId (the numeric form); null removes the
// key, null as the whole object clears all.
void merge_sound_table(const Value& entry, std::string_view key, NamedValues<int>& ids,
                       NamedValues<std::string>& names, const audio::SoundRegistry* registry) {
    const Value* object = entry.find(key);
    if (!object) {
        return;
    }
    const std::string field(key);
    if (object->is_null()) {
        ids.clear();
        names.clear();
        return;
    }
    if (!object->is_object()) {
        fail(where(*object, "\"" + field + "\" must be an object or null"));
    }
    for (const auto& [slot, value] : object->members()) {
        if (!slot.empty() && slot.front() == '_') {
            continue;
        }
        if (value.is_null()) {
            ids.erase(slot);
            names.erase(slot);
            continue;
        }
        if (value.is_string()) {
            const int id = registry != nullptr ? registry->id(value.string()) : -1;
            if (id < 0) {
                fail(where(value, "\"" + field + "." + slot + "\": unknown sound \"" + value.string() +
                                      "\" (not in gamedata/sounds)"));
            }
            ids[slot] = id;
            names[slot] = value.string();
            continue;
        }
        if (!value.is_integer()) {
            fail(where(value, "\"" + field + "." + slot + "\" must be a sound name, an id or null"));
        }
        ids[slot] = static_cast<int>(value.integer());
        const std::string* name = registry != nullptr ? registry->name(static_cast<int>(value.integer())) : nullptr;
        if (name != nullptr) {
            names[slot] = *name;
        } else {
            names.erase(slot);
        }
    }
}

// A list of strings: absent keeps `current`, null clears it, a string is a list of one.
void merge_strings(const Value& parent, std::string_view key, std::vector<std::string>& current) {
    const Value* field = parent.find(key);
    if (!field) {
        return;
    }
    if (field->is_null()) {
        current.clear();
        return;
    }
    if (field->is_string()) {
        current = {field->string()};
        return;
    }
    if (!field->is_array()) {
        fail(where(*field, "\"" + std::string(key) + "\" must be a list of strings or null"));
    }
    std::vector<std::string> out;
    for (const Value& item : field->array()) {
        if (!item.is_string()) {
            fail(where(item, "\"" + std::string(key) + "\" must hold strings"));
        }
        out.push_back(item.string());
    }
    current = std::move(out);
}

bool ends_with_nocase(std::string_view text, std::string_view tail) {
    if (text.size() < tail.size()) {
        return false;
    }
    text.remove_prefix(text.size() - tail.size());
    return std::equal(text.begin(), text.end(), tail.begin(), [](char a, char b) {
        return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
    });
}

// The files of a character: the converted keys ("model" .glb, "animations", "textures"), or
// the disc-path keys of older mods ("anim", "texture", "model" naming an ILM), which still
// load with a warning. Whichever form a file is given in last wins.
void merge_files(const Value& entry, CharacterDef& def, std::vector<std::string>* notes) {
    auto legacy = [&](std::string_view key, std::string_view now) {
        if (notes != nullptr) {
            notes->push_back("\"" + std::string(key) + "\" is the old disc-path key; name the converted file in \"" +
                             std::string(now) + "\"");
        }
    };
    if (const Value* model = entry.find("model"); model && model->is_string() &&
                                                  !ends_with_nocase(model->string(), ".glb")) {
        legacy("model", "model\": \"models/CHARA/<NAME>.glb");
        def.disc_model = model->string();
        def.model.clear();
    } else if (model) {
        merge_string(entry, "model", def.model);
        def.disc_model.clear();
    }
    if (entry.find("anim")) {
        legacy("anim", "animations");
        merge_string(entry, "anim", def.disc_anim);
        def.animations.clear();
    }
    if (entry.find("animations")) {
        merge_string(entry, "animations", def.animations);
        def.disc_anim.clear();
    }
    if (entry.find("texture")) {
        legacy("texture", "textures");
        merge_string(entry, "texture", def.disc_texture);
        def.textures.clear();
    }
    if (entry.find("textures")) {
        merge_strings(entry, "textures", def.textures);
        def.disc_texture.clear();
    }
}

// "spawn": {"state": n, "kind": "enemy"|"boss"|"actor"|"prop", "auto": "full"|"fallback"|"none",
// "note": "..."}; null resets all.
void merge_spawn(const Value& entry, CharacterDef& def) {
    const Value* spawn = entry.find("spawn");
    if (!spawn) {
        return;
    }
    if (spawn->is_null()) {
        def.spawn_state = -1;
        def.spawn_kind.clear();
        def.spawn_auto.clear();
        def.spawn_note.clear();
        def.spawn_map_states.clear();
        def.has_spawn = false;
        return;
    }
    if (!spawn->is_object()) {
        fail(where(*spawn, "\"spawn\" must be an object or null"));
    }
    def.has_spawn = true;
    if (const Value* state = spawn->find("state")) {
        if (state->is_null()) {
            def.spawn_state = -1;
        } else if (state->is_integer() && state->integer() >= 0 && state->integer() <= 255) {
            def.spawn_state = static_cast<int>(state->integer());
        } else {
            fail(where(*state, "\"spawn.state\" must be an integer 0..255 or null"));
        }
    }
    merge_string(*spawn, "kind", def.spawn_kind);
    merge_string(*spawn, "auto", def.spawn_auto);
    merge_string(*spawn, "note", def.spawn_note);
    merge_named(*spawn, "map_state", def.spawn_map_states);
    for (const auto& [map, state] : def.spawn_map_states) {
        if (state < 0 || state > 255) {
            fail(where(*spawn, "\"spawn.map_state." + map + "\" must be an integer 0..255"));
        }
    }
}

// "preview": {"rotate": [x, y, z] degrees, "zoom": f, "lift": f}; null puts all back.
void merge_preview(const Value& entry, CharacterDef& def) {
    const Value* preview = entry.find("preview");
    if (!preview) {
        return;
    }
    const CharacterDef defaults;
    if (preview->is_null()) {
        def.preview_rotate = defaults.preview_rotate;
        def.preview_zoom = defaults.preview_zoom;
        def.preview_lift = defaults.preview_lift;
        return;
    }
    if (!preview->is_object()) {
        fail(where(*preview, "\"preview\" must be an object or null"));
    }
    for (const auto& [name, value] : preview->members()) {
        if (name != "rotate" && name != "zoom" && name != "lift" && name != "description") {
            fail(where(value, "\"preview." + name + "\": unknown key (rotate, zoom, lift)"));
        }
    }
    if (const Value* rotate = preview->find("rotate")) {
        if (rotate->is_null()) {
            def.preview_rotate = defaults.preview_rotate;
        } else if (rotate->is_array() && rotate->array().size() == 3) {
            for (std::size_t k = 0; k < 3; ++k) {
                const Value& v = rotate->array()[k];
                if (!v.is_number()) {
                    fail(where(v, "\"preview.rotate\" must hold three numbers (degrees)"));
                }
                def.preview_rotate[k] = v.number();
            }
        } else {
            fail(where(*rotate, "\"preview.rotate\" must be [x, y, z] in degrees or null"));
        }
    }
    merge_number(*preview, "zoom", def.preview_zoom);
    merge_number(*preview, "lift", def.preview_lift);
    if (!(def.preview_zoom > 0.05 && def.preview_zoom < 20.0)) {
        fail(where(*preview, "\"preview.zoom\" must lie in (0.05, 20)"));
    }
}

// The per-type traits (CharacterDef); null puts a trait back to its default.
void merge_traits(const Value& entry, CharacterDef& def) {
    const CharacterDef defaults;
    if (const Value* blood = entry.find("blood")) {
        if (blood->is_null()) {
            def.blood = defaults.blood;
        } else {
            merge_string(entry, "blood", def.blood);
        }
    }
    merge_bool(entry, "blood_at_zero_health", def.blood_at_zero_health, defaults.blood_at_zero_health);
    merge_int(entry, "blood_strength", def.blood_strength, defaults.blood_strength);
    merge_int(entry, "fresh_hit_blood_strength", def.fresh_hit_blood_strength, defaults.fresh_hit_blood_strength);
    merge_int(entry, "fresh_hit_blood_strength_scatter", def.fresh_hit_blood_strength_scatter,
              defaults.fresh_hit_blood_strength_scatter);
    merge_bool(entry, "blood_follows_body", def.blood_follows_body, defaults.blood_follows_body);
    merge_int(entry, "blood_depth_bias", def.blood_depth_bias, defaults.blood_depth_bias);
    merge_string(entry, "impact_sound", def.impact_sound);
    merge_bool(entry, "is_player", def.is_player, defaults.is_player);
    merge_bool(entry, "enemy", def.enemy, defaults.enemy);
    merge_bool(entry, "is_boss", def.is_boss, defaults.is_boss);
    if (const Value* acid = entry.find("acid_attack"); acid != nullptr && acid->is_string()) {
        def.acid_attack_name = acid->string();
        def.acid_attack = 0;
    } else {
        merge_int(entry, "acid_attack", def.acid_attack, defaults.acid_attack);
        if (acid != nullptr) {
            def.acid_attack_name.clear();
        }
    }
    merge_bool(entry, "single_clip_table", def.single_clip_table, defaults.single_clip_table);
    merge_string(entry, "state_layout", def.state_layout);
    if (const Value* pool = entry.find("pool")) {
        if (pool->is_null()) {
            def.pool.reset();
        } else if (pool->is_bool()) {
            def.pool = pool->boolean();
        } else {
            fail(where(*pool, "\"pool\" must be true, false or null"));
        }
    }
    if (const Value* files = entry.find("pool_files")) {
        if (files->is_null()) {
            def.pool_model.clear();
            def.pool_texture.clear();
            def.pool_anim.clear();
        } else if (files->is_object()) {
            merge_string(*files, "model", def.pool_model);
            merge_string(*files, "texture", def.pool_texture);
            merge_string(*files, "anim", def.pool_anim);
        } else {
            fail(where(*files, "\"pool_files\" must be an object or null"));
        }
    }
}

void merge_character(const Value& entry, CharacterDef& def, const audio::SoundRegistry* registry,
                     std::vector<std::string>* notes) {
    merge_string(entry, "description", def.description);
    merge_strings(entry, "maps", def.maps);
    merge_string(entry, "category", def.category);
    merge_string(entry, "behavior", def.behavior);
    merge_files(entry, def, notes);
    merge_number(entry, "shadow", def.shadow);
    merge_string(entry, "blend_mode", def.blend_mode);
    merge_string(entry, "camera_anchor", def.camera_anchor);
    merge_number(entry, "camera_offset_y", def.camera_offset_y);
    merge_sound_table(entry, "sounds", def.sounds, def.sound_names, registry);
    merge_sound_table(entry, "footsteps", def.footsteps, def.footstep_names, registry);
    merge_named(entry, "params", def.params);
    merge_named(entry, "anims", def.anims);
    merge_named(entry, "states", def.states);
    merge_named(entry, "controls", def.controls);
    merge_spawn(entry, def);
    merge_preview(entry, def);
    merge_strings(entry, "harry_reactions", def.harry_reactions);
    merge_strings(entry, "clip_tables", def.clip_tables);
    merge_traits(entry, def);
}

std::string bare_name(std::string_view enum_name) {
    if (enum_name.starts_with("Chara_")) {
        enum_name.remove_prefix(6);
    }
    return std::string(enum_name);
}

CharaNumbering& default_numbering_storage() {
    static CharaNumbering numbering;
    return numbering;
}

// An entry of the old layout: numbered by its "id", named by its "enum" ("Chara_Harry").
CharacterDef parse_character(const Value& entry, const audio::SoundRegistry* registry,
                             std::vector<std::string>* notes) {
    if (!entry.is_object()) {
        fail(where(entry, "a character must be an object"));
    }
    const Value* id = entry.find("id");
    if (!id || !id->is_integer() || id->integer() < 0 || id->integer() > std::numeric_limits<int>::max()) {
        fail(where(entry, "missing or invalid \"id\" (a non-negative integer)"));
    }
    CharacterDef def;
    def.number = static_cast<int>(id->integer());
    const Value* name = entry.find("enum");
    def.folder = name && name->is_string() ? bare_name(name->string()) : "Chara" + std::to_string(def.number);
    def.id = ids::qualify(def.folder);
    merge_character(entry, def, registry, notes);
    return def;
}

// The keys of the earlier layout: the folder name is the identifier.
void note_numeric_keys(const Value& entry, std::vector<std::string>* notes) {
    if (notes != nullptr && (entry.find("id") != nullptr || entry.find("enum") != nullptr)) {
        notes->push_back("\"id\" / \"enum\" are ignored: the folder name is the identifier (docs/modding/data.md)");
    }
}

std::string lower_alnum(std::string_view text) {
    std::string out;
    for (const char c : text) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
    }
    return out;
}

bool valid_key(std::string_view name) {
    if (name.empty()) {
        return false;
    }
    return std::all_of(name.begin(), name.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
    });
}

Result<std::string> read_file(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        return Result<std::string>::fail("cannot read " + file.string());
    }
    return Result<std::string>::ok(
        std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>()));
}

// Character folders of one chara/ directory (any subdirectory: a mod folder may hold only
// Lua), sorted by name for a stable order.
std::vector<std::filesystem::path> character_folders(const std::filesystem::path& dir) {
    namespace fs = std::filesystem;
    std::vector<fs::path> dirs;
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) {
        return dirs;
    }
    for (const fs::directory_entry& entry : fs::directory_iterator(dir, ec)) {
        if (entry.is_directory(ec)) {
            dirs.push_back(entry.path());
        }
    }
    std::sort(dirs.begin(), dirs.end());
    return dirs;
}

// Flat patch files of one chara/ directory (the old layout), sorted by name.
std::vector<std::filesystem::path> patch_files(const std::filesystem::path& dir) {
    namespace fs = std::filesystem;
    std::vector<fs::path> files;
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) {
        return files;
    }
    for (const fs::directory_entry& entry : fs::directory_iterator(dir, ec)) {
        const fs::path& path = entry.path();
        if (entry.is_regular_file(ec) && path.extension() == ".json" && path.filename() != "characters.json") {
            files.push_back(path);
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

}  // namespace

bool patch_changes_look_only(std::string_view patch_text) {
    const Result<Value> doc = json::parse(patch_text);
    if (!doc || !doc.value().is_object()) {
        return false;
    }
    for (const auto& [key, value] : doc.value().members()) {
        const bool look = key == "model" || key == "textures" || key == "shadow" || key == "blend_mode" ||
                          key == "description" || key.starts_with("_") || key.starts_with("$");
        if (!look) {
            return false;
        }
    }
    return true;
}

const char* sfx_slot_name(Pc_CharaSfxSlot slot) {
    switch (slot) {
        case PC_CHARA_SFX_FOOTSTEP: return "footstep";
        case PC_CHARA_SFX_FOOTSTEP_GRASS: return "footstep_grass";
        case PC_CHARA_SFX_FOOTSTEP_METAL: return "footstep_metal";
        case PC_CHARA_SFX_DEATH: return "death";
        case PC_CHARA_SFX_ATTACK: return "attack";
        case PC_CHARA_SFX_DAMAGE: return "damage";
    }
    return nullptr;
}

std::int32_t to_q12(double value) {
    // The original Q12(x) is (s32)(x * 4096.0f) on a float literal: float multiply, then
    // truncation toward zero. Keep both steps in float so data and code agree to the bit.
    volatile float scaled = static_cast<float>(value) * 4096.0f;
    return static_cast<std::int32_t>(scaled);
}

// ---- Character -------------------------------------------------------------------------

int Character::sfx(std::string_view slot, int fallback) const {
    if (def_ == nullptr) {
        return fallback;
    }
    const auto it = def_->sounds.find(slot);
    return it != def_->sounds.end() && it->second >= 0 ? it->second : fallback;
}

int Character::footstep(std::string_view surface, int fallback) const {
    if (def_ == nullptr) {
        return fallback;
    }
    auto it = def_->footsteps.find(surface);
    if (it == def_->footsteps.end()) {
        it = def_->footsteps.find("*");
    }
    return it != def_->footsteps.end() && it->second >= 0 ? it->second : fallback;
}

double Character::param(std::string_view name, double fallback) const {
    if (def_ == nullptr) {
        return fallback;
    }
    const auto it = def_->params.find(name);
    return it != def_->params.end() ? it->second : fallback;
}

std::int32_t Character::param_q12(std::string_view name, std::int32_t fallback) const {
    if (def_ == nullptr) {
        return fallback;
    }
    const auto it = def_->params.find(name);
    return it != def_->params.end() ? to_q12(it->second) : fallback;
}

std::int32_t Character::param_int(std::string_view name, std::int32_t fallback) const {
    if (def_ == nullptr) {
        return fallback;
    }
    const auto it = def_->params.find(name);
    return it != def_->params.end() ? static_cast<std::int32_t>(it->second) : fallback;
}

int Character::anim(std::string_view name, int fallback) const {
    if (def_ == nullptr) {
        return fallback;
    }
    const auto it = def_->anims.find(name);
    return it != def_->anims.end() && it->second >= 0 ? it->second : fallback;
}

int Character::state(std::string_view name, int fallback) const {
    if (def_ == nullptr) {
        return fallback;
    }
    const auto it = def_->states.find(name);
    return it != def_->states.end() && it->second >= 0 ? it->second : fallback;
}

int Character::control(std::string_view name, int fallback) const {
    if (def_ == nullptr) {
        return fallback;
    }
    const auto it = def_->controls.find(name);
    return it != def_->controls.end() && it->second >= 0 ? it->second : fallback;
}

// ---- CharaLibrary ----------------------------------------------------------------------

void CharaLibrary::sort() {
    std::stable_sort(characters_.begin(), characters_.end(),
                     [](const CharacterDef& a, const CharacterDef& b) { return a.number < b.number; });
}

void CharaLibrary::set_default_numbering(CharaNumbering numbering) {
    default_numbering_storage() = std::move(numbering);
}

const CharaNumbering& CharaLibrary::default_numbering() { return default_numbering_storage(); }

Result<CharaLibrary> CharaLibrary::parse(std::string_view text, std::vector<std::string>& warnings,
                                         const audio::SoundRegistry* sounds) try {
    const Result<Value> doc = json::parse(text);
    if (!doc) {
        return Result<CharaLibrary>::fail("characters.json: " + doc.error());
    }
    const Value* characters = doc.value().find("characters");
    if (!characters || !characters->is_array()) {
        return Result<CharaLibrary>::fail("characters.json: no \"characters\" array");
    }
    CharaLibrary library;
    if (sounds != nullptr) {
        library.sounds_ = *sounds;
    }
    std::vector<std::string> notes;
    for (const Value& entry : characters->array()) {
        library.characters_.push_back(parse_character(entry, sounds, &notes));
    }
    for (std::string& note : notes) {
        warnings.push_back("characters.json: " + std::move(note));
    }
    library.sort();
    return Result<CharaLibrary>::ok(std::move(library));
} catch (const std::exception& e) {
    return Result<CharaLibrary>::fail(std::string(e.what()));
}

Result<int> CharaLibrary::apply_patch(std::string_view text, std::string_view origin, std::string_view target,
                                      std::vector<std::string>* warnings) try {
    std::vector<std::string> notes;
    const Result<Value> doc = json::parse(text);
    if (!doc) {
        return Result<int>::fail(std::string(origin) + ": " + doc.error());
    }
    std::vector<const Value*> entries;
    if (const Value* list = doc.value().find("characters"); list && list->is_array()) {
        for (const Value& entry : list->array()) {
            entries.push_back(&entry);
        }
    } else if (doc.value().is_object()) {
        entries.push_back(&doc.value());
    } else {
        return Result<int>::fail(std::string(origin) + ": a patch must be a character object");
    }
    // Validate everything before touching the library: a bad patch changes nothing.
    std::vector<CharacterDef> merged = characters_;
    auto by_id = [&](std::string_view id) -> CharacterDef* {
        const CharacterDef* found = find_by_id(id);
        if (found == nullptr) {
            return nullptr;
        }
        for (CharacterDef& c : merged) {
            if (c.id == found->id) {
                return &c;
            }
        }
        return nullptr;
    };
    int applied = 0;
    for (const Value* entry : entries) {
        if (!entry->is_object()) {
            fail(where(*entry, "a character must be an object"));
        }
        CharacterDef* character = nullptr;
        if (!target.empty()) {
            note_numeric_keys(*entry, &notes);
            character = by_id(target);
            if (character == nullptr) {
                fail(where(*entry, "no character \"" + std::string(target) + "\" to patch"));
            }
        } else if (const Value* id = entry->find("id"); id && id->is_integer()) {
            // The old layout: a patch by number, or a new character with a number of its own.
            for (CharacterDef& c : merged) {
                if (c.number == id->integer()) {
                    character = &c;
                }
            }
            if (character == nullptr) {
                merged.push_back(parse_character(*entry, &sounds_, &notes));
                ++applied;
                continue;
            }
        } else if (const Value* name = entry->find("enum"); name && name->is_string()) {
            character = by_id(ids::qualify(bare_name(name->string())));
            if (character == nullptr) {
                fail(where(*entry, "no character \"" + name->string() + "\" to patch"));
            }
        } else {
            fail(where(*entry, "a patch names its character by its folder (chara/<Name>/character.json)"));
        }
        merge_character(*entry, *character, &sounds_, &notes);
        ++applied;
    }
    characters_ = std::move(merged);
    if (warnings != nullptr) {
        for (std::string& note : notes) {
            warnings->push_back(std::string(origin) + ": " + std::move(note));
        }
    }
    sort();
    return Result<int>::ok(applied);
} catch (const std::exception& e) {
    return Result<int>::fail(std::string(origin) + ": " + e.what());
}

Result<CharaLibrary> CharaLibrary::load(const std::filesystem::path& gamedata_dir,
                                        const std::filesystem::path& mods_dir,
                                        std::vector<std::string>& warnings, LoadSource* source) {
    namespace fs = std::filesystem;
    const CharaNumbering& numbering = default_numbering();
    const fs::path legacy_file = fs::path("chara") / "characters.json";
    std::error_code ec;
    std::vector<mods::ModInfo> found;
    if (!mods_dir.empty() && fs::is_directory(mods_dir, ec)) {
        std::vector<std::string> mod_warnings;
        found = mods::discover(mods_dir, mod_warnings);
        for (const std::string& w : mod_warnings) {
            warnings.push_back("mods: " + w);
        }
    }
    Result<audio::SoundRegistry> sounds = audio::SoundRegistry::load(gamedata_dir, mods_dir, warnings);
    const audio::SoundRegistry registry = sounds ? std::move(sounds.value()) : audio::SoundRegistry{};
    auto note_legacy = [&](const std::string& what) {
        warnings.push_back(what + " uses the old character layout; move it to chara/<Name>/character.json "
                                  "(docs/modding/data.md)");
        if (source) {
            source->legacy = true;
        }
    };

    // The base set: a mod's old single file replaces it whole (highest priority first).
    Result<CharaLibrary> library = Result<CharaLibrary>::fail("no character data");
    const mods::ModInfo* replacing = nullptr;
    for (auto it = found.rbegin(); it != found.rend(); ++it) {
        if (fs::is_regular_file(it->root / legacy_file, ec)) {
            replacing = &*it;
            break;
        }
    }
    if (replacing != nullptr) {
        const fs::path file = replacing->root / legacy_file;
        if (source) {
            source->mod_id = replacing->id;
            source->mod_gameplay = replacing->gameplay;
        }
        if (!replacing->gameplay) {
            warnings.push_back("mod " + replacing->id + " changes character data (game data) but does not "
                                                        "declare \"gameplay\": true in mod.json");
        }
        note_legacy(file.string());
        const Result<std::string> text = read_file(file);
        if (!text) {
            return Result<CharaLibrary>::fail(text.error());
        }
        library = parse(text.value(), warnings, &registry);
    } else {
        CharaLibrary base;
        base.sounds_ = registry;
        try {
            for (const fs::path& dir : character_folders(gamedata_dir / "chara")) {
                const fs::path file = dir / "character.json";
                if (!fs::is_regular_file(file, ec)) {
                    continue;
                }
                const Result<std::string> text = read_file(file);
                if (!text) {
                    return Result<CharaLibrary>::fail(text.error());
                }
                const Result<Value> doc = json::parse(text.value());
                if (!doc) {
                    return Result<CharaLibrary>::fail(file.string() + ": " + doc.error());
                }
                std::vector<std::string> notes;
                CharacterDef def;
                def.folder = dir.filename().string();
                def.id = ids::qualify(def.folder);
                const int number = numbering ? numbering(def.id) : -1;
                if (number < 0) {
                    warnings.push_back(file.string() + ": " + def.id + " has no number in this build (folder ignored)");
                    continue;
                }
                def.number = number;
                note_numeric_keys(doc.value(), &notes);
                merge_character(doc.value(), def, &registry, &notes);
                base.characters_.push_back(std::move(def));
                for (std::string& note : notes) {
                    warnings.push_back(file.string() + ": " + std::move(note));
                }
            }
        } catch (const std::exception& e) {
            return Result<CharaLibrary>::fail(e.what());
        }
        if (!base.characters_.empty()) {
            base.sort();
            library = Result<CharaLibrary>::ok(std::move(base));
        } else if (fs::is_regular_file(gamedata_dir / legacy_file, ec)) {
            note_legacy((gamedata_dir / legacy_file).string());
            const Result<std::string> text = read_file(gamedata_dir / legacy_file);
            if (!text) {
                return Result<CharaLibrary>::fail(text.error());
            }
            library = parse(text.value(), warnings, &registry);
        } else {
            return Result<CharaLibrary>::fail("no character data in " + (gamedata_dir / "chara").string());
        }
    }
    if (!library) {
        return library;
    }
    CharaLibrary& lib = library.value();

    // Patches; a broken one is skipped with a warning, the rest still apply.
    auto apply_file = [&](const fs::path& patch, std::string_view target, const std::string& mod_id, bool gameplay) {
        const Result<std::string> patch_text = read_file(patch);
        if (!patch_text) {
            warnings.push_back(patch_text.error());
            return;
        }
        const Result<int> applied = lib.apply_patch(patch_text.value(), patch.string(), target, &warnings);
        if (!applied) {
            warnings.push_back(applied.error() + " (patch skipped)");
            return;
        }
        // A patch of the look alone (another model, texture, shadow) changes no rule of the game.
        if (!mod_id.empty() && !gameplay && !(!target.empty() && patch_changes_look_only(patch_text.value()))) {
            warnings.push_back("mod " + mod_id + " patches character data (" +
                               (target.empty() ? patch.filename().string()
                                               : patch.parent_path().filename().string() + "/character.json") +
                               ") but does not declare \"gameplay\": true in mod.json");
        }
        if (source) {
            source->patches.push_back(patch.string());
        }
    };
    // A mod's chara/<Name>/: the base character of that name, else the mod's own <ns>:<Name>,
    // which a character.json there adds.
    auto apply_dir = [&](const fs::path& chara_dir, const mods::ModInfo* mod) {
        const std::string mod_id = mod != nullptr ? mod->id : std::string();
        const bool gameplay = mod == nullptr || mod->gameplay;
        const std::string ns = mod != nullptr ? mod->id : std::string(ids::kBaseNamespace);
        std::vector<std::string> refused;
        const bool lua_ok = mod == nullptr || mods::scripts_allowed(*mod, refused);
        for (const std::string& w : refused) {
            warnings.push_back("mods: " + w);
        }
        for (const fs::path& patch : patch_files(chara_dir)) {
            note_legacy(patch.string());
            apply_file(patch, {}, mod_id, gameplay);
        }
        for (const fs::path& dir : character_folders(chara_dir)) {
            const std::string folder = dir.filename().string();
            std::string target = ids::qualify(folder);
            if (mod != nullptr && lib.find_by_id(target) == nullptr) {
                target = ids::qualify(folder, ns);
            }
            const fs::path file = dir / "character.json";
            if (mod != nullptr && fs::is_regular_file(file, ec)) {
                const Result<std::string> text = read_file(file);
                const Result<Value> doc = text ? json::parse(text.value()) : Result<Value>::fail("");
                const bool old_keys = doc && doc.value().is_object() &&
                                      (doc.value().find("id") != nullptr || doc.value().find("enum") != nullptr);
                if (old_keys) {
                    // The old way: the file says which character it is; the folder still holds its files.
                    note_legacy(file.string());
                    apply_file(file, {}, mod_id, gameplay);
                    if (const Value* e = doc.value().find("enum"); e && e->is_string()) {
                        target = ids::qualify(bare_name(e->string()));
                    } else if (const Value* id = doc.value().find("id"); id && id->is_integer()) {
                        const CharacterDef* c = id->integer() >= 0 && id->integer() <= std::numeric_limits<int>::max()
                                                       ? lib.find(static_cast<int>(id->integer()))
                                                       : nullptr;
                        target = c != nullptr ? c->id : target;
                    }
                } else {
                    if (lib.find_by_id(target) == nullptr) {
                        const int number = numbering ? numbering(target) : -1;
                        if (number < 0 || lib.find(number) != nullptr) {
                            warnings.push_back(dir.string() + ": no free character number for " + target +
                                               " (folder ignored)");
                            continue;
                        }
                        CharacterDef def;
                        def.number = number;
                        def.id = target;
                        def.folder = folder;
                        lib.characters_.push_back(std::move(def));
                        lib.sort();
                    }
                    apply_file(file, target, mod_id, gameplay);
                }
            }
            if (const CharacterDef* c = lib.find_by_id(target)) {
                lib.folders_[c->number].push_back(dir);
                if (mod != nullptr && !lua_ok) {
                    lib.lua_blocked_.push_back(dir);
                }
            } else if (mod != nullptr || fs::is_regular_file(file, ec)) {
                warnings.push_back(dir.string() + ": no character of that name (folder ignored)");
            }
        }
    };
    apply_dir(gamedata_dir / "chara", nullptr);
    for (const mods::ModInfo& mod : found) {  // ascending priority: the last one wins
        apply_dir(mod.root / "chara", &mod);
    }
    const fs::path blood_file = fs::path("effects") / "blood.json";
    if (fs::is_regular_file(gamedata_dir / blood_file, ec)) {
        Result<mods::MergedFile> blood = mods::read_data_file(gamedata_dir, mods_dir, blood_file, warnings);
        if (blood) {
            lib.resolve_blood(blood.value().text, (gamedata_dir / blood_file).string(), warnings);
        } else {
            warnings.push_back(blood.error());
        }
    }
    return library;
}

void CharaLibrary::resolve_blood(std::string_view blood_json, std::string_view origin,
                                 std::vector<std::string>& warnings) {
    std::map<std::string, int, std::less<>> kinds;
    const Result<Value> doc = json::parse(blood_json);
    const Value* table = doc && doc.value().is_object() ? doc.value().find("kinds") : nullptr;
    if (table == nullptr || !table->is_object()) {
        warnings.push_back(std::string(origin) + ": no \"kinds\" object");
        return;
    }
    for (const auto& [name, entry] : table->members()) {
        if (name.empty() || name.front() == '_') {
            continue;
        }
        const Value* kind = entry.is_object() ? entry.find("kind") : nullptr;
        if (kind == nullptr || !kind->is_integer()) {
            warnings.push_back(std::string(origin) + ": blood \"" + name + "\" has no integer \"kind\"");
            continue;
        }
        kinds[name] = static_cast<int>(kind->integer());
    }
    for (CharacterDef& def : characters_) {
        const auto it = kinds.find(def.blood);
        if (it != kinds.end()) {
            def.blood_kind = it->second;
        } else {
            warnings.push_back(def.id + ": unknown blood \"" + def.blood + "\" (not in " + std::string(origin) +
                               "); the default blood is used");
            def.blood_kind = CharacterDef::kDefaultBloodKind;
        }
    }
}

std::vector<std::filesystem::path> CharaLibrary::folders(int id) const {
    const auto it = folders_.find(id);
    return it != folders_.end() ? it->second : std::vector<std::filesystem::path>{};
}

std::filesystem::path CharaLibrary::script_file(int id, std::string_view file) const {
    const auto it = folders_.find(id);
    if (it == folders_.end()) {
        return {};
    }
    std::error_code ec;
    const bool lua = mods::is_lua_path(file);
    for (auto dir = it->second.rbegin(); dir != it->second.rend(); ++dir) {  // highest priority first
        if (lua && std::find(lua_blocked_.begin(), lua_blocked_.end(), *dir) != lua_blocked_.end()) {
            continue;
        }
        if (std::filesystem::is_regular_file(*dir / file, ec)) {
            return *dir / file;
        }
    }
    return {};
}

std::vector<std::filesystem::path> CharaLibrary::script_files(int id, std::string_view file) const {
    std::vector<std::filesystem::path> out;
    const auto it = folders_.find(id);
    if (it == folders_.end()) {
        return out;
    }
    std::error_code ec;
    const bool lua = mods::is_lua_path(file);
    for (const std::filesystem::path& dir : it->second) {
        if (lua && std::find(lua_blocked_.begin(), lua_blocked_.end(), dir) != lua_blocked_.end()) {
            continue;
        }
        if (std::filesystem::is_regular_file(dir / file, ec)) {
            out.push_back(dir / file);
        }
    }
    return out;
}

Result<std::string> CharaLibrary::data_file(int id, std::string_view file, std::string* source,
                                            std::vector<std::string>& warnings) const {
    namespace fs = std::filesystem;
    const auto it = folders_.find(id);
    std::vector<fs::path> copies;
    std::error_code ec;
    if (it != folders_.end()) {
        for (const fs::path& dir : it->second) {  // base first, then mods in load order
            if (fs::is_regular_file(dir / file, ec)) {
                copies.push_back(dir / file);
            }
        }
    }
    if (copies.empty()) {
        return Result<std::string>::fail("no file " + std::string(file));
    }
    // The first copy is the base; the folders after it are mods' chara/<Name>/ (their root two levels up).
    std::vector<mods::DataLayer> layers;
    for (std::size_t i = 1; i < copies.size(); ++i) {
        const fs::path root = copies[i].parent_path().parent_path().parent_path();
        layers.push_back({root.filename().string(), copies[i], root, true});
    }
    const std::string relative = "chara/" + copies.front().parent_path().filename().string() + "/" + std::string(file);
    Result<mods::MergedFile> merged = mods::merge_layers(relative, copies.front(), layers, warnings);
    if (!merged) {
        return Result<std::string>::fail(merged.error());
    }
    mods::emit_merge_trace(merged.value());
    if (source != nullptr) {
        *source = merged.value().source();
    }
    return Result<std::string>::ok(std::move(merged.value().text));
}

const CharacterDef* CharaLibrary::find(int id) const {
    const auto it = std::lower_bound(characters_.begin(), characters_.end(), id,
                                     [](const CharacterDef& c, int id) { return c.number < id; });
    return it != characters_.end() && it->number == id ? &*it : nullptr;
}

const CharacterDef* CharaLibrary::find_by_id(std::string_view id) const {
    if (id.empty()) {
        return nullptr;
    }
    auto exact = [&](std::string_view full) -> const CharacterDef* {
        for (const CharacterDef& c : characters_) {
            if (c.id == full) {
                return &c;
            }
        }
        return nullptr;
    };
    if (id.find(':') != std::string_view::npos) {
        return exact(id);
    }
    if (const CharacterDef* c = exact(ids::qualify(id))) {
        return c;
    }
    const CharacterDef* found = nullptr;
    for (const CharacterDef& c : characters_) {
        if (ids::local_name(c.id) == id) {
            if (found != nullptr) {
                return nullptr;  // two mods have it: the caller has to say which
            }
            found = &c;
        }
    }
    return found;
}

int CharaLibrary::find_id(std::string_view name) const {
    if (name.empty()) {
        return -1;
    }
    int number = 0;
    const auto [end, ec] = std::from_chars(name.data(), name.data() + name.size(), number);
    if (ec == std::errc() && end == name.data() + name.size()) {
        return number >= 0 && find(number) ? number : -1;
    }
    if (name.starts_with("Chara_")) {
        name.remove_prefix(6);
    }
    if (const CharacterDef* c = find_by_id(name)) {
        return c->number;
    }
    // Any spelling of a base name: "groaner", "ghost_child_alessa".
    const std::string wanted = lower_alnum(name);
    for (const CharacterDef& c : characters_) {
        if (ids::is_base(c.id) && lower_alnum(ids::local_name(c.id)) == wanted) {
            return c.number;
        }
    }
    return -1;
}

int CharaLibrary::sfx(int id, Pc_CharaSfxSlot slot) const {
    const char* name = sfx_slot_name(slot);
    return name != nullptr ? character(id).sfx(name) : -1;
}

namespace {
constexpr std::string_view kSpawnCategoryOrder[] = {"CommonEnemies", "OtherworldEnemies", "Bosses",
                                                    "StoryCharacters", "Animals", "Other"};
constexpr std::string_view kModsCategory = "Mods";
}  // namespace

std::string CharaLibrary::spawn_category(const CharacterDef& def) {
    if (!def.category.empty()) return def.category;
    constexpr std::string_view base = "silenthill:";
    return def.id.compare(0, base.size(), base) == 0 || def.id.find(':') == std::string::npos
               ? "Other"
               : std::string(kModsCategory);
}

std::vector<std::string> CharaLibrary::spawn_categories() const {
    std::vector<std::string> used;
    for (const CharacterDef& d : characters_) {
        const std::string c = spawn_category(d);
        if (std::find(used.begin(), used.end(), c) == used.end()) used.push_back(c);
    }
    auto rank = [](const std::string& c) {
        for (std::size_t i = 0; i < std::size(kSpawnCategoryOrder); ++i)
            if (c == kSpawnCategoryOrder[i]) return static_cast<int>(i);
        return c == kModsCategory ? 1000 : 100;
    };
    std::stable_sort(used.begin(), used.end(), [&](const std::string& a, const std::string& b) {
        const int ra = rank(a), rb = rank(b);
        return ra != rb ? ra < rb : a < b;
    });
    return used;
}

std::vector<std::string> CharaLibrary::check_consistency() const {
    std::vector<std::string> problems;
    for (std::size_t i = 1; i < characters_.size(); ++i) {
        if (characters_[i - 1].number == characters_[i].number) {
            problems.push_back("character number " + std::to_string(characters_[i].number) + " given twice");
        }
    }
    for (const CharacterDef& c : characters_) {
        if (!c.spawn_kind.empty() && c.spawn_kind != "enemy" && c.spawn_kind != "boss" && c.spawn_kind != "actor" &&
            c.spawn_kind != "prop") {
            problems.push_back(c.id + ": unknown spawn.kind \"" + c.spawn_kind + "\"");
        }
        if (!c.spawn_auto.empty() && c.spawn_auto != "full" && c.spawn_auto != "fallback" && c.spawn_auto != "none") {
            problems.push_back(c.id + ": unknown spawn.auto \"" + c.spawn_auto + "\"");
        }
        if (!c.blend_mode.empty() && c.blend_mode != "average" && c.blend_mode != "additive" &&
            c.blend_mode != "subtractive") {
            problems.push_back(c.id + ": unknown blend_mode \"" + c.blend_mode + "\"");
        }
        if (!c.camera_anchor.empty() && c.camera_anchor != "character" && c.camera_anchor != "ground" &&
            c.camera_anchor != "camera") {
            problems.push_back(c.id + ": unknown camera_anchor \"" + c.camera_anchor + "\"");
        }
        auto converted = [&](const std::string& file, std::string_view tail) {
            if (file.empty()) {
                return;
            }
            if (file.front() == '/' || file.find("..") != std::string::npos || file.find('\\') != std::string::npos ||
                !ends_with_nocase(file, tail)) {
                problems.push_back(c.id + ": file \"" + file + "\" is not a converted " + std::string(tail) +
                                   " under convert/");
            }
        };
        converted(c.model, ".glb");
        converted(c.animations, ".anim.json");
        for (const std::string& texture : c.textures) {
            converted(texture, ".png");
        }
        for (const std::string* file : {&c.disc_anim, &c.disc_model, &c.disc_texture}) {
            if (!file->empty() && file->find('/') == std::string::npos) {
                problems.push_back(c.id + ": file \"" + *file + "\" is not a disc path");
            }
        }
        for (const std::string& map : c.maps) {
            if (!valid_key(map)) {
                problems.push_back(c.id + ": map \"" + map + "\" is not a map name");
            }
        }
        for (const auto& [slot, id] : c.sounds) {
            if (!valid_key(slot)) {
                problems.push_back(c.id + ": sound slot \"" + slot + "\" is not snake_case");
            }
            if (id < 0 || id > 0xFFFF) {
                problems.push_back(c.id + ": sound " + slot + " = " + std::to_string(id) + " is not a sfx id");
            }
        }
        for (const auto& [surface, id] : c.footsteps) {
            if (surface != "*" && !valid_key(surface)) {
                problems.push_back(c.id + ": footstep surface \"" + surface + "\" is not snake_case");
            }
            if (id < 0 || id > 0xFFFF) {
                problems.push_back(c.id + ": footstep " + surface + " = " + std::to_string(id) + " is not a sfx id");
            }
        }
        for (const auto& [name, value] : c.params) {
            if (!valid_key(name)) {
                problems.push_back(c.id + ": param \"" + name + "\" is not snake_case");
            }
            if (!std::isfinite(value)) {
                problems.push_back(c.id + ": param " + name + " is not finite");
            }
        }
        for (const auto& [name, index] : c.anims) {
            if (!valid_key(name)) {
                problems.push_back(c.id + ": anim \"" + name + "\" is not snake_case");
            }
            if (index < 0) {
                problems.push_back(c.id + ": anim " + name + " has a negative index");
            }
        }
    }
    return problems;
}

}  // namespace sh::chara
