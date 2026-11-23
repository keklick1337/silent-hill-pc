// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Harry's reaction clips on demand (harry_reaction_import.h). The plan is sh::chara::plan_reaction_import;
// this applies it to the running map: a writable copy of the map's reaction rows and of Harry's map clips with the
// loaded ones appended, and the keyframes in the entity bank. The copies replace the map's tables until the next
// map load. Also the map's Harry tables and the attack rows the player's hosts read.

#include "engine/chara/game/harry_reaction_import.h"
#include "engine/loader/active_map.h"
#include "engine/loader/map_list.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <set>
#include <span>
#include <string>
#include <vector>

#include "engine/chara/chara_library.h"
#include "engine/chara/game/chara_entry.h"
#include "engine/chara/game/chara_link.h"
#include "engine/chara/game/clip_info.h"
#include "engine/chara/harry_reactions.h"
#include "engine/chara/runtime/chara_data.h"
#include "engine/game_state/session.h"
#include "engine/ids/game_numbers.h"
#include "engine/log/sh_log.h"
#include "engine/maps/map_rules.h"
#include "engine/maps/map_runtime.h"
#include "engine/maps/map_table_data.h"
#include "engine/maps/map_tables.h"
#include "engine/vfs/vfs_c.h"
#include "engine/mods/data_reload.h"

namespace cg = sh::chara::game;
namespace rc = sh::chara;
namespace rt = sh::maps;

extern "C" {
const char* PcPort_GetGameDataPath(void);
// The entity bank's Harry keyframes (entity_host.cpp).
int PcEntityAnim_HarryMapKeyframeFirst(void);
int PcEntityAnim_HarryBankAppend(const char* game_path, int local_first, int count);
void PcEntityAnim_HarryBankClear(void);
int PcEntityAnim_FileKeyframes(const char* game_path);
int PcEntityAnim_HarryBankNext(void);
extern cg::ClipInfo HARRY_BASE_ANIM_INFOS[256];
}

namespace {

constexpr int kRowsOriginal = 40;  // the rows the original's scan walks
constexpr int kRowsMax = 256;      // sh.entity.overlay_row reads up to row 255
constexpr int kClipsMax = rc::kHarryStatusMax + 1 - rc::kHarryMapStatusFirst;

struct Extended {
    bool installed = false;
    std::string map;
    rt::PlayerAnimRow rows[kRowsMax] = {};
    cg::ClipInfo clips[kClipsMax + 1] = {};
    int row_count = 0;
    int clip_count = 0;
    std::set<int> imported_rows;
    std::set<int> failed;  // reactions no map could give this visit
};
Extended s_ext;

std::unique_ptr<rc::HarryReactions> s_catalog;
std::uint32_t s_catalog_epoch = sh::mods::kNeverRead;
std::map<std::string, std::unique_ptr<rc::MapReactionTable>> s_tables;  // by map, null: unreadable

rt::PlayerAnimRow* map_rows() { return rt::map_runtime().player_anim_rows(); }
cg::ClipInfo* map_clips() { return static_cast<cg::ClipInfo*>(rt::map_runtime().player_anim_infos()); }

bool read_file(const std::string& path, std::string* out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }
    out->assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

bool read_data_file(const std::string& relative, std::string* out) {
    char path[1024];
    return ShVfs_FindDataFile(relative.c_str(), path, sizeof(path)) && read_file(path, out);
}

const rc::HarryReactions* catalog() {
    if (sh::mods::data_stale(s_catalog_epoch)) {
        s_catalog.reset();
        s_tables.clear();
        std::string text;
        if (!read_data_file("chara/Harry/reactions.json", &text)) {
            SH_ERR("[REACTIONS] gamedata/chara/Harry/reactions.json is missing: spawned enemies keep the map's reactions");
            return nullptr;
        }
        auto parsed = rc::HarryReactions::parse(text);
        if (!parsed) {
            SH_ERR("[REACTIONS] chara/Harry/reactions.json: %s", parsed.error().c_str());
            return nullptr;
        }
        s_catalog = std::make_unique<rc::HarryReactions>(std::move(parsed.value()));
    }
    return s_catalog.get();
}

class GameTableNames final : public rt::TableNames {
public:
    int item(std::string_view name) const override { return sh::ids::item_number(std::string(name).c_str()); }
    int reaction(std::string_view name) const override {
        const rc::HarryReactions* names = catalog();
        return names != nullptr ? names->id_of(name) : -1;
    }
};

const GameTableNames s_table_names;

const rc::MapReactionTable* map_table(const std::string& map) {
    static std::uint32_t epoch = sh::mods::kNeverRead;
    if (sh::mods::data_stale(epoch)) {
        s_tables.clear();
    }
    if (auto it = s_tables.find(map); it != s_tables.end()) {
        return it->second.get();
    }
    std::unique_ptr<rc::MapReactionTable> table;
    std::string json;
    std::string bin;
    if (read_data_file("maps/" + map + "/tables.json", &json)) {
        const std::string file = rc::tables_bin(json);
        if (!file.empty() && read_file(std::string(PcPort_GetGameDataPath()) + "/raw/" + file, &bin)) {
            // The map's readable table data over its overlay, as when the map itself loads.
            auto data = rt::apply_table_data(
                json, std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(bin.data()), bin.size()),
                s_table_names);
            if (!data) {
                SH_ERR("[REACTIONS] maps/%s/tables.json: %s", map.c_str(), data.error().c_str());
            }
            const std::vector<std::uint8_t> empty;
            const std::vector<std::uint8_t>& bytes = data ? data.value().overlay : empty;
            auto t = rc::read_map_reactions(map, json, std::span<const std::uint8_t>(bytes.data(), bytes.size()));
            if (t) {
                table = std::make_unique<rc::MapReactionTable>(std::move(t.value()));
            } else {
                SH_WARN("[REACTIONS] %s", t.error().c_str());
            }
        }
    }
    const rc::MapReactionTable* out = table.get();
    s_tables.emplace(map, std::move(table));
    return out;
}

