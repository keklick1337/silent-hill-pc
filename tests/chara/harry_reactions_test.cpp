// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "framework/sh_test.h"

#include "engine/chara/harry_reactions.h"

#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using sh::chara::HarryReactions;
using sh::chara::MapReactionTable;
using sh::chara::ReactionClip;
using sh::chara::ReactionRow;

namespace {

void put16(std::vector<std::uint8_t>& b, std::size_t at, int v) {
    b[at] = static_cast<std::uint8_t>(v & 0xFF);
    b[at + 1] = static_cast<std::uint8_t>((v >> 8) & 0xFF);
}

void put32(std::vector<std::uint8_t>& b, std::size_t at, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) {
        b[at + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>((v >> (8 * i)) & 0xFF);
    }
}

// A map with the torso hit (105 blend) and a looping pin (128 blend, variable duration) at clips 76..79,
// keyframes 676..700 of its own file and one row on Harry's base keyframes.
const char* kTables = R"({
  "bin": "VIN/MAPX.BIN", "base": "0x80000000",
  "symbols": [
    { "symbol": "HARRY_MX_ANIM_INFOS", "va": "0x80000000", "type": "s_AnimInfo", "count": 7 },
    { "symbol": "g_MapHeaderTable_38", "va": "0x80000100", "type": "s_UnkStruct3_Mo", "count": 4, "bytes": 32 }
  ],
  "functions": [
    { "va": "0x800449F0", "symbol": "Anim_PlaybackOnce" },
    { "va": "0x80044B38", "symbol": "Anim_PlaybackLoop" },
    { "va": "0x80044CA4", "symbol": "Anim_BlendLinear" },
    { "va": "0x800706E4", "symbol": "Player_VariableAnimDurationGet" }
  ]
})";

std::vector<std::uint8_t> bin() {
    std::vector<std::uint8_t> b(0x200, 0);
    auto clip = [&](int i, std::uint32_t fn, int status, int var, int link, std::uint32_t dur, int k0, int k1) {
        const std::size_t e = static_cast<std::size_t>(i) * 16;
        put32(b, e, fn);
        b[e + 4] = static_cast<std::uint8_t>(status);
        b[e + 5] = static_cast<std::uint8_t>(var);
        b[e + 6] = static_cast<std::uint8_t>(link);
        put32(b, e + 8, dur);
        put16(b, e + 12, k0);
        put16(b, e + 14, k1);
    };
    clip(0, 0x80044CA4, 76, 0, 77, 40960, -1, 676);
    clip(1, 0x800449F0, 77, 0, 77, 61440, 676, 690);
    clip(2, 0x80044CA4, 78, 0, 79, 61440, -1, 691);
    clip(3, 0x80044B38, 79, 1, 255, 0x800706E4, 691, 700);
    clip(4, 0x80044CA4, 80, 0, 81, 40960, -1, 485);
    clip(5, 0x800449F0, 81, 0, 81, 81920, 485, 502);
    // clip 6: the terminator (zero)
    auto row = [&](int i, int status, int id, int t, int k) {
        const std::size_t e = 0x100 + static_cast<std::size_t>(i) * 8;
        put16(b, e, status);
        put16(b, e + 2, id);
        put16(b, e + 4, t);
        put16(b, e + 6, k);
    };
    row(0, 77 << 8 | 76, 210, 676, 690);
    row(1, 79 << 8 | 78, 256, 691, 700);
    row(2, 81 << 8 | 80, 500, 485, 502);
    return b;
}

MapReactionTable table() {
    const std::vector<std::uint8_t> b = bin();
    auto t = sh::chara::read_map_reactions("mapx", kTables, b);
    SH_CHECK(static_cast<bool>(t));
    return t ? t.value() : MapReactionTable{};
}

}  // namespace

SH_TEST(harry_reactions_read_map_table) {
    const MapReactionTable t = table();
    SH_CHECK(t.rows.size() == 3);
    SH_CHECK(t.clips.size() == 6);
    SH_CHECK(t.row(256) != nullptr && t.row(256)->time == 691);
    SH_CHECK(t.clips[3].playback == "Anim_PlaybackLoop");
    SH_CHECK(t.clips[3].variable && t.clips[3].duration_func == "Player_VariableAnimDurationGet");
    SH_CHECK(t.clips[0].start == -1 && t.clips[0].end == 676);
    SH_CHECK(sh::chara::tables_bin(kTables) == "VIN/MAPX.BIN");
}

