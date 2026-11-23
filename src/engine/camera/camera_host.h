#pragma once

// What the camera reads from outside itself: the characters it
// frames, the ground, where the player is, the renderer's world-screen matrix and frame geometry,
// the frame clock. The camera entry points (camera_entry.cpp, view_entry.cpp) own the logic; the
// host only answers. The game registers its host once at start-up; until then (and in tests) a
// neutral host answers with empty values.

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "engine/camera/camera_map_rules.h"
#include "engine/camera/camera_types.h"
#include "engine/camera/view_space.h"

namespace sh::camera {

struct FrameRecord;

// Harry as the camera driver and the self-view gaze read him.
struct HarryView {
    Vec3 position;
    std::int16_t yaw = 0;
    std::int32_t move_speed = 0;
    std::int32_t heading = 0;
    std::int16_t turn_speed = 0;
    std::int32_t anim_status = 0;
};

class CameraHost {
public:
    virtual ~CameraHost() = default;

    // The renderer's world-screen matrix (the view system writes it, the renderer and the logic
    // screen test read it).
    virtual Matrix& world_screen() = 0;
    // The PC frame the draw-side box test widens its bounds for (the margin is the camera's own).
    virtual ScreenGeometry screen_geometry() = 0;
    virtual HorPlusWidths hor_plus_widths() = 0;
    // Q12 seconds of the frame, and the frame's vblank count.
    virtual std::int32_t delta_time() = 0;
    virtual std::int32_t vblanks() = 0;
    // Where camera.json is looked up: the game data directory and the mods directory.
    virtual std::string data_path() = 0;
    virtual std::string mods_path() = 0;

    virtual HarryView harry() = 0;
    virtual FrameRecord* harry_head() = 0;
    // Index of Harry's clip `name` (his character.json), `fallback` when he has none of that name.
    virtual int harry_clip(std::string_view name, int fallback) = 0;
    // One entry per NPC slot.
    virtual void npcs(std::vector<NpcView>& out) = 0;
    virtual void set_npc_dead_time(std::size_t slot, std::int32_t dead_time) = 0;

    // Walkable ground at (x, z) (Q12).
    virtual GroundSample ground_at(std::int32_t x, std::int32_t z) = 0;
    virtual MapPlace map_place() = 0;
    // A fixed-angle shot is on screen this frame (the renderer's projection-centre correction).
    virtual void set_fixed_angle_shot(bool on) = 0;
};

void set_camera_host(CameraHost* host);
CameraHost& camera_host();

struct CameraConfig;
// camera.json (base or mod), read once from the host's paths.
const CameraConfig& camera_config();
// The same for the camera API: its first use also gives the script-tunable motion defaults of the
// game camera camera.json's numbers.
const CameraConfig& camera_api_config();

}  // namespace sh::camera
