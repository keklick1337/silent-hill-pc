// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Harry's tick: the move, both halves' clips and the pose layers.

#include "engine/player/player.h"

#include "engine/player/harry_states.h"
#include "engine/player/player_math.h"
#include "engine/world/fixed_math.h"
#include "engine/world/fixed_trig.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <initializer_list>

namespace sh::game {

namespace {

using V = PlayerVar;
namespace pm = player_math;
namespace st = harry::state;

constexpr std::int32_t kFallDrop = 2662;        // 0.65 m below the feet: a ledge
constexpr std::int32_t kWalkRunBlendLimit = 2048;  // 0.5 s
constexpr std::int32_t kPinnedDuration = 61440;    // 15 keyframes/s
constexpr std::int32_t kOnFloorDuration = 40960;   // 10 keyframes/s

// A room whose floor is level whatever the ground says (overlay.json "level_floor_rooms").
bool level_floor_room(const sh::maps::MapRules& rules, int room) {
    return std::find(rules.level_floor_rooms.begin(), rules.level_floor_rooms.end(), room) !=
           rules.level_floor_rooms.end();
}

// Packed statuses of the map reaction rows that end in a dying pose (pinned; on the floor).
constexpr int kPinDeathRows[] = {256, 258};
constexpr int kFloorDeathRows[] = {265, 266};

// States that leave the held item where it is (scripted scenes that place it themselves).
constexpr int kHeldItemUntouched[] = {61,  62,  63,  64,  65,  66,  67,  68,  69,  76,  77,  78,  79,  80,
                                      83,  84,  85,  86,  87,  88,  93,  94,  95,  97,  98,  99,  100, 102,
                                      103, 104, 105, 107, 108, 111, 112, 118, 119, 122, 134, 136, 137, 138,
                                      139, 141, 142, 143, 144, 145, 146, 147, 148, 152, 162};

// Mesh variant of the hand holding a weapon (by weapon name): melee weapons, the pistol-grip guns, the rifle.
int held_variant(const char* weapon) {
    if (weapon == nullptr) {
        return 0;
    }
    for (const char* m : {"KitchenKnife", "SteelPipe", "RockDrill", "Hammer", "Chainsaw", "Katana", "Axe"}) {
        if (std::strcmp(weapon, m) == 0) {
            return 2;
        }
    }
    for (const char* g : {"Handgun", "Shotgun", "HyperBlaster"}) {
        if (std::strcmp(weapon, g) == 0) {
            return 3;
        }
    }
    return std::strcmp(weapon, "HuntingRifle") == 0 ? 4 : 0;
}

// The alternate cameras' steady gun-forward keyframe per weapon (by packed tap attack), third or first person.
int aim_hold_keyframe(int attack, bool first_person) {
    switch (attack) {
        case 33: return first_person ? 597 : 587;  // hunting rifle
        case 32:                                   // handgun
        case 34: return first_person ? 592 : 588;  // shotgun
        case 35: return 574;  // hyper blaster: its clip block only spans 568..579
        default: return 591;
    }
}

// Mixes bone matrix `src` into `dst` by weight `w` (Q12): keyed channels only (bones without keyed translation
// keep their rest offset). The rotation rows are lerped, then made orthonormal again at the lerped row length,
// so the legs keep their thickness between clips whose hips sit far apart.
void mix_bone(BoneMatrix& dst, const BoneMatrix& src, std::int32_t w, bool keyed_translation, bool keyed_rotation) {
    const float fw = static_cast<float>(w) / 4096.0f;
    float m[3][3];
    float len[3];
    for (int i = 0; i < 3; ++i) {
        float la = 0.0f;
        float lb = 0.0f;
        for (int j = 0; j < 3; ++j) {
            la += static_cast<float>(dst.m[i][j]) * dst.m[i][j];
            lb += static_cast<float>(src.m[i][j]) * src.m[i][j];
            m[i][j] = static_cast<float>(dst.m[i][j]) + static_cast<float>(src.m[i][j] - dst.m[i][j]) * fw;
        }
        len[i] = std::sqrt(la) + (std::sqrt(lb) - std::sqrt(la)) * fw;
        if (keyed_translation) {
            dst.t[i] += ((src.t[i] - dst.t[i]) * w) >> 12;
        }
    }
    if (!keyed_rotation) {
        return;
    }
    for (int i = 0; i < 3; ++i) {
        for (int k = 0; k < i; ++k) {
            const float d = m[i][0] * m[k][0] + m[i][1] * m[k][1] + m[i][2] * m[k][2];
            for (int j = 0; j < 3; ++j) {
                m[i][j] -= d * m[k][j];
            }
        }
        const float d = std::sqrt(m[i][0] * m[i][0] + m[i][1] * m[i][1] + m[i][2] * m[i][2]);
        if (d < 1e-3f) {
            return;
        }
        for (int j = 0; j < 3; ++j) {
            m[i][j] /= d;
        }
    }
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            dst.m[i][j] = static_cast<std::int16_t>(std::lrint(m[i][j] * len[i]));
        }
    }
}

}  // namespace