SH_TEST(harry_reactions_plan_renumbers_statuses_and_keyframes) {
    const MapReactionTable t = table();
    // The pin and a base-keyframe row onto a map with 10 clips (statuses 76..85) and 2 banked keyframes.
    auto planned = sh::chara::plan_reaction_import(t, {256, 500, 999}, 86, 4002, 676);
    SH_CHECK(static_cast<bool>(planned));
    if (!planned) {
        return;
    }
    const auto& p = planned.value();
    SH_CHECK((p.ids == std::vector<int>{256, 500}));
    SH_CHECK(p.clips.size() == 4);
    SH_CHECK(p.clips[0].status == 86 && p.clips[0].link == 87 && p.clips[0].start == -1 && p.clips[0].end == 4002);
    SH_CHECK(p.clips[1].status == 87 && p.clips[1].link == 255 && p.clips[1].start == 4002 && p.clips[1].end == 4011);
    // Base keyframes stay.
    SH_CHECK(p.clips[2].status == 88 && p.clips[3].start == 485 && p.clips[3].end == 502);
    SH_CHECK(p.keyframes.size() == 1);
    SH_CHECK((p.keyframes[0] == sh::chara::KeyframeCopy{691, 11, 4002}));  // 691..700 and one after
    SH_CHECK(p.rows.size() == 2);
    SH_CHECK(p.rows[0].status == (87 << 8 | 86) && p.rows[0].id == 256 && p.rows[0].time == 4002 &&
             p.rows[0].last == 4011);
    SH_CHECK(p.rows[1].status == (89 << 8 | 88) && p.rows[1].time == 485);
}

SH_TEST(harry_reactions_plan_merges_adjacent_ranges) {
    const MapReactionTable t = table();
    auto planned = sh::chara::plan_reaction_import(t, {210, 256}, 76, 4000, 676);
    SH_CHECK(static_cast<bool>(planned));
    if (!planned) {
        return;
    }
    // 676..690 (+691) and 691..700 (+701) touch: one copy 676..701.
    SH_CHECK(planned.value().keyframes.size() == 1);
    SH_CHECK((planned.value().keyframes[0] == sh::chara::KeyframeCopy{676, 26, 4000}));
    SH_CHECK(planned.value().rows[1].time == 4015);
}

SH_TEST(harry_reactions_plan_refuses_status_overflow) {
    const MapReactionTable t = table();
    SH_CHECK(!sh::chara::plan_reaction_import(t, {210, 256}, 254, 4000, 676));
    SH_CHECK(!sh::chara::plan_reaction_import(t, {210}, 77, 4000, 676));
}

SH_TEST(harry_reactions_names_and_groups) {
    auto r = HarryReactions::parse(R"({
        "clips": { "DamageTorsoFront": { "anim": 105, "active": false },
                   "DamageTorsoBack": { "anim": 105, "active": true },
                   "Death": { "anim": 101, "active": false } },
        "groups": { "Hit": ["DamageTorsoFront", "DamageTorsoBack"] },
        "hostile": ["Hit", "Death"] })");
    SH_CHECK(static_cast<bool>(r));
    if (!r) {
        return;
    }
    std::vector<std::string> unknown;
    SH_CHECK((r.value().resolve(r.value().hostile(), &unknown) == std::vector<int>{202, 210, 211}));
    SH_CHECK(unknown.empty());
    SH_CHECK((r.value().resolve({"Nope", "Death"}, &unknown) == std::vector<int>{202}));
    SH_CHECK(unknown.size() == 1 && unknown[0] == "Nope");
    SH_CHECK(r.value().name_of(211) == "DamageTorsoBack");
}

// The shipped file: every group member is a clip, every character's harry_reactions resolves.
SH_TEST(harry_reactions_shipped_data) {
    std::ifstream in(std::string(SH_PROJECT_ROOT) + "/gamedata/chara/Harry/reactions.json");
    if (!in) {
        SH_SKIP("gamedata/chara/Harry/reactions.json not found");
    }
    std::stringstream text;
    text << in.rdbuf();
    auto r = HarryReactions::parse(text.str());
    SH_CHECK(static_cast<bool>(r));
    if (!r) {
        return;
    }
    std::vector<std::string> unknown;
    const std::vector<std::string> all = {"Hit", "HeadHit", "FeetHit", "Push", "LegGrab", "TorsoGrab", "NeckGrab",
                                          "Pin", "Thrown", "Bolt", "Death", "InstantDeath"};
    SH_CHECK(r.value().resolve(all, &unknown).size() == 36);
    SH_CHECK(unknown.empty());
}
