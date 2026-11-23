// SPDX-License-Identifier: GPL-3.0-or-later
// The map's effect frame (map_effect_frame.h).

#include "engine/effects/map_effect_frame.h"

#include <algorithm>

namespace sh::effects {

namespace {

constexpr std::uint32_t kFlagEmitter = 1u << 0;
constexpr std::uint32_t kFlagSmoke = 1u << 1;
constexpr std::uint32_t kFlagWater = 1u << 3;
constexpr std::uint32_t kFlagLake = 1u << 5;
// The fastest an effect anchor follows its character: 1.25 m/s.
constexpr std::int32_t kAnchorSpeed = 5120;

std::int32_t rand_below(SlotEffectHost& host, std::uint32_t n) {
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(host.rand16()) % n);
}

std::int32_t clamp(std::int32_t v, std::int32_t lo, std::int32_t hi) { return v >= hi ? hi : (v < lo ? lo : v); }

void emitter_frame(MapEffectFrameHost& host, SlotView& view) {
    const EmitterParams p = host.emitter_params();
    const std::int32_t dt = host.delta_time();
    view.rise = static_cast<std::int16_t>(mul12_wide(dt, p.speed));
    view.fade_begin = static_cast<std::int16_t>(p.fade_phase * 16);
    view.fade_amp = static_cast<std::int16_t>(p.fade_amp * 16);
    view.fade_base = static_cast<std::int16_t>((0x100 - p.fade_amp) * 16);
    // An emitter without height gets no phase step (the console's divide gave garbage there).
    view.age_step = static_cast<std::int16_t>(p.height != 0 ? (dt * p.speed) / p.height : 0);
    view.swirl_rate = static_cast<std::int16_t>(static_cast<std::uint16_t>(p.swirl) >> 2);
    view.offset_x = p.ember_x;
    view.offset_z = p.ember_z;
    view.spark_size = p.spark_size * (2 * view.screen_distance);
}

// The fire's mode: 0 brings idle particles back, 1 flares the level up, 2 puts every flame out, 3 dies down.
void emitter_mode(MapEffectFrameHost& host) {
    EmitterParams p = host.emitter_params();
    const std::int32_t dt = host.delta_time();
    const int count = host.slot_count();
    const bool fire = host.declared(SlotDrawer::Flame);
    switch (p.mode) {
        case 0:
            for (int i = 0; i < count; i++) {
                if (host.load(i).kind == slot_kind::kEmitterIdle && fire) ParticleEmitter::respawn(host, i, false);
            }
            break;
        case 1:
            p.level = static_cast<std::int16_t>(std::min(p.level + ((dt * p.level_speed) >> 5), 4096));
            host.store_emitter_params(p);
            break;
        case 2:
            for (int i = 0; i < count; i++) {
                EffectSlot s = host.load(i);
                if (s.kind >= slot_kind::kFlame && s.kind <= slot_kind::kEmber) {
                    s.kind = slot_kind::kEmitterIdle;
                    host.store(i, s);
                }
            }
            break;
        case 3:
            p.level = static_cast<std::int16_t>(std::max(p.level - ((dt * p.level_speed) >> 5), 0));
            host.store_emitter_params(p);
            for (int i = 0; i < count; i++) {
                if (host.load(i).kind == slot_kind::kEmitterIdle && rand_below(host, 4096) > p.level &&
                    host.rand16() % 16 != 0 && fire) {
                    ParticleEmitter::respawn(host, i, true);
                }
            }
            break;
        default:
            break;
    }
}

void smoke_frame(MapEffectFrameHost& host) {
    SmokeColumnParams p = host.smoke_params();
    if (p.fade_delay > 0) {
        p.fade_delay = std::max(p.fade_delay - host.delta_time(), 0);
    } else {
        p.fade = static_cast<std::int16_t>(std::min(mul12_wide(host.delta_time(), p.fade_rate) + p.fade, 1024));
    }
    host.store_smoke_params(p);
}

void follow_anchors(MapEffectFrameHost& host) {
    AnchorMotion motion;
    const std::int32_t step = mul12_wide(host.delta_time(), kAnchorSpeed);
    for (int i = 0; i < kEffectAnchors; i++) {
        EffectAnchor a = host.anchor(i);
        if (a.refs == 0) continue;
        const Vec3 t = host.anchor_target(a.chara);
        // Rising is limited to a sixteenth of the speed.
        motion.x[static_cast<std::size_t>(i)] = static_cast<std::int16_t>(clamp(t.x - a.x, -step, step));
        motion.y[static_cast<std::size_t>(i)] = static_cast<std::int16_t>(clamp(t.y - a.y, (-step) >> 4, step));
        motion.z[static_cast<std::size_t>(i)] = static_cast<std::int16_t>(clamp(t.z - a.z, -step, step));
        a.x += motion.x[static_cast<std::size_t>(i)];
        a.y = static_cast<std::int16_t>(a.y + motion.y[static_cast<std::size_t>(i)]);
        a.z += motion.z[static_cast<std::size_t>(i)];
        host.store_anchor(i, a);
    }
    host.publish_motion(motion);
}

void draw_slot(MapEffectFrameHost& host, int slot, SlotDrawer drawer) {
    // The engine's drawers run here; a kind the map did not declare is left alone.
    switch (drawer) {
        case SlotDrawer::Flame:
            if (host.declared(drawer)) ParticleEmitter::draw_flame(host, slot);
            return;
        case SlotDrawer::FireSmoke:
            if (host.declared(drawer)) ParticleEmitter::draw_smoke(host, slot);
            return;
        case SlotDrawer::SplitHeadDrool:
            if (host.declared(drawer)) SplitHeadEffects::draw_drool(host, slot);
            return;
        case SlotDrawer::SplitHeadDrag:
            if (host.declared(drawer)) SplitHeadEffects::draw_drag(host, slot);
            return;
        case SlotDrawer::SmokeWait:
            if (host.declared(drawer)) SmokeColumn::wait(host, slot);
            return;
        case SlotDrawer::SmokePuff:
            if (host.declared(drawer)) SmokeColumn::draw(host, slot);
            return;
        case SlotDrawer::Drip:
            if (host.declared(drawer)) Drips::draw(host, slot);
            return;
        case SlotDrawer::LightGlow:
            if (host.declared(drawer)) LightGlow::draw(host, slot);
            return;
        case SlotDrawer::BloodSpray:
            CombatEffects::draw_spray(host, slot);
            return;
        case SlotDrawer::BloodDrop:
            CombatEffects::draw_drop(host, slot);
            return;
        case SlotDrawer::BloodPool:
            CombatEffects::draw_pool(host, slot);
            return;
        case SlotDrawer::MuzzleFlash:
            if (const CombatEffectParams* p = host.combat_params()) CombatEffects::draw_flash(host, *p, slot);
            return;
        case SlotDrawer::GunSmoke:
            if (const CombatEffectParams* p = host.combat_params()) CombatEffects::draw_smoke(host, *p, slot);
            return;
        case SlotDrawer::HitSpark:
            CombatEffects::draw_spark(host, slot);
            return;
        case SlotDrawer::None:
            return;
        default:
            host.draw(drawer, slot);
            return;
    }
}

}  // namespace