const Player::Clips& Player::clips() {
    if (!clips_.known) {
        clips_.still = r_.clip_index("still");
        clips_.walk_forward = r_.clip_index("walk_forward");
        clips_.run_forward = r_.clip_index("run_forward");
        clips_.walk_backward = r_.clip_index("walk_backward");
        clips_.sidestep_left = r_.clip_index("sidestep_left");
        clips_.sidestep_right = r_.clip_index("sidestep_right");
        clips_.run_left = r_.clip_index("run_left");
        clips_.run_right = r_.clip_index("run_right");
        clips_.idle_exhausted = r_.clip_index("idle_exhausted");
        clips_.recoil = r_.clip_index("handgun_recoil");
        clips_.aim_settle = r_.clip_index("handgun_aim_start");
        clips_.aim_fire = r_.clip_index("weapon_attack_2");
        clips_.known = true;
    }
    return clips_;
}

void Player::tick() {
    if (get(V::DeltaTime) != 0) {
        r_.receive_damage();

        // While the legs hand over between walk and run, nothing the player presses counts.
        if (get(V::WalkRunBlend) != 0) {
            for (V v : {V::ActionInput, V::MoveInput, V::ShootInput, V::AttackInput, V::HoldAttackInput,
                        V::AimInput, V::RunInput, V::MoveBackward, V::MoveForward, V::StepRightTap,
                        V::StepRightHold, V::TurnRight, V::StepLeftTap, V::StepLeftHold, V::TurnLeft}) {
                set(v, 0);
            }
        }

        if (get(V::ControlDisabled) == 0) {
            behave();
        } else {
            r_.scripted_control();
        }
        if (get(V::ControlDisabled) == 0) {
            integrate();
        } else {
            r_.scripted_move();
        }

        const bool alt_camera = get(V::AltCamera) != 0;
        if (alt_camera) {
            // The alternate cameras play the whole body's walk and run: the upper body follows the legs' movement
            // clip unless it is busy with its own (aiming, a swing or a shot, a reload).
            const int c = static_cast<int>(get(V::BodyAnimStatus)) >> 1;
            const Clips& k = clips();
            const bool moving = c == k.run_forward || c == k.walk_forward || c == k.walk_backward ||
                                c == k.run_left || c == k.run_right || c == k.sidestep_left || c == k.sidestep_right;
            const std::int64_t upper = get(V::UpperBody);
            const bool upper_busy = get(V::Aiming) != 0 || upper == harry::upper::kAttack ||
                                    upper == harry::upper::kReload;
            if (moving && !upper_busy) {
                set(V::UpperAnimStatus, get(V::BodyAnimStatus));
                set(V::UpperAnimKeyframe, get(V::BodyAnimKeyframe));
                set(V::UpperAnimTime, get(V::BodyAnimTime));
            }
        }

        animate();
        if (!alt_camera) {
            lean_classic();
        } else {
            free_aim_pose();
        }
        rear_look();
        r_.keyframe_inspector();
    }

    set(V::PushX, 0);
    set(V::PushZ, 0);
}

