// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Named binds of the final map's effects, on a map whose overlay.json lists the Incubus in "character_effects"
// (elsewhere an error):
//   map.lightning_init(slot)          the lightning cloud set up around the character of the slot
//   map.lightning_clear() / map.lightning_update() / map.lightning_bolt(unused)
//   map.lightning_sfx_volume_set(v)   the thunder volume (0..255)
//   map.lightning_stop() / map.lightning_resume()   no new bolts / new bolts again
//   map.boss_fire_init() / map.boss_fire_stop() / map.boss_fire_resume() / map.boss_fire_update()
//   map.ending_flare_draw(n)          the glow at fixed point n (0..2) this frame
//   map.boss_bolts_load()             the bolt pool set up (its sounds start from their gaps)
//   map.thunder_gaps_reset() / map.thunder_gaps_tick()   the thunder pacing slots cleared / run down
//   map.boss_fx_init() / map.boss_fx_update() / map.boss_fx_mode_set(mode)   the light beams around the bosses
// and of the ending's effect cues, read from gamedata/maps/map7_s03/ending_cues.json (a mod may patch it), on
// map7_s03:
//   map.ending_cue_flags(scene, step, i): -1 when the step has no cue i, else bit 0 = another cue follows, bit 1 =
//       once per step.  map.ending_cue_play(scene, step, i): the cue fires.

#include "engine/scripting/binds/binds.h"

#include <array>
#include <cstdlib>
#include <cstring>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "engine/log/sh_log.h"
#include "engine/script/sh_script.h"
#include "engine/scripting/binds/game_links.h"
#include "engine/vfs/vfs_data_json.h"
#include "formats/common/json.h"
#include "engine/mods/data_reload.h"

