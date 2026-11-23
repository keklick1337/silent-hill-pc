// SPDX-License-Identifier: GPL-3.0-or-later
// The map effect frame and the slot effects (engine/effects/map_effect_frame.h, slot_effects.h) over the running
// game: the frame's host (the view it publishes, the anchors' targets, the drawers of the kinds the engine's slot
// effects do not cover), the slot table over sh::maps::MapRuntime, and the entry points the effects a map draws
// (overlay.json "effect_draws") and the Lua binds call.

#include <cmath>
#include <cstdint>

#include "engine/maps/map_globals.h"
#include "engine/camera/camera_host.h"
#include "engine/camera/view_space.h"
#include "engine/effects/game/slot_hosts.h"
#include "engine/effects/effect_view.h"
#include "engine/effects/map_effect_frame.h"
#include "engine/effects/map_effect_view.h"
#include "engine/scripting/render_binds.h"
#include "render/gpu/render_queries.h"

extern "C" {
// The frame's effect flags and the slot table's allocation cursor (map_effect_view.h).
std::int8_t g_MapEffectFlags = 0;
std::int16_t g_MapEffectSlotCursor = 0;

// Story-driven spawn data of the map at world-object init and the map's world objects.
void MapData_SpawnOverridesApply(void);
void Map_WorldObjectsInit(void);
// The drawers of the kinds a map names outside slot_effects.h (water_bridge.cpp, particle_bridge.cpp,
// engine/scripting/render_binds.cpp, lake_surface_host.cpp); each steps the packet cursor it is given. Their 4-byte C
// boolean results are not read.
std::int32_t WaterStream_DropDraw(unsigned char** cursor, std::int32_t slot);
void WaterStream_Ribbon(void);
std::int32_t Glass_Draw(unsigned char** cursor, std::int32_t slot);
std::int32_t Acid_SpitDraw(unsigned char** cursor, std::int32_t slot);
std::int32_t Acid_DropDraw(unsigned char** cursor, std::int32_t slot);
void LakeSurface_Draw(void);
}


namespace {

namespace fx = sh::effects;
namespace hosts = sh::effects::slot_hosts;
using sh::maps::MapEffect;

using SlotHost = hosts::SlotHostT<fx::SlotEffectHost>;

fx::MapEffectView s_view;

// The map effect frame's host: the slot effects' host plus the frame's view, anchors and flags, and the drawers of
// the kinds that are not the engine's.
class FrameHost final : public hosts::CombatHostT<fx::MapEffectFrameHost> {
public:
    FrameHost() : CombatHostT(&cursor_) {}

    void begin_view(const fx::Vec3& origin, fx::SlotView* view) override {
        s_view.world_screen = sh::camera::world_screen_at(sh::camera::camera_host().world_screen(),
                                                          sh::camera::Vec3{origin.x, origin.y, origin.z});
        // The engine slot drawers take this view from the geometry state (sh::effects::drawer::set_current), and the
        // other drawers project with it.
        sh::effects::drawer::load_view(&s_view.world_screen);
        const sh::camera::PackedAngles& angles = sh::camera::view_system().angles();
        float authored = 0.0f, wide = 0.0f;
        const bool hor_plus = GR_HorPlusHalfWidths(&authored, &wide) != 0;
        fx::set_screen_box_gain(hor_plus ? static_cast<std::int32_t>(std::ceil(wide - authored)) : 0);
        view->screen_distance = sh::effects::drawer::screen_distance();
        view->pitch = angles.x;
        view->yaw = angles.y;
    }

    void publish_view(const fx::SlotView& v) override {
        s_view.view = v;
        view_ = v;
    }

    void publish_motion(const fx::AnchorMotion& m) override { motion_ = m; }

    std::uint32_t effect_flags() override { return static_cast<std::uint8_t>(g_MapEffectFlags); }
    void set_effect_flags(std::uint32_t flags) override { g_MapEffectFlags = static_cast<std::int8_t>(flags); }
    void draws_begin() override { cursor_ = GsOUT_PACKET_P; }
    void draws_end() override { GsOUT_PACKET_P = cursor_; }

    // The anchors name the player after the last NPC (combat_effects_host.cpp, anchor_index). The console's anchor 6
    // was the player when it had six NPC slots; reading 6 as the player and the player's anchor as an NPC read the
    // bone memory past the NPC table, so blood on Harry followed whatever lay there.
    fx::Vec3 anchor_target(std::uint8_t chara) override {
        const sh::entities::CharacterSlots& slots = sh::game::session().system.characters;
        if (chara < sh::entities::kNpcSlotCount) {
            return hosts::anchor_point(slots.npcs[chara]);
        }
        return hosts::anchor_point(slots.player);
    }

