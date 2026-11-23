#pragma once

// Room and map loads: the door's open sound and an out-fade
// that runs over presented frames (bounded by a real-time budget so a door can never hang), then
// the load itself: the arrival point (arrival.h), the map or room change, the door's flag, the
// camera rail and the loading screen.

#include <cstdint>

#include "engine/gameflow/arrival.h"
#include "engine/gameflow/event_triggers.h"

#include "engine/game_state/session.h"

namespace sh::gameflow {

struct AreaLoadRules {
    std::int32_t fade_budget = 0;  // Q12 seconds the out-fade may take at most
    std::int32_t min_step = 0;     // Q12 seconds counted for a frame whose unscaled delta is 0
};

// What the door event loads (read before the load replaces the map's event list).
struct AreaTarget {
    bool load_map = false;          // a new map; otherwise a room of this map
    std::int32_t map = 0;           // map number (a room load passes it to the music change)
    int point = 0;                  // the arrival point of the map
    std::int16_t done_flag = 0;
    bool camera_rail = false;
};

class AreaLoadHost {
public:
    virtual ~AreaLoadHost() = default;

    virtual bool can_load() const = 0;   // the door event and the map points exist
    virtual int step() const { return sh::game::session().system.flow.sys_steps[0]; }
    virtual void set_step(int step) { sh::game::session().system.flow.sys_steps[0] = step; }
    virtual std::int32_t waited() const { return sh::game::session().system.flow.sys_steps[1]; }
    virtual void set_waited(std::int32_t t) { sh::game::session().system.flow.sys_steps[1] = t; }

    virtual void door_opened() = 0;      // remembers the door's sound set and plays its open sound
    // attract demos keep their recorded frame timing: no fade
    virtual bool demo() const { return sh::game::session().sys_flag(sh::game::sys_flag::kDemo); }
    virtual bool fade_done() const = 0;
    virtual void start_out_fade() = 0;
    virtual void fade_progress() = 0;    // a frame of the out-fade passed
    virtual std::int32_t raw_delta() const = 0;
    virtual void set_delta(std::int32_t d) = 0;

    // The load.
    virtual void begin_load() = 0;              // loading screen, door sound set, transition kind
    virtual ArrivalPoint arrival_target() const = 0;
    virtual ArrivalPoint door_point() const = 0;
    virtual std::int32_t player_x() const = 0;
    virtual std::int32_t player_z() const = 0;
    virtual void set_arrival(const ArrivalPoint& p) = 0;
    virtual AreaTarget target() const = 0;
    virtual void load_map(std::int32_t map) = 0;
    virtual void change_room(std::int32_t map, int point) = 0;
    virtual void set_flag(std::int16_t flag) = 0;
    virtual void set_camera_rail(bool on) = 0;
    virtual void start_load_screen() = 0;
};

class AreaLoad {
public:
    void set_rules(const AreaLoadRules& r) { rules_ = r; }
    void frame(AreaLoadHost& host) const;

private:
    AreaLoadRules rules_{};
};

}  // namespace sh::gameflow
