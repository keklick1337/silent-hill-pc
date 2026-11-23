// SPDX-License-Identifier: GPL-3.0-or-later
// The combat effects of the map's effect slot table: what a hit, a
// dying body and a gunshot leave in the world, and how each slot of it is drawn every frame.
//
//   kind 1        blood spray    a puff of blood at the wound that follows its character for a moment
//   kind 2        blood drop     a drop that falls to the ground, splats and spreads into a puddle
//   kinds 3, 4    blood pool     the pool spreading under a dead body (3) or at a wound on the floor (4)
//   kinds 15..19  muzzle flash   a fan of six quads spinning at the muzzle of a gun for a few frames
//   kinds 20..22  gun smoke      the puffs rising from a shot, carried by the wind
//   kind 31       hit spark      a one-frame glow where a hit landed
//
// Every number, the order of the random draws, the 16-bit truncation of the table words and the ordering-table
// buckets are the original's, with the engine's fixes on top (bucket clamps, the fog fade of the blood layers, the
// four-layer spray, the dimmed muzzle smoke).
// The per-weapon numbers, the hit strengths and the body pool shapes are data: gamedata/effects/combat_effects.json.

#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "engine/effects/slot_effects.h"
#include "engine/math/fixed_geometry.h"
#include "formats/common/status.h"