    void water_frame(const fx::SlotView& v) override {
        const auto& w = *static_cast<const fx::WaterStreamBlock*>(
            sh::maps::map_runtime().effect_block(sh::maps::EffectBlock::Water));
        fx::WaterSegmentTables& t = s_view.water;
        for (int i = 0; i < w.count; i++) {
            const auto k = static_cast<std::size_t>(i);
            const std::int32_t half = w.spread[i] >> 1;
            if (w.mode[i] == 0) {
                const std::int32_t s = sh::world::fixed_sin(w.angle[i]);
                const std::int32_t c = sh::world::fixed_cos(w.angle[i]);
                t.sin[k] = static_cast<std::int16_t>(s);
                t.cos[k] = static_cast<std::int16_t>(c);
                t.ec[k] = half * s;
                t.fc[k] = half * c;
                t.far_x[k] = w.ramp[i] * s;
                t.far_z[k] = w.ramp[i] * c;
            } else {
                // A straight fall: the ribbon faces the camera (the halves are its left and right corners).
                const std::int32_t left = v.yaw - kQuarterTurn;
                const std::int32_t right = v.yaw + kQuarterTurn;
                fx::set_lo16(reinterpret_cast<std::uint32_t&>(t.ec[k]), (half * sh::world::fixed_sin(left)) >> 12);
                fx::set_hi16(reinterpret_cast<std::uint32_t&>(t.ec[k]), (half * sh::world::fixed_sin(right)) >> 12);
                fx::set_lo16(reinterpret_cast<std::uint32_t&>(t.fc[k]), (half * sh::world::fixed_cos(left)) >> 12);
                fx::set_hi16(reinterpret_cast<std::uint32_t&>(t.fc[k]), (half * sh::world::fixed_cos(right)) >> 12);
            }
        }
        if (sh::maps::map_runtime().draws(MapEffect::Water)) WaterStream_Ribbon();
    }

    void draw(fx::SlotDrawer drawer, int slot) override {
        using D = fx::SlotDrawer;
        const sh::maps::MapRuntime& map = sh::maps::map_runtime();
        switch (drawer) {
            case D::Glass: if (map.draws(MapEffect::GlassShards)) Glass_Draw(&cursor_, slot); break;
            case D::WaterDrop: if (map.draws(MapEffect::Water)) WaterStream_DropDraw(&cursor_, slot); break;
            case D::Script:
                if (map.draws(MapEffect::Script)) {
                    const auto& origin = fx::map_effect_view().view.origin;
                    sh::script::draw_effect_slot_script(slot, origin.x, origin.y, origin.z);
                }
                break;
            case D::AcidSpit: if (map.draws(MapEffect::Acid)) Acid_SpitDraw(&cursor_, slot); break;
            case D::AcidDrop: if (map.draws(MapEffect::Acid)) Acid_DropDraw(&cursor_, slot); break;
            default: return;
        }
    }

    bool declared(fx::SlotDrawer drawer) override {
        using D = fx::SlotDrawer;
        const sh::maps::MapRuntime& map = sh::maps::map_runtime();
        switch (drawer) {
            case D::Flame:
            case D::FireSmoke: return map.draws(MapEffect::ParticleEmitter);
            case D::SplitHeadDrool:
            case D::SplitHeadDrag: return map.draws(MapEffect::SplitHead);
            case D::SmokeWait:
            case D::SmokePuff: return map.draws(MapEffect::SmokeColumn);
            case D::Drip: return map.draws(MapEffect::Drips);
            case D::LightGlow: return map.draws(MapEffect::LightGlow);
            default: return true;
        }
    }

    const fx::CombatEffectParams* combat_params() override { return hosts::combat_params(); }
    void lake() override {
        if (sh::maps::map_runtime().draws(MapEffect::LakeSurface)) LakeSurface_Draw();
    }
    void flashlight_check() override {
        sh::game::Session& s = sh::game::session();
        if (s.system.environment.flashlight_unavailable && s.sys_state() == 0) {
            fx::map_lighting().set_flashlight(false);
        }
    }
    void init_world_objects() override {
        // The map's story-driven spawn data, applied where the original's per-map init wrote the spawn table.
        MapData_SpawnOverridesApply();
        Map_WorldObjectsInit();
    }

private:
    static constexpr std::int32_t kQuarterTurn = 1024;

