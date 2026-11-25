// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Harry's behaviour of the tick: the Lua controller, legs, upper body and reaction states,
// and the PC control layer that replaces the legs under the alternate cameras (third person, over the shoulder,
// first person) and 2D control (movement relative to the screen).

#include "engine/player/player.h"

#include "engine/player/harry_states.h"
#include "engine/player/player_math.h"
#include "engine/world/fixed_math.h"
#include "engine/world/fixed_trig.h"

#include <algorithm>
#include <cstdlib>
#include <initializer_list>
#include <string_view>

namespace sh::game {

namespace {

using V = PlayerVar;
namespace pm = player_math;
namespace st = harry::state;
namespace lower = harry::lower;

// Movement key bits of PlayerRecord::movement_keys.
constexpr unsigned kKeyForward = 1u, kKeyLeft = 2u, kKeyBack = 4u, kKeyRight = 8u;
// Pad word bits of the directions: digital pad and the left stick.
constexpr std::int32_t kPadUp = (1 << 4) | (1 << 24), kPadRight = (1 << 5) | (1 << 25), kPadDown = (1 << 6) | (1 << 26),
                       kPadLeft = (1 << 7) | (1 << 27);
constexpr std::int32_t kStickUp = 1 << 24, kStickDown = 1 << 26;

// Run speeds of the control layer (Q12 m/s): the alternate cameras' run, the classic camera's by speed zone (the
// authored speeds of the legs' run, behavior.lua run_top), the walk.
constexpr std::int32_t kOrbitRunSpeed = 18432;  // 4.5
constexpr std::int32_t kSlowRunSpeed = 14336, kNormalRunSpeed = 16384, kFastRunSpeed = 20480;  // 3.5, 4, 5
constexpr std::int32_t kWalkSpeed = 6144;       // 1.5
constexpr std::int32_t kSidestepRate = 98;      // 0.024 m per keyframe of the side step
constexpr std::int32_t kJumpBackRate = 901;     // 0.22 m per keyframe of the jump back

constexpr std::int32_t kFpsIdleLook = 512;  // 45 degrees

constexpr std::int32_t signed_angle(std::int32_t a) {
    const std::int32_t n = a & 0xFFF;
    return n > 2048 ? n - 4096 : n;
}

}  // namespace

void Player::credit_distance(std::int32_t dx, std::int32_t dz, bool running) {
    const std::int32_t step = sh::world::planar_length(dx, dz);
    const V total = running ? V::RunDistance : V::WalkDistance;
    set(total, pm::add_distance(static_cast<std::uint32_t>(get(total)), step));
}

void Player::behave() {
    r_.timer_update();
    r_.rest_before();
    if (!r_.map_rules().player_updates.empty()) {
        r_.map_player_tick();
    }
    // Hold-to-aim (a GAMEPLAY CHANGE option) is pinned before the controller reads the weapon control.
    if (get(V::ForceHoldAim) != 0) {
        set(V::OptionWeaponControl, 1);
    }
    if (get(V::DeltaTime) != 0) {
        r_.run_controller();
    }

    const std::int64_t state = get(V::State);
    if (state == st::kIdle && r_.rest_idle()) {
        r_.rest_after();
        return;
    }
    if (state != st::kIdle && state != st::kNone && state != st::kCombat) {
        if (r_.rest_state()) {
            return;
        }
        r_.rest_after();
        return;
    }

    // The legs' own machine unless an alternate camera or 2D control (outside aiming) takes the legs over. A
    // demo replays input recorded against that machine, so it always runs it.
    const bool alt_camera = get(V::AltCamera) != 0;
    const bool control_2d =
        get(V::Control2d) != 0 && get(V::FpsCamera) == 0 && get(V::Aiming) == 0;
    if (get(V::Demo) != 0 || (get(V::MovementOriginal) != 0 && !alt_camera && !control_2d)) {
        // The quick turn button: the legs' animated half turn, from walking, running, standing or aimed
        // movement, never while the legs hand over between walk and run (the hand-over would overwrite it).
        if (get(V::QuickTurnRequest) != 0) {
            set(V::QuickTurnRequest, 0);
            const std::int64_t legs = get(V::LowerBody);
            if (get(V::WalkRunBlend) == 0 &&
                (legs <= lower::kRunLeft || (legs >= lower::kAim && legs <= lower::kAimRunLeft))) {
                set(V::LowerBody, (legs < lower::kAim ? 0 : lower::kAim) + lower::kQuickTurnRight);
                set(V::BodyStateStep, 0);
                set(V::BodyControlState, 0);
            }
        }
        r_.run_legs();
        if (static_cast<std::uint64_t>(get(V::State)) < static_cast<std::uint64_t>(st::kIdle)) {
            r_.run_upper_body();
        }
    } else {
        alt_control();
    }
    r_.rest_after();
}

void Player::alt_control() {
    const std::int32_t dt = static_cast<std::int32_t>(get(V::DeltaTime));
    const auto turn_speed = static_cast<std::int16_t>(pm::per_tick_30(dt, 45));  // 4 degrees per 30 fps tick
    const unsigned keys = r_.movement_keys();
    const auto pad = static_cast<std::int32_t>(get(V::PadHeld));
    const bool alt_camera = get(V::AltCamera) != 0;

    const bool control_2d = get(V::Control2d) != 0 && get(V::FpsCamera) == 0 && get(V::Aiming) == 0 &&
                            get(V::Demo) == 0;
    if (control_2d) {
        alt_steer_2d(keys, pad);
    } else if (alt_camera) {
        alt_steer_orbit(keys, pad);
    } else {
        // The classic camera on this layer: a clean snapshot of forward and back from the pad (bit 1 of forward is
        // the controller's aged history and stays), straight heading.
        set(V::MoveForward, (get(V::MoveForward) & 2) | ((pad & kStickUp) != 0 ? 1 : 0));
        set(V::MoveBackward, (pad & kStickDown) != 0 ? 1 : 0);
        set(V::HeadingOffset, 0);
        set(V::LegsHeading, 0);
    }

    if (!alt_camera) {
        std::int32_t yaw = static_cast<std::int16_t>(get(V::RotationY));
        if (get(V::TurnLeft) != 0) {
            yaw = static_cast<std::int16_t>(yaw - turn_speed);
        }
        if (get(V::TurnRight) != 0) {
            yaw = static_cast<std::int16_t>(yaw + turn_speed);
        }
        set(V::RotationY, yaw);
    }
    set(V::RotationY, pm::wrap_angle(static_cast<std::int32_t>(get(V::RotationY)) + 4096));

    const bool jump_back = alt_jump_back();
    alt_quick_turn(jump_back);
    if (!jump_back) {
        alt_legs_clip();
    }
    alt_aim_and_fire();
    if (!jump_back) {
        alt_lower_state();
    }
    if (alt_camera) {
        alt_katana_lunge();
    }

    // The footsteps of the legs' behaviour write the step speed; this layer owns it.
    const std::int64_t speed = get(V::StepSpeed);
    r_.run_footsteps();
    set(V::StepSpeed, speed);

    if (static_cast<std::uint64_t>(get(V::State)) < static_cast<std::uint64_t>(st::kIdle)) {
        r_.run_upper_body();
    }
}

void Player::alt_steer_2d(unsigned keys, std::int32_t pad) {
    const std::int32_t dt = static_cast<std::int32_t>(get(V::DeltaTime));
    const bool fwd = (keys & kKeyForward) != 0 || (pad & kPadUp) != 0;
    const bool back = (keys & kKeyBack) != 0 || (pad & kPadDown) != 0;
    const bool left = (keys & kKeyLeft) != 0 || (pad & kPadLeft) != 0;
    const bool right = (keys & kKeyRight) != 0 || (pad & kPadRight) != 0;
    const int in_x = (right ? 1 : 0) - (left ? 1 : 0);
    const int in_z = (fwd ? 1 : 0) - (back ? 1 : 0);
    bool any_input = in_x != 0 || in_z != 0;
    // Past a small dead zone the stick steers by its absolute angle; keys and the digital pad stay 8-way.
    const std::int32_t ax = static_cast<std::int32_t>(get(V::AnalogLeftX)) - 128;
    const std::int32_t ay = static_cast<std::int32_t>(get(V::AnalogLeftY)) - 128;
    const bool analog = ax * ax + ay * ay >= 24 * 24;
    const std::int32_t stick_x = analog ? ax : in_x;
    const std::int32_t stick_z = analog ? -ay : in_z;
    if (analog) {
        any_input = true;
    }

    std::int32_t move_yaw = 0;
    bool have_move = false;
    std::int32_t cam_yaw = static_cast<std::int16_t>(get(V::OrbitYaw));
    bool cut = false;  // the fixed camera changed shot this tick

    // After a gap (aiming, a scene, a menu) or a room or map change all camera history is stale.
    {
        const std::uint32_t now = r_.time_ms();
        const int map = static_cast<int>(static_cast<std::int8_t>(get(V::MapIndex)));
        const int room = static_cast<int>(static_cast<std::int8_t>(get(V::RoomIndex)));
        if (now - alt_.last_run_ms > 300u || map != alt_.map || room != alt_.room) {
            alt_.locked = false;
            alt_.have_cam = false;
            alt_.have_travel = false;
        }
        alt_.last_run_ms = now;
        alt_.map = map;
        alt_.room = room;
    }

    if (get(V::AltCamera) == 0) {
        // The fixed camera: "into the screen" is its own look yaw. A shot change shows as the camera jumping
        // (over 3 m) or its yaw swinging faster than any pan (25 degrees per 30 fps tick).
        std::int32_t pos[3] = {0, 0, 0};
        std::int32_t yaw = 0;
        r_.view_camera(pos, &yaw);
        cam_yaw = static_cast<std::int16_t>(pm::wrap_angle(yaw + 4096));
        if (alt_.have_cam) {
            const std::int64_t dx = pos[0] - alt_.cam_x;
            const std::int64_t dz = pos[2] - alt_.cam_z;
            const std::int32_t swing = signed_angle(cam_yaw - alt_.cam_yaw);
            if (dx * dx + dz * dz > std::int64_t{12288} * 12288 || std::abs(swing) > pm::per_tick_30(dt, 284)) {
                cut = true;
            }
        }
        alt_.cam_x = pos[0];
        alt_.cam_y = pos[1];
        alt_.cam_z = pos[2];
        alt_.cam_yaw = cam_yaw;
        alt_.have_cam = true;
    } else {
        // The orbit camera never cuts; drop the fixed camera's history.
        alt_.have_cam = false;
        alt_.locked = false;
    }

    // A pan tracks live; a cut with a direction held keeps the world line Harry is on (+-45 degrees of steering
    // around the stick angle at the cut; leaving that window or letting go returns to live tracking).
    if (any_input) {
        const std::int32_t stick_yaw = sh::world::fixed_atan2(stick_x, stick_z);
        std::int32_t target;
        if (cut && alt_.have_travel && !alt_.locked) {
            alt_.locked = true;
            alt_.lock_heading = alt_.travel_yaw;
            alt_.lock_stick = stick_yaw;
        }
        if (alt_.locked) {
            const std::int32_t delta = signed_angle(stick_yaw - alt_.lock_stick);
            if (std::abs(delta) <= 512) {
                target = pm::wrap_angle(alt_.lock_heading + delta + 4096);
            } else {
                alt_.locked = false;
                target = pm::wrap_angle(cam_yaw + stick_yaw + 4096);
            }
        } else {
            target = pm::wrap_angle(cam_yaw + stick_yaw + 4096);
        }
        move_yaw = static_cast<std::int16_t>(target);
        have_move = true;
        alt_.travel_yaw = static_cast<std::int16_t>(target);
        alt_.have_travel = true;

        const std::int32_t facing = static_cast<std::int16_t>(get(V::RotationY));
        if (get(V::Control2dSnap) != 0) {
            set(V::RotationY, pm::wrap_angle(target + 4096));
        } else {
            // The body swings toward the travel (cosmetic: travel never waits for it).
            const std::int32_t diff = static_cast<std::int16_t>(signed_angle(target - facing));
            const auto limit = static_cast<std::int16_t>(pm::per_tick_30(dt, 341));  // 30 degrees
            const std::int32_t step = std::clamp<std::int32_t>(diff, -limit, limit);
            set(V::RotationY, pm::wrap_angle(facing + step + 4096));
        }
    } else {
        alt_.locked = false;
        alt_.have_travel = false;
    }

    set(V::MoveForward, (get(V::MoveForward) & 2) | (any_input ? 1 : 0));
    set(V::MoveBackward, 0);
    set(V::StepLeftHold, 0);
    set(V::StepRightHold, 0);
    set(V::TurnLeft, 0);
    set(V::TurnRight, 0);
    set(V::MoveInput, any_input ? 1 : 0);
    {
        const std::int32_t run_bind = static_cast<std::uint16_t>(get(V::BindRun));
        const bool bind_run = get(V::OptionWalkRun) != 0 ? (pad & run_bind) == 0 : (pad & run_bind) != 0;
        const bool stick_sprint = get(V::AltButtonSprint) == 0 && ax * ax + ay * ay >= 96 * 96;
        set(V::RunInput, (bind_run || stick_sprint) ? 1 : 0);
    }
    // Travel along the move direction while the body catches up: the heading offset is the move less the facing.
    const std::int32_t offset =
        have_move ? static_cast<std::int16_t>(signed_angle(move_yaw - static_cast<std::int16_t>(get(V::RotationY))))
                  : 0;
    set(V::HeadingOffset, offset);
    set(V::LegsHeading, offset);
}

void Player::alt_steer_orbit(unsigned keys, std::int32_t pad) {
    const std::int32_t orbit = static_cast<std::int16_t>(get(V::OrbitYaw));
    const std::int32_t facing = static_cast<std::int16_t>(get(V::RotationY));
    const std::int32_t gap = signed_angle(orbit - facing);
    std::int32_t lag = 0;  // what the body still has to turn after this tick
    if (get(V::FpsCamera) != 0) {
        // First person standing still and not aiming looks around without turning the body, up to 45 degrees;
        // past that the body comes along (the camera stops the look at 90 degrees of it, which a turn in place
        // must never reach); any move or aim turns the body to the camera.
        bool idle_look = false;
        if (get(V::Aiming) == 0) {
            const bool move = (keys & (kKeyForward | kKeyLeft | kKeyBack | kKeyRight)) != 0 ||
                              (pad & (kPadUp | kPadDown | kPadLeft | kPadRight)) != 0;
            idle_look = !move;
        }
        if (!idle_look) {
            set(V::RotationY, pm::wrap_angle(orbit + 4096));
        } else if (std::abs(gap) > kFpsIdleLook) {
            set(V::RotationY, pm::wrap_angle(orbit + (gap > 0 ? -kFpsIdleLook : kFpsIdleLook) + 4096));
        }
    } else {
        // Third person: the body eases toward the camera (a share of the gap a tick, under a top rate) instead of
        // being bolted to it; aiming lines him up quickly.
        const std::int32_t dt = static_cast<std::int32_t>(get(V::DeltaTime));
        const bool aiming = get(V::Aiming) != 0;
        const std::int32_t share = std::min<std::int32_t>(pm::per_tick_30(dt, aiming ? 2048 : 737), 4096);
        const std::int32_t limit = std::max<std::int32_t>(pm::per_tick_30(dt, aiming ? 341 : 136), 1);
        std::int32_t step = static_cast<std::int32_t>((static_cast<std::int64_t>(gap) * share) / 4096);
        if (step == 0 && gap != 0) {
            step = gap > 0 ? 1 : -1;
        }
        step = std::clamp(step, -limit, limit);
        set(V::RotationY, pm::wrap_angle(facing + step + 4096));
        lag = gap - step;
    }

    const bool fwd = (keys & kKeyForward) != 0 || (pad & kPadUp) != 0;
    const bool back = (keys & kKeyBack) != 0 || (pad & kPadDown) != 0;
    const bool left = (keys & kKeyLeft) != 0 || (pad & kPadLeft) != 0;
    const bool right = (keys & kKeyRight) != 0 || (pad & kPadRight) != 0;
    // Bit 1 of forward is the controller's aged history (a dropped input frame must not read as a release).
    set(V::MoveForward, (get(V::MoveForward) & 2) | (fwd ? 1 : 0));
    set(V::MoveBackward, back ? 1 : 0);
    set(V::StepLeftHold, left ? 1 : 0);
    set(V::StepRightHold, right ? 1 : 0);
    {
        // Walk by default; sprint on the run bind or a near-full stick push (unless sprinting is button-only).
        const std::int32_t run_bind = static_cast<std::uint16_t>(get(V::BindRun));
        const bool bind_run = get(V::OptionWalkRun) != 0 ? (pad & run_bind) == 0 : (pad & run_bind) != 0;
        const std::int32_t ax = static_cast<std::int32_t>(get(V::AnalogLeftX)) - 128;
        const std::int32_t ay = static_cast<std::int32_t>(get(V::AnalogLeftY)) - 128;
        const bool stick_sprint = get(V::AltButtonSprint) == 0 && ax * ax + ay * ay >= 96 * 96;
        set(V::RunInput, (bind_run || stick_sprint) ? 1 : 0);
    }
    set(V::TurnLeft, 0);
    set(V::TurnRight, 0);
    set(V::MoveInput, (fwd || back || left || right) ? 1 : 0);

    // Forward or back with a side held: travel 45 degrees toward that side (back mirrors it), the walk clip plays.
    const int along = (get(V::MoveForward) != 0 ? 1 : 0) - (get(V::MoveBackward) != 0 ? 1 : 0);
    const int across = (get(V::StepRightHold) != 0 ? 1 : 0) - (get(V::StepLeftHold) != 0 ? 1 : 0);
    std::int32_t heading = 0;
    if (along != 0 && across != 0) {
        heading = across > 0 ? 512 : -512;
        if (along < 0) {
            heading = -heading;
        }
    }
    // Travel follows the camera at once while the body catches up.
    if (fwd || back || left || right) {
        heading += lag;
    }
    set(V::HeadingOffset, heading);
    set(V::LegsHeading, heading);
}

bool Player::alt_jump_back() {
    // A press of pure back (not held over) with the run control starts the hop; it then plays to its end and
    // lands before the layer moves on. Classic uses the run flag; the alternate cameras the run bind itself (a
    // stick pulled back reads as running there), and first person never hops.
    const bool pure_back = get(V::MoveBackward) != 0 && get(V::MoveForward) == 0;
    const bool back_edge = pure_back && !alt_.prev_back;
    alt_.prev_back = pure_back;

    bool run_held;
    if (get(V::AltCamera) == 0) {
        run_held = get(V::RunInput) != 0;
    } else {
        const auto pad = static_cast<std::int32_t>(get(V::PadHeld));
        const std::int32_t run_bind = static_cast<std::uint16_t>(get(V::BindRun));
        run_held = get(V::FpsCamera) == 0 &&
                   (get(V::OptionWalkRun) != 0 ? (pad & run_bind) == 0 : (pad & run_bind) != 0);
    }

    const int hop = r_.clip_index("jump_backward");
    if (back_edge && run_held && !alt_.jumping) {
        alt_.jumping = true;
        alt_.jump_elapsed = 0;
        alt_.jump_prev_time = -1;
        set(V::BodyAnimStatus, status(hop, false));
        set(V::BodyStateStep, 0);
        set(V::UpperAnimStatus, status(hop, false));
        set(V::UpperStateStep, 0);
        // The upper body follows the legs' hop through its own jump-back state.
        set(V::LowerBody, lower::kJumpBackward);
        set(V::UpperBody, harry::upper::kRunJumpBackward);
        set(V::UpperControlState, 0);
    }

    if (alt_.jumping) {
        bool finished = false;
        alt_.jump_elapsed += static_cast<std::int32_t>(get(V::DeltaTime));
        const bool timed_out = alt_.jump_elapsed > 12288;  // 3 s of game time
        const std::int64_t body = get(V::BodyAnimStatus);
        if (body == status(hop, true) && body < 76) {
            const int end = r_.clip_end(static_cast<int>(body));
            if (end > 0 && get(V::BodyAnimKeyframe) >= end) {
                finished = true;
            }
        }
        // +Y is down: on or below the floor is on the ground. The hop is a real leap: it holds until he lands.
        const bool grounded = get(V::PositionY) >= get(V::GroundHeight);
        if (finished && !grounded && !timed_out) {
            finished = false;
        } else if ((finished || timed_out) && !grounded) {
            enter_state(st::kFallBackward);
        } else if (finished || timed_out) {
            set(V::FallSpeed, 0);
        }
        if (finished || timed_out) {
            alt_.jumping = false;
            alt_.jump_elapsed = 0;
            alt_.jump_prev_time = -1;
            set(V::LowerBody, lower::kNone);
            set(V::UpperBody, harry::upper::kNone);
            set(V::UpperStateStep, 0);
            set(V::UpperControlState, 0);
            set(V::BodyStateStep, 0);
            set(V::BodyControlState, 0);
        }
    }

    if (alt_.jumping) {
        if (get(V::BodyAnimStatus) == status(hop, true)) {
            // The hop moves with the clip's time (smooth between keyframes), straight back.
            const std::int32_t now = static_cast<std::int32_t>(get(V::BodyAnimTime));
            std::int32_t played = 0;
            if (alt_.jump_prev_time >= 0) {
                played = std::max(now - alt_.jump_prev_time, 0);
            }
            alt_.jump_prev_time = now;
            if (played > 0) {
                const std::int32_t step = pm::mul_wide(kJumpBackRate, played);
                const std::int32_t yaw = static_cast<std::int16_t>(get(V::RotationY));
                const std::int32_t dx = pm::mul(step, sh::world::fixed_sin(yaw));
                const std::int32_t dz = pm::mul(step, sh::world::fixed_cos(yaw));
                set(V::PositionX, get(V::PositionX) - dx);
                set(V::PositionZ, get(V::PositionZ) - dz);
                credit_distance(dx, dz, true);
            }
        } else {
            alt_.jump_prev_time = -1;
        }
        set(V::StepSpeed, 0);
    } else if (get(V::MoveForward) != 0) {
        if (get(V::RunInput) != 0) {
            std::int32_t run = kOrbitRunSpeed;
            if (get(V::AltCamera) == 0) {
                const int zone = r_.speed_zone(static_cast<std::int32_t>(get(V::PositionX)),
                                               static_cast<std::int32_t>(get(V::PositionZ)));
                run = zone == 2 ? kFastRunSpeed : (zone == 0 ? kSlowRunSpeed : kNormalRunSpeed);
            }
            set(V::StepSpeed, run);
        } else {
            set(V::StepSpeed, kWalkSpeed);
        }
    } else if (get(V::MoveBackward) != 0) {
        set(V::StepSpeed, -kWalkSpeed);
    } else {
        set(V::StepSpeed, 0);
    }
    return alt_.jumping;
}

void Player::alt_quick_turn(bool jump_back) {
    // The quick turn button on this layer: a smooth half turn at the legs' rate, the orbit camera following.
    if (get(V::QuickTurnRequest) != 0) {
        set(V::QuickTurnRequest, 0);
        if (!alt_.turning && !jump_back) {
            alt_.turning = true;
            alt_.turn_start = static_cast<std::int16_t>(get(V::RotationY));
            alt_.turn_done = 0;
        }
    }
    if (!alt_.turning) {
        return;
    }
    std::int32_t step = (static_cast<std::int32_t>(get(V::DeltaTime)) * 24) >> 4;
    step = std::max(step, 1);
    if (alt_.turn_done + step >= 2048) {
        step = 2048 - alt_.turn_done;
        alt_.turning = false;
    }
    alt_.turn_done += step;
    set(V::RotationY, pm::wrap_angle(alt_.turn_start + alt_.turn_done + 4096));
    if (get(V::AltCamera) != 0) {
        set(V::OrbitYaw, get(V::RotationY));
    }
}

void Player::alt_legs_clip() {
    const Clips& k = clips();
    // The legs take the movement clip; the upper body too unless it plays its own (aim, or any attack running:
    // writing it would restart the swing or hide the shot's end from the upper body's machine).
    const bool aiming = get(V::AimInput) != 0 && static_cast<std::int8_t>(get(V::WeaponAttack)) != harry::kNoWeapon;
    const bool attacking = gun_attack(get(V::WeaponAttack)) || get(V::UpperBody) == harry::upper::kAttack;
    const bool upper_free = !aiming && !attacking;
    auto play = [&](int clip) {
        const std::int64_t s = get(V::BodyAnimStatus);
        if (s != status(clip, true) && s != status(clip, false)) {
            set(V::BodyAnimStatus, status(clip, false));
            set(V::BodyStateStep, 0);
            if (upper_free) {
                set(V::UpperAnimStatus, status(clip, false));
                set(V::UpperStateStep, 0);
            }
            return true;
        }
        return false;
    };

    const bool step_left_tap = get(V::StepLeftTap) != 0, step_right_tap = get(V::StepRightTap) != 0;
    const bool step_left = get(V::StepLeftHold) != 0, step_right = get(V::StepRightHold) != 0;
    if (get(V::MoveForward) != 0) {
        play(get(V::RunInput) != 0 ? k.run_forward : k.walk_forward);
    } else if (get(V::MoveBackward) != 0) {
        if (get(V::BodyAnimStatus) != status(r_.clip_index("jump_backward"), false)) {
            play(k.walk_backward);
        }
    } else if (step_left || step_left_tap || step_right || step_right_tap) {
        // Side steps move with the clip's time (the step only slides while it plays); in an alternate camera a
        // held side step with the run control is the side run at the forward run's speed.
        const bool to_left = step_left || step_left_tap;
        const bool side_run = get(V::AltCamera) != 0 && get(V::RunInput) != 0 && (step_left || step_right);
        const int clip = to_left ? (side_run ? k.run_left : k.sidestep_left) : (side_run ? k.run_right : k.sidestep_right);
        if (play(clip)) {
            alt_.step_prev_time = -1;
        }

        const std::int32_t now = static_cast<std::int32_t>(get(V::BodyAnimTime));
        std::int32_t played = 0;
        if (alt_.step_prev_time >= 0) {
            played = now - alt_.step_prev_time;
            if (played < 0) {
                played += 102400;  // the loop wrapped (25 keyframes)
            }
            if (played < 0 || played > 8192) {
                played = 0;
            }
        }
        alt_.step_prev_time = now;

        std::int32_t step = 0;
        if (side_run) {
            step = pm::mul_wide(kOrbitRunSpeed, static_cast<std::int32_t>(get(V::DeltaTime)));
        } else if (played > 0) {
            step = pm::mul_wide(kSidestepRate, played);
        }
        if (step != 0) {
            // Through the walls like the forward move (the sweep cannot tunnel at any frame rate), without
            // touching the tick's collision state.
            const std::int32_t yaw = static_cast<std::int16_t>(get(V::RotationY));
            const std::int32_t c = pm::mul(step, sh::world::fixed_cos(yaw));
            const std::int32_t s = pm::mul(step, sh::world::fixed_sin(yaw));
            const std::int32_t wish_x = to_left ? -c : c;
            const std::int32_t wish_z = to_left ? s : -s;
            std::int32_t dx = wish_x, dz = wish_z;
            if (get(V::DebugNoWalls) == 0) {
                const MoveResult moved = r_.slide(wish_x, wish_z);
                dx = moved.dx;
                dz = moved.dz;
            }
            set(V::PositionX, get(V::PositionX) + dx);
            set(V::PositionZ, get(V::PositionZ) + dz);
            credit_distance(dx, dz, side_run);
        }
    } else if (get(V::TurnLeft) != 0) {
        play(r_.clip_index("turn_left"));
    } else if (get(V::TurnRight) != 0) {
        play(r_.clip_index("turn_right"));
    } else {
        // The tired idle carries the heavy breath: the same choice as the legs' own idle.
        const bool tired = get(V::Exhaustion) >= 40960 || get(V::Health) < 122880;
        play(tired ? k.idle_exhausted : r_.clip_index("idle"));
    }
}

void Player::alt_aim_and_fire() {
    const bool alt_camera = get(V::AltCamera) != 0;
    const auto pad = static_cast<std::int32_t>(get(V::PadHeld));
    const bool armed = static_cast<std::int8_t>(get(V::WeaponAttack)) != harry::kNoWeapon;
    const std::int64_t attack = get(V::WeaponAttack);
    bool aim_held = (pad & static_cast<std::uint16_t>(get(V::BindAim))) != 0;
    const bool fire_held = (pad & static_cast<std::uint16_t>(get(V::BindAction))) != 0;
    // The rear look faces the camera behind Harry: an aim then would point away from the view.
    if (get(V::RearLook) != 0) {
        aim_held = false;
    }
    // Melee has no aim pose: moving in run mode drops its aim (the arms would swing in place).
    if (alt_camera && get(V::RunInput) != 0 && get(V::MoveInput) != 0 && !gun_attack(attack)) {
        aim_held = false;
        set(V::AimInput, 0);
    }
    // The multi-tap queue of the upper body reads the alternate cameras' fire button here.
    set(V::AltFireHeld, (alt_camera && fire_held) ? 1 : 0);

    const bool aimed_legs = get(V::LowerBody) >= lower::kAim;
    if (alt_camera) {
        set(V::AimInput, (aim_held && armed) ? 1 : 0);
        if (gun_attack(attack) && aimed_legs) {
            set(V::ShootInput, fire_held ? 1 : 0);
            set(V::AttackInput, fire_held ? 1 : 0);
        } else if (armed && aimed_legs) {
            // Melee: a press held 130 ms is the wide swing; a shorter click is a 100 ms tap pulse on release
            // (the stab the combo chains from), timed on the wall clock.
            const std::uint32_t now = r_.time_ms();
            if (fire_held && !alt_.prev_fire) {
                alt_.fire_down_ms = now;
            }
            if (!fire_held && alt_.prev_fire && now - alt_.fire_down_ms < 130u) {
                alt_.tap_until_ms = now + 100u;
            }
            set(V::HoldAttackInput, fire_held ? 0x1F : 0);
            set(V::AttackInput, (fire_held && now - alt_.fire_down_ms >= 130u) ? 1 : 0);
            set(V::ShootInput, now < alt_.tap_until_ms ? 1 : 0);
        }
    }

    if (alt_camera && get(V::AimInput) != 0 && armed) {
        set(V::Aiming, 1);
        // The swing's own lower state carries its root motion (the katana's lunge).
        if (get(V::LowerBody) != lower::kAttack) {
            set(V::LowerBody, lower::kAim);
        }
    } else if (alt_camera && gun_attack(attack)) {
        set(V::Aiming, 0);
    }

    // The aim let go: the attack registers flush once, and a pending tap must not swing when the aim returns.
    if (alt_camera && alt_.prev_aim && !aim_held) {
        set(V::HoldAttackInput, 0);
        set(V::AttackInput, 0);
        set(V::ShootInput, 0);
        alt_.tap_until_ms = 0;
    }
    alt_.prev_aim = aim_held;
    alt_.prev_fire = fire_held;
}

void Player::alt_lower_state() {
    // The lower state the footsteps key on, following the clip choice of alt_legs_clip.
    const bool step_left = get(V::StepLeftHold) != 0 || get(V::StepLeftTap) != 0;
    const bool step_right = get(V::StepRightHold) != 0 || get(V::StepRightTap) != 0;
    const bool alt_camera = get(V::AltCamera) != 0;
    const bool side_run = alt_camera && get(V::RunInput) != 0 &&
                          (get(V::StepLeftHold) != 0 || get(V::StepRightHold) != 0);
    int legs;
    if (get(V::MoveForward) != 0 && get(V::RunInput) != 0) {
        legs = lower::kRunForward;
    } else if (get(V::MoveForward) != 0) {
        legs = lower::kWalkForward;
    } else if (get(V::MoveBackward) != 0) {
        legs = lower::kWalkBackward;
    } else if (step_left) {
        legs = side_run ? lower::kRunLeft : lower::kSidestepLeft;
    } else if (step_right) {
        legs = side_run ? lower::kRunRight : lower::kSidestepRight;
    } else {
        legs = lower::kNone;
    }
    if (alt_camera && get(V::AimInput) != 0) {
        // Moving while aiming is the movement's aim variant (same clips and footsteps); a swing keeps its state.
        if (get(V::LowerBody) != lower::kAttack) {
            set(V::LowerBody, legs + lower::kAim);
        }
    } else if (get(V::AimInput) == 0) {
        set(V::LowerBody, legs);
    }
}

void Player::alt_katana_lunge() {
    // The katana's stab steps forward through the step speed (the legs' attack state does it in the classic
    // camera): the tap on its first attack tick, the hold once the swing passes its hit keyframe.
    const bool in_attack = get(V::LowerBody) == lower::kAttack;
    const auto attack = static_cast<std::int32_t>(get(V::WeaponAttack));
    const char* weapon = attack < 30 && attack >= 0 ? r_.weapon_name(attack % 10) : nullptr;
    const bool katana = weapon != nullptr && std::string_view(weapon) == "Katana";
    if (in_attack && attack < harry::kFirstGunAttack && katana) {
        if (attack >= 10 && attack < 20) {
            if (alt_.lunge == 0 &&
                get(V::UpperAnimKeyframe) >= r_.weapon_clip_start(static_cast<int>(get(V::WeaponClip))) + 7) {
                alt_.lunge = 20480;
            }
        } else if (get(V::BodyStateStep) == 0 && get(V::AttackInput) == 0) {
            alt_.lunge = 20480;
        }
        if (get(V::BodyStateStep) == 0) {
            set(V::BodyStateStep, get(V::BodyStateStep) + 1);
        }
    }
    if (alt_.lunge != 0) {
        alt_.lunge -= pm::per_tick_30(static_cast<std::int32_t>(get(V::DeltaTime)), in_attack ? 0x444 : 1638);
        alt_.lunge = std::max(alt_.lunge, 0);
        set(V::StepSpeed, alt_.lunge);
        set(V::HeadingOffset, 0);
    }
}

}  // namespace sh::game