namespace sh::effects {

// ---- data ------------------------------------------------------------------------------------------------------

// What a hit of one strength leaves: spray slots, ground splats (a fixed number, or a random number in
// [splats_min, splats_max] when splats_random), and the spray's size.
struct BloodHit {
    std::int32_t sprays = 0;
    bool splats_random = false;
    std::int32_t splats_min = 0;
    std::int32_t splats_max = 0;
    std::int32_t size = 0;
};

// One patch of a body pool: forward and sideways offsets from the body, each `base + rand % range` (Q12).
struct PoolPatch {
    std::int16_t forward_range = 1;
    std::int16_t forward_base = 0;
    std::int16_t side_range = 1;
    std::int16_t side_base = 0;
};

// The pool a dead body leaves for one death pose: its patches and their size (the size grows by up to a quarter).
struct BodyPool {
    std::uint8_t size = 0;
    std::vector<PoolPatch> patches;
};

// The two bone hierarchies a muzzle can sit on: the gun of whoever shot last, and a second gun (an NPC's).
enum class MuzzleBones : std::uint8_t { Weapon = 0, Second = 1 };

struct MuzzleFlash {
    std::int32_t life = 0;        // Q12 seconds the flash lasts
    std::int16_t length = 0;      // quad length along the barrel
    std::int16_t half_width = 0;
    std::int32_t swirl = 0;       // Q12 amplitude of the spin's wobble
    Vec3s muzzle;                 // muzzle point in the bone's space
    MuzzleBones bones = MuzzleBones::Weapon;
};

struct GunSmoke {
    std::int32_t drift_after = 0;  // Q12 age from which a puff drifts along its heading
    std::int32_t life = 0;         // Q12 age at which the puff is gone
};

// The light of a gun's muzzle flash for the modern lighting, for the guns of story characters that have no weapon
// file (a player weapon's own "muzzle_light" in gamedata/weapons/ wins). Rendering only.
struct GunLight {
    std::array<float, 3> color{1.0f, 0.75f, 0.45f};  // linear
    float intensity = 4.0f;
    float range = 6.0f;    // metres
    float radius = 0.03f;  // metres
    float fade = 0.5f;     // the share of the intensity gone at the end of the flash
    bool casts_shadows = true;
    bool gi = true;
};

struct Gun {
    MuzzleBones bones = MuzzleBones::Weapon;
    Vec3s muzzle;
    std::optional<std::int32_t> flash;  // index into CombatEffectParams::flashes (slot kind 15 + index)
    std::int32_t smoke = 0;             // index into CombatEffectParams::smoke (slot kind 20 + index)
    std::int32_t puffs = 0;
    // The flash's fixed orientation (yaw, pitch); absent: the shot's own aim.
    std::optional<std::array<std::int16_t, 2>> flash_angles;
    std::optional<GunLight> light;  // "muzzle_light"
};

struct CombatEffectParams {
    std::vector<BloodHit> blood_hits;  // by hit strength
    std::vector<BodyPool> body_pools;  // by death pose
    std::vector<MuzzleFlash> flashes;  // slot kinds 15..19
    std::vector<GunSmoke> smoke;       // slot kinds 20..22
    std::map<std::string, Gun, std::less<>> guns;
};

Result<CombatEffectParams> parse_combat_effect_params(std::string_view json_text);
Result<CombatEffectParams> load_combat_effect_params(const std::filesystem::path& gamedata_dir,
                                                     const std::filesystem::path& mods_dir, std::string* source);

// ---- the effect anchors ------------------------------------------------------------------------------------------

// An effect anchor: a character whose motion the blood of its slots follows. `refs` counts what still follows it:
// bit 0 a spray, every 2 a falling drop.
struct EffectAnchor {
    std::uint8_t refs = 0;
    std::uint8_t chara = 0;  // 6 = the player, else the NPC slot
    std::int16_t y = 0;      // last followed position, Q12
    std::int32_t x = 0;
    std::int32_t z = 0;
};
constexpr int kEffectAnchors = 24;
// The anchor index of a slot that follows nothing.
constexpr int kNoAnchor = 24;

// The motion of each anchor this frame (what the drawers add to their slots).
struct AnchorMotion {
    std::array<std::int16_t, kEffectAnchors> x{};
    std::array<std::int16_t, kEffectAnchors> y{};
    std::array<std::int16_t, kEffectAnchors> z{};
};

// The anchors live in the engine.
std::array<EffectAnchor, kEffectAnchors>& effect_anchors();

// ---- host ------------------------------------------------------------------------------------------------------

struct GroundSample {
    std::int32_t height = 0;
    std::int32_t type = 0;
};

struct FogColors {
    draw3d::Rgb fog;   // the glow toward the fog colour
    draw3d::Rgb tint;  // the darkened tint
};

class CombatEffectHost : public SlotEffectHost {
public:
    // The extra blood colour option (14 = the green "no blood" setting).
    virtual std::int32_t blood_option() = 0;
    // The map light colours effects through the fog (neither of its two dark modes is on).
    virtual bool fog_lit() = 0;
    virtual FogColors fog_colors(std::uint8_t level, std::int32_t depth) = 0;
    virtual draw3d::Rgb faded_tint(std::uint8_t level) = 0;
    // How much of a blood layer survives the world fog at `depth` (0..256).
    virtual std::int32_t fog_keep(std::int32_t depth) = 0;
    // The ground at a point (the query may change the projection's matrices; restore_view puts them back).
    virtual GroundSample surface(std::int32_t x, std::int32_t z) = 0;
    virtual Vec3 camera_position() = 0;
    // The camera's view direction on the ground plane (the z column of its frame, x and z rows), Q12.
    virtual std::array<std::int16_t, 2> camera_forward() = 0;
    virtual bool raining() = 0;
    virtual bool snowing() = 0;
    virtual std::array<std::int32_t, 2> wind() = 0;  // x, z
    // NPC slot `index` (6: past the NPCs, as the original read it) holds a character.
    virtual bool npc_present(int index) = 0;
    // A muzzle flash lights the scene this frame.
    virtual void flash_light() = 0;
    // A muzzle flash (index into CombatEffectParams::flashes) of effect slot `slot` is drawn at `muzzle` (world),
    // `age` of its `life` (Q12 seconds): the modern lighting's light at the muzzle. Rendering only.
    virtual void flash_shown(int slot, int flash, const Vec3& muzzle, std::int32_t age, std::int32_t life) {
        (void)slot, (void)flash, (void)muzzle, (void)age, (void)life;
    }

    virtual EffectAnchor anchor(int index) = 0;
    virtual void store_anchor(int index, const EffectAnchor& a) = 0;
    virtual const AnchorMotion& motion() = 0;