SlotDrawer slot_drawer(std::uint8_t kind) {
    switch (kind) {
        case 1: return SlotDrawer::BloodSpray;
        case 2: return SlotDrawer::BloodDrop;
        case 3:
        case 4: return SlotDrawer::BloodPool;
        case 15:
        case 16:
        case 17:
        case 18:
        case 19: return SlotDrawer::MuzzleFlash;
        case 20:
        case 21:
        case 22: return SlotDrawer::GunSmoke;
        case 31: return SlotDrawer::HitSpark;
        case 8:
        case 10: return SlotDrawer::Flame;
        case 9:
        case 11: return SlotDrawer::FireSmoke;
        case 7: return SlotDrawer::SplitHeadDrool;
        case 5: return SlotDrawer::SplitHeadDrag;
        case 13: return SlotDrawer::SmokeWait;
        case 14: return SlotDrawer::SmokePuff;
        case 23:
        case 24:
        case 25:
        case 26: return SlotDrawer::Glass;
        case 27: return SlotDrawer::WaterDrop;
        case 28: return SlotDrawer::Drip;
        case 32: return SlotDrawer::Script;
        case 33: return SlotDrawer::AcidSpit;
        case 34: return SlotDrawer::AcidDrop;
        case 35: return SlotDrawer::LightGlow;
        default: return SlotDrawer::None;  // 0 free, 6 the drag's other end, 12 idle fire, 29, 30
    }
}

int MapEffectFrame::alloc(SlotTable& table, std::uint8_t kind) {
    const int count = table.count();
    std::int32_t at = table.cursor();
    int tried = 0;
    for (; tried < count; tried++, at++) {
        if (at >= count) at = 0;
        if (table.kind(at) == 0) break;
    }
    if (tried == count) return -1;
    table.set_kind(at, kind);
    table.set_cursor(at + 1);
    return at;
}

void MapEffectFrame::init(MapEffectFrameHost& host, SlotTable& table) {
    for (int i = 0; i < table.count(); i++) table.set_kind(i, 0);
    for (int i = 0; i < host.blood_splat_count(); i++) host.set_blood_splat(i, -1);
    table.set_cursor(0);
    host.set_effect_flags(0);
    host.init_world_objects();
}

void MapEffectFrame::reset(MapEffectFrameHost& host) {
    for (int i = 0; i < host.slot_count(); i++) {
        EffectSlot s = host.load(i);
        switch (s.kind) {
            case 1:
            case 2:
            case 3:
            case 15:
            case 16:
            case 17:
            case 20:
            case 21:
            case 22:
                s.kind = 0;
                host.store(i, s);
                break;
            default:
                break;
        }
    }
    for (int i = 0; i < host.blood_splat_count(); i++) host.set_blood_splat(i, -1);
    host.set_effect_flags(0);
}

void MapEffectFrame::update(MapEffectFrameHost& host) {
    const Vec3 player = host.player_position();
    const Vec3 metres{player.x >> 12, player.y >> 12, player.z >> 12};
    SlotView view = host.view();
    view.origin = {metres.x * 256, metres.y * 256, metres.z * 256};
    host.begin_view(Vec3{metres.x * 4096, metres.y * 4096, metres.z * 4096}, &view);

    const std::uint32_t flags = host.effect_flags();
    if (flags & kFlagEmitter) emitter_frame(host, view);
    host.publish_view(view);

    if (flags & kFlagEmitter) {
        emitter_mode(host);
    } else if (flags & kFlagSmoke) {
        smoke_frame(host);
    } else if (flags & kFlagWater) {
        host.water_frame(view);
    }

    follow_anchors(host);

    host.draws_begin();
    for (int i = 0; i < host.slot_count(); i++) {
        const SlotDrawer drawer = slot_drawer(host.load(i).kind);
        if (drawer != SlotDrawer::None) draw_slot(host, i, drawer);
    }
    host.draws_end();

    // A blood splat whose slot went free is free again.
    for (int i = 0; i < host.blood_splat_count(); i++) {
        const std::int32_t slot = host.blood_splat_slot(i);
        if (slot != -1 && host.load(slot).kind == 0) host.set_blood_splat(i, -1);
    }

    if (host.effect_flags() & kFlagLake) host.lake();
    host.flashlight_check();
}

}  // namespace sh::effects
