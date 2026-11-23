// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The hosts of the slot effects (engine/effects/slot_effects.h) and of the blood and gunfire effects
// (combat_effects.h) over the running game: the session's records, the map's effect slot table and parameter blocks
// (sh::maps::MapRuntime), the frame's view (map_effect_view.h), the camera, the world queries and the frame's order
// keys. Shared by the map effect frame (map_effect_frame_host.cpp) and the combat effects' entry points
// (combat_effects_host.cpp).

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>

#include "engine/camera/view_system.h"
#include "engine/combat/combat_math.h"
#include "engine/effects/combat_effects.h"
#include "engine/effects/drawer_projection.h"
#include "engine/effects/map_effect_view.h"
#include "engine/effects/map_lighting.h"
#include "engine/effects/map_lights.h"
#include "engine/effects/slot_effects.h"
#include "engine/entities/character_record.h"
#include "engine/game_state/game_state_ui.h"
#include "engine/game_state/session.h"
#include "engine/ids/game_numbers.h"
#include "engine/items/item_library.h"
#include "engine/loop/frame_view.h"
#include "engine/loop/game_random.h"
#include "engine/maps/map_runtime.h"
#include "engine/world/fixed_trig.h"
#include "engine/world/game/character_world.h"
#include "engine/math/geometry_state.h"
#include "render/draw2d/draw2d.h"
#include "render/frame/frame.h"

extern "C" {
// The game's packet cursor: the drawers after these place their packets by it.
extern unsigned char* GsOUT_PACKET_P;
// The world environment's lighting of a point and its fog colours.
std::uint8_t WorldEnv_DarknessAtGet(std::int32_t x, std::int32_t y, std::int32_t z);
void WorldEnv_ColorFogApply(void* fog, void* tint, std::uint8_t level, std::int32_t depth);
void WorldEnv_ColorFadeApply(void* color, std::uint8_t level);
int Pc_BloodFogKeep(std::int32_t depth);
void Sfx_WithFlagsPlay(int sound, const std::int32_t* at, std::int32_t volume, std::int32_t flags);
}

namespace sh::effects::slot_hosts {

namespace fx = sh::effects;

// The packets the original's drawers reserved for these effects' primitives: the packet area keeps every later drawer's
// addresses by their sizes (a tag of an address and a length word, then the primitive's words).
namespace packet {
inline constexpr std::size_t kTag = sizeof(std::uintptr_t) + 4;
inline constexpr std::size_t kTexturedQuad = kTag + 36;
inline constexpr std::size_t kShadedQuad = kTag + 32;
inline constexpr std::size_t kSprite = kTag + 16;
inline constexpr std::size_t kTile = kTag + 12;
inline constexpr std::size_t kDrawPage = kTag + 4;
inline constexpr std::size_t kDrawMask = kTag + 8;
inline constexpr std::size_t kDrawMode = kTag + 8;
// The palette half-word of a sprite packet.
inline constexpr std::size_t kSpritePalette = kTag + 10;
}  // namespace packet

// The blood and gunfire numbers, loaded at the first use (null when the file did not load); combat_effects_host.cpp.
const fx::CombatEffectParams* combat_params();

// The bone hierarchies the muzzle flashes sit on (set by the shot that made them).
inline sh::entities::BoneCoord* s_muzzleBones[2] = {nullptr, nullptr};

// The light of each muzzle flash kind, from the weapon whose shot made it last (weapons/<Name>.json "muzzle_light").
inline std::array<std::optional<sh::items::WeaponMuzzleLight>,
                  fx::combat_kind::kGunSmoke - fx::combat_kind::kMuzzleFlash>
    s_flashLights;

// A 2D command at an order key of the lists being built, after the drawer's projections (the packet the drawer
// reserved for it only keeps the packet area's addresses).
inline void place_2d(sh::frame::OrderKey key, unsigned char* packet, const sh::draw2d::Command& command) {
    (void)packet;
    sh::draw2d::submit(command, key, sh::math::geometry().depth_history_max());
}

// The page word of a 15-bit page of the display buffer `buffer`, 256-pixel column `column`, blend mode `blend`.
inline std::uint16_t display_page(int blend, int buffer, int column) {
    return static_cast<std::uint16_t>((2 << 7) | ((blend & 3) << 5) | ((buffer & 1) << 4) | ((column << 2) & 0xF));
}

// The palette half-word an unwritten sprite packet held (the original never set it: the GPU read the memory).
inline sh::draw2d::Palette stale_palette(const unsigned char* sprite) {
    std::uint16_t word;
    std::memcpy(&word, sprite + packet::kSpritePalette, sizeof(word));
    return sh::draw2d::Palette::from_word(word);
}

inline fx::Vec3 position_of(const sh::entities::CharacterRecord& c) {
    return fx::Vec3{c.position.x, c.position.y, c.position.z};
}

// The point the blood of a character follows: its collision box centre.
inline fx::Vec3 anchor_point(const sh::entities::CharacterRecord& c) {
    return fx::Vec3{c.position.x + c.collision.offsets.box.x, c.position.y + c.collision.box.offset_y,
                    c.position.z + c.collision.offsets.box.z};
}

// The slot effects' host over the game; Base is SlotEffectHost or a host that extends it. `cursor` is the packet
// cursor the polygons step (the frame's run of packet memory, or a spawner's that draws nothing).
template <class Base>
class SlotHostT : public Base {
public:
    explicit SlotHostT(unsigned char** cursor) : cursor_(cursor), view_(fx::map_effect_view().view) {}

