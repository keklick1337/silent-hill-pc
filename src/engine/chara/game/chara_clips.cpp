// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/chara/game/chara_clips.h"

#include <array>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "engine/chara/behavior_host.h"
#include "engine/chara/chara_library.h"
#include "engine/chara/game/chara_link.h"
#include "engine/chara/runtime/chara_data.h"
#include "engine/game_state/game_ids.h"
#include "engine/log/sh_log.h"
#include "engine/tables/anim_table.h"
#include "engine/vfs/vfs_data_json.h"
#include "formats/common/json.h"
#include "engine/mods/data_reload.h"

namespace sh::chara::game {
namespace {

namespace ids = sh::game_state::chara;
namespace behavior = sh::chara::behavior;

struct SpeedFn {
    std::string name;
    ClipSpeedFn fn;
};

std::vector<SpeedFn>& speeds() {
    static std::vector<SpeedFn> s;
    return s;
}

// The clip files read so far, by name; their entries never move.
std::map<std::string, std::unique_ptr<std::vector<ClipInfo>>, std::less<>>& files() {
    static std::map<std::string, std::unique_ptr<std::vector<ClipInfo>>, std::less<>> f;
    return f;
}

ClipPlayback playback_of(sh::tables::AnimKind kind) {
    switch (kind) {
        case sh::tables::AnimKind::BlendLinear: return link::clip_playback_blend();
        case sh::tables::AnimKind::PlaybackOnce: return link::clip_playback_once();
        case sh::tables::AnimKind::PlaybackLoop: return link::clip_playback_loop();
        case sh::tables::AnimKind::None: break;
    }
    return nullptr;
}

// gamedata/chara/<file>: the clip file in the anim table format; empty on an error (reported).
ClipTableRef clip_file(const std::string& file) {
    auto& cache = files();
    // After `mods reload` the files are read again; a record may still point at an old table, so those stay alive.
    static std::vector<std::unique_ptr<std::vector<ClipInfo>>> retired;
    static std::uint32_t epoch = sh::mods::kNeverRead;
    if (sh::mods::data_stale(epoch)) {
        for (auto& [name, clips] : cache) {
            retired.push_back(std::move(clips));
        }
        cache.clear();
    }
    if (auto it = cache.find(file); it != cache.end()) {
        return {it->second->data(), static_cast<int>(it->second->size())};
    }
    auto clips = std::make_unique<std::vector<ClipInfo>>();
    const std::string relative = "chara/" + file;
    const std::optional<std::string> text = ShVfs_ReadDataJsonText(relative.c_str());
    if (!text) {
        SH_ERR("[CLIPS] %s cannot be read", relative.c_str());
    } else if (auto doc = sh::json::parse(*text); !doc) {
        SH_ERR("[CLIPS] %s: %s", relative.c_str(), doc.error().c_str());
    } else {
        const sh::json::Value* entries = doc.value().find("entries");
        const sh::json::Value* symbol = doc.value().find("symbol");
        const auto count = static_cast<std::uint16_t>(entries != nullptr && entries->is_array() ? entries->array().size() : 0);
        std::string symbol_text = symbol != nullptr && symbol->is_string() ? symbol->string() : std::string();
        std::vector<ShAnimFunc> funcs;
        for (std::size_t i = 0; i < speeds().size(); ++i) {
            funcs.push_back({speeds()[i].name.c_str(), static_cast<std::int32_t>(i + 1)});
        }
        ShAnimTableInfo info{};
        info.name = file.c_str();
        info.symbol = symbol_text.c_str();
        info.count = count;
        info.fileCount = count;
        info.linkLimit = count;
        auto table = sh::tables::read_anim_table(*text, info, funcs);
        if (!table) {
            SH_ERR("[CLIPS] %s: %s", relative.c_str(), table.error().c_str());
        } else {
            for (const sh::tables::AnimEntry& e : table.value().entries) {
                ClipInfo c{};
                c.playback = playback_of(e.kind);
                c.status = e.status;
                c.variable_speed = e.variable_duration ? 1 : 0;
                c.link = e.link;
                if (e.variable_duration) {
                    for (const SpeedFn& s : speeds()) {
                        if (s.name == e.duration_func) {
                            c.speed_fn = s.fn;
                        }
                    }
                } else {
                    c.speed = e.speed_q12;
                }
                c.start_keyframe = e.start_kf;
                c.end_keyframe = e.end_kf;
                clips->push_back(c);
            }
        }
    }
    const ClipTableRef out{clips->data(), static_cast<int>(clips->size())};
    cache.emplace(file, std::move(clips));
    return out;
}

ClipTableRef resolve(const std::string& name) {
    if (name.ends_with(".json")) {
        return clip_file(name);
    }
    const ClipTableRef t = link::anim_table(name.c_str());
    if (t.clips == nullptr) {
        SH_ERR("[CLIPS] no animation table \"%s\"", name.c_str());
    }
    return t;
}

// The tables of every type, resolved when the character library changes.
const std::vector<ClipTableRef>& tables_of(int chara_id) {
    static const std::vector<ClipTableRef> none;
    static const CharaLibrary* resolved_for = nullptr;
    static std::array<std::vector<ClipTableRef>, ids::kMaxTypes> tables;
    if (chara_id <= ids::kNone || chara_id >= ids::kMaxTypes) {
        return none;
    }
    const CharaLibrary* lib = game_library();
    if (lib == nullptr) {
        return none;
    }
    if (lib != resolved_for) {
        resolved_for = lib;
        for (int id = 0; id < ids::kMaxTypes; ++id) {
            auto& list = tables[static_cast<std::size_t>(id)];
            list.clear();
            const CharacterDef* def = id > ids::kNone ? lib->find(id) : nullptr;
            if (def == nullptr) {
                continue;
            }
            for (const std::string& name : def->clip_tables) {
                if (const ClipTableRef t = resolve(name); t.clips != nullptr && t.count > 0) {
                    list.push_back(t);
                }
            }
        }
    }
    return tables[static_cast<std::size_t>(chara_id)];
}

}  // namespace

bool data_clip_table(int chara_id, int index, behavior::ClipTable* out) {
    const std::vector<ClipTableRef>& list = tables_of(chara_id);
    if (index < 0 || static_cast<std::size_t>(index) >= list.size()) {
        return false;
    }
    const ClipTableRef& t = list[static_cast<std::size_t>(index)];
    *out = {chara_id, t.clips, t.count};
    return true;
}

bool type_clip_table(int chara_id, int index, ClipTableRef* out) {
    behavior::ClipTable t;
    if (!behavior::clip_table(chara_id, index, &t)) {
        return false;
    }
    *out = {static_cast<ClipInfo*>(t.table), t.count};
    return true;
}

ClipTableRef active_clip_table(int chara_id, const void* active) {
    behavior::ClipTable t;
    if (!behavior::active_clip_table(chara_id, active, &t)) {
        return {};
    }
    return {static_cast<ClipInfo*>(t.table), t.count};
}

void register_clip_speed(std::string_view name, ClipSpeedFn fn) {
    speeds().push_back({std::string(name), fn});
}

}  // namespace sh::chara::game
