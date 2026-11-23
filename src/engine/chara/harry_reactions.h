// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Harry's reaction clips as data. Every hit, grab, fall and death Harry plays
// in reply to an enemy is a clip of the map he stands on: the map's reaction table maps a reaction id
// (Harry's state machine asks for it: 105 = torso hit, 127 = Romper pin, ...) to one of the map's own
// clips (statuses 76.., keyframes of the map's HB_M*.ANM file). A map carries only the rows of the
// enemies it hosts, so an enemy spawned on another map finds nothing to play.
//
// This module is the pure half of borrowing them: the names of the reactions (gamedata/chara/Harry/reactions.json),
// reading a map's table from its disc overlay (tables.json + the BIN bytes), and planning which clips,
// rows and keyframes of another map have to be appended to the current one. The game side applies the
// plan.

#pragma once

#include "formats/common/status.h"

#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sh::chara {

// The first status of a map's clips (anim 38): statuses below it are Harry's base clips.
constexpr int kHarryMapStatusFirst = 76;
// The last status a clip can have (the model's status is a byte).
constexpr int kHarryStatusMax = 255;

// gamedata/chara/Harry/reactions.json:
// { "clips":  { "DamageTorsoFront": { "anim": 105, "active": false }, ... },
//   "groups": { "Hit": ["DamageTorsoFront", ...], ... },
//   "hostile": ["Hit", "Death"] }
// A reaction id is anim << 1 | active (the table's packed status).
class HarryReactions {
public:
    static Result<HarryReactions> parse(std::string_view json_text);

    // The ids of `names` (clip or group names, a group expands to its clips): sorted, unique. Names
    // that are neither go to `unknown`.
    std::vector<int> resolve(const std::vector<std::string>& names, std::vector<std::string>* unknown) const;
    // What every hostile spawn needs whatever it attacks with.
    const std::vector<std::string>& hostile() const { return hostile_; }
    // The id of a clip name (not a group), -1 when the file has no such clip.
    int id_of(std::string_view name) const;
    // The clip name of an id, "" when the file names none.
    std::string name_of(int id) const;
    std::size_t clip_count() const { return clips_.size(); }

private:
    std::map<std::string, int, std::less<>> clips_;
    std::map<std::string, std::vector<std::string>, std::less<>> groups_;
    std::vector<std::string> hostile_;
};

// One row of a map's reaction table (the disc's 8-byte record). `status` keeps both bytes the disc
// stores: the low one is the clip status the row starts, the high one the status after it.
struct ReactionRow {
    int status = 0;
    int id = 0;    // the reaction id Harry asks for
    int time = 0;  // first keyframe
    int last = 0;  // last keyframe
    bool operator==(const ReactionRow&) const = default;
};

// One of Harry's map clip entries (the disc's 16-byte s_AnimInfo). Functions are by name, from the
// table's "functions" list ("" when the word is 0, "?" for an address the list does not name).
struct ReactionClip {
    std::string playback;      // "Anim_PlaybackOnce" ...
    int status = 0;
    bool variable = false;     // the duration is a function
    int link = 0;              // status after the clip (255: none)
    std::int32_t duration = 0;      // constant duration (Q12) when !variable
    std::string duration_func;      // the duration function when variable
    int start = 0;             // first keyframe (-1 for a blend)
    int end = 0;               // last keyframe
    bool operator==(const ReactionClip&) const = default;
};

// A map's table and its clips (index = status - kHarryMapStatusFirst), up to their terminators.
struct MapReactionTable {
    std::string map;
    std::vector<ReactionRow> rows;
    std::vector<ReactionClip> clips;

    const ReactionRow* row(int id) const;
};

// Reads the table of a map from its gamedata/maps/<map>/tables.json text (symbols "g_MapHeaderTable_38"
// and the s_AnimInfo array named HARRY_*_ANIM_INFOS) and the bytes of its overlay BIN.
Result<MapReactionTable> read_map_reactions(std::string_view map, std::string_view tables_json,
                                            std::span<const std::uint8_t> bin);

// The disc overlay file a tables.json reads ("VIN/MAP1_S02.BIN"), "" when it names none.
std::string tables_bin(std::string_view tables_json);

// Keyframes [source_first, source_first + count) of the source map become
// [target_first, target_first + count) of the current one.
struct KeyframeCopy {
    int source_first = 0;
    int count = 0;
    int target_first = 0;
    bool operator==(const KeyframeCopy&) const = default;
};

// What to append to the current map to play some reactions of `source`.
struct ReactionImport {
    std::vector<ReactionClip> clips;  // statuses first_status, first_status + 1, ...
    std::vector<ReactionRow> rows;
    std::vector<KeyframeCopy> keyframes;
    std::vector<int> ids;             // the reaction ids the rows give
};

// Plans the import of `ids` from `source`. New clips take statuses from `first_status` (even) on,
// keyframes of the source's map file (>= `map_keyframe_first`) are renumbered from `first_keyframe`
// (one extra keyframe after each range: a clip ending on its last keyframe samples the next one at
// weight 0); keyframes below it are Harry's base file and stay. A clip's link to another map clip pulls
// that clip in too. Ids the source has no row for are skipped (not in `ids` of the result). Fails when
// the statuses would pass kHarryStatusMax.
Result<ReactionImport> plan_reaction_import(const MapReactionTable& source, const std::vector<int>& ids,
                                            int first_status, int first_keyframe, int map_keyframe_first);

}  // namespace sh::chara
