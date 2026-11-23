// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// A row of the running map's event list (sh::maps::MapRuntime::events()) with readable
// accessors of its packed fields; the same 12 bytes as sh::maps::MapEventRecord.

#pragma once

#include <cstdint>

namespace sh::game_state {

// One row of a map's event table (12 bytes): when the event triggers, where, and what it does.
struct MapEventRecord {
    std::int16_t required_flag;  // event flag that must be set (0: none)
    std::int16_t disabled_flag;  // event flag that disables the row (0: none)
    std::uint8_t trigger_bits;   // low nibble: trigger type (signed, -1 ends the table), high: activation
    std::uint8_t point;          // index into the map's points
    std::uint8_t required_item;  // the item an item-activated row wants
    std::uint8_t reserved;
    // sys state (5 bits), parameter (8), event state (6), sound pair (5), camera rail (1), map (6), spare (1).
    std::uint32_t packed;

    constexpr int trigger_type() const { return static_cast<std::int8_t>(static_cast<std::uint8_t>(trigger_bits << 4)) >> 4; }
    constexpr void set_trigger_type(int type) {
        trigger_bits = static_cast<std::uint8_t>((trigger_bits & 0xF0) | (static_cast<unsigned>(type) & 0x0F));
    }
    constexpr int activation_type() const { return trigger_bits >> 4; }
    constexpr void set_activation_type(int type) {
        trigger_bits = static_cast<std::uint8_t>((trigger_bits & 0x0F) | ((static_cast<unsigned>(type) & 0x0F) << 4));
    }
    constexpr int sys_state() const { return static_cast<int>(packed & 0x1F); }
    constexpr void set_sys_state(int state) { set_bits(0, 5, static_cast<std::uint32_t>(state)); }
    constexpr int param() const { return static_cast<int>((packed >> 5) & 0xFF); }
    constexpr void set_param(std::uint32_t param) { set_bits(5, 8, param); }
    constexpr int map() const { return static_cast<int>((packed >> 25) & 0x3F); }
    constexpr void set_map(int map) { set_bits(25, 6, static_cast<std::uint32_t>(map)); }

private:
    constexpr void set_bits(int shift, int width, std::uint32_t value) {
        const std::uint32_t mask = ((1u << width) - 1u) << shift;
        packed = (packed & ~mask) | ((value << shift) & mask);
    }
};
static_assert(sizeof(MapEventRecord) == 12);

}  // namespace sh::game_state