void Player::integrate() {
    const std::int32_t dt = static_cast<std::int32_t>(get(V::DeltaTime));
    const std::int32_t x = static_cast<std::int32_t>(get(V::PositionX));
    const std::int32_t y = static_cast<std::int32_t>(get(V::PositionY));
    const std::int32_t z = static_cast<std::int32_t>(get(V::PositionZ));
    set(V::PrevX, x);
    set(V::PrevY, y);
    set(V::PrevZ, z);

    const GroundSurface here = r_.ground_at(x, z);
    const std::int32_t heading = static_cast<std::int32_t>(get(V::Heading));
    const std::int32_t speed =
        pm::slope_speed(static_cast<std::int32_t>(get(V::MoveSpeed)), heading, here.tilt_x, here.tilt_z);
    set(V::MoveSpeed, speed);

    const std::int32_t step = pm::mul_wide(speed, dt);
    set(V::IntendedStep, step < 0 ? -step : step);
    pm::Offset wish = pm::step_offset(step, heading);
    const std::int32_t fall = pm::mul_wide(static_cast<std::int32_t>(get(V::FallSpeed)), dt);

    const bool push_floor = r_.character_push_take() || r_.map_rules().floor_push;
    const std::int32_t push_x = static_cast<std::int32_t>(get(V::PushX));
    const std::int32_t push_z = static_cast<std::int32_t>(get(V::PushZ));
    if (push_floor) {
        wish.x += push_x;
        wish.z += push_z;
    }

    MoveResult moved = r_.move_through_walls(wish.x, fall, wish.z, get(V::DebugNoWalls) == 0, here);
    // The wall pass can miss the floor under its cell; the point query above found the real one.
    if (moved.ground.height == 0 && here.height != 0) {
        moved.ground.height = here.height;
    }
    const std::int32_t previous_ground = static_cast<std::int32_t>(get(V::GroundHeight));
    const pm::GroundClamp ground = pm::clamp_ground(moved.ground.height, previous_ground, y, dt);
    if (ground.phantom) {
        r_.count_phantom_floor();
    }
    std::int32_t floor = ground.height;

    if (push_floor) {
        // What the walls refused of the push goes back to the floor's own motion.
        auto refused = [](std::int32_t push, std::int32_t wanted, std::int32_t got) {
            const bool opposite = (wanted >= 0) != (got >= 0);
            return push != 0 && (opposite || std::abs(wanted) >= std::abs(got)) ? wanted - got : 0;
        };
        r_.push_feedback(-refused(push_x, wish.x, moved.dx), -refused(push_z, wish.z, moved.dz));
    }

    std::int32_t nx = x + moved.dx;
    std::int32_t ny = y + moved.dy;
    std::int32_t nz = z + moved.dz;

    const std::int64_t upper = get(V::UpperBody);
    if (upper == harry::upper::kRunForward || upper == harry::upper::kRunRight || upper == harry::upper::kRunLeft) {
        const std::int32_t length = sh::world::game_sqrt(sh::world::wrap_add(
            sh::world::wrap_add(sh::world::wrap_mul(moved.dx, moved.dx), sh::world::wrap_mul(moved.dy, moved.dy)),
            sh::world::wrap_mul(moved.dz, moved.dz)));
        set(V::RunAhead, get(V::RunAhead) + length);
    } else {
        set(V::RunAhead, 0);
    }

    // The results screen's totals for the ticks the legs' behaviour did not count itself (alternate cameras).
    if (get(V::DistanceCounted) == 0) {
        const std::int32_t length = sh::world::planar_length(moved.dx, moved.dz);
        const V total = get(V::RunInput) != 0 ? V::RunDistance : V::WalkDistance;
        set(total, pm::add_distance(static_cast<std::uint32_t>(get(total)), length));
    }
    set(V::DistanceCounted, 0);

    if (level_floor_room(r_.map_rules(), static_cast<int>(get(V::RoomIndex)))) {
        floor = 0;
    }
    if (moved.ground.type == 0) {
        floor = previous_ground;
    }
    set(V::FloorHeight, floor);
    set(V::PositionX, nx);
    set(V::PositionZ, nz);
    if (ny > floor && get(V::DebugNoFloor) == 0) {
        ny = floor;
        set(V::FallSpeed, 0);
    }
    set(V::PositionY, ny);

    const std::int32_t travel = pm::wrap_angle(sh::world::fixed_atan2(nx - x, nz - z) + 4096);
    const std::int64_t state = get(V::State);
    if (!(state >= st::kFallForward && state < st::kKickEnemy) && get(V::WalkRunBlend) == 0 &&
        moved.ground.type != 0 && floor - ny >= kFallDrop) {
        const std::int32_t facing = static_cast<std::int16_t>(get(V::RotationY));
        const std::int32_t off = std::abs(facing - travel);
        if (off >= 1024 && off < 3072) {
            // Backing off the ledge; the jump back lands on its own.
            if (get(V::LowerBody) != harry::lower::kJumpBackward) {
                enter_state(st::kFallBackward);
            }
        } else {
            enter_state(st::kFallForward);
        }
        set(V::Aiming, 0);
    }

    set(V::GroundHeight, floor);
    r_.set_root_translation(nx >> 4, ny >> 4, nz >> 4);
}

