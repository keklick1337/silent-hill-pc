// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Stalker, Grey Child and Mumbler (one behaviour, gamedata/chara/Stalker/behavior.lua; their clip file is
// gamedata/chara/Stalker/clips.json): the player-movement prediction every stalker of a map shares.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <string_view>

#include "engine/chara/behavior_host.h"
#include "engine/chara/game/chara_link.h"

namespace {

namespace cg = sh::chara::game;
using cg::link::OverlayVariable;
using sh::chara::behavior::Args;
using sh::chara::behavior::fail;

const char* const kNames[] = {
    "entity.stalker_track",
    "entity.set_stalker_track",
};

// Harry's movement as the stalkers of a map sample it once per frame (the first stalker updated that frame does it):
// the last position, five speed and heading samples, and the predicted position they chase. The map overlay keeps
// them (they live and reset with it), so every stalker type of the map shares them.
struct Track {
    const char* name;
    OverlayVariable variable;
    int bytes;  // 1: u8, 2: u16, 4: s32
    int count;
};

const Track kTrack[] = {
    {"samples", OverlayVariable::StalkerSamples, 1, 1},
    {"prev_x", OverlayVariable::StalkerPrevX, 4, 1},
    {"prev_z", OverlayVariable::StalkerPrevZ, 4, 1},
    {"speed", OverlayVariable::StalkerSpeed, 4, 5},
    {"heading", OverlayVariable::StalkerHeading, 2, 5},
    {"predict_x", OverlayVariable::StalkerPredictX, 4, 1},
    {"predict_z", OverlayVariable::StalkerPredictZ, 4, 1},
};

// Where a map's tables have no such variable the executable's own words stand in (the update writes them before
// reading).
unsigned char* track_ptr(const Track& t, int idx) {
    alignas(4) static unsigned char s_fallback[std::size(kTrack)][4 * 5] = {};
    void* p = cg::link::overlay_variable(t.variable);
    if (p == nullptr) {
        p = s_fallback[&t - kTrack];
    }
    return static_cast<unsigned char*>(p) + idx * t.bytes;
}

long long track_get(const Track& t, int idx) {
    auto* p = track_ptr(t, idx);
    switch (t.bytes) {
        case 1: return *p;
        case 2: {
            std::uint16_t v;
            std::memcpy(&v, p, 2);
            return v;
        }
        default: {
            std::int32_t v;
            std::memcpy(&v, p, 4);
            return v;
        }
    }
}

void track_set(const Track& t, int idx, long long v) {
    auto* p = track_ptr(t, idx);
    switch (t.bytes) {
        case 1: *p = static_cast<std::uint8_t>(v); break;
        case 2: {
            const auto x = static_cast<std::uint16_t>(v);
            std::memcpy(p, &x, 2);
            break;
        }
        default: {
            const auto x = static_cast<std::int32_t>(v);
            std::memcpy(p, &x, 4);
            break;
        }
    }
}

int call(std::string_view fn, const Args& a, long long* out, char* err, int errCap) {
    if (fn == "stalker_track" || fn == "set_stalker_track") {
        // stalker_track(name[, index]) / set_stalker_track(name, index, value)
        const char* name = a.text(0);
        for (std::size_t i = 0; name != nullptr && i < sizeof(kTrack) / sizeof(kTrack[0]); ++i) {
            if (std::strcmp(kTrack[i].name, name) != 0) {
                continue;
            }
            const long long idx = a.integer(1, 0);
            if (idx < 0 || idx >= kTrack[i].count) {
                return fail(err, errCap, "track index out of range");
            }
            if (fn == "stalker_track") {
                out[0] = track_get(kTrack[i], static_cast<int>(idx));
                return 1;
            }
            track_set(kTrack[i], static_cast<int>(idx), a.integer(2));
            return 0;
        }
        return fail(err, errCap, "unknown stalker track '%s'", name != nullptr ? name : "?");
    }
    return fail(err, errCap, "unknown function");
}

sh::chara::behavior::Extension s_extension = {
    nullptr, 0, kNames, sizeof(kNames) / sizeof(kNames[0]), call, nullptr, 0, nullptr,
};

const sh::chara::behavior::Registrar s_registrar{&s_extension};

}  // namespace
