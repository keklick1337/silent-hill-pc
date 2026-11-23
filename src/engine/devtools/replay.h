// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Replay harness: per-tick pad input from a file, a per-tick state dump, live recording.
// Off unless SH_REPLAY_INPUT, SH_REPLAY_DUMP, SH_REPLAY_RECORD or SH_REPLAY_END is set; when off, each call is one
// branch on a flag.
//
// The dump reads the session (sh::game::session()), the progress record, the camera and the game random by their
// names. The frame clock counters and the character records come through a ReplayProbe the game installs.

#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace sh::game {
struct RawPad;
}

namespace sh::devtools {

// One character record as the dump prints and hashes it. Every value is the record's field widened to 32 bits the
// way the field's own type widens.
struct ReplayCharacter {
    std::int32_t chara_id = 0;
    std::int32_t control_state = 0;
    std::int32_t state_step = 0;
    std::int32_t anim_status = 0;
    std::int32_t anim_flags = 0;
    std::int32_t keyframe = 0;
    std::int32_t anim_time = 0;
    std::int32_t anim_alpha = 0;
    std::int32_t position[3] = {0, 0, 0};
    std::int16_t rotation[3] = {0, 0, 0};  // x, y, z
    std::int32_t move_speed = 0;
    std::int32_t fall_speed = 0;
    std::int32_t heading = 0;
    std::int32_t flags = 0;
    std::int32_t npc_index = 0;
    std::int32_t attack_received = 0;
    std::int32_t health = 0;
    std::int32_t death_timer = 0;
    std::int32_t timer = 0;
};

// The player's second record: the upper-body animation and the movement states.
struct ReplayPlayerExtra {
    std::int32_t anim_status = 0;
    std::int32_t keyframe = 0;
    std::int32_t anim_time = 0;
    std::int32_t anim_alpha = 0;
    std::int32_t state = 0;
    std::int32_t upper_body = 0;
    std::int32_t lower_body = 0;
    std::int32_t last_used_item = 0;
};

// Counters of the frame clock.
struct ReplayClock {
    std::int32_t tick = 0;
    std::int32_t vblanks = 0;
    std::int32_t uncapped_vblanks = 0;
    std::int32_t interval_vblanks = 0;
    std::int32_t delta_time = 0;
    std::int32_t delta_time_raw = 0;
    std::int32_t gravity = 0;
    std::int32_t demo_frames = 0;
    std::int32_t warm_boot_timer = 0;
};

// Digests of the session's records, each over its values field by field: the live progress
// record ("sav"), the game block without the raw pad the host writes from live input ("gw"), the player's weapon in
// hand ("cmb").
struct ReplayDigests {
    std::uint32_t save = 0;
    std::uint32_t game = 0;
    std::uint32_t combat = 0;
};

class ReplayProbe {
public:
    virtual ~ReplayProbe() = default;
    virtual ReplayClock clock() = 0;
    virtual ReplayDigests digests() = 0;
    // Slot 0 the player, 1..kNpcSlots the NPCs.
    virtual void character(int slot, ReplayCharacter& out) = 0;
    virtual void player_extra(ReplayPlayerExtra& out) = 0;
    // The player's per-type block (hashed whole).
    virtual std::span<const std::uint8_t> player_properties() = 0;
    // Virtual time of the host (two runs of one input give the same dump only with it).
    virtual bool virtual_time() = 0;
    virtual bool virtual_time_paced() = 0;
    // Runs a console command line (the end-of-run shot).
    virtual void console(const std::string& line) = 0;
};

void set_replay_probe(ReplayProbe* probe);

// Reads the environment and the input file. Once, before the first tick.
void replay_init();
// After the pad read: replaces the pad of this tick with the input file's buttons, or records the live buttons.
void replay_pad(sh::game::RawPad& pad);
// Before the game state update: writes the dump line of this tick and ends the process after the last tick.
void replay_tick();

// The pure parts, for the unit tests.
namespace replay {

struct InputEvent {
    std::int32_t tick = 0;
    std::uint16_t pressed = 0;  // pad bits (1: down)
};

struct Input {
    std::vector<InputEvent> events;
    std::int32_t end_tick = 0;  // 0: last event + 30
    std::string error;          // "path:line: what 'token'" when the file is bad
};

// Parses an input file's text.
Input parse_input(const std::string& path, const std::string& text);
// " Name Name" of the pressed bits (pad order), " -" for none.
std::string button_list(std::uint16_t pressed);

// 32-bit FNV-1a.
constexpr std::uint32_t kFnvBasis = 2166136261u;
std::uint32_t fnv(std::uint32_t h, const void* data, std::size_t size);
std::uint32_t fnv_s32(std::uint32_t h, std::int32_t v);
std::uint32_t character_hash(std::uint32_t h, const ReplayCharacter& c);
// " tag=..." of one character.
std::string character_text(const char* tag, const ReplayCharacter& c);

}  // namespace replay

}  // namespace sh::devtools