void Player::attach_held_item(bool moving) {
    const int attack = static_cast<int>(get(V::WeaponAttack));
    const int joint = moving ? 2 : 1;
    if (attack == harry::kNoWeapon) {
        if (moving) {
            r_.attach_held_item(2, 2);
        } else {
            r_.attach_held_item(get(V::MatchAnim) != 0 ? 2 : 1, 1);
        }
        return;
    }
    // A packed melee attack is the weapon number + 10 * input; guns (32 and up) have only the tap form. A mod's
    // weapon's rows name their weapon through the ids.
    const int owner = r_.mod_attack_weapon(attack);
    const int weapon = owner >= 0 ? owner : attack < 30 ? attack % 10 : attack;
    const int variant = held_variant(r_.weapon_name(weapon));
    if (variant != 0) {
        r_.attach_held_item(variant, joint);
    }
}

void Player::animate() {
    const std::int64_t state = get(V::State);
    bool untouched = false;
    for (int s : kHeldItemUntouched) {
        untouched = untouched || state == s;
    }
    if (!untouched) {
        bool moving;
        if (state == st::kRunInScene) {
            moving = true;
        } else if (state == st::kNone) {
            const std::int64_t upper = get(V::UpperBody);
            moving = upper == harry::upper::kRunForward || upper == harry::upper::kRunRight ||
                     upper == harry::upper::kRunLeft;
        } else {
            moving = false;
        }
        attach_held_item(moving);
    }

    const bool blending = get(V::WalkRunBlend) != 0;
    if (!blending) {
        walk_run_blend_time_ = 0;
    }
    set(V::DisabledBones, harry::kUpperBodyBones);
    const int still = status(clips().still, false);
    if (blending) {
        set(V::BodyAnimStatus, still);
    }
    r_.play_clip(PlaybackHalf::Body, static_cast<int>(get(V::BodyAnimStatus)));
    diagonal_legs();
    set(V::DisabledBones, harry::kLowerBodyBones);
    r_.play_clip(PlaybackHalf::Upper, static_cast<int>(get(V::UpperAnimStatus)));
    if (!blending) {
        return;
    }

    if (get(V::BodyAnimStatus) == r_.clip_link(still)) {
        set(V::WalkRunBlend, 0);
    }
    // The hand-over has one exit (the Still clip reaching its link); a reaction that rewrites the legs' clip every
    // tick starves it and the controls stay dead. It is a ~0.1 s blend: cut it after half a second.
    walk_run_blend_time_ += static_cast<std::int32_t>(get(V::DeltaTime));
    if (get(V::WalkRunBlend) != 0 && walk_run_blend_time_ > kWalkRunBlendLimit) {
        set(V::WalkRunBlend, 0);
        walk_run_blend_time_ = 0;
    }
}