    std::int32_t rand16() override { return sh::loop::game_random().next15(); }
    std::int32_t sin(std::int32_t a) override { return sh::world::fixed_sin(a); }
    std::int32_t cos(std::int32_t a) override { return sh::world::fixed_cos(a); }
    void flame_shown(const std::array<float, 3>& position, float amount) override {
        fx::fire_particle_shown("ParticleEmitter", position, amount);
    }
    std::int32_t atan2(std::int32_t y, std::int32_t x) override { return sh::world::fixed_atan2(y, x); }
    std::int32_t sqrt(std::int32_t v) override { return sh::world::fixed_sqrt(v); }
    std::int32_t angle_remap(std::int32_t a) override { return sh::combat::coarse_arc_cosine(a); }
    std::int32_t length2(std::int32_t x, std::int32_t z) override {
        // Q12 components squared in Q6 (with the 32-bit wrap of the original's products), the root back in Q12.
        const std::uint32_t a = static_cast<std::uint32_t>(x >> 6);
        const std::uint32_t b = static_cast<std::uint32_t>(z >> 6);
        return static_cast<std::int32_t>(
            static_cast<std::uint32_t>(sh::world::fixed_sqrt(static_cast<std::int32_t>(a * a + b * b))) << 6);
    }
    std::int32_t delta_time() override { return sh::loop::frame_delta(); }
    std::int32_t gravity_step() override { return sh::loop::frame_gravity(); }
    fx::Vec3 player_position() override {
        return position_of(sh::game::session().system.characters.player);
    }

    std::int32_t ground_height(std::int32_t x, std::int32_t z) override {
        return sh::world::game::surface_at(x, z).height;
    }

    void restore_view() override {
        // The slot drawers after this one (the overlays' own) still project with the geometry state's view.
        sh::effects::drawer::load_view(&fx::map_effect_view().world_screen);
        if (projection_.active()) sh::effects::drawer::set_matrix(projection_.vp(), &fx::map_effect_view().world_screen);
    }

    void play_landing_sound(const fx::Vec3& at) override {
        static const int sound = sh::ids::sound_number("drool_drip_land");
        const std::int32_t pos[3] = {at.x, at.y, at.z};
        Sfx_WithFlagsPlay(sound, pos, 0x80, 0);
    }

    std::int32_t darkness(std::int32_t x, std::int32_t y, std::int32_t z) override {
        return WorldEnv_DarknessAtGet(x, y, z);
    }

