// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The player's live values by name: what sh::game::Player and Harry's Lua
// (gamedata/chara/Harry/*.lua, sh.entity.harry / set_harry) read and write. Units are the game's: positions Q12
// metres (+Y down), angles Q12 turns, times Q12 seconds, health Q12. Every value is an integer; a store keeps
// the low bits of the field behind it, like an assignment in the game.
//
// The storage is the game's records: PlayerRecord (player_record.h) is implemented over them by the player's
// host.

#pragma once

#include <cstdint>
#include <string_view>

namespace sh::game {

enum class PlayerVar : std::uint8_t {
    // Body states (Harry's state, upper and lower body state numbers) and the upper body's own clip.
    LowerBody, UpperBody, State, UpperAnimStatus, UpperAnimKeyframe, UpperAnimTime, UpperStateStep,
    UpperControlState, UpperAnimFlags, UpperAnimAlpha,
    // The body's (legs') clip and its behaviour steps.
    BodyAnimStatus, BodyAnimKeyframe, BodyAnimTime, BodyAnimFlags, BodyStateStep, BodyControlState,
    // Bones the next clip playback leaves alone (bit per bone).
    DisabledBones,
    StopFlags, Aiming, WeaponAttack, TargetNpc, WalkDistance, RunDistance, TurnSpeed, StepSpeed, StickHold,
    WallDistance, WallGroundType, HeadingOffset, WalkRunBlend, IntendedStep, PrevX, PrevY, PrevZ,
    DistanceCounted, FloorGroundType, FloorHeight, WeaponClip, WeaponAnimAimStop, WeaponAnimAttack,
    WeaponAnimAttackHold, RockDrillDirection, OptionWalkRun, OptionWeaponControl, OptionRetreatTurn, AltCamera,
    // Combat.
    Ammo, AmmoReserve, WeaponSlot, AttackX, AttackY, AttackZ, LockedTarget, TargetPitch, TargetYaw,
    HitWindowStart, HitWindowEnd, MeleeAttackType, Multitap, AttackClip, MeleeClickQueue, MeleeNeedsRelease,
    PrevAltFire, FreeAimActive, FreeAimState, FreeAimStuck, FreeAimFireHeld, FreeAimRefire, FreeAimReleased, AimResume,
    AfkFraction, PrevAttackX, PrevAttackY, PrevAttackZ, TargetEnemyX, TargetEnemyY, TargetEnemyZ,
    ReloadRequest, ReloadStartKeyframe, ReloadEndKeyframe, PoweredAimKeyframe, WeaponClipIndex,
    WeaponReloadSoundKeyframe, ShotYaw, ShotPitch, PostFireTrace, FlexX, Timer2C, OptionAutoAim, FpsCamera,
    Control2d, Demo, InfiniteAmmo, AutoReload, OptionAutoReload, AimAssist, AltFireHeld, EquippedGun,
    EquippedWeapon, LastWeapon,
    // Damage, grabs and reactions.
    PendingAttack, Dead, DamageDisabled, GodMode, GrabReleaseTimer, PinTargetX, PinTargetZ, FlagTimerLimit,
    Saturation, Difficulty, MapIndex, RoomIndex, Unequipped, GrabberFront, GrabberBack, GrabberUnused2,
    GrabberUnused3, RearLook, StuckState, StuckBodyKeyframe, StuckUpperKeyframe, StuckStill, StuckTotal,
    PrevRotY, PrevRotValid,
    // Scenes.
    CutsceneMove, CutsceneStop, FlexRotY, MatchAnim, ControlDisabled, PathStep, PushX, PushZ,
    // The controls of this tick (the controller's flags, chara/Harry/controller.lua).
    ActionInput, MoveInput, ShootInput, AttackInput, HoldAttackInput, AimInput, RunInput, MoveForward,
    MoveBackward, StepLeftTap, StepLeftHold, StepRightTap, StepRightHold, TurnLeft, TurnRight,
    // The character itself: transform, motion, health, its flags and attack links.
    PositionX, PositionY, PositionZ, RotationY, MoveSpeed, Heading, FallSpeed, Health, CharaFlags,
    AngleToTarget, Attacker, AttackReceived,
    // Harry's own block: idle time, ground, flex target, run steps, tiredness, aim hold, attack time, the
    // run's free distance, step sound class, stun timer, gas weapon timer, flags, quick turn heading, aim pitch,
    // heading and speed the legs ask for.
    AfkTimer, GroundHeight, FlexTargetX, RunSteps, Exhaustion, AimHold, AttackTime, RunAhead, StepSound,
    FlagTimer, GasWeaponTimer, PlayerFlags, QuickTurnHeading, AimPitch, LegsHeading, LegsMoveSpeed,
    // Collision shape: cylinder radius and height offset, box top, bottom and Y offset, the weapon's reach,
    // the shape offsets, the collision state.
    CylinderRadius, CylinderHeight, BoxTop, BoxBottom, BoxOffsetY, HandReach, CylinderOffsetX, CylinderOffsetZ,
    BoxOffsetX, BoxOffsetZ, CollisionState,
    // The map's animation file loaded for Harry (-1 none).
    MapAnimIndex,
    // Read-only inputs of the tick: game time of the tick (Q12 s), the movement stick (-128..127), debug
    // switches (no wall / no floor collision).
    DeltaTime, StickX, StickY, DebugNoWalls, DebugNoFloor,
    // The pad: buttons held, the raw left stick (0..255, 128 centred), the binds of run, aim and action.
    PadHeld, AnalogLeftX, AnalogLeftY, BindRun, BindAim, BindAction,
    // PC settings: the original movement machine, 2D control snapping, button-only sprint, hold-to-aim; the
    // orbit camera's yaw; a quick turn asked by its button.
    MovementOriginal, Control2dSnap, AltButtonSprint, ForceHoldAim, OrbitYaw, QuickTurnRequest,
    Count
};

// Lua name of a value ("lower_body"), whether Lua may write it; nullptr for values Lua does not see.
struct PlayerVarName {
    const char* name;
    PlayerVar var;
    bool writable;
};
const PlayerVarName* player_var_find(std::string_view name);

}  // namespace sh::game
