#pragma once

// Numbers of the camera and view system that are data, not code: gamedata/config/camera.json
// (mods patch it with the data-merge rules). Every number has the shipped value as its default, so a missing
// file or key changes nothing; the enemy lists have no built-in copy.
//
// The JSON gives lengths in metres and angles in degrees; they become the engine's Q12 integers by
// the game's own rule (multiply in single precision, truncate toward zero), so "1.7" is the same
// 6963 the shipped data held.

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "engine/camera/camera_map_rules.h"
#include "engine/camera/camera_types.h"
#include "formats/common/status.h"

namespace sh::camera {

// How the camera reads its subject (the followed character) from the character's position.
struct SubjectRules {
    std::int32_t top_offset_y = -6963;        // top of the subject above its feet (-1.7 m)
    std::int32_t bottom_offset_y = -2048;     // bottom, scaled by the elevation rate (-0.5 m)
    std::int32_t connected_ground_y = -8192;  // ground while the subject rides a connection (-2.0 m)
    std::int32_t connected_head_y = -7782;    // head above the feet in that case (-1.9 m)
    std::int32_t ground_missing_y = 32768;    // the world's "no ground here" height (8.0 m), read as 0
    std::int16_t eye_half_angle = 1365;       // sight half angle the camera assumes (120 degrees)
    std::int32_t watch_radius = 45056;        // XZ reach of the subject's look (11 m)
    std::int16_t head_bone_offset_y = -25;    // head point in the head bone's frame (Q8, -0.1 m)
    std::int32_t head_offset_y = -1228;       // then raised by (-0.3 m)
};

// The developer camera of the camera driver (fly and orbit modes behind the debug mode switch).
struct DebugCameraRules {
    std::int32_t start_offset_x = 28672;       // first placement beside the subject (7.0 m)
    std::int32_t start_y = -9011;              // ... at this absolute height (-2.2 m)
    std::int32_t start_radius = 12288;         // orbit radius when a map begins (3.0 m)
    std::int32_t orbit_step = 409;             // radius / height step per frame (0.1 m)
    std::int32_t orbit_min_radius = 4096;      // (1.0 m)
    std::int32_t turn_per_vblank = 11;         // yaw / pitch step per vertical blank (Q12 angle)
    std::int32_t reference_ahead = 2048;       // orbit centre ahead of the subject (0.5 m)
    std::int32_t reference_y = -4096;          // ... and above its feet (-1.0 m)
    std::int32_t fly_step = 25;                // fly step per frame (Q8, 0.1 m)
    std::int32_t fly_boost = 4;                // multiplier while the boost button is held
    std::int32_t fly_reference_distance = 1280;  // orbit centre ahead of the flying camera (Q8 5.0)
    // The head-orbit mode: reference point in the head bone's frame and the orbit around it.
    Vec3 head_reference{0, -614, 4096};        // (0, -0.15, 1.0) m
    std::int16_t head_orbit_yaw = 1877;        // 165 degrees
    std::int16_t head_orbit_roll = 0;
    std::int32_t head_orbit_height = -819;     // -0.2 m
    std::int32_t head_orbit_radius = 4096;     // 1.0 m
};

struct CameraConfig {
    // Loose margin (screen units) of the draw-side box test (box_in_frustum). Generous on purpose:
    // every tight bound of that test eventually showed up on screen after some view change.
    std::int32_t draw_cull_margin = 64;

    // Look-at motion of the automatic camera and of a script's look-at given no parameters.
    WatchMoveParams default_watch{4915, 9011, 901, 2867};  // 1.2, 2.2, 0.22, 0.7
    // Camera motion of a script's camera target given no parameters (also the automatic camera's).
    CamMoveParams user_camera{40960, 12288, 24576, 8192};  // 10, 3, 6, 2
    // How far above the character the look-at may rise (metres, Q12).
    std::int32_t watch_max_y = 122880;  // 30 m

    // Where the camera starts when a map begins: behind the character by `distance` and above its
    // feet by `height` (Q12).
    std::int32_t first_camera_distance = 6144;  // 1.5 m
    std::int32_t first_camera_height = 6963;    // 1.7 m

    SubjectRules subject{};
    DebugCameraRules debug{};
    // "map_rules": each list given replaces the shipped one whole.
    MapCameraRules map_rules{};

    // "enemies": the characters (by name) the camera tracks as enemies, and those of them that are always an
    // active camera target. No built-in list: a camera.json without the section tracks nobody and says so in
    // `warnings`.
    std::vector<std::string> enemy_characters;
    std::vector<std::string> always_active_characters;

    // Non-fatal problems of the read (a missing section, a mod patch without "gameplay"), for the log.
    std::vector<std::string> warnings;
};

Result<CameraConfig> parse_camera_config(std::string_view json_text);

// The game's camera.json (config/camera.json of gamedata, or of the highest-priority mod that has
// one), read on the first call and kept for every later one, whose paths are then ignored. A
// missing file keeps the built-in numbers, and so does a broken one; its error goes to `error` on
// that first call only.
const CameraConfig& shared_camera_config(const std::filesystem::path& gamedata_dir,
                                         const std::filesystem::path& mods_dir, std::string* error = nullptr);

}  // namespace sh::camera
