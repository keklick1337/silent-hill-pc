// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The replay harness's file formats and hashes.

#include "engine/devtools/replay.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace sh::devtools::replay {

namespace {

struct ButtonName {
    const char* name;
    std::uint16_t bit;
};

// Pad word order; the recorder writes names in this order.
constexpr ButtonName kButtons[] = {
    {"Select", 1 << 0},    {"L3", 1 << 1},      {"R3", 1 << 2},     {"Start", 1 << 3},
    {"Up", 1 << 4},        {"Right", 1 << 5},   {"Down", 1 << 6},   {"Left", 1 << 7},
    {"L2", 1 << 8},        {"R2", 1 << 9},      {"L1", 1 << 10},    {"R1", 1 << 11},
    {"Triangle", 1 << 12}, {"Circle", 1 << 13}, {"Cross", 1 << 14}, {"Square", 1 << 15},
};

std::string fail(const std::string& path, int line, const char* what, const char* token) {
    return path + ":" + std::to_string(line) + ": " + what + " '" + token + "'";
}

bool is_separator(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

// Buttons of the rest of an event line: names separated by blanks, "-" for none, '#' starts a comment.
bool parse_buttons(const std::string& path, int line_no, const char* list, std::uint16_t* out, std::string* error) {
    std::uint16_t pressed = 0;
    const char* p = list;
    for (;;) {
        while (*p != '\0' && is_separator(*p))
            p++;
        if (*p == '\0' || *p == '#')
            break;
        const char* start = p;
        while (*p != '\0' && !is_separator(*p))
            p++;
        const std::string token(start, p);
        if (token != "-") {
            bool known = false;
            for (const ButtonName& b : kButtons) {
                if (token == b.name) {
                    pressed |= b.bit;
                    known = true;
                    break;
                }
            }
            if (!known) {
                *error = fail(path, line_no, "unknown button", token.c_str());
                return false;
            }
        }
    }
    *out = pressed;
    return true;
}

}  // namespace

Input parse_input(const std::string& path, const std::string& text) {
    Input in;
    int line_no = 0;
    std::size_t pos = 0;
    while (pos < text.size()) {
        std::size_t end = text.find('\n', pos);
        end = end == std::string::npos ? text.size() : end + 1;
        const std::string line = text.substr(pos, end - pos);
        pos = end;
        line_no++;

        const char* p = line.c_str();
        while (*p == ' ' || *p == '\t')
            p++;
        if (*p == '#' || *p == '\n' || *p == '\r' || *p == '\0')
            continue;

        char* rest = nullptr;
        if (std::strncmp(p, "end", 3) == 0 && (p[3] == ' ' || p[3] == '\t')) {
            const long tick = std::strtol(p + 4, &rest, 10);
            if (rest == p + 4 || tick < 1) {
                in.error = fail(path, line_no, "bad end tick", p);
                return in;
            }
            in.end_tick = static_cast<std::int32_t>(tick);
            continue;
        }

        const long tick = std::strtol(p, &rest, 10);
        if (rest == p || tick < 1) {
            in.error = fail(path, line_no, "bad tick", p);
            return in;
        }
        if (!in.events.empty() && tick <= in.events.back().tick) {
            in.error = fail(path, line_no, "ticks must increase", p);
            return in;
        }
        InputEvent ev;
        ev.tick = static_cast<std::int32_t>(tick);
        if (!parse_buttons(path, line_no, rest, &ev.pressed, &in.error))
            return in;
        in.events.push_back(ev);
    }
    if (in.end_tick == 0 && !in.events.empty())
        in.end_tick = in.events.back().tick + 30;
    return in;
}

std::string button_list(std::uint16_t pressed) {
    std::string out;
    for (const ButtonName& b : kButtons) {
        if (pressed & b.bit) {
            out += ' ';
            out += b.name;
        }
    }
    return out.empty() ? " -" : out;
}

std::uint32_t fnv(std::uint32_t h, const void* data, std::size_t size) {
    const auto* p = static_cast<const std::uint8_t*>(data);
    for (std::size_t i = 0; i < size; i++) {
        h ^= p[i];
        h *= 16777619u;
    }
    return h;
}

std::uint32_t fnv_s32(std::uint32_t h, std::int32_t v) { return fnv(h, &v, sizeof(v)); }

std::uint32_t character_hash(std::uint32_t h, const ReplayCharacter& c) {
    h = fnv_s32(h, c.chara_id);
    h = fnv_s32(h, c.control_state);
    h = fnv_s32(h, c.state_step);
    h = fnv_s32(h, c.anim_status);
    h = fnv_s32(h, c.anim_flags);
    h = fnv_s32(h, c.keyframe);
    h = fnv_s32(h, c.anim_time);
    h = fnv_s32(h, c.anim_alpha);
    h = fnv(h, c.position, sizeof(c.position));
    h = fnv(h, c.rotation, sizeof(c.rotation));
    h = fnv_s32(h, c.move_speed);
    h = fnv_s32(h, c.fall_speed);
    h = fnv_s32(h, c.heading);
    h = fnv_s32(h, c.flags);
    h = fnv_s32(h, c.npc_index);
    h = fnv_s32(h, c.attack_received);
    h = fnv_s32(h, c.health);
    h = fnv_s32(h, c.death_timer);
    h = fnv_s32(h, c.timer);
    return h;
}

std::string character_text(const char* tag, const ReplayCharacter& c) {
    char buf[512];
    std::snprintf(buf, sizeof(buf), " %s=%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d", tag,
                  static_cast<int>(c.chara_id), static_cast<int>(c.control_state), static_cast<int>(c.state_step),
                  static_cast<int>(c.anim_status), static_cast<int>(c.keyframe), static_cast<int>(c.anim_time),
                  static_cast<int>(c.anim_alpha), static_cast<int>(c.position[0]), static_cast<int>(c.position[1]),
                  static_cast<int>(c.position[2]), static_cast<int>(c.rotation[0]), static_cast<int>(c.rotation[1]),
                  static_cast<int>(c.rotation[2]), static_cast<int>(c.move_speed), static_cast<int>(c.fall_speed),
                  static_cast<int>(c.heading), static_cast<int>(c.health), static_cast<int>(c.flags),
                  static_cast<int>(c.attack_received), static_cast<int>(c.death_timer));
    return buf;
}

}  // namespace sh::devtools::replay
