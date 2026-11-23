#include "engine/gameflow/area_load.h"

namespace sh::gameflow {

void AreaLoad::frame(AreaLoadHost& host) const {
    if (!host.can_load()) {
        return;
    }

    if (host.step() == 0) {
        host.set_step(1);
        host.set_waited(0);
        // Retail plays the door sound under the fade, not after it.
        host.door_opened();
        // A scene that already handed over on black is not faded again (the world would pop back).
        if (!host.demo() && !host.fade_done()) {
            host.start_out_fade();
        }
    }

    // The budget is real time, not frames, so the fade lasts as long at any frame rate; it only
    // keeps a fade that never finishes from hanging the door.
    if (!host.fade_done() && !host.demo() && host.waited() < rules_.fade_budget) {
        const std::int32_t raw = host.raw_delta();
        host.set_waited(host.waited() + (raw > 0 ? raw : rules_.min_step));
        host.fade_progress();
        // The world holds still under the fade, as under retail's decaying frozen frame.
        host.set_delta(0);
        return;
    }

    host.begin_load();
    const ArrivalPoint door = host.door_point();
    host.set_arrival(arrival_point(host.arrival_target(), door.x, door.z, host.player_x(), host.player_z()));

    const AreaTarget t = host.target();
    if (t.load_map) {
        host.load_map(t.map);
    } else {
        host.change_room(t.map, t.point);
    }
    host.set_flag(t.done_flag);
    host.set_camera_rail(t.camera_rail);
    host.start_load_screen();
}

}  // namespace sh::gameflow