    virtual int blood_splat_count() = 0;
    virtual std::int32_t blood_splat_slot(int index) = 0;  // -1 = free
    virtual void set_blood_splat(int index, std::int32_t slot) = 0;

    // The world frame of a muzzle's bone (the matrix of bone 10 of the hierarchy), and a point transformed by it
    // the way the game did (the geometry state keeps that matrix afterwards).
    virtual math::Transform bone_frame(MuzzleBones bones) = 0;
    virtual Vec3 transform(const math::Transform& frame, const Vec3s& v) = 0;

    // Projects the corners of a quad that the original wrote three of straight into its packet.
    virtual void project_quad(const Vec3s relative[4], ScreenPoint out[4]) = 0;
    // A quad of the scene's ordering table at `bucket`; `bind`: the corners take the projection records stored
    // under the packet's corner fields.
    virtual void emit_quad(const draw3d::Polygon& polygon, std::int32_t bucket, bool bind) = 0;
};

// ---- the effects ---------------------------------------------------------------------------------------------------

namespace combat_kind {
constexpr std::uint8_t kBloodSpray = 1;
constexpr std::uint8_t kBloodDrop = 2;
constexpr std::uint8_t kBodyPool = 3;
constexpr std::uint8_t kWoundPool = 4;
constexpr std::uint8_t kMuzzleFlash = 15;  // ..19
constexpr std::uint8_t kGunSmoke = 20;     // ..22
constexpr std::uint8_t kHitSpark = 31;
}  // namespace combat_kind

// Who bleeds: the anchor the blood follows (the character's position plus its collision box offset) and its type's
// blood traits (character.json "blood_follows_body", "blood_depth_bias").
struct Bleeder {
    std::uint8_t chara = 0;  // the anchor's character (6 = the player)
    Vec3 anchor;             // Q12 (y as the 16-bit anchor word takes it)
    bool follows_body = true;     // false: its blood follows nothing (the Split Head)
    std::uint8_t depth_bias = 32;  // the sprays' depth bias (96: the Floatstinger's sit further back)
};

class CombatEffects {
public:
    // A hit of `strength` (0..9) at `at` (Q12) on `who`, blood colour kind `color`.
    static void spawn_blood(CombatEffectHost& host, const CombatEffectParams& params, const Bleeder& who,
                            const Vec3& at, std::int32_t strength, std::int32_t color);
    // The pool under a body in death pose `pose` at `at` facing `yaw`; `npc` is the body's NPC slot.
    static void spawn_body_pool(CombatEffectHost& host, const CombatEffectParams& params, const Vec3& at,
                                std::int16_t yaw, std::uint8_t npc, std::int32_t pose, std::int32_t color);
    // A pool spreading at a wound on the floor at `at`, final size `size`, after `delay` (5 s units).
    static void spawn_wound_pool(CombatEffectHost& host, const Vec3& at, std::int16_t size, std::int32_t color,
                                 std::int32_t delay);
    // The flash and smoke of a shot of `gun` aimed at (yaw, pitch); the bones are bound by the host first.
    static void spawn_shot(CombatEffectHost& host, const Gun& gun, std::int16_t yaw, std::int16_t pitch);
    // A hit spark at `at` (Q12) of `size`, `variant` kept for the slot.
    static void spawn_spark(CombatEffectHost& host, const Vec3& at, std::uint8_t variant, std::int16_t size);

    // The per-frame drawers; true when something was drawn.
    static bool draw_spray(CombatEffectHost& host, int slot);
    static bool draw_drop(CombatEffectHost& host, int slot);
    static bool draw_pool(CombatEffectHost& host, int slot);
    static bool draw_flash(CombatEffectHost& host, const CombatEffectParams& params, int slot);
    static bool draw_smoke(CombatEffectHost& host, const CombatEffectParams& params, int slot);
    static bool draw_spark(CombatEffectHost& host, int slot);
};

// The last extra blood colour setting: sprays age four times as fast in colour slot 5 (or the place's darkness), a
// hit leaves no splats and a body no pool.
constexpr std::int32_t kShortBloodOption = 14;

}  // namespace sh::effects
