#pragma once

// The running map event and what goes with it: the event the trigger scan
// picked and its state and parameter, the item trigger slots, the arrival point a door chose, the event background's
// second picture, the path waypoints of the scripts and the latches of the frame. Engine-owned storage; the game
// code reads it through views under its C names.

#include <array>
#include <cstdint>

namespace sh::gameflow {

// A map point record as the map data stores it (12 bytes): position and a packed word (paper map, weather on
// enter, loading screen, two trigger parameters).
struct MapPointRecord {
    std::int32_t x = 0;
    std::uint32_t packed = 0;
    std::int32_t z = 0;

    int paper_map() const { return static_cast<int>(packed & 0x1F); }
    int weather_on_enter() const { return static_cast<int>((packed >> 5) & 0xF); }
    int loading_screen() const { return static_cast<int>((packed >> 9) & 0x7); }
    std::uint8_t param0() const { return static_cast<std::uint8_t>(packed >> 16); }
    std::uint8_t param1() const { return static_cast<std::uint8_t>(packed >> 24); }
};
static_assert(sizeof(MapPointRecord) == 12);

// Where a picture sits in texture memory (8 bytes, the file queue's image record).
struct ImagePlace {
    std::uint8_t page[2] = {0, 0};
    std::uint8_t u = 0;
    std::uint8_t v = 0;
    std::int16_t clut_x = 0;
    std::int16_t clut_y = 0;
};
static_assert(sizeof(ImagePlace) == 8);

struct MapEventState {
    static constexpr int kItemSlots = 5;
    static constexpr int kWaypoints = 8;

    // The running event's record in the map's event list (opaque here: the map data owns it), null for none.
    void* event = nullptr;
    std::int32_t sys_state = 0;  // the in-game state the event asks for (gameplay: none)
    std::uint32_t param = 0;
    std::int32_t used_item = 0;  // the item the event was started with

    // Events that wait for an item from the inventory, and their items.
    std::array<void*, kItemSlots> item_events{};
    std::array<std::int32_t, kItemSlots> item_ids{};

    MapPointRecord arrival;          // where the last door puts the player
    std::uint8_t load_flag_ticks = 0;  // frames of the loading flag

    // The fade has finished and the map chunks are loaded (latched once per frame by the game state).
    std::int32_t screen_ready = 0;
    // Radio pitch from the distance of the nearest enemy, 0..3.
    std::int32_t radio_pitch = 0;

    // The second picture an event background command shows over the first.
    ImagePlace secondary_background{{1, 21}, 0, 0, 224, 13};

    // Path waypoints of the player (slot 0) and of one character (slot 1), Q12 x, y, z; one heading per slot (the
    // last one set wins, sized for 8 as the original).
    std::int32_t waypoints[2][kWaypoints][3] = {};
    std::int16_t waypoint_headings[kWaypoints] = {};

    // Map message voice handshake of the PC subtitles: the out-of-range voice command was dropped for this page; the
    // page shown is a re-display of the line that already finished.
    std::int32_t voice_dropped = 0;
    std::int32_t redisplay_setup = 0;

    // An event a script queued while none was running (load_map or fmv from the game's own script, a mod's
    // main.lua): the trigger scan rewrites the event of every frame, so the next scan that finds nothing of its own
    // starts this one instead (map_events_host.cpp Event_Update). Null for none.
    void* queued_event = nullptr;
    std::int32_t queued_sys_state = 0;
    std::uint32_t queued_param = 0;
};

namespace detail {
// Zero-initialised static storage: the records' views bind to it before any code runs.
inline MapEventState g_map_event_state;
}  // namespace detail

inline MapEventState& map_event_state() { return detail::g_map_event_state; }

}  // namespace sh::gameflow
