// SPDX-License-Identifier: GPL-3.0-or-later
// The map's effect frame: the slot table's allocator, the
// clearing on map load and state reset, and the per-frame step that sets up the effect view, advances the
// running map effects' frame numbers (fire, smoke column), tracks the characters' effect anchors, and hands
// every busy slot to the drawer of its kind.
//
//  The drawers a map names in
// overlay.json are reached through MapEffectFrameHost::draw or run here (the engine's slot
// effects, slot_effects.h); the records (the table, the parameter blocks, the anchors) stay where the game keeps
// them and are read and written through the host.

#pragma once

#include <array>
#include <cstdint>

#include "engine/effects/combat_effects.h"
#include "engine/effects/slot_effects.h"

namespace sh::effects {

// Who draws a slot kind (the original's dispatch, kinds 0..35).
enum class SlotDrawer : std::uint8_t {
    None,
    BloodSpray,      // 1      (engine CombatEffects)
    BloodDrop,       // 2      (engine CombatEffects)
    BloodPool,       // 3, 4   (engine CombatEffects)
    MuzzleFlash,     // 15..19 (engine CombatEffects)
    GunSmoke,        // 20..22 (engine CombatEffects)
    HitSpark,        // 31     (engine CombatEffects)
    Flame,           // 8, 10  (engine ParticleEmitter)
    FireSmoke,       // 9, 11  (engine ParticleEmitter)
    SplitHeadDrool,  // 7      (engine SplitHeadEffects)
    SplitHeadDrag,   // 5      (engine SplitHeadEffects)
    SmokeWait,       // 13     (engine SmokeColumn)
    SmokePuff,       // 14     (engine SmokeColumn)
    Glass,           // 23..26
    WaterDrop,       // 27
    Drip,            // 28     (engine Drips)
    Script,          // 32
    AcidSpit,        // 33
    AcidDrop,        // 34
    LightGlow,       // 35     (engine LightGlow)
};

SlotDrawer slot_drawer(std::uint8_t kind);

class MapEffectFrameHost : public CombatEffectHost {
public:
    // Sets the effect view at the player's whole-metre position (Q12) and fills the view record's distance to
    // the projection plane and view angles.
    virtual void begin_view(const Vec3& origin, SlotView* view) = 0;
    // Publishes the frame's view and the anchors' motion where the drawers read them (view() returns the
    // published view; before the first publish of the frame, the last frame's).
    virtual void publish_view(const SlotView& view) = 0;
    virtual void publish_motion(const AnchorMotion& motion) = 0;
    virtual std::uint32_t effect_flags() = 0;
    virtual void set_effect_flags(std::uint32_t flags) = 0;
    // The drawers share one run of packet memory, from draws_begin to draws_end.
    virtual void draws_begin() = 0;
    virtual void draws_end() = 0;

    // The point of a character the anchors follow (position plus its collision box offset), Q12.
    virtual Vec3 anchor_target(std::uint8_t chara) = 0;

    // The water's per-frame direction tables and its ribbon (effect flag bit 3).
    virtual void water_frame(const SlotView& view) = 0;
    // Runs a drawer that is not the engine's.
    virtual void draw(SlotDrawer drawer, int slot) = 0;
    // The map declared this drawer (overlay.json effect_draws).
    virtual bool declared(SlotDrawer drawer) = 0;

    // The blood and gunfire numbers (null: the file did not load; flashes and smoke are not drawn).
    virtual const CombatEffectParams* combat_params() = 0;
    // The lake surface (effect flag bit 5).
    virtual void lake() = 0;
    // Turns the flashlight off where the map has none, outside the system menus.
    virtual void flashlight_check() = 0;
    // The map's spawn overrides and its world objects' set-up, at the end of the effects' init.
    virtual void init_world_objects() = 0;
};

// The slot table and its allocation cursor, for the allocator.
class SlotTable {
public:
    virtual ~SlotTable() = default;
    virtual int count() = 0;
    virtual std::uint8_t kind(int slot) = 0;
    virtual void set_kind(int slot, std::uint8_t kind) = 0;
    virtual std::int32_t cursor() = 0;
    virtual void set_cursor(std::int32_t cursor) = 0;
};

class MapEffectFrame {
public:
    // Takes a free slot for `kind`, the search starting after the last one taken; -1 when the table is full.
    static int alloc(SlotTable& table, std::uint8_t kind);
    // A map was loaded: every slot and blood splat free, the cursor and the flags cleared, then the map's own
    // set-up.
    static void init(MapEffectFrameHost& host, SlotTable& table);
    // The effects that do not survive a state reset (the blood and chunk effects) are freed, the flags cleared.
    static void reset(MapEffectFrameHost& host);
    static void update(MapEffectFrameHost& host);
};

}  // namespace sh::effects