// "map2_s00" -> "ANIM/HB_M2S00.ANM".
std::string harry_file_of(const std::string& map) {
    std::string tag;
    for (char c : map) {
        if (c != '_' && c != 'a' && c != 'p') {
            tag += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }
    }
    return "ANIM/HB_" + tag + ".ANM";
}

// The map's Harry keyframe file and the ones its overlay.json "harry_anim_files" adds.
std::vector<std::string> harry_files_of(const std::string& map) {
    std::vector<std::string> files{harry_file_of(map)};
    std::string json;
    if (read_data_file("maps/" + map + "/overlay.json", &json)) {
        std::string error;
        const rt::MapRules rules = rt::parse_map_rules(json, &error);
        if (!error.empty()) {
            SH_WARN("[REACTIONS] maps/%s/overlay.json: %s", map.c_str(), error.c_str());
        }
        files.insert(files.end(), rules.harry_anim_files.begin(), rules.harry_anim_files.end());
    }
    return files;
}

const char* loaded_map() {
    const char* name = sh::loader::active_map_name();
    return name != nullptr ? name : "";
}

// Points the map and Harry's two models at the copies.
void point_at_copies() {
    rt::map_runtime().set_player_anim_rows(s_ext.rows);
    rt::map_runtime().set_player_anim_infos(s_ext.clips);
    auto& characters = sh::game::session().system.characters;
    characters.player.model.anim.map_clip_table = s_ext.clips;
    characters.player_upper.model.anim.map_clip_table = s_ext.clips;
}

bool same_map() { return s_ext.installed && s_ext.map == loaded_map(); }

// The writable copies of the map's tables, made once per map.
bool install() {
    if (!rt::map_runtime().loaded()) {
        return false;
    }
    if (same_map()) {
        if (map_rows() != s_ext.rows) {
            point_at_copies();
        }
        return true;
    }
    s_ext.installed = false;
    s_ext.imported_rows.clear();
    s_ext.failed.clear();
    s_ext.row_count = 0;
    s_ext.clip_count = 0;
    std::fill(std::begin(s_ext.rows), std::end(s_ext.rows), rt::PlayerAnimRow{});
    std::fill(std::begin(s_ext.clips), std::end(s_ext.clips), cg::ClipInfo{});
    if (const rt::PlayerAnimRow* rows = map_rows()) {
        while (s_ext.row_count < kRowsOriginal - 1 &&
               (rows[s_ext.row_count].status != 0 || rows[s_ext.row_count].harry_status != 0)) {
            s_ext.rows[s_ext.row_count] = rows[s_ext.row_count];
            ++s_ext.row_count;
        }
    }
    if (const cg::ClipInfo* clips = map_clips()) {
        while (s_ext.clip_count < kClipsMax && clips[s_ext.clip_count].playback != nullptr) {
            s_ext.clips[s_ext.clip_count] = clips[s_ext.clip_count];
            ++s_ext.clip_count;
        }
    }
    PcEntityAnim_HarryBankClear();
    s_ext.map = loaded_map();
    s_ext.installed = true;
    point_at_copies();
    return true;
}