namespace sh::script::binds {

namespace {

using named::Call;

// ---- effects ----

bool effects_on(Call& c) {
    if (EndingFx_Enabled() == 0) {
        c.fail("%s: the final bosses' effects are not on this map", c.name());
        return false;
    }
    return true;
}

template <void (*Fn)()>
int effect(Call& c) {
    if (!effects_on(c)) {
        return -1;
    }
    Fn();
    return 0;
}

template <void (*Fn)(std::int32_t)>
int effect_int(Call& c) {
    if (!effects_on(c)) {
        return -1;
    }
    if (c.argc() != 1 || !c.is_int(0)) {
        return c.fail("%s: takes one integer", c.name());
    }
    Fn(static_cast<std::int32_t>(c.integer(0)));
    return 0;
}

int lightning_init(Call& c) {
    if (!effects_on(c)) {
        return -1;
    }
    if (c.argc() != 1 || !c.is_int(0) || !sh::script::links::chara_slot_valid(static_cast<int>(c.integer(0)))) {
        return c.fail("%s: takes a character slot", c.name());
    }
    EndingFx_LightningInit(sh::script::links::chara_position_record(static_cast<int>(c.integer(0))));
    return 0;
}

// ---- cues ----

// A cue as the fire pool reads it: 3 position words, 11 effect values, the flags word (60 bytes).
constexpr int kCueValues = 11;
constexpr int kFlagsWord = 14;
using Cue = std::array<std::int32_t, 15>;

constexpr const char* kScenes[] = {"incubus_death", "incubator_death", "good_plus", "good", "bad_plus_cybil"};

struct CueTable {
    bool loaded = false;
    std::string map;
    std::map<std::string, std::map<int, std::vector<Cue>>> scenes;
};

bool read_cue(const sh::json::Value& v, Cue& out) {
    const sh::json::Value* pos = v.find("position");
    const sh::json::Value* vals = v.find("values");
    const sh::json::Value* flags = v.find("flags");
    if (pos == nullptr || !pos->is_array() || pos->array().size() != 3 || vals == nullptr || !vals->is_array() ||
        vals->array().size() != kCueValues || flags == nullptr || !flags->is_integer()) {
        return false;
    }
    for (int i = 0; i < 3; ++i) {
        out[static_cast<std::size_t>(i)] = static_cast<std::int32_t>(pos->array()[static_cast<std::size_t>(i)].integer());
    }
    for (int i = 0; i < kCueValues; ++i) {
        out[static_cast<std::size_t>(3 + i)] =
            static_cast<std::int32_t>(vals->array()[static_cast<std::size_t>(i)].integer());
    }
    out[kFlagsWord] = static_cast<std::int32_t>(flags->integer());
    return true;
}

void load_table(CueTable& t, const std::string& map) {
    t = CueTable{};
    t.loaded = true;
    t.map = map;
    const std::string path = "maps/" + map + "/ending_cues.json";
    // Base and every active mod's patch, in load order.
    const std::optional<std::string> text = ShVfs_ReadDataJsonText(path.c_str(), PcPort_GetGameDataPath());
    if (!text) {
        SH_ERR("[ENDINGCUES] %s/%s cannot be read", PcPort_GetGameDataPath(), path.c_str());
        return;
    }
    auto parsed = sh::json::parse(*text);
    if (!parsed) {
        SH_ERR("[ENDINGCUES] %s: %s", path.c_str(), parsed.error().c_str());
        return;
    }
    const sh::json::Value* scenes = parsed.value().find("scenes");
    if (scenes == nullptr || !scenes->is_object()) {
        SH_ERR("[ENDINGCUES] %s: \"scenes\" must be an object", path.c_str());
        return;
    }
    for (const auto& scene : scenes->members()) {
        if (!scene.second.is_object()) {
            SH_ERR("[ENDINGCUES] %s: scene %s must be an object", path.c_str(), scene.first.c_str());
            continue;
        }
        for (const auto& step : scene.second.members()) {
            std::vector<Cue> cues;
            const char* problem = step.second.is_array() ? nullptr : "a step must be an array of cues";
            if (problem == nullptr) {
                for (const sh::json::Value& v : step.second.array()) {
                    Cue cue{};
                    if (!read_cue(v, cue)) {
                        problem = "a cue needs position[3], values[11] and flags";
                        break;
                    }
                    cues.push_back(cue);
                }
            }
            if (problem != nullptr) {
                SH_ERR("[ENDINGCUES] %s: %s step %s: %s", path.c_str(), scene.first.c_str(), step.first.c_str(),
                       problem);
                continue;
            }
            t.scenes[scene.first][std::atoi(step.first.c_str())] = std::move(cues);
        }
    }
}

CueTable& table(const std::string& map) {
    static CueTable t;
    static std::uint32_t epoch = sh::mods::kNeverRead;
    if (sh::mods::data_stale(epoch) || !t.loaded || t.map != map) {
        load_table(t, map);
    }
    return t;
}

std::string map_name() {
    const char* m = ShScript_MapName();
    return m != nullptr ? m : "";
}

// (scene, step, i) -> the cue; nullptr with *bad set on a wrong argument, nullptr alone when the step has no cue.
const Cue* find_cue(Call& c, bool* bad) {
    *bad = true;
    if (c.argc() < 3 || !c.is_text(0) || !c.is_int(1) || !c.is_int(2)) {
        c.fail("ending cue: (scene, step, i) expected");
        return nullptr;
    }
    bool known = false;
    for (const char* s : kScenes) {
        known = known || std::strcmp(s, c.text(0)) == 0;
    }
    if (!known) {
        c.fail("unknown ending cue scene '%s'", c.text(0));
        return nullptr;
    }
    const long long step = c.integer(1);
    const long long index = c.integer(2);
    if (step < 0 || step >= 64 || index < 0) {
        c.fail("ending cue step or index out of range");
        return nullptr;
    }
    *bad = false;
    CueTable& t = table(map_name());
    const auto scene = t.scenes.find(c.text(0));
    if (scene == t.scenes.end()) {
        return nullptr;
    }
    const auto run = scene->second.find(static_cast<int>(step));
    if (run == scene->second.end() || run->second.empty()) {
        return nullptr;
    }
    if (index >= static_cast<long long>(run->second.size())) {
        *bad = true;
        c.fail("ending cue index past the step's last cue");
        return nullptr;
    }
    return &run->second[static_cast<std::size_t>(index)];
}

// The cues are the final map's (the map whose overlay.json lists the final bosses in "character_effects");
// elsewhere the names are not served here.
bool on_final_map() { return EndingFx_Enabled() != 0; }

int cue_flags(Call& c) {
    if (!on_final_map()) {
        return named::kPass;
    }
    bool bad = false;
    const Cue* cue = find_cue(c, &bad);
    if (bad) {
        return -1;
    }
    return c.reply({cue != nullptr ? (*cue)[kFlagsWord] : -1});
}

int cue_play(Call& c) {
    if (!on_final_map()) {
        return named::kPass;
    }
    bool bad = false;
    const Cue* cue = find_cue(c, &bad);
    if (bad) {
        return -1;
    }
    if (cue == nullptr) {
        return c.fail("no cue at this step");
    }
    if (EndingFx_Enabled() == 0) {
        return c.fail("the final bosses' effects are not on this map");
    }
    Cue copy = *cue;
    EndingFx_FireCue(copy.data());
    return 0;
}

const named::Bind kBinds[] = {
    {"map.lightning_init", lightning_init},
    {"map.lightning_clear", effect<EndingFx_LightningClear>},
    {"map.lightning_update", effect<EndingFx_LightningUpdate>},
    {"map.lightning_bolt", effect_int<EndingFx_LightningBolt>},
    {"map.lightning_sfx_volume_set", effect_int<EndingFx_LightningVolumeSet>},
    {"map.lightning_stop", effect<EndingFx_LightningStop>},
    {"map.lightning_resume", effect<EndingFx_LightningResume>},
    {"map.boss_fire_init", effect<EndingFx_FireInit>},
    {"map.boss_fire_stop", effect<EndingFx_FireStop>},
    {"map.boss_fire_resume", effect<EndingFx_FireResume>},
    {"map.boss_fire_update", effect<EndingFx_FireUpdate>},
    {"map.ending_flare_draw", effect_int<EndingFx_FlareDraw>},
    {"map.boss_bolts_load", effect<EndingFx_BoltsLoad>},
    {"map.thunder_gaps_reset", effect<EndingFx_ThunderGapsReset>},
    {"map.thunder_gaps_tick", effect<EndingFx_ThunderGapsTick>},
    {"map.boss_fx_init", effect<EndingFx_BeamsInit>},
    {"map.boss_fx_update", effect<EndingFx_BeamsUpdate>},
    {"map.boss_fx_mode_set", effect_int<EndingFx_BeamsModeSet>},
    {"map.ending_cue_flags", cue_flags},
    {"map.ending_cue_play", cue_play},
};

}  // namespace

std::span<const named::Bind> ending_binds() { return kBinds; }

}  // namespace sh::script::binds