    const fx::SlotView& view() override { return view_; }

    fx::ScreenPoint project(const fx::Vec3s& r) override {
        const sh::math::PreciseVertex p = view_projection().project({r.x, r.y, r.z});
        return fx::ScreenPoint{p.v.sx, p.v.sy, p.v.sz};
    }

    void project_corners(const fx::Vec3s r[4], fx::ScreenPoint out[4]) override {
        project_into_packet(r, out);
        bind_corners_ = true;
    }

    int slot_count() override { return fx::effect_slot_count(); }
    fx::EffectSlot load(int slot) override { return fx::load_effect_slot(slot); }
    void store(int slot, const fx::EffectSlot& s) override { fx::store_effect_slot(slot, s); }

    int alloc(std::uint8_t kind) override;
    void set_effect_flag(std::uint32_t bit) override { g_MapEffectFlags |= static_cast<std::int8_t>(bit); }

    fx::DripSource drip_source(int index) override {
        const auto* block = static_cast<const fx::DripBlock*>(
            sh::maps::map_runtime().effect_block(sh::maps::EffectBlock::Drips));
        const fx::DripBlock& e = block[index];
        fx::DripSource d;
        d.x = e.x;
        d.z = e.z;
        d.y = e.y;
        d.angle = e.angle;
        d.spread = e.spread;
        d.size = e.size;
        d.period = e.period;
        d.count = e.count;
        d.pattern = e.pattern;
        return d;
    }

    fx::SmokeColumnParams smoke_params() override {
        const fx::SmokeColumnBlock& b = smoke_block();
        fx::SmokeColumnParams p;
        p.shape = b.shape;
        p.size = b.size;
        p.keep = b.keep;
        p.fade_rate = b.fade_rate;
        p.puffs = b.puffs;
        p.top = b.top;
        p.base = b.base;
        p.radius = b.radius;
        p.rise = b.rise;
        p.fade = b.fade;
        p.depth_bias = b.depth_bias;
        p.offset_x = b.offset_x;
        p.offset_z = b.offset_z;
        p.fade_delay = b.fade_delay;
        return p;
    }

    void store_smoke_params(const fx::SmokeColumnParams& p) override {
        fx::SmokeColumnBlock& b = smoke_block();
        b.shape = p.shape;
        b.size = p.size;
        b.keep = p.keep;
        b.fade_rate = p.fade_rate;
        b.puffs = p.puffs;
        b.top = p.top;
        b.base = p.base;
        b.radius = p.radius;
        b.rise = p.rise;
        b.fade = p.fade;
        b.depth_bias = p.depth_bias;
        b.offset_x = p.offset_x;
        b.offset_z = p.offset_z;
        b.fade_delay = p.fade_delay;
    }

    fx::EmitterParams emitter_params() override {
        const fx::EmitterBlock& b = emitter_block();
        fx::EmitterParams p;
        p.flags = b.flags;
        p.mode = b.mode;
        p.level_speed = b.level_speed;
        p.spark_spread = b.spark_spread;
        p.fade_phase = b.fade_phase;
        p.fade_amp = b.fade_amp;
        p.spark_size = b.spark_size;
        p.wobble = b.wobble;
        p.swirl = b.swirl;
        p.count = b.count;
        p.speed = b.speed;
        p.level = b.level;
        p.top = b.top;
        p.bottom = b.bottom;
        p.height = b.height;
        p.half_x = b.half_x;
        p.half_z = b.half_z;
        p.radius = b.radius;
        p.ember_x = b.ember_x;
        p.ember_z = b.ember_z;
        return p;
    }

    void store_emitter_params(const fx::EmitterParams& p) override {
        fx::EmitterBlock& b = emitter_block();
        b.flags = p.flags;
        b.mode = p.mode;
        b.level_speed = p.level_speed;
        b.spark_spread = p.spark_spread;
        b.fade_phase = p.fade_phase;
        b.fade_amp = p.fade_amp;
        b.spark_size = p.spark_size;
        b.wobble = p.wobble;
        b.swirl = p.swirl;
        b.count = p.count;
        b.speed = p.speed;
        b.level = p.level;
        b.top = p.top;
        b.bottom = p.bottom;
        b.height = p.height;
        b.half_x = p.half_x;
        b.half_z = p.half_z;
        b.radius = p.radius;
        b.ember_x = p.ember_x;
        b.ember_z = p.ember_z;
    }