bool has_row(int id) {
    const rt::PlayerAnimRow* rows = map_rows();
    if (!rt::map_runtime().loaded() || rows == nullptr) {
        return false;
    }
    const int n = Pc_HarryReactions_RowScan();
    for (int i = 0; i < n; ++i) {
        if (rows[i].status == 0 && rows[i].harry_status == 0) {
            break;
        }
        if (rows[i].harry_status == id) {
            return true;
        }
    }
    return false;
}

cg::ClipInfo clip_of(const rc::ReactionClip& c, bool* ok) {
    cg::ClipInfo info{};
    info.playback = reinterpret_cast<cg::ClipPlayback>(MapTables_GameFunction(c.playback.c_str()));
    info.status = static_cast<std::uint8_t>(c.status);
    info.variable_speed = c.variable ? 1 : 0;
    info.link = static_cast<std::uint8_t>(c.link);
    if (c.variable) {
        info.speed_fn = reinterpret_cast<cg::ClipSpeedFn>(MapTables_GameFunction(c.duration_func.c_str()));
        *ok = *ok && info.speed_fn != nullptr;
    } else {
        info.speed = c.duration;
    }
    info.start_keyframe = static_cast<std::int16_t>(c.start);
    info.end_keyframe = static_cast<std::int16_t>(c.end);
    *ok = *ok && info.playback != nullptr;
    return info;
}

// True when the source file holds every keyframe the plan copies (the extra one after a range may be past
// its end: it repeats the last).
bool keyframes_present(const rc::ReactionImport& plan, const std::string& file, int map_first) {
    const int count = PcEntityAnim_FileKeyframes(file.c_str());
    for (const rc::KeyframeCopy& k : plan.keyframes) {
        const int local = k.source_first - map_first;
        if (local < 0 || local + k.count - 1 > count) {
            return false;
        }
    }
    return count > 0 || plan.keyframes.empty();
}

// Applies one plan made against the current statuses and bank; returns the ids it gave.
std::vector<int> apply(const rc::MapReactionTable& source, const std::vector<int>& ids, int map_first) {
    int first_status = rc::kHarryMapStatusFirst + s_ext.clip_count;
    if ((first_status & 1) != 0) {
        ++first_status;
    }
    auto planned = rc::plan_reaction_import(source, ids, first_status, PcEntityAnim_HarryBankNext(), map_first);
    if (!planned) {
        SH_WARN("[REACTIONS] %s: %s", source.map.c_str(), planned.error().c_str());
        return {};
    }
    const rc::ReactionImport& plan = planned.value();
    if (plan.ids.empty()) {
        return {};
    }
    if (s_ext.row_count + static_cast<int>(plan.rows.size()) >= kRowsMax - 1 ||
        first_status - rc::kHarryMapStatusFirst + static_cast<int>(plan.clips.size()) > kClipsMax) {
        SH_WARN("[REACTIONS] %s: Harry's tables are full", loaded_map());
        return {};
    }
    std::vector<cg::ClipInfo> infos;
    bool ok = true;
    for (const rc::ReactionClip& c : plan.clips) {
        infos.push_back(clip_of(c, &ok));
    }
    if (!ok) {
        return {};  // a clip names a function the game lacks
    }
    // The map's own file first, then the files its scenes load over it (map6_s04's fight rows play
    // ANIM/HBM6_S4B.ANM): the first that holds every keyframe the plan copies.
    std::string file;
    for (const std::string& f : harry_files_of(source.map)) {
        if (keyframes_present(plan, f, map_first)) {
            file = f;
            break;
        }
    }
    if (file.empty()) {
        return {};
    }
    for (const rc::KeyframeCopy& k : plan.keyframes) {
        if (PcEntityAnim_HarryBankAppend(file.c_str(), k.source_first - map_first, k.count) != k.target_first) {
            SH_WARN("[REACTIONS] %s: keyframes %d..%d could not be copied", file.c_str(), k.source_first,
                    k.source_first + k.count - 1);
            return {};
        }
    }
    // An odd map clip count leaves one status empty before the pair: it repeats the last clip.
    while (rc::kHarryMapStatusFirst + s_ext.clip_count < first_status) {
        s_ext.clips[s_ext.clip_count] = s_ext.clips[s_ext.clip_count - 1];
        ++s_ext.clip_count;
    }
    for (const cg::ClipInfo& info : infos) {
        s_ext.clips[s_ext.clip_count] = info;
        HARRY_BASE_ANIM_INFOS[info.status] = info;
        ++s_ext.clip_count;
    }
    s_ext.clips[s_ext.clip_count] = cg::ClipInfo{};
    for (const rc::ReactionRow& r : plan.rows) {
        rt::PlayerAnimRow& row = s_ext.rows[s_ext.row_count];
        row.status = static_cast<std::int16_t>(r.status);
        row.harry_status = static_cast<std::int16_t>(r.id);
        row.first_keyframe = static_cast<std::int16_t>(r.time);
        row.last_keyframe = static_cast<std::int16_t>(r.last);
        s_ext.imported_rows.insert(s_ext.row_count);
        ++s_ext.row_count;
    }
    s_ext.rows[s_ext.row_count] = rt::PlayerAnimRow{};
    return plan.ids;
}