void Player::diagonal_legs() {
    struct DiagonalClip {
        int base;           // status of the legs' clip this applies to
        int left, right;    // the turned run clips blended in (running)
        int left_phase, right_phase;  // phase offsets of those clips (fraction of a cycle, Q12)
        int turn;           // hip yaw (walking), positive toward Harry's right
    };
    const Clips& k = clips();
    const DiagonalClip table[] = {
        {status(k.walk_forward, true), 0, 0, 0, 0, 512},
        {status(k.walk_backward, true), 0, 0, 0, 0, -512},
        {status(k.run_forward, true), k.run_left, k.run_right, 0, -245, 0},
    };

    if (r_.anim_bone_count() != harry::kBoneCount ||
        r_.anim_keyframe_count() <= r_.clip_end(status(k.run_right, true))) {
        diag_weight_ = 0;
        diag_fade_ = 0;
        return;
    }

    const DiagonalClip* clip = nullptr;
    const std::int64_t body_status = get(V::BodyAnimStatus);
    for (const DiagonalClip& c : table) {
        if (body_status == c.base) {
            clip = &c;
            break;
        }
    }

    const std::int32_t dt = static_cast<std::int32_t>(get(V::DeltaTime));
    std::int32_t step = (dt * 16384) >> 12;
    if (clip != nullptr) {
        // Only the alternate cameras' diagonal travel sets a heading offset with both axes held.
        const int along = ((get(V::MoveForward) & 1) != 0 ? 1 : 0) - (get(V::MoveBackward) != 0 ? 1 : 0);
        const int across = (get(V::StepRightHold) != 0 ? 1 : 0) - (get(V::StepLeftHold) != 0 ? 1 : 0);
        std::int32_t target = (get(V::AltCamera) != 0 && along != 0 && across != 0 && get(V::HeadingOffset) != 0)
                                  ? (clip->turn != 0 ? 4096 : 2048)
                                  : 0;
        if (clip->turn != 0) {
            step *= 2;
        }
        // Changing sides drains to the base clip first.
        if (target != 0 && across != diag_side_) {
            if (diag_weight_ == 0) {
                diag_side_ = across;
            } else {
                target = 0;
            }
        }
        diag_weight_ = diag_weight_ < target ? std::min(diag_weight_ + step, target)
                                             : std::max(diag_weight_ - step, target);
    } else if (diag_weight_ > 0) {
        // The next clip blends in from its own keyframe, not from the bones: fade the last blended pose out.
        diag_weight_ = 0;
        diag_fade_ = 4096;
    }

    if (diag_weight_ > 0 && clip->turn != 0) {
        BoneMatrix hips = r_.bone_local(harry::bone::kHips);
        const std::int32_t angle = pm::mul(clip->turn * diag_side_, diag_weight_);
        const std::int32_t c = sh::world::fixed_cos(angle);
        const std::int32_t s = sh::world::fixed_sin(angle);
        for (int j = 0; j < 3; ++j) {
            const std::int32_t r0 = hips.m[0][j];
            const std::int32_t r2 = hips.m[2][j];
            hips.m[0][j] = static_cast<std::int16_t>((c * r0 + s * r2) >> 12);
            hips.m[2][j] = static_cast<std::int16_t>((c * r2 - s * r0) >> 12);
        }
        r_.set_bone_local(harry::bone::kHips, hips, true);
    } else if (diag_weight_ > 0) {
        const int base = clip->base;
        const int side = status(diag_side_ > 0 ? clip->right : clip->left, true);
        const std::int32_t base_len = r_.clip_end(base) + 1 - r_.clip_start(base);
        const std::int32_t side_len = r_.clip_end(side) + 1 - r_.clip_start(side);
        std::int32_t phase = (static_cast<std::int32_t>(get(V::BodyAnimTime)) - r_.clip_start(base) * 4096) / base_len;
        phase += diag_side_ > 0 ? clip->right_phase : clip->left_phase;
        phase = (phase & 0xFFF) * side_len;
        const int kf0 = r_.clip_start(side) + (phase >> 12);
        int kf1 = kf0 + 1;
        if (kf1 > r_.clip_end(side)) {
            kf1 = r_.clip_start(side);
        }
        r_.pose_scratch(kf0, kf1, phase & 0xFFF);
        for (int i = harry::bone::kHips; i < harry::kBoneCount; ++i) {
            BoneMatrix m = r_.bone_local(i);
            mix_bone(m, r_.scratch_bone(i), diag_weight_, r_.bone_keys_translation(i), r_.bone_keys_rotation(i));
            r_.set_bone_local(i, m, false);
        }
    }

    if (diag_fade_ > 0) {
        for (int i = harry::bone::kHips; i < harry::kBoneCount; ++i) {
            BoneMatrix m = r_.bone_local(i);
            mix_bone(m, diag_last_[i], diag_fade_, r_.bone_keys_translation(i), r_.bone_keys_rotation(i));
            r_.set_bone_local(i, m, false);
        }
        diag_fade_ = std::max(diag_fade_ - step * 2, 0);
    } else if (diag_weight_ > 0) {
        for (int i = harry::bone::kHips; i < harry::kBoneCount; ++i) {
            diag_last_[i] = r_.bone_local(i);
        }
    }
}