    void emit(const sh::draw3d::Polygon& polygon, fx::SlotLayer layer, std::int32_t bucket) override {
        unsigned char* packet = *cursor_;
        sh::draw3d::Polygon p = polygon;
        if (bind_corners_) {
            // The first three corners were stored with their projection records into this packet.
            if (pending_) {
                for (int k = 0; k < 3; k++) sh::effects::drawer::corner_records(corners_[k], p.v[k]);
            }
            bind_corners_ = false;
        }
        pending_ = false;
        projection_.place(p, {layer == fx::SlotLayer::World ? sh::frame::Layer::World : sh::frame::Layer::Overlay,
                              static_cast<std::uint32_t>(bucket)});
        *cursor_ = packet + packet::kTexturedQuad;
    }

    void screen_copy() override {
        const int buffer = sh::game_state::frame_buffer_index();
        unsigned char* at = *cursor_;
        // The displayed frame drawn back over the scene, one sprite per 256-pixel page.
        for (int i = 0; i < 2; i++) {
            sh::draw2d::Sprite copy;
            copy.at = {static_cast<std::int16_t>((i << 8) - 160), -112};
            copy.w = i == 0 ? 256 : 64;
            copy.h = kFrameHeight;
            copy.uv = {0, static_cast<std::uint8_t>(buffer == 0 ? 32 : 0)};
            copy.palette = stale_palette(at);
            copy.color = {128, 128, 128};
            copy.shade = {true, false};
            place_2d({sh::frame::Layer::Overlay, 15}, at, copy);
            at += packet::kSprite;

            sh::draw2d::DrawMode page;
            page.page = sh::draw2d::TexturePage::from_word(display_page(0, buffer, i));
            place_2d({sh::frame::Layer::Overlay, 15}, at, page);
            at += packet::kDrawPage;
        }

        sh::draw2d::Mask mask;
        mask.set = true;
        place_2d({sh::frame::Layer::World, sh::frame::slot_count(sh::frame::Layer::World) - 1}, at, mask);
        at += packet::kDrawMask;
        mask.set = false;
        place_2d({sh::frame::Layer::Overlay, 0}, at, mask);
        at += packet::kDrawMask;
        *cursor_ = at;
    }

protected:
    // The height of the progressive frame buffer.
    static constexpr std::uint16_t kFrameHeight = 224;

    static fx::SmokeColumnBlock& smoke_block() {
        return *static_cast<fx::SmokeColumnBlock*>(sh::maps::map_runtime().effect_block(sh::maps::EffectBlock::Smoke));
    }
    static fx::EmitterBlock& emitter_block() {
        return *static_cast<fx::EmitterBlock*>(sh::maps::map_runtime().effect_block(sh::maps::EffectBlock::Fire));
    }

    // The view the map effect frame loaded before the slot drawers ran (or restored since).
    sh::math::ViewProjection& view_projection() {
        if (!projection_.active()) sh::effects::drawer::set_current(projection_.vp());
        return projection_.vp();
    }

    // The first three corners keep their projection records for the polygon placed in this packet (the fourth
    // never had one).
    void project_into_packet(const fx::Vec3s r[4], fx::ScreenPoint out[4]) {
        const auto c = view_projection().project3(
            {sh::math::Vec3s{r[0].x, r[0].y, r[0].z}, sh::math::Vec3s{r[1].x, r[1].y, r[1].z},
             sh::math::Vec3s{r[2].x, r[2].y, r[2].z}});
        out[3] = project(r[3]);
        for (int k = 0; k < 3; k++) {
            corners_[k] = c[static_cast<std::size_t>(k)];
            out[k] = {c[static_cast<std::size_t>(k)].v.sx, c[static_cast<std::size_t>(k)].v.sy,
                      c[static_cast<std::size_t>(k)].v.sz};
        }
        pending_ = true;
    }

