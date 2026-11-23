// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Named binds of the game's state machine, the session store, the camera and Harry's script values:
//   session.get(key[, default]) / session.set(key, value): an integer kept for the life of the game process, by
//       name. Not saved, survives map loads, as a few map variables of the original do (they sit outside the data a
//       map load resets). A key never set reads as the default (0 without one).
//   game.bgm_idx(): the music track the game is on. game.set_npc_flags_id(v): the 1-based NPC id the NPC flags
//       refer to.
//   save.map_room_idx(): the room of the loaded map the player is in.
//   camera.is_chase(): 1 while the camera follows the player. camera.exec(): one camera step now (a scene that
//       places the camera and hands it back in the same tick). camera.look_at_height(): the height (Q12) the
//       camera looks at. camera.view_button_active() / view_button_was_active(): the look-view request this and
//       last frame ("view control: reverse" flips the held button). camera.self_view_enabled(): the "view mode"
//       option is Self View. camera.angle_speed_to_target(speed, angle, target, accel, max_speed, decel): the
//       camera's new turning speed towards a target angle.
//   map.harry_fall_direction(): the fall direction Harry's script keeps. player.damage_feet_front(): Harry's
//       reaction to the floor giving way. map.spawn_variants_apply(): the map's spawn variant chosen again.

#include "engine/scripting/binds/binds.h"

#include <cstdio>
#include <map>
#include <string>

#include "engine/camera/camera_types.h"
#include "engine/camera/game_camera.h"
#include "engine/game_state/game_state.h"
#include "engine/game_state/session.h"
#include "engine/scripting/binds/game_links.h"
#include "engine/script/sh_script.h"

namespace sh::script::binds {

namespace {

using named::Call;

constexpr int kHarry = 1;

std::map<std::string, long long, std::less<>>& session_values() {
    static std::map<std::string, long long, std::less<>> values;
    return values;
}

int session_get(Call& c) {
    if (!c.is_text(0)) {
        return c.fail("session.get/set take the key as a string");
    }
    const auto& values = session_values();
    const auto it = values.find(c.text(0));
    return c.reply({it != values.end() ? it->second : c.integer(1, 0)});
}

int session_set(Call& c) {
    if (!c.is_text(0)) {
        return c.fail("session.get/set take the key as a string");
    }
    if (!c.is_int(1)) {
        return c.fail("session.set takes the value as an integer");
    }
    session_values()[c.text(0)] = c.integer(1);
    return 0;
}

int no_args(Call& c) { return c.argc() == 0 ? 0 : c.fail("%s: no arguments expected", c.name()); }

int bgm_idx(Call& c) { return c.reply({sh::game::session().game.music_track}); }

int set_npc_flags_id(Call& c) {
    sh::game::session().system.scene.npc_flags_owner = static_cast<std::int32_t>(c.integer(0));
    return 0;
}

int map_room_idx(Call& c) { return no_args(c) < 0 ? -1 : c.reply({sh::game_state::save_room()}); }

int camera_is_chase(Call& c) { return c.reply({vcRetCamMvSmoothF() == 0 ? 1 : 0}); }

int camera_exec(Call& c) {
    if (c.argc() != 0) {
        return c.fail("%s takes no arguments", c.name());
    }
    vcExecCamera();
    return 0;
}

int camera_look_at_height(Call& c) { return c.reply({Vc_LookAtPositionYGet()}); }

// The reverse option flips what the held button means.
int view_button(Call& c, std::uint32_t flag) {
    if (c.argc() != 0) {
        return c.fail("%s takes no arguments", c.name());
    }
    const bool held = (sh::camera::game_camera().state().flags & flag) == flag;
    const bool reverse = sh::game::session().game.settings.view_reverse != 0;
    return c.reply({held != reverse ? 1 : 0});
}

int view_button_active(Call& c) { return view_button(c, sh::camera::camera_flags::view_button); }
int view_button_was_active(Call& c) { return view_button(c, sh::camera::camera_flags::view_button_previous); }

int self_view_enabled(Call& c) {
    if (c.argc() != 0) {
        return c.fail("%s takes no arguments", c.name());
    }
    return c.reply({sh::game::session().game.settings.self_view != 0 ? 1 : 0});
}

int angle_speed_to_target(Call& c) {
    if (c.argc() != 6 || !c.ints_at(0, 6)) {
        return c.fail("%s takes speed, angle, target, accel, max_speed, decel", c.name());
    }
    return c.reply({vwRetNewAngSpdToTargetAng(
        static_cast<std::int32_t>(c.integer(0)), static_cast<std::int16_t>(c.integer(1)),
        static_cast<std::int16_t>(c.integer(2)), static_cast<std::int32_t>(c.integer(3)),
        static_cast<std::int32_t>(c.integer(4)), static_cast<std::int32_t>(c.integer(5)))});
}

int harry_fall_direction(Call& c) {
    long long value = 0;
    if (ShScript_CharaBehaviorHas(kHarry, "Harry") == 0 ||
        ShScript_CharaBehaviorCall(kHarry, "fall_direction", 0, 0, &value) == 0) {
        static bool reported = false;
        if (!reported) {
            reported = true;
            std::fprintf(stderr, "[harry-overlay] chara/Harry/scripted.lua: fall_direction did not run\n");
        }
        value = 0;
    }
    return c.reply({static_cast<std::int32_t>(value)});
}

int damage_feet_front(Call& c) {
    if (c.argc() != 0) {
        return c.fail("%s: no arguments expected", c.name());
    }
    Player_DamageFeetFront();
    return 0;
}

int spawn_variants_apply(Call& c) {
    if (c.argc() != 0) {
        return c.fail("%s takes no arguments", c.name());
    }
    MapData_SpawnVariantsApply();
    return 0;
}

const named::Bind kBinds[] = {
    {"session.get", session_get},
    {"session.set", session_set},
    {"game.bgm_idx", bgm_idx},
    {"game.set_npc_flags_id", set_npc_flags_id},
    {"save.map_room_idx", map_room_idx},
    {"camera.is_chase", camera_is_chase},
    {"camera.exec", camera_exec},
    {"camera.look_at_height", camera_look_at_height},
    {"camera.view_button_active", view_button_active},
    {"camera.view_button_was_active", view_button_was_active},
    {"camera.self_view_enabled", self_view_enabled},
    {"camera.angle_speed_to_target", angle_speed_to_target},
    {"map.harry_fall_direction", harry_fall_direction},
    {"player.damage_feet_front", damage_feet_front},
    {"map.spawn_variants_apply", spawn_variants_apply},
};

}  // namespace

std::span<const named::Bind> game_binds() { return kBinds; }

}  // namespace sh::script::binds