void Player::lean_classic() {
    const std::int64_t state = get(V::State);
    pm::Lean lean{static_cast<std::int16_t>(get(V::FlexX)), static_cast<std::int16_t>(get(V::FlexRotY))};
    auto lean_body = [this](const pm::Lean& l) {
        r_.rotate_bone(harry::bone::kTorso, 0, l.x >> 1, l.y, false);
        // The upper arms compose the pitch onto their animated pose (an overwrite would T-pose the arms).
        r_.rotate_bone(harry::bone::kLeftUpperArm, l.x >> 1, 0, 0, false);
        r_.rotate_bone(harry::bone::kRightUpperArm, l.x >> 1, 0, 0, false);
    };

    if (state == st::kCombat) {
        const std::int64_t attack = get(V::WeaponAttack);
        if (get(V::UpperBody) == harry::upper::kReload || !gun_attack(attack)) {
            set(V::FlexX, 0);
            set(V::FlexRotY, 0);
        } else {
            pm::AimLeanInput in;
            in.aim_pitch = static_cast<std::int32_t>(get(V::AimPitch));
            in.angle_to_target = static_cast<std::int32_t>(get(V::AngleToTarget));
            in.rotation_y = static_cast<std::int32_t>(get(V::RotationY));
            in.aim_hold = static_cast<std::int32_t>(get(V::AimHold));
            in.flex_target_x = static_cast<std::int32_t>(get(V::FlexTargetX));
            in.weapon_attack = static_cast<std::int32_t>(attack);
            const std::int64_t upper = get(V::UpperBody);
            in.lock_starting =
                upper == harry::upper::kAimStartTargetLock || upper == harry::upper::kAimTargetLockSwitch;
            in.settled = (get(V::PlayerFlags) & harry::flag::kAimLockSettled) != 0;
            in.dt = static_cast<std::int32_t>(get(V::DeltaTime));
            lean = pm::aim_lean(in, lean);
            set(V::FlexX, lean.x);
            set(V::FlexRotY, lean.y);
            lean_body(lean);
        }
    } else if (state == st::kNone) {
        lean = pm::relax_lean(lean, static_cast<std::int32_t>(get(V::DeltaTime)));
        set(V::FlexX, lean.x);
        set(V::FlexRotY, lean.y);
        lean_body(lean);
    } else if (state == st::kHeadAndTorsoTurn) {
        if (lean.y != 0) {
            r_.rotate_bone(harry::bone::kTorso, 0, 0, 192, false);   // 16.9 degrees
            r_.rotate_bone(harry::bone::kHead, 0, 320, 224, false);  // 28.2, 19.7 degrees
        }
    } else if (state >= st::kFirstHeadTurn && state < st::kEndHeadTurn) {
        r_.rotate_bone(harry::bone::kHead, 0, 0, lean.y, false);
    } else {
        set(V::FlexRotY, 0);
        set(V::FlexX, 0);
    }

    if (get(V::MatchAnim) != 0 && get(V::State) < st::kFirstNoMatchArm) {
        // The arm holding the lit match up.
        r_.rotate_bone(harry::bone::kRightUpperArm, 0, 720, -100, false);    // 63.3, -8.8 degrees
        r_.rotate_bone(harry::bone::kRightForearm, -160, 256, -350, false);  // -14.1, 22.5, -30.8 degrees
        r_.rotate_bone(harry::bone::kRightHand, 150, 0, 0, false);           // 13.2 degrees
    }
}

void Player::free_aim_pose() {
    // The match arm the classic lean would hold up; the flame reads the hand this tick, so the arm chain is
    // marked stale.
    if (get(V::MatchAnim) != 0 && get(V::State) < st::kFirstNoMatchArm) {
        r_.rotate_bone(harry::bone::kRightUpperArm, 0, 720, -100, true);
        r_.rotate_bone(harry::bone::kRightForearm, -160, 256, -350, true);
        r_.rotate_bone(harry::bone::kRightHand, 150, 0, 0, true);
    }

    const std::int64_t attack = get(V::WeaponAttack);
    if (get(V::Aiming) == 0 || !gun_attack(attack) || get(V::UpperBody) == harry::upper::kReload) {
        return;
    }
    // A steady aim holds the gun-forward keyframe on the upper body, except while the shot's own clips play.
    const int upper_clip = static_cast<int>(get(V::UpperAnimStatus)) >> 1;
    const Clips& k = clips();
    if (upper_clip != k.aim_settle && upper_clip != k.aim_fire && upper_clip != k.recoil) {
        const int kf = aim_hold_keyframe(static_cast<int>(attack), get(V::FpsCamera) != 0);
        set(V::DisabledBones, harry::kLowerBodyBones);
        r_.pose_keyframes(kf, kf, 0);
    }
    // The torso leans to the aim's pitch; the arms follow it as its children.
    const std::int32_t pitch = std::clamp<std::int32_t>(static_cast<std::int32_t>(get(V::AimPitch)) - 1024, -640, 640);
    r_.rotate_bone(harry::bone::kTorso, 0, pitch >> 1, 0, true);
}

