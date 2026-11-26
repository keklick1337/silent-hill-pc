// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Rules of the map NPCs: gamedata/npc/rules.json and the arithmetic.

#include "engine/entities/npc_rules.h"

#include <stdexcept>

#include "formats/common/json.h"

namespace sh::entities {

namespace {

using json::Value;

[[noreturn]] void fail(const Value& at, const std::string& what) {
    throw std::runtime_error("line " + std::to_string(at.line()) + ", column " + std::to_string(at.column()) + ": " + what);
}

const Value& member(const Value& parent, std::string_view key) {
    const Value* v = parent.find(key);
    if (v == nullptr) {
        fail(parent, "\"" + std::string(key) + "\" is missing");
    }
    return *v;
}

void reject_unknown(const Value& object, std::initializer_list<std::string_view> known) {
    if (!object.is_object()) {
        fail(object, "an object is expected");
    }
    for (const auto& m : object.members()) {
        bool ok = (!m.first.empty() && m.first[0] == '_') || m.first == "description";
        for (std::string_view k : known) {
            ok = ok || m.first == k;
        }
        if (!ok) {
            fail(m.second, "unknown key \"" + m.first + "\"");
        }
    }
}

std::int32_t integer(const Value& v, std::int64_t lo, std::int64_t hi) {
    if (!v.is_integer() || v.integer() < lo || v.integer() > hi) {
        fail(v, "an integer from " + std::to_string(lo) + " to " + std::to_string(hi) + " is expected");
    }
    return static_cast<std::int32_t>(v.integer());
}

std::int32_t integer_of(const Value& parent, std::string_view key, std::int64_t lo, std::int64_t hi) {
    return integer(member(parent, key), lo, hi);
}

}  // namespace

Result<NpcRules> parse_npc_rules(std::string_view json_text) try {
    const Result<json::Value> doc = json::parse(json_text);
    if (!doc) {
        return Result<NpcRules>::fail(doc.error());
    }
    const Value& root = doc.value();
    reject_unknown(root, {"spawn", "despawn", "radio_range_sq", "kill_persist_threshold", "idle_clip_duration", "idle_clips", "state_restart", "kill_tally"});
    NpcRules r;
    const Value& s = member(root, "spawn");
    reject_unknown(s, {"ring_outer", "ring_inner", "long_view_outer", "long_view_inner", "cooldown_ticks", "frame_budget_vblanks"});
    r.spawn.ring_outer = integer_of(s, "ring_outer", 1, 400);
    r.spawn.ring_inner = integer_of(s, "ring_inner", 0, 400);
    r.spawn.long_view_outer = integer_of(s, "long_view_outer", 1, 400);
    r.spawn.long_view_inner = integer_of(s, "long_view_inner", 0, 400);
    r.spawn.cooldown_ticks = integer_of(s, "cooldown_ticks", 0, 100000);
    r.spawn.frame_budget_vblanks = integer_of(s, "frame_budget_vblanks", 1, 100);
    const Value& d = member(root, "despawn");
    reject_unknown(d, {"alive", "corpse", "long_view_alive", "long_view_corpse", "long_view_hard", "hide_distance"});
    r.despawn.alive = integer_of(d, "alive", 1, 400);
    r.despawn.corpse = integer_of(d, "corpse", 1, 400);
    r.despawn.long_view_alive = integer_of(d, "long_view_alive", 1, 400);
    r.despawn.long_view_corpse = integer_of(d, "long_view_corpse", 1, 400);
    r.despawn.long_view_hard = integer_of(d, "long_view_hard", 1, 400);
    r.despawn.hide_distance = integer_of(d, "hide_distance", 1, 400);
    r.radio_range_sq = integer_of(root, "radio_range_sq", 1, 1 << 20);
    r.kill_persist_threshold = integer_of(root, "kill_persist_threshold", 0, 4096);
    r.idle_clip_duration = integer_of(root, "idle_clip_duration", 1, 1 << 24);
    const Value& clips = member(root, "idle_clips");
    if (!clips.is_array()) {
        fail(clips, "a list is expected");
    }
    for (const Value& c : clips.array()) {
        reject_unknown(c, {"chara", "first", "last"});
        NpcRules::IdleClip clip;
        const Value& name = member(c, "chara");
        if (!name.is_string()) {
            fail(name, "a character id is expected");
        }
        clip.chara = name.string();
        clip.first = integer_of(c, "first", 0, 1000);
        clip.last = integer_of(c, "last", 0, 1000);
        r.idle_clips.push_back(std::move(clip));
    }
    if (const Value* sr = root.find("state_restart")) {
        reject_unknown(*sr, {"session", "spawn", "spawn_slots"});
        auto names = [](const Value& list) {
            if (!list.is_array()) {
                fail(list, "a list of character ids is expected");
            }
            std::vector<std::string> out;
            for (const Value& n : list.array()) {
                if (!n.is_string()) {
                    fail(n, "a character id is expected");
                }
                out.push_back(n.string());
            }
            return out;
        };
        r.state_restart.session = names(member(*sr, "session"));
        r.state_restart.spawn = names(member(*sr, "spawn"));
        r.state_restart.spawn_slots = integer_of(*sr, "spawn_slots", 0, 32);
    }
    if (const Value* kt = root.find("kill_tally")) {
        reject_unknown(*kt, {"player_attacks", "ranged_state"});
        r.kill_tally.player_attacks = integer_of(*kt, "player_attacks", 0, 70);
        r.kill_tally.ranged_state = integer_of(*kt, "ranged_state", 0, 255);
    }
    return Result<NpcRules>::ok(std::move(r));
} catch (const std::exception& e) {
    return Result<NpcRules>::fail(e.what());
}

bool outside_radius(std::int32_t ax, std::int32_t az, std::int32_t bx, std::int32_t bz, std::int32_t radius) {
    const std::int32_t dx = ax - bx;
    if (radius < dx || radius < -dx) {
        return true;
    }
    const std::int32_t dz = az - bz;
    if (radius < dz || radius < -dz) {
        return true;
    }
    const std::int32_t sum = static_cast<std::int32_t>((static_cast<std::int64_t>(dx) * dx) >> 12) +
                             static_cast<std::int32_t>((static_cast<std::int64_t>(dz) * dz) >> 12);
    const std::int32_t radius_sq = static_cast<std::int32_t>((static_cast<std::int64_t>(radius) * radius) >> 12);
    return sum > radius_sq;
}

namespace {
std::int32_t square_q12(std::int32_t v) { return static_cast<std::int32_t>((static_cast<std::int64_t>(v) * v) >> 12); }
}  // namespace

std::uint32_t tracked_distance_sq(std::int32_t ax, std::int32_t az, std::int32_t bx, std::int32_t bz) {
    return static_cast<std::uint32_t>(square_q12((ax >> 6) - (bx >> 6)) + square_q12((az >> 6) - (bz >> 6)));
}

std::int32_t shifted_difference_sq(std::int32_t ax, std::int32_t az, std::int32_t bx, std::int32_t bz) {
    return square_q12((ax - bx) >> 6) + square_q12((az - bz) >> 6);
}

bool kill_credit_due(std::int32_t health, std::uint32_t mask) { return health <= 0 && (mask & 0x600000u) == 0x200000u; }

bool damage_pending(std::int32_t damage_amount) { return damage_amount > 0; }

void RadioTracker::reset() {
    for (RadioEntry& e : entries_) {
        e = RadioEntry{kNoSlot, range_sq_, 0, 0};
    }
}

void RadioTracker::offer(int slot, std::uint32_t distance_sq, std::int32_t x, std::int32_t z) {
    for (int j = 0; j < kRadioEntries; ++j) {
        if (distance_sq >= entries_[static_cast<std::size_t>(j)].distance_sq) {
            continue;
        }
        for (int m = kRadioEntries - 1; j < m; --m) {
            entries_[static_cast<std::size_t>(m)] = entries_[static_cast<std::size_t>(m - 1)];
        }
        entries_[static_cast<std::size_t>(j)] = RadioEntry{slot, distance_sq, x, z};
        return;
    }
}

int RadioTracker::pitch_state() const {
    for (int k = kRadioEntries - 1; k >= 0; --k) {
        if (entries_[static_cast<std::size_t>(k)].slot != kNoSlot) {
            return k + 1;
        }
    }
    return 0;
}

int RadioTracker::index_of(int slot) const {
    for (int i = 0; i < 2; ++i) {  // only the two nearest can be heard
        if (slot == entries_[static_cast<std::size_t>(i)].slot) {
            return i;
        }
    }
    return kNoSlot;
}

int RadioTracker::take_free(RadioSlotSet& taken) const {
    for (int i = 0; i < 2; ++i) {
        const std::int32_t slot = entries_[static_cast<std::size_t>(i)].slot;
        if (slot == kNoSlot) {
            break;
        }
        if (!taken.test(static_cast<std::size_t>(slot))) {
            taken.set(static_cast<std::size_t>(slot));
            return i;
        }
    }
    return kNoSlot;
}

}  // namespace sh::entities
