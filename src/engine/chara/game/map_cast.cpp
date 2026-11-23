// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/chara/game/map_cast.h"

#include <optional>
#include <string>
#include <vector>

#include "engine/chara/game/chara_link.h"
#include "engine/chara/game/chara_records.h"
#include "engine/chara/game/clip_info.h"
#include "engine/ids/game_numbers.h"
#include "engine/log/sh_log.h"
#include "engine/loop/frame_view.h"
#include "engine/maps/chara_path_walk.h"
#include "engine/maps/chara_rules.h"
#include "engine/maps/sh_maps.h"
#include "engine/vfs/vfs_data_json.h"
#include "formats/common/json.h"

extern "C" {
// The clip playback (src/engine/entities/game/model_runtime.cpp).
std::int32_t Anim_DurationGet(void* model, void* clip);
}  // extern "C"

namespace sh::chara::game::cast {
namespace {

namespace nv = sh::chara::game::npc_vars;

// The sound placement that ignores the distance.
constexpr std::int32_t kSoundNoDistance = 1 << 1;

// Collision state of a character whose collision was just reset.
constexpr int kCollisionReset = 3;

// Animation bits and statuses the helpers set.
constexpr std::uint16_t kAnimUnlocked = 1u << 0;
constexpr std::uint8_t kStillTransitionStatus = (1 << 1) | 1;
constexpr std::int32_t kOne = 4096;

// Results of the playback state query beside done / not done.
constexpr std::int32_t kAnimNotFinished = -1;
constexpr std::int32_t kAnimBlending = -2;

std::int32_t& word(CharacterRecord* chara, std::size_t offset) {
    return *reinterpret_cast<std::int32_t*>(chara->vars.bytes + offset);
}

struct MapCast {
    bool loaded = false;
    std::string map;
    std::vector<ClipInfo*> tables;
    sh::maps::CharaTableLookup playback;
    sh::maps::CharaTableLookup start_keyframe;
    sh::maps::CueSoundRules sounds;
    bool missing_table_logged = false;
};

MapCast g_cast;
sh::maps::PathWalk g_walk;

int table_index(MapCast& m, const std::string& relative, const sh::json::Value& v) {
    if (!v.is_string()) {
        SH_ERR("[CHARAS] %s:%u: an animation table is named by its file name (\"cybil\")", relative.c_str(), v.line());
        return -2;
    }
    ClipInfo* table = link::anim_table(v.string().c_str()).clips;
    if (table == nullptr) {
        SH_ERR("[CHARAS] %s:%u: no animation table \"%s\"", relative.c_str(), v.line(), v.string().c_str());
        return -2;
    }
    for (std::size_t i = 0; i < m.tables.size(); ++i) {
        if (m.tables[i] == table) {
            return static_cast<int>(i);
        }
    }
    m.tables.push_back(table);
    return static_cast<int>(m.tables.size() - 1);
}

bool lookup_parse(MapCast& m, const std::string& relative, const sh::json::Value& v, sh::maps::CharaTableLookup& out) {
    out.clear();
    if (!v.is_object()) {
        SH_ERR("[CHARAS] %s:%u: a lookup is an object with \"tables\" and/or \"fallback\"", relative.c_str(), v.line());
        return false;
    }
    if (const sh::json::Value* tables = v.find("tables")) {
        if (!tables->is_object()) {
            SH_ERR("[CHARAS] %s:%u: \"tables\" maps a character name to a table", relative.c_str(), tables->line());
            return false;
        }
        for (const auto& [name, table] : tables->members()) {
            const int chara = ShMaps_CharaNumberOf(name.c_str());
            if (chara < 0) {
                SH_ERR("[CHARAS] %s:%u: unknown character \"%s\"", relative.c_str(), table.line(), name.c_str());
                return false;
            }
            const int index = table_index(m, relative, table);
            if (index < 0) {
                return false;
            }
            out.set(chara, index);
        }
    }
    if (const sh::json::Value* fallback = v.find("fallback")) {
        const int index = table_index(m, relative, *fallback);
        if (index < 0) {
            return false;
        }
        out.set_fallback(index);
    }
    return true;
}

bool sounds_parse(MapCast& m, const std::string& relative, const sh::json::Value& v) {
    if (!v.is_array()) {
        SH_ERR("[CHARAS] %s:%u: \"cue_sounds\" is a list of rules", relative.c_str(), v.line());
        return false;
    }
    for (const sh::json::Value& rule : v.array()) {
        const sh::json::Value* sounds = rule.find("sounds");
        const sh::json::Value* volume = rule.find("volume");
        if (sounds == nullptr || !sounds->is_array() || volume == nullptr || !volume->is_integer() ||
            volume->integer() < 0 || volume->integer() > 255) {
            SH_ERR("[CHARAS] %s:%u: a cue sound rule needs \"sounds\" (names) and \"volume\" (0..255)",
                   relative.c_str(), rule.line());
            return false;
        }
        sh::maps::CueSound how;
        how.volume = static_cast<std::int32_t>(volume->integer());
        if (const sh::json::Value* flat = rule.find("no_distance_attenuation");
            flat != nullptr && flat->is_bool() && flat->boolean()) {
            how.flags |= kSoundNoDistance;
        }
        for (const sh::json::Value& name : sounds->array()) {
            const int sound = name.is_string() ? sh::ids::sound_number(name.string().c_str()) : -1;
            if (sound < 0) {
                SH_ERR("[CHARAS] %s:%u: unknown sound %s", relative.c_str(), name.line(),
                       name.is_string() ? name.string().c_str() : "(not a name)");
                return false;
            }
            m.sounds.set(sound, how);
        }
    }
    return true;
}

bool cast_parse(MapCast& m, const std::string& relative, const std::string& text) {
    auto doc = sh::json::parse(text);
    if (!doc) {
        SH_ERR("[CHARAS] %s: %s", relative.c_str(), doc.error().c_str());
        return false;
    }
    const sh::json::Value& root = doc.value();
    const sh::json::Value* playback = root.find("playback");
    if (playback == nullptr || !lookup_parse(m, relative, *playback, m.playback)) {
        if (playback == nullptr) {
            SH_ERR("[CHARAS] %s: needs \"playback\" (which table each character's status indexes)", relative.c_str());
        }
        return false;
    }
    // The start keyframe query looks the same tables up unless the map gives it its own.
    if (const sh::json::Value* start = root.find("start_keyframe")) {
        if (!lookup_parse(m, relative, *start, m.start_keyframe)) {
            return false;
        }
    } else {
        m.start_keyframe = m.playback;
    }
    if (const sh::json::Value* sounds = root.find("cue_sounds")) {
        if (!sounds_parse(m, relative, *sounds)) {
            return false;
        }
    }
    return true;
}

ClipInfo* clip_of(const sh::maps::CharaTableLookup& lookup, CharacterRecord* chara) {
    const int table = lookup.find(chara->model.chara_id);
    if (table < 0) {
        return nullptr;
    }
    // Indexed by the status without a bound, as the original: every status the cast plays is in its table.
    return &g_cast.tables[static_cast<std::size_t>(table)][chara->model.anim.status];
}

}  // namespace

bool load(const char* map) {
    g_walk.reset();
    g_cast = MapCast{};
    g_cast.map = map != nullptr ? map : "";
    const std::string relative = "maps/" + g_cast.map + "/charas.json";
    // Base + every active mod's patch, in load order.
    const std::optional<std::string> text =
        g_cast.map.empty() ? std::nullopt : ShVfs_ReadDataJsonText(relative.c_str());
    if (!text) {
        return false;
    }
    MapCast parsed;
    parsed.map = g_cast.map;
    if (!cast_parse(parsed, relative, *text)) {
        return false;
    }
    parsed.loaded = true;
    g_cast = std::move(parsed);
    return true;
}

void control_end(CharacterRecord* chara, std::uint32_t to_still) {
    word(chara, nv::kWalkDone) = 1;
    if (to_still != 0) {
        chara->model.anim.status = kStillTransitionStatus;
        chara->model.anim.keyframe = 0;
        chara->model.anim.time = 0;
        chara->model.anim.alpha = kOne;
    } else {
        word(chara, nv::kControlState) = 0;
        chara->model.control_step = 0;
    }
    chara->model.anim.flags |= kAnimUnlocked;
}

void control_state_set(CharacterRecord* chara, std::int32_t state) {
    word(chara, nv::kTimer) = 0;
    word(chara, nv::kResetControlState) = 0;
    word(chara, nv::kFreeze) = 0;
    word(chara, nv::kControlState) = state;
    chara->model.control_step = 0;
}

void control_reset(CharacterRecord* chara) { word(chara, nv::kResetControlState) = 1; }

void anim_lock(CharacterRecord* chara) { chara->model.anim.flags &= static_cast<std::uint16_t>(~kAnimUnlocked); }

void anim_unlock(CharacterRecord* chara) { chara->model.anim.flags |= kAnimUnlocked; }

std::int32_t anim_playback_state(CharacterRecord* chara) {
    ClipInfo* info = clip_of(g_cast.playback, chara);
    if (info == nullptr) {
        return kAnimNotFinished;
    }
    if (info->playback == link::clip_playback_once()) {
        // A range test: a long frame can step past the end keyframe, and the waiting cutscene would never see it.
        if (Anim_DurationGet(&chara->model, info) > 0) {
            return chara->model.anim.keyframe >= info->end_keyframe;
        }
        return chara->model.anim.keyframe <= info->start_keyframe;
    }
    if (info->playback == link::clip_playback_blend()) {
        return kAnimBlending;
    }
    return kAnimNotFinished;
}

std::uint32_t path_step(CharacterRecord* chara, std::int32_t walk_anim, const sh::entities::Vec3* points,
                        std::int32_t final_heading, std::int32_t count) {
    sh::maps::PathWalkActor a;
    a.x = chara->position.x;
    a.z = chara->position.z;
    a.yaw = chara->rotation.y;
    a.phase = word(chara, nv::kWalkPhase);
    a.anim = word(chara, nv::kControlState);
    a.anim_request = word(chara, nv::kWalkAnim);
    a.arrived = word(chara, nv::kWalkDone);
    const bool ended = g_walk.step(
        a, walk_anim, [points](std::int32_t i) { return sh::maps::WalkPoint{points[i].x, points[i].z}; },
        final_heading, count, sh::loop::frame_delta());
    chara->rotation.y = a.yaw;
    word(chara, nv::kWalkPhase) = a.phase;
    word(chara, nv::kControlState) = a.anim;
    word(chara, nv::kWalkAnim) = a.anim_request;
    word(chara, nv::kWalkDone) = a.arrived;
    if (a.restart_state) {
        chara->model.control_step = 0;
    }
    if (a.stop_motion) {
        var_write<std::int16_t>(*chara, nv::kMoveDistance, 0);
        var_write<std::int16_t>(*chara, nv::kMoveSpeed, 0);
    }
    return ended ? 1u : 0u;
}

std::int32_t anim_start_keyframe(CharacterRecord* chara) {
    ClipInfo* info = clip_of(g_cast.start_keyframe, chara);
    if (info == nullptr) {
        // The original read through a null table here; the cast of this map never asks.
        if (!g_cast.missing_table_logged) {
            g_cast.missing_table_logged = true;
            SH_ERR("[CHARAS] %s: character %d has no start keyframe table", g_cast.map.c_str(),
                   static_cast<int>(chara->model.chara_id));
        }
        return 0;
    }
    return info->start_keyframe;
}

void collision_reset(CharacterRecord* chara) {
    chara->model.control_step = 0;
    chara->collision.box.top = 0;
    chara->collision.box.bottom = 0;
    chara->collision.box.offset_y = 0;
    chara->collision.cylinder.radius = 0;
    chara->collision.cylinder.chara_radius = 0;
    chara->collision.offsets.cylinder.z = 0;
    chara->collision.offsets.cylinder.x = 0;
    chara->collision.offsets.box.z = 0;
    chara->collision.offsets.box.x = 0;
    word(chara, nv::kWalkPhase) = 0;
    word(chara, nv::kControlState) = 0;
    word(chara, nv::kFreeze) = 0;
    word(chara, nv::kWalkDone) = 0;
    chara->model.control_state++;
    chara->collision.set_state(kCollisionReset);
}

}  // namespace sh::chara::game::cast