// Loads `ids` (missing on the current map) from `source`: all at once, else one by one (some of the
// source's clips may not be loadable); returns the ids it gave.
std::vector<int> import_from(const rc::MapReactionTable& source, const std::vector<int>& ids, int map_first) {
    std::vector<int> got = apply(source, ids, map_first);
    if (!got.empty() || ids.size() < 2) {
        return got;
    }
    for (int id : ids) {
        for (int g : apply(source, {id}, map_first)) {
            got.push_back(g);
        }
    }
    return got;
}

}  // namespace

extern "C" int Pc_HarryReactions_EnsureFor(int charaId, int hostile) {
    const rc::HarryReactions* names = catalog();
    const rc::CharaLibrary* lib = Pc_CharaData_Library();
    if (names == nullptr || lib == nullptr || charaId <= 0 || !rt::map_runtime().loaded()) {
        return -1;
    }
    const rc::CharacterDef* def = lib->find(charaId);
    std::vector<std::string> wanted = def != nullptr ? def->harry_reactions : std::vector<std::string>{};
    if (hostile != 0) {
        wanted.insert(wanted.end(), names->hostile().begin(), names->hostile().end());
    }
    std::vector<std::string> unknown;
    const std::vector<int> ids = names->resolve(wanted, &unknown);
    for (const std::string& u : unknown) {
        SH_WARN("[REACTIONS] %s: unknown Harry reaction \"%s\"", def != nullptr ? def->id.c_str() : "?", u.c_str());
    }
    std::vector<int> missing;
    for (int id : ids) {
        if (!has_row(id)) {
            missing.push_back(id);
        }
    }
    if (missing.empty()) {
        return 0;
    }
    const int map_first = PcEntityAnim_HarryMapKeyframeFirst();
    if (map_first < 0 || !install()) {
        return static_cast<int>(missing.size());
    }
    // The character's own maps first (their clips were made for it), then every map.
    std::vector<std::string> sources = def != nullptr ? def->maps : std::vector<std::string>{};
    for (int i = 0; i < sh::loader::map_count(); ++i) {
        if (const char* m = sh::loader::map_name(i)) {
            sources.emplace_back(m);
        }
    }
    const std::string here = loaded_map();
    // What no map could give stays missing for the rest of the visit (asked again by every spawn).
    const std::size_t asked = missing.size();
    std::erase_if(missing, [](int id) { return s_ext.failed.contains(id); });
    const int known_missing = static_cast<int>(asked - missing.size());
    std::set<std::string> tried;
    for (const std::string& map : sources) {
        if (missing.empty()) {
            break;
        }
        if (map == here || !tried.insert(map).second) {
            continue;
        }
        const rc::MapReactionTable* table = map_table(map);
        if (table == nullptr) {
            continue;
        }
        const std::vector<int> got = import_from(*table, missing, map_first);
        if (got.empty()) {
            continue;
        }
        std::string list;
        for (int id : got) {
            list += (list.empty() ? "" : ", ") + names->name_of(id);
            std::erase(missing, id);
        }
        SH_LOG("[REACTIONS] %s: %s loaded Harry's %s from %s", here.c_str(),
               def != nullptr ? def->id.c_str() : "?", list.c_str(), map.c_str());
    }
    for (int id : missing) {
        s_ext.failed.insert(id);
        SH_WARN("[REACTIONS] %s: no map gives Harry's %s (id %d) in a form this map can load", here.c_str(),
                names->name_of(id).c_str(), id);
    }
    return static_cast<int>(missing.size()) + known_missing;
}