void Player::rear_look() {
    constexpr std::int32_t kOverShoulder = 967;  // 85 degrees
    const bool looking = get(V::AltCamera) != 0 && get(V::FpsCamera) == 0 && get(V::RearLook) != 0;
    const std::int32_t target = looking ? kOverShoulder : 0;
    const std::int32_t step = pm::per_tick_30(static_cast<std::int32_t>(get(V::DeltaTime)), 159);  // 14 degrees
    const std::int32_t diff = std::clamp(target - rear_head_yaw_, -step, step);
    rear_head_yaw_ = static_cast<std::int16_t>(rear_head_yaw_ + diff);
    if (rear_head_yaw_ != 0) {
        r_.rotate_bone(harry::bone::kHead, 0, 0, rear_head_yaw_, true);
    }
}

std::int32_t Player::clip_duration(int clip_status, int keyframe) {
    const std::int64_t state = get(V::State);
    const int row = static_cast<int>(get(V::WeaponClip));
    const std::int32_t dt = static_cast<std::int32_t>(get(V::DeltaTime));
    const bool dying = get(V::Health) <= 0;

    // A dying pose of the map's reaction rows runs out with the idle timer, `rate` times as fast.
    auto run_out = [this, dt](std::int32_t rate) -> std::int32_t {
        set(V::AfkTimer, get(V::AfkTimer) - dt * rate);
        if (get(V::AfkTimer) < 0) {
            return 0;
        }
        set(V::AfkTimer, get(V::AfkTimer) - dt * rate);
        return static_cast<std::int32_t>(get(V::AfkTimer));
    };
    auto row_in = [this, row](const int (&rows)[2]) {
        const int s = r_.reaction_row(row).trigger;
        return s == rows[0] || s == rows[1];
    };

    if (state == st::kPinnedFront || state == st::kPinnedBack) {
        // The map's reaction table carries a Romper's pin (overlay.json "pin_reactions"), or the row was imported.
        const bool pin_map = r_.reaction_row_imported(row) || r_.map_rules().pin_reactions;
        if (!pin_map) {
            return 0;
        }
        if (row_in(kPinDeathRows) && dying) {
            return run_out(1);
        }
        return kPinnedDuration;
    }
    if (state == st::kOnFloorFront || state == st::kOnFloorBehind) {
        if (!r_.map_rules().floor_reactions && !r_.reaction_row_imported(row)) {
            return 0;
        }
        if (row_in(kFloorDeathRows) && dying) {
            return run_out(2);
        }
        return kOnFloorDuration;
    }

    const Clips& k = clips();
    pm::VariableClip clip = pm::VariableClip::Other;
    if (clip_status == status(k.walk_forward, true)) {
        clip = pm::VariableClip::WalkForward;
    } else if (clip_status == status(k.run_forward, true)) {
        clip = pm::VariableClip::RunForward;
    } else if (clip_status == status(k.walk_backward, true)) {
        clip = pm::VariableClip::WalkBackward;
    } else if (clip_status == status(k.idle_exhausted, true)) {
        clip = pm::VariableClip::IdleExhausted;
    }
    const pm::StickState stick{static_cast<std::int32_t>(get(V::StickX)), static_cast<std::int32_t>(get(V::StickY)),
                               static_cast<std::int32_t>(get(V::StickHold)), r_.stick_radius()};
    return pm::legs_clip_duration(clip, stick, keyframe, static_cast<std::int32_t>(get(V::Health)),
                                  static_cast<std::int32_t>(get(V::Exhaustion)));
}

}  // namespace sh::game