    unsigned char* cursor_ = nullptr;
};

class SlotTableImpl final : public fx::SlotTable {
public:
    int count() override { return sh::maps::map_runtime().effect_slot_count(); }
    std::uint8_t kind(int slot) override { return sh::maps::map_runtime().effect_slots()[slot].kind; }
    void set_kind(int slot, std::uint8_t kind) override { sh::maps::map_runtime().effect_slots()[slot].kind = kind; }
    std::int32_t cursor() override { return g_MapEffectSlotCursor; }
    void set_cursor(std::int32_t cursor) override { g_MapEffectSlotCursor = static_cast<std::int16_t>(cursor); }
};

// For the entry points that draw nothing: no packet memory is touched.
unsigned char* s_noPackets = nullptr;

}  // namespace

fx::MapEffectView& fx::map_effect_view() { return s_view; }

int fx::effect_slot_count() { return sh::maps::map_runtime().effect_slot_count(); }

fx::EffectSlot fx::load_effect_slot(int slot) {
    const sh::maps::EffectSlotRecord& r = sh::maps::map_runtime().effect_slots()[slot];
    return fx::EffectSlot{r.x, r.z, r.y, r.kind, r.flags, r.a, r.b};
}

void fx::store_effect_slot(int slot, const fx::EffectSlot& s) {
    sh::maps::EffectSlotRecord& r = sh::maps::map_runtime().effect_slots()[slot];
    r.x = s.x;
    r.z = s.z;
    r.y = s.y;
    r.kind = s.kind;
    r.flags = s.flags;
    r.a = s.a;
    r.b = s.b;
}

int hosts::alloc_slot(std::uint8_t kind) {
    SlotTableImpl table;
    return fx::MapEffectFrame::alloc(table, kind);
}

extern "C" {

// ---- the map effect frame ----------------------------------------------------------------------------------------

std::int32_t MapOverlay_SlotAlloc(std::int32_t kind) {
    // -1 (the game's "no value") when the table is full.
    return hosts::alloc_slot(static_cast<std::uint8_t>(kind));
}

void Map_EffectsInit(std::int32_t mapId) {
    (void)mapId;
    FrameHost host;
    SlotTableImpl table;
    fx::MapEffectFrame::init(host, table);
}

void Map_EffectStatesReset(void) {
    FrameHost host;
    fx::MapEffectFrame::reset(host);
}

void Gfx_2dEffectsUpdate(void) {
    FrameHost host;
    fx::MapEffectFrame::update(host);
}

// ---- drips (map1_s03) --------------------------------------------------------------------------------------------

void SlotFx_DripsStart(std::int32_t source) {
    SlotHost host(&s_noPackets);
    fx::Drips::start(host, source);
}

// ---- light glow --------------------------------------------------------------------------------------------------

void SlotFx_LightGlowSpawn(const std::int32_t* at, std::uint16_t size, std::uint16_t spread, std::uint32_t flags) {
    SlotHost host(&s_noPackets);
    fx::LightGlow::spawn(host, fx::Vec3{at[0], at[1], at[2]}, size, spread, flags);
}

// ---- smoke column (map1_s01, map6_s04) ---------------------------------------------------------------------------

void SlotFx_SmokeStart(void) {
    SlotHost host(&s_noPackets);
    fx::SmokeColumn::start(host);
}

// ---- particle emitter (map1_s05, map3_s05, map4_s01) -------------------------------------------------------------

void SlotFx_EmitterStart(void) {
    SlotHost host(&s_noPackets);
    fx::ParticleEmitter::start(host);
}

// ---- Split Head (map1_s05, map1_s06) -----------------------------------------------------------------------------

void SlotFx_DroolSpawn(std::int32_t x, std::int32_t y, std::int32_t z, std::int32_t speed, std::int32_t heading) {
    SlotHost host(&s_noPackets);
    fx::SplitHeadEffects::spawn_drool(host, x, y, z, speed, heading);
}

void SlotFx_DragSpawn(std::int32_t row, std::int32_t brightness, std::int32_t x0, std::int32_t y0, std::int32_t z0,
                      std::int32_t x1, std::int32_t y1, std::int32_t z1) {
    SlotHost host(&s_noPackets);
    fx::SplitHeadEffects::spawn_drag(host, row, brightness, fx::Vec3{x0, y0, z0}, fx::Vec3{x1, y1, z1});
}

}  // extern "C"

namespace {

// The map variables name the flags (map_vars "map_effect_flags").
[[maybe_unused]] const bool g_map_globals_bound = [] {
    sh::maps::map_globals().bind("map_effect_flags", &g_MapEffectFlags);
    return true;
}();

}  // namespace