extern "C" int Pc_HarryReactions_Row(int id, int* first, int* last) {
    const rt::PlayerAnimRow* rows = map_rows();
    if (!rt::map_runtime().loaded() || rows == nullptr) {
        return -1;
    }
    const int n = Pc_HarryReactions_RowScan();
    for (int i = 0; i < n; ++i) {
        if (rows[i].status == 0 && rows[i].harry_status == 0) {
            break;
        }
        if (rows[i].harry_status == id) {
            if (first != nullptr) {
                *first = rows[i].first_keyframe;
            }
            if (last != nullptr) {
                *last = rows[i].last_keyframe;
            }
            return i;
        }
    }
    return -1;
}

extern "C" int Pc_HarryReactions_RowScan(void) {
    if (!s_ext.installed || s_ext.imported_rows.empty() || !rt::map_runtime().loaded()) {
        return kRowsOriginal;
    }
    if (!same_map()) {
        return kRowsOriginal;
    }
    if (map_rows() != s_ext.rows || map_clips() != s_ext.clips) {
        point_at_copies();
    }
    return s_ext.row_count + 1 > kRowsOriginal ? s_ext.row_count + 1 : kRowsOriginal;
}

extern "C" int Pc_HarryReactions_RowImported(int index) {
    return same_map() && rt::map_runtime().loaded() && map_rows() == s_ext.rows && s_ext.imported_rows.contains(index)
               ? 1
               : 0;
}

extern "C" void Pc_HarryReactions_MapLoaded(void) {
    s_ext.installed = false;
    s_ext.imported_rows.clear();
    s_ext.failed.clear();
    PcEntityAnim_HarryBankClear();
}

// ---- The map's Harry tables and the attack rows, for the player's hosts (player_host.cpp & co.) ----

extern "C" const cg::ClipInfo* HarryMap_ClipTable(void) { return map_clips(); }

/* Row `row` of the map's reaction table: the clip status, its trigger id, the start time and the last keyframe;
 * 0 when the map has no table. */
extern "C" int HarryMap_ReactionRow(int row, int* status, int* trigger, int* time, int* last) {
    const rt::PlayerAnimRow* rows = map_rows();
    if (rows == nullptr) {
        return 0;
    }
    *status = rows[row].status;
    *trigger = rows[row].harry_status;
    *time = rows[row].first_keyframe;
    *last = rows[row].last_keyframe;
    return 1;
}

/* An attack row: reach, the hit window's first keyframe after the clip start and its length; 0 out of range. */
extern "C" int HarryAttack_Info(int attack, int* reach, int* hit_start, int* hit_length) {
    if (attack < 0 || attack >= cg::link::attack_count()) {
        return 0;
    }
    const sh::items::WeaponAttack row = cg::link::attack_row(attack);
    *reach = row.reach_q12;
    *hit_start = row.keyframe_start;
    *hit_length = row.keyframe_length;
    return 1;
}

/* The class of an attack that picks Harry's damage sound (the attack table's sound class; 0 out of range). */
extern "C" int HarryAttack_SoundClass(int attack) {
    return attack >= 0 && attack < cg::link::attack_count()
               ? static_cast<std::uint8_t>(cg::link::attack_row(attack).hurt_sound)
               : 0;
}

/* The pushing floor of map1_s05 / map1_s06 takes back the part of a push the walls refused. Harry's tick calls this
 * only while a push applies: on the Split Head maps, or for an engine-spawned Split Head off them (its push). */
extern "C" void HarryMap_PushFeedback(std::int32_t dx, std::int32_t dz) {
    Pc_SplitHead_MoveOffsetAdd(dx, dz);
}

const sh::maps::TableNames& HarryReactions_TableNames() { return s_table_names; }
