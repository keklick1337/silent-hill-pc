// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Attract-mode demo playback: which recorded demo plays next, the
// game settings and random seed a demo swaps in and back, and each frame's decision — feed the recorded pad
// and frame interval, keep stepping, or leave the demo for the title.
//
// A demo is two files of the disc: the setup (2048 bytes: the settings, the save it starts from, the number
// of recorded frames, the start seed) and the play file (16 bytes per frame: the pad report, the game state
// the frame expects, the frame's VBlank interval, the frame's seed). The rotation and the state rules are
// gamedata/config/demos.json; everything that touches the game's records goes through DemoHost.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "formats/common/status.h"

namespace sh::gameflow {

struct DemoFiles {
    std::string setup;  // disc path of the setup file ("MISC/DEMO0009.DAT")
    std::string play;   // disc path of the recorded frames ("MISC/PLAY0009.DAT")
};

struct DemoRules {
    std::vector<DemoFiles> rotation;         // played in order, wrapping
    std::vector<std::int32_t> step_states;   // game states in which the recording advances
    std::int32_t exit_from_state = -1;       // ...except this one (InGame) when:
    std::int32_t exit_on_sys_state = -1;     //   the system state is this (GameOver), or
    std::int32_t exit_after_state = -1;      //   the previous game state was this (SaveScreen)
    std::int32_t exit_timer = 0;             // the demo timer set on exit, past the soft reset's limit
    std::uint32_t seed_advance = 0;          // added to the backed-up seed between two world passes

    static DemoRules builtin();
    static Result<DemoRules> parse(std::string_view json);
};

class DemoHost {
public:
    virtual ~DemoHost() = default;

    virtual bool demo_flag() const = 0;  // the system's "demo running" flag
    virtual void set_demo_flag(bool on) = 0;
    virtual void request_warm_reset() = 0;
    virtual void set_demo_timer(std::int32_t vblanks) = 0;

    virtual std::int32_t fade_status() const = 0;
    virtual std::int32_t game_state() const = 0;
    virtual std::int32_t previous_game_state() const = 0;
    virtual std::int32_t sys_state() const = 0;

    virtual std::uint16_t pad_buttons() const = 0;               // first pad, active low
    virtual void pad_from_recording(const std::uint8_t* report) = 0;  // 8 bytes
    virtual void pad_neutral(std::uint16_t buttons) = 0;         // digital mode, sticks centred
    virtual void step_error(std::int32_t expected, std::int32_t actual) = 0;

    // The setup file's settings over the player's (the player keeps screen, sound and brightness;
    // music and vibration are off) and back.
    virtual void apply_demo_settings() = 0;
    virtual void restore_settings() = 0;
    virtual void mark_started() = 0;

    virtual std::uint32_t random_seed() const = 0;
    virtual void set_random_seed(std::uint32_t seed) = 0;

    virtual std::uint8_t* setup_buffer() = 0;  // 2048 bytes
    virtual std::int32_t file_size(const std::string& path) const = 0;
    virtual void queue_read(const std::string& path, void* dest) = 0;
    // The end address of the map overlay the setup's save starts in: the play buffer must not reach under it.
    virtual std::uintptr_t map_overlay_end() const = 0;

    virtual bool near_active_cell(std::int32_t x, std::int32_t z) const = 0;
};

class DemoPlayer {
public:
    static constexpr std::size_t kSetupSize = 2048;
    static constexpr std::size_t kFrameSize = 16;

    explicit DemoPlayer(DemoRules rules = DemoRules::builtin()) : rules_(std::move(rules)) {}

    const DemoRules& rules() const { return rules_; }
    void set_rules(DemoRules rules) { rules_ = std::move(rules); }

    // Moves `by` places along the rotation (0 = re-select the current one) and picks its files.
    void advance(std::int32_t by);
    void read_setup(DemoHost& host) const;
    void read_play(DemoHost& host);
    // Places the play buffer right under the setup buffer; false when it would reach the map overlay, or when no
    // demo is selected or its recording is not on the disc.
    bool place_play_buffer(DemoHost& host);

    void start(DemoHost& host);
    void stop(DemoHost& host);
    void exit(DemoHost& host);

    // The world runs twice in a demo frame; both passes must draw the same numbers.
    void back_up_seed(DemoHost& host);
    void restore_seed(DemoHost& host) const;
    void advance_seed(DemoHost& host) const;

    // Per frame, in this order: the pad, the recording step, the seed, the frame interval.
    bool feed_pad(DemoHost& host);
    bool update(DemoHost& host);
    bool apply_frame_seed(DemoHost& host) const;
    bool update_present_interval();

    // Enemies spawn in a demo only next to the loaded cells, as the recording expects.
    bool spawn_allowed(const DemoHost& host, std::int32_t x, std::int32_t z) const;

    std::int32_t index() const { return index_; }
    const std::string& setup_file() const { return setup_file_; }
    const std::string& play_file() const { return play_file_; }
    std::int32_t step() const { return step_; }
    bool has_frame() const { return frame_ != nullptr; }
    bool playing() const { return playing_; }
    std::int32_t present_interval() const { return present_interval_; }
    void* play_buffer() const { return play_; }
    void set_play_buffer(void* buffer) { play_ = static_cast<std::uint8_t*>(buffer); }

    // A fade state in which the recording does not run (fading to black or white, or held there).
    static bool fade_blocks(std::int32_t fade_status);

private:
    enum class Decision { None, Step, Exit };
    Decision decide(std::int32_t game_state, std::int32_t previous, std::int32_t sys_state) const;

    DemoRules rules_;
    std::int32_t index_ = 0;
    std::string setup_file_;
    std::string play_file_;
    std::uint8_t* play_ = nullptr;
    const std::uint8_t* frame_ = nullptr;
    std::int32_t step_ = 0;
    std::int32_t present_interval_ = 0;
    std::uint32_t loading_chunks_ = 0;
    std::int32_t previous_fade_ = 0;
    std::uint32_t seed_before_ = 0;
    std::uint32_t seed_backup_ = 0;
    std::uint16_t start_seed_ = 0;
    bool playing_ = false;
};

}  // namespace sh::gameflow