    unsigned char** cursor_;
    fx::SlotView view_;
    bool bind_corners_ = false;
    sh::effects::drawer::DrawerProjection projection_;
    sh::math::PreciseVertex corners_[3];
    bool pending_ = false;  // corners_ belong to the packet at *cursor_
};

// The slot table's rotating allocator (map_effect_frame_host.cpp); -1 when the table is full.
int alloc_slot(std::uint8_t kind);

template <class Base>
int SlotHostT<Base>::alloc(std::uint8_t kind) {
    return alloc_slot(kind);
}

// The blood and gunfire effects' host: the slot host plus the fog, the camera, the weather, the anchors and blood
// splats, the muzzle bones. Base is CombatEffectHost or the frame's host that extends it.
template <class Base>
class CombatHostT : public SlotHostT<Base> {
public:
    using SlotHostT<Base>::SlotHostT;

    std::int32_t blood_option() override { return sh::game::session().game.settings.blood_color; }
    bool fog_lit() override { return !(sh::game::session().system.environment.current.preset.flags & 3); }

    fx::FogColors fog_colors(std::uint8_t level, std::int32_t depth) override {
        Rgbc fog{};
        Rgbc tint{};
        WorldEnv_ColorFogApply(&fog, &tint, level, depth);
        return fx::FogColors{{fog.r, fog.g, fog.b}, {tint.r, tint.g, tint.b}};
    }

    sh::draw3d::Rgb faded_tint(std::uint8_t level) override {
        Rgbc c{};
        WorldEnv_ColorFadeApply(&c, level);
        return {c.r, c.g, c.b};
    }

    std::int32_t fog_keep(std::int32_t depth) override { return Pc_BloodFogKeep(depth); }

    fx::GroundSample surface(std::int32_t x, std::int32_t z) override {
        const sh::world::game::SurfaceRecord s = sh::world::game::surface_at(x, z);
        return fx::GroundSample{s.height, s.ground_type};
    }

    fx::Vec3 camera_position() override {
        const sh::camera::Vec3& at = sh::camera::view_system().position();
        return fx::Vec3{at.x, at.y, at.z};
    }

    std::array<std::int16_t, 2> camera_forward() override {
        const sh::camera::Matrix& camera = sh::camera::view_system().frame()->local;
        return {camera.m[0][2], camera.m[2][2]};
    }

    bool raining() override { return sh::maps::map_runtime().raining(); }
    bool snowing() override { return sh::maps::map_runtime().snowing(); }
    std::array<std::int32_t, 2> wind() override {
        return {sh::maps::map_runtime().wind(0), sh::maps::map_runtime().wind(1)};
    }

    bool npc_present(int index) override {
        // Slot 6 (the player's pools) reads one record past the NPCs, as the original did; an unknown character
        // (-1) keeps its pool.
        if (index > sh::entities::kNpcSlotCount) return true;
        const sh::entities::CharacterSlots& slots = sh::game::session().system.characters;
        if (index == sh::entities::kNpcSlotCount) {
            // The record's type byte there is the low byte of the first player bone's world counter.
            return (slots.player_bones[0].world_current & 0xFFu) != 0;
        }
        return slots.npcs[index].model.chara_id != 0;
    }

    void flash_light() override { fx::map_lighting().flash(); }

