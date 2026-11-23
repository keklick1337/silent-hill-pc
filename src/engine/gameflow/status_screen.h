#pragma once

// The controller rumble of a gameplay frame and the game state that loads the inventory screen.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "formats/common/status.h"

#include "engine/game_state/session.h"

namespace sh::gameflow {

// Sounds a map's rumble update stops (gamedata/config/map_sound_stops.json): looping sounds that would otherwise
// keep playing into the inventory screen.
class MapSoundStops {
public:
    static Result<MapSoundStops> parse(std::string_view json);

    // The sound names to stop in `map` (a map name such as "map3_s03"); empty for the others.
    const std::vector<std::string>& sounds(std::string_view map) const;

private:
    struct Entry {
        std::string map;
        std::vector<std::string> sounds;
    };
    std::vector<Entry> entries_;
};

class RumbleHost {
public:
    virtual ~RumbleHost() = default;
    virtual void rumble_reset() = 0;                                  // the strong motor off
    virtual void rumble_main(bool normal, std::int32_t amount) = 0;   // the weak motor; `normal` is false for the drill
    virtual void rumble_drill(std::int32_t amount) = 0;               // the strong motor of the drill
    virtual std::string map_name() const = 0;
    virtual void stop_sound(const std::string& name) = 0;
};

struct RumbleInput {
    std::int32_t level = 0;        // Q12; rumble starts above 256.0
    std::int32_t drill_level = 0;  // Q12
    bool rock_drill = false;       // the equipped weapon's current attack is the rock drill's tap
};

// Updates the motors from the player's rumble levels (the level above 256.0, in whole units) and stops the sounds
// the map asks for.
void rumble_update(RumbleHost& h, const RumbleInput& in, const MapSoundStops& stops);

class StatusScreenHost : public RumbleHost {
public:
    // the game state's step
    virtual int step() const { return sh::game::session().game.game_steps[0]; }
    virtual void advance_step() { ++sh::game::session().game.game_steps[0]; }
    virtual void wait_for_draw() = 0;
    virtual void one_vblank_per_frame() = 0;
    virtual void reset_fade() = 0;
    virtual RumbleInput rumble_input() const = 0;
    virtual bool streaming_audio() = 0;
    virtual void play_streaming_stop() = 0;      // the sound driver's command for it
    virtual void prepare_items() = 0;            // the inventory's per-map state and item textures
    virtual void motion_blur() = 0;
    virtual bool files_loaded() = 0;
    virtual void open_inventory() = 0;
};

// The game state that sets the inventory screen up: the first frame prepares it, then the frames wait for the files.
void load_status_screen(StatusScreenHost& h, const MapSoundStops& stops);

}  // namespace sh::gameflow
