#pragma once

// Who drives the view: the predicates the alternate cameras, the frame
// pacer and the flashlight ask ("does a script own the scene / the shot"), and the vertical anchor the world
// is projected with. Both work on a snapshot the host fills, so they are pure apart from the held anchor.
// The state lists and the base offset are gamedata/config/scene_owner.json.

#include <cstdint>
#include <string_view>
#include <utility>
#include <vector>

#include "formats/common/status.h"

namespace sh::gameflow {

struct SceneOwnerParams {
    std::int32_t anchor_base = 8;           // console vertical anchor the knobs are deltas on top of
    std::vector<std::int32_t> player_sys_states;   // states the player opened (never a script)
    std::vector<std::int32_t> player_game_states;  // screens the player opened

    static SceneOwnerParams builtin();
    static Result<SceneOwnerParams> parse(std::string_view json);
};

struct SceneSnapshot {
    bool cutscene_flag = false;      // the system flag of a running cutscene
    bool border_active = false;      // letterbox bars are up or moving
    std::int32_t sys_state = 0;
    std::int32_t game_state = 0;
    bool fade_in_started = false;    // the screen fade is in its fade-in part or later
    bool camera_user_flags = false;  // a script raised the camera or the watch target
    bool control_disabled = false;
};

struct AnchorSnapshot {
    std::int32_t game_state = 0;
    std::int32_t sys_state = 0;
    bool pickup_active = false;
    bool cutscene_active = false;    // the cutscene framing mode
    bool border_active = false;
    bool third_person = false;
    std::int32_t cutscene_shift = 0;  // already truncated to whole rows by the host
    std::int32_t world_shift = 0;
};

class SceneOwner {
public:
    explicit SceneOwner(SceneOwnerParams params) : params_(std::move(params)) {}

    bool owns_scene(const SceneSnapshot& s) const;
    bool owns_shot(const SceneSnapshot& s) const;
    std::int32_t world_anchor(const AnchorSnapshot& s);

private:
    static bool contains(const std::vector<std::int32_t>& v, std::int32_t x);

    SceneOwnerParams params_;
    std::int32_t held_ = 0;
    bool held_set_ = false;  // the held anchor starts at the base
};

}  // namespace sh::gameflow