    void flash_shown(int slot, int flash, const fx::Vec3& muzzle, std::int32_t age, std::int32_t life) override {
        if (flash < 0 || static_cast<std::size_t>(flash) >= s_flashLights.size()) return;
        const std::optional<sh::items::WeaponMuzzleLight>& m = s_flashLights[static_cast<std::size_t>(flash)];
        if (!m) return;
        const float t = life > 0 ? std::clamp(static_cast<float>(age) / static_cast<float>(life), 0.0f, 1.0f) : 0.0f;
        const float level = m->intensity * (1.0f - m->fade * t);
        fx::EffectLight l;
        l.position = {static_cast<float>(muzzle.x), static_cast<float>(muzzle.y), static_cast<float>(muzzle.z)};
        for (std::size_t i = 0; i < 3; ++i) l.color[i] = m->color[i] * level;
        l.range = m->range;
        l.radius = m->radius;
        // The barrel and the hand around the muzzle would otherwise shadow the whole scene.
        l.near_clip = 0.1f;
        l.casts_shadows = m->casts_shadows;
        l.gi = m->gi;
        fx::effect_light_shown(slot, l);
    }

    fx::EffectAnchor anchor(int i) override { return fx::effect_anchors()[static_cast<std::size_t>(i)]; }
    void store_anchor(int i, const fx::EffectAnchor& a) override {
        fx::effect_anchors()[static_cast<std::size_t>(i)] = a;
    }
    const fx::AnchorMotion& motion() override { return motion_; }

    int blood_splat_count() override { return sh::maps::map_runtime().blood_splat_count(); }
    std::int32_t blood_splat_slot(int i) override { return sh::maps::map_runtime().blood_splats()[i]; }
    void set_blood_splat(int i, std::int32_t slot) override {
        sh::maps::map_runtime().blood_splats()[i] = static_cast<std::int16_t>(slot);
    }

    sh::math::Transform bone_frame(fx::MuzzleBones bones) override {
        sh::math::Transform t;
        sh::entities::BoneCoord* hierarchy = s_muzzleBones[static_cast<int>(bones)];
        if (hierarchy == nullptr) return t;  // no shot made the flash (the original read past a null pointer)
        const sh::camera::Matrix m =
            sh::camera::frame_world(&hierarchy[10]);
        for (int r = 0; r < 3; r++) {
            for (int c = 0; c < 3; c++) t.rot.m[r][c] = m.m[r][c];
        }
        t.trans = {m.t[0], m.t[1], m.t[2]};
        return t;
    }

    fx::Vec3 transform(const sh::math::Transform& frame, const fx::Vec3s& v) override {
        sh::camera::Matrix m;
        for (int r = 0; r < 3; r++) {
            for (int c = 0; c < 3; c++) m.m[r][c] = frame.rot.m[r][c];
        }
        m.t[0] = frame.trans.x;
        m.t[1] = frame.trans.y;
        m.t[2] = frame.trans.z;
        // A point through the bone frame (no projection); the drawer restores the view after it.
        sh::math::ViewProjection& vp = this->projection_.vp();
        sh::effects::drawer::set_matrix(vp, &m);
        const sh::math::Vec3i out = vp.transform({v.x, v.y, v.z});
        return fx::Vec3{out.x, out.y, out.z};
    }

    void project_quad(const fx::Vec3s r[4], fx::ScreenPoint out[4]) override { this->project_into_packet(r, out); }

    void emit_quad(const sh::draw3d::Polygon& polygon, std::int32_t bucket, bool bind) override {
        unsigned char* packet = *this->cursor_;
        sh::draw3d::Polygon p = polygon;
        if (bind && this->pending_) {
            // A corner keeps the record of the projection made for this packet while its position is that one.
            for (int k = 0; k < 3; k++) {
                const sh::math::PreciseVertex& c = this->corners_[k];
                if (p.v[k].xy.x == c.v.sx && p.v[k].xy.y == c.v.sy) sh::effects::drawer::corner_records(c, p.v[k]);
            }
        }
        this->pending_ = false;
        this->projection_.place(p, {sh::frame::Layer::World, static_cast<std::uint32_t>(bucket)});
        *this->cursor_ = packet + packet::kTexturedQuad;
    }

protected:
    // A colour record of the world environment (three channels and a code byte).
    struct Rgbc {
        std::uint8_t r, g, b, code;
    };

    fx::AnchorMotion motion_{};
};

}  // namespace sh::effects::slot_hosts
