// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The draw-packet arenas of the main loop: two big zeroed byte buffers (one
// per display buffer)
// with a canary run after each, and a monitor that watches how much of the arena a frame used, whether
// a canary was overwritten (an overrun garbles primitives already linked into the ordering table) and
// the high-water mark. The monitor only decides; the host logs the events.
//
// Numbers are gamedata/config/packet_arena.json; a mod replaces the file whole.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "formats/common/status.h"

namespace sh::gameflow {

struct PacketArenaParams {
    std::uint32_t size_bytes = 16u * 1024u * 1024u;  // usable bytes per arena
    std::uint32_t canary_bytes = 64;                 // canary run after the arena
    std::uint32_t canary_value = 0xDE;
    std::uint32_t stats_window_frames = 120;         // average-usage report period (in-game frames)
    std::uint32_t headroom_log_percent = 50;         // one line when the peak first passes this share

    static PacketArenaParams builtin();
    static Result<PacketArenaParams> parse(std::string_view json);
};

class PacketArena {
public:
    explicit PacketArena(PacketArenaParams params) : params_(params) {}
    PacketArena(const PacketArena&) = delete;
    PacketArena& operator=(const PacketArena&) = delete;
    ~PacketArena();

    // Allocates both arenas (zeroed) and lays down the canaries; later calls do nothing.
    void ensure_allocated();
    bool allocated() const { return buf_[0] != nullptr; }
    std::uint8_t* base(int which) const { return buf_[which & 1]; }
    std::uint8_t* end(int which) const { return end_[which & 1]; }
    bool canary_intact(int which) const;
    const PacketArenaParams& params() const { return params_; }

private:
    PacketArenaParams params_;
    std::uint8_t* buf_[2] = {nullptr, nullptr};
    std::uint8_t* end_[2] = {nullptr, nullptr};
};

struct PacketArenaReport {
    bool cull_stat = false;          // a window closed: the averages below are valid
    long cull_avg = 0;               // packet bytes per frame over the window
    long cull_frames = 0;
    bool overrun_new[2] = {false, false};  // this buffer's canary was found damaged for the first time
    bool high_water_new = false;     // the peak first passed the headroom share
    long used = 0;                   // this frame's bytes
    long peak = 0;
};

class PacketArenaMonitor {
public:
    explicit PacketArenaMonitor(PacketArenaParams params) : params_(params) {}

    // One in-game frame: `used` bytes of the active arena, and the canary state of both.
    PacketArenaReport observe(long used, const bool canary_ok[2]);

private:
    PacketArenaParams params_;
    long peak_ = 0;
    bool high_water_logged_ = false;
    bool overrun_reported_[2] = {false, false};
    long cull_sum_ = 0;
    long cull_frames_ = 0;
};

}  // namespace sh::gameflow
