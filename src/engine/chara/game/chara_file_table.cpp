// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/chara/game/chara_file_table.h"

#include <array>
#include <string>
#include <vector>

#include "engine/game_state/game_state.h"
#include "engine/loader/queue/disc_files.h"
#include "engine/chara/chara_library.h"
#include "engine/chara/game/chara_link.h"
#include "engine/log/sh_log.h"
#include "engine/mods/mods.h"

namespace sh::chara::game {
namespace {


std::array<CharaFileRow, kCharaFileRows> s_rows;
std::array<std::int16_t, kCharaFileRows> s_data_model = [] {
    std::array<std::int16_t, kCharaFileRows> none;
    none.fill(-1);
    return none;
}();
CharaFileRow s_scratch;
bool s_loaded = false;

// The data names converted files; the file table knows disc files. The import's manifests say which disc file each
// converted one came from, so the disc path stays out of character.json.
std::string disc_path_of(const ConvertedSources& sources, const std::string& converted, const std::string& legacy,
                         const char* chara, const char* what) {
    if (converted.empty()) {
        return legacy;
    }
    std::string disc = sources.disc_path(converted);
    if (disc.empty()) {
        SH_WARN("[CHARA] %s: %s %s has no import manifest naming its disc file", chara, what, converted.c_str());
    }
    return disc;
}

int file_of(const std::string& path, const char* chara, const char* what) {
    if (path.empty()) {
        return -1;
    }
    const int file = sh::loader::disc_files().find(path.c_str());
    if (file < 0) {
        SH_WARN("[CHARA] %s: %s file %s is not in the file table", chara, what, path.c_str());
    }
    return file;
}

std::uint8_t blend_of(const std::string& name, std::uint8_t keep) {
    if (name == "average") return static_cast<std::uint8_t>(CharaBlend::Average);
    if (name == "additive") return static_cast<std::uint8_t>(CharaBlend::Additive);
    if (name == "subtractive") return static_cast<std::uint8_t>(CharaBlend::Subtractive);
    return keep;
}

std::uint8_t anchor_of(const std::string& name, std::uint8_t keep) {
    if (name == "character") return static_cast<std::uint8_t>(CameraAnchor::Character);
    if (name == "ground") return static_cast<std::uint8_t>(CameraAnchor::Ground);
    if (name == "camera") return static_cast<std::uint8_t>(CameraAnchor::Camera);
    return keep;
}

}  // namespace

CharaFileRow& chara_file_row(int chara_id) {
    if (chara_id < 0 || chara_id >= kCharaFileRows) {
        s_scratch = CharaFileRow{};
        return s_scratch;
    }
    return s_rows[static_cast<std::size_t>(chara_id)];
}

std::int32_t chara_data_model_file(int chara_id) {
    return chara_id >= 0 && chara_id < kCharaFileRows ? s_data_model[static_cast<std::size_t>(chara_id)] : -1;
}

void chara_files_reload(const std::filesystem::path& gamedata_dir, const std::filesystem::path& mods_dir) {
    s_rows.fill(CharaFileRow{});
    s_data_model.fill(-1);
    s_loaded = false;
    chara_files_load(gamedata_dir, mods_dir);
}

void chara_files_load(const std::filesystem::path& gamedata_dir, const std::filesystem::path& mods_dir) {
    if (s_loaded) {
        return;
    }
    s_loaded = true;
    std::vector<std::string> warnings;
    LoadSource source;
    auto library = CharaLibrary::load(gamedata_dir, mods_dir, warnings, &source);
    if (!library) {
        SH_ERR("[CHARA] %s: no character has files", library.error().c_str());
        return;
    }
    for (const std::string& warning : warnings) {
        SH_WARN("[CHARA] %s", warning.c_str());
    }
    for (const std::string& problem : library.value().check_consistency()) {
        SH_WARN("[CHARA] %s", problem.c_str());
    }
    // A mod's own converted files name their disc files in the manifests next to them (mods/<mod>/convert/),
    // the highest-priority mod first.
    std::vector<std::filesystem::path> mod_convert_roots;
    if (!mods_dir.empty()) {
        std::vector<std::string> mod_warnings;
        const std::vector<mods::ModInfo> mods = mods::discover(mods_dir, mod_warnings);
        for (auto it = mods.rbegin(); it != mods.rend(); ++it) {
            mod_convert_roots.push_back(it->root / "convert");
        }
    }
    const ConvertedSources sources = ConvertedSources::load(gamedata_dir, std::move(mod_convert_roots));

    int applied = 0;
    for (const CharacterDef& def : library.value().characters()) {
        if (def.number >= kCharaFileRows) {
            SH_WARN("[CHARA] %s: number %d is past the file table", def.id.c_str(), def.number);
            continue;
        }
        CharaFileRow& row = s_rows[def.number];
        const char* name = def.id.c_str();
        row.anim = static_cast<std::int16_t>(
            file_of(disc_path_of(sources, def.animations, def.disc_anim, name, "animations"), name, "anim"));
        row.model = static_cast<std::int16_t>(
            file_of(disc_path_of(sources, def.model, def.disc_model, name, "model"), name, "model"));
        s_data_model[static_cast<std::size_t>(def.number)] = row.model;
        const std::string texture = def.textures.empty() ? std::string() : def.textures.front();
        row.texture = static_cast<std::int16_t>(
            file_of(disc_path_of(sources, texture, def.disc_texture, name, "texture"), name, "texture"));
        row.blend = blend_of(def.blend_mode, row.blend);
        row.camera_anchor = anchor_of(def.camera_anchor, row.camera_anchor);
        row.shadow_q8 = static_cast<std::int16_t>(wrap_signed_bits(static_cast<int>(def.shadow * 256.0), 10));
        row.camera_offset_y_q8 =
            static_cast<std::int16_t>(wrap_signed_bits(static_cast<int>(def.camera_offset_y * 256.0), 14));
        ++applied;
    }
    const std::string from = source.mod_id.empty() ? std::string() : " (mod " + source.mod_id + ")";
    SH_LOG("[CHARA] gamedata/chara: %d characters from data%s", applied, from.c_str());
}

}  // namespace sh::chara::game

// ---- The calls of the boot and of the game code that reads the rows by type ----

namespace cg = sh::chara::game;

extern "C" {

void CharaFiles_Load(const char* gamedata_dir, const char* mods_dir) {
    if (gamedata_dir != nullptr) {
        cg::chara_files_load(gamedata_dir, mods_dir != nullptr ? mods_dir : "");
    }
}

}  // extern "C"

// The character hosts' link (chara_link.h).
namespace sh::chara::game::link {

CharaFiles chara_files(int chara_id) {
    const CharaFileRow& r = chara_file_row(chara_id);
    return {r.model, r.texture, r.anim};
}

void set_chara_files(int chara_id, const CharaFiles& files) {
    CharaFileRow& r = chara_file_row(chara_id);
    r.model = files.model, r.texture = files.texture, r.anim = files.anim;
}

}  // namespace sh::chara::game::link
