// SPDX-License-Identifier: GPL-3.0-or-later
// The hosts of the engine's particle effects over the running game: the weapon beams,
// snow and rain, the acid spit, the window-glass shards and the insects. They give the effects the projection, the
// world queries, the random numbers and the game's state, and place the effects' draw records by order key. The
// player's combat code calls the beams, the map weather update the weather and insects, the map effect frame and the
// Lua binds the acid and glass.

#include <array>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <string>

#include "engine/loader/active_map.h"
#include "engine/app/pc_paths.h"
#include "engine/camera/view_system.h"
#include "engine/chara/chara_library.h"
#include "engine/chara/game/chara_link.h"
#include "engine/chara/runtime/chara_data.h"
#include "engine/config/pc_config.h"
#include "engine/effects/acid.h"
#include "engine/effects/drawer_projection.h"
#include "engine/effects/game/effect_link.h"
#include "engine/effects/map_effect_view.h"
#include "engine/maps/weather_control.h"
#include "engine/effects/glass.h"
#include "engine/effects/insects.h"
#include "engine/effects/map_lighting.h"
#include "engine/effects/weapon_beams.h"
#include "engine/effects/weather.h"
#include "engine/game_state/game_state.h"
#include "engine/game_state/save_record.h"
#include "engine/game_state/session.h"
#include "engine/gameflow/map_event_state.h"
#include "engine/items/powered_weapon.h"
#include "engine/log/sh_log.h"
#include "engine/loop/frame_view.h"
#include "engine/loop/game_random.h"
#include "engine/maps/map_runtime.h"
#include "engine/maps/sh_maps.h"
#include "engine/math/fixed_geometry.h"
#include "engine/player/harry_states.h"
#include "engine/world/fixed_trig.h"
#include "engine/world/game/character_world.h"
#include "engine/world/world_atmosphere.h"
#include "render/gpu/render_state.h"
#include "render/scene/scene.h"
#include "engine/mods/data_reload.h"

extern "C" {
const char* PcPort_GetGameDataPath(void);
// The renderer draws precise positions.
// Sound placement and the sound driver's commands (src/audio/game).
void Sfx_WithFalloffAndPitchPlay(int sfx, void* position, std::int32_t volume, std::int32_t falloff,
                                 std::int8_t pitch);
void SD_Call(std::uint32_t command);
void Sd_SfxStop(std::uint16_t sfx);
}

namespace {

namespace fx = sh::effects;
namespace fg = sh::effects::game_link;
namespace wg = sh::world::game;
using sh::entities::CharacterRecord;

// The rain's loop sound.
constexpr int kRainLoopSound = 1360;

// The NPC slots the room spawner fills; a boss the engine spawns stands after them.
constexpr int kMapNpcSlots = 32;

// The session's particle anchors.
constexpr int kWeatherAnchor = 0;
constexpr int kBeamAnchor = 1;

sh::game::SystemBlock& sys() { return sh::game::session().system; }
CharacterRecord& player() { return sys().characters.player; }

std::string loaded_map() {
    const char* name = sh::loader::active_map_name();
    return name != nullptr ? name : "";
}

std::string mods_dir() {
    char mods[1024];
    PcPaths_ExeFile(mods, sizeof(mods), "mods");
    return mods;
}

fx::Vec3 vec(const sh::entities::Vec3& v) { return fx::Vec3{v.x, v.y, v.z}; }

void set_view(sh::math::ViewProjection& vp, const sh::camera::Matrix& view) { sh::effects::drawer::set_matrix(vp, &view); }

// The anchor's frame at `t` (Q8) as the view of the effect's points.
void anchor_view(sh::effects::drawer::DrawerProjection& projection, int anchor, std::int32_t x, std::int32_t y,
                 std::int32_t z) {
    const std::int32_t t[3] = {x, y, z};
    set_view(projection.vp(), fg::particle_anchor_view(anchor, t));
}

// One projected point: the screen position and the ordering depth (newest depth / 4) or the depth itself.
fx::ScreenPoint project_point(sh::math::ViewProjection& vp, const fx::Vec3s& rel, bool ordering) {
    const sh::math::PreciseVertex r = vp.project({rel.x, rel.y, rel.z});
    return fx::ScreenPoint{r.v.sx, r.v.sy, ordering ? vp.ordering_depth() : static_cast<std::int32_t>(r.v.sz)};
}

// Turns draw records into polygons of the frame (shared by the beams, the weather, the acid and the insects).
struct PrimWriter {
    static sh::draw3d::Palette palette(const fx::TextureRef& t) {
        return sh::draw3d::Palette::from_word(
            static_cast<std::uint16_t>((t.palette_y << 6) | ((t.palette_x >> 4) & 0x3F)));
    }

    // A record without a colour keeps what the packet memory held (the original never set it).
    static sh::draw3d::Polygon polygon(const fx::EffectPrim& p, const unsigned char* packet) {
        namespace d3 = sh::draw3d;
        d3::Polygon out;
        out.count = 4;
        out.textured = true;
        out.shade = {p.semi_transparent, false};
        out.page = d3::TexturePage::from_word(p.texture.page);
        out.palette = palette(p.texture);
        if (p.kind == fx::EffectPrim::Kind::Sprite) {
            // A sprite box as a textured quad, one texel per pixel at any resolution (the original covered it with
            // a right triangle, which only works at 320x240).
            const std::int16_t x = p.x[0];
            const std::int16_t y = p.y[0];
            const std::uint8_t u = p.u[0];
            const std::uint8_t v = p.v[0];
            const std::int16_t xs[4] = {x, static_cast<std::int16_t>(x + p.w), x, static_cast<std::int16_t>(x + p.w)};
            const std::int16_t ys[4] = {y, y, static_cast<std::int16_t>(y + p.h), static_cast<std::int16_t>(y + p.h)};
            const std::uint8_t us[4] = {u, static_cast<std::uint8_t>(u + p.w), u, static_cast<std::uint8_t>(u + p.w)};
            const std::uint8_t vs[4] = {v, v, static_cast<std::uint8_t>(v + p.h), static_cast<std::uint8_t>(v + p.h)};
            for (int i = 0; i < 4; i++) {
                out.v[i].xy = {xs[i], ys[i]};
                out.v[i].uv = {us[i], vs[i]};
            }
        } else {
            for (int i = 0; i < 4; i++) {
                out.v[i].xy = {p.x[i], p.y[i]};
                out.v[i].uv = {p.u[i], p.v[i]};
            }
        }
        out.gouraud = p.kind != fx::EffectPrim::Kind::Sprite && p.gouraud;
        fg::packet::QuadGouraudTextured old;
        std::memcpy(&old, packet, sizeof(old));
        const d3::Rgb stale[4] = {{old.r0, old.g0, old.b0}, {old.r1, old.g1, old.b1}, {old.r2, old.g2, old.b2},
                                  {old.r3, old.g3, old.b3}};
        for (int i = 0; i < (out.gouraud ? 4 : 1); i++) {
            out.v[i].color = p.has_color ? d3::Rgb{p.rgb[i].r, p.rgb[i].g, p.rgb[i].b} : stale[i];
        }
        return out;
    }

    // The scene renderer draws the world before every effect and tests a translucent polygon against the world's
    // depth only when its corners carry a view depth; without one it counts as 2D and is drawn over the walls, so
    // snow seen through a window would cover the room. The records' corners are computed by the effect, not by the
    // projection, so they get a precise position of their own: the integer position and a depth on the ordering
    // table's scale (a slot is the screen z / 8). The polygon's depth itself still comes from its slot.
    // The depth itself is the record's own view depth (or the slot's, slot = screen z / 8) as a per-corner depth, so
    // the opaque sprites (the flies) are hidden by nearer walls and fences too: a bucket depth alone does not order
    // them against the scene.
    static void bind_view_depth(sh::draw3d::Polygon& poly, std::int32_t slot, std::int32_t view_depth) {
        if (!sh::render::precise().enabled) {
            return;
        }
        const std::int32_t z = view_depth > 0 ? view_depth : (slot > 0 ? slot : 1) << 3;
        const float w = static_cast<float>(z);
        for (sh::draw3d::Vertex& v : poly.v) {
            v.precise = sh::draw3d::Precise{static_cast<float>(v.xy.x), static_cast<float>(v.xy.y), w};
        }
        sh::draw3d::Depth d;
        d.sz.fill(static_cast<std::uint32_t>(z));
        d.source = sh::draw3d::DepthSource::Actor;
        poly.depth = d;
    }

    // Places `p` at its order key with the packet memory at `*cursor` (advanced by the packet it stands for).
    // `view_depth`: the record is a point of the 3D scene (bind_view_depth).
    static void link_at(unsigned char** cursor, const fx::EffectPrim& p, bool view_depth,
                        sh::effects::drawer::DrawerProjection& projection) {
        sh::draw3d::Polygon poly = polygon(p, *cursor);
        if (view_depth || p.view_depth > 0) {
            bind_view_depth(poly, p.slot, p.view_depth);
        }
        projection.place(poly, {sh::frame::Layer::World, static_cast<std::uint32_t>(p.slot)});
        *cursor += poly.gouraud ? sizeof(fg::packet::QuadGouraudTextured) : sizeof(fg::packet::QuadTextured);
    }

    static void link(const fx::EffectPrim& p, sh::effects::drawer::DrawerProjection& projection,
                     bool view_depth = false) {
        link_at(&fg::packet_cursor(), p, view_depth, projection);
    }
};

// ---------------------------------------------------------------------------------------
// Weapon beams
// ---------------------------------------------------------------------------------------

std::optional<fx::WeaponBeams> s_beams;
std::uint32_t s_beamsEpoch = sh::mods::kNeverRead;

const fx::WeaponBeams* beams() {
    if (sh::mods::data_stale(s_beamsEpoch)) {
        s_beams.reset();
        std::string source;
        sh::Result<fx::WeaponBeams> loaded = fx::load_weapon_beams(PcPort_GetGameDataPath(), mods_dir(), &source);
        if (loaded) {
            s_beams.emplace(std::move(loaded.value()));
            SH_LOG("[FX] weapon beams from %s", source.c_str());
        } else {
            SH_ERR("[FX] %s - the weapon beams are not drawn", loaded.error().c_str());
        }
    }
    return s_beams ? &*s_beams : nullptr;
}

bool beams_on_this_map(const fx::WeaponBeams* b) { return b != nullptr && b->drawn_on(loaded_map()); }

// The view of Harry's position, as the projection of every beam point expects (the points are given relative to
// him).
void view_at_harry(sh::effects::drawer::DrawerProjection& projection) {
    const sh::entities::Vec3& p = player().position;
    anchor_view(projection, kBeamAnchor, p.x >> 4, p.y >> 4, p.z >> 4);
}

class BeamHostImpl final : public fx::BeamHost {
public:
    sh::effects::drawer::DrawerProjection projection;

    std::int32_t sin(std::int32_t angle) override { return sh::world::fixed_sin(angle); }
    std::int32_t cos(std::int32_t angle) override { return sh::world::fixed_cos(angle); }

    fx::BeamHit trace(const fx::Vec3& from, const fx::Vec3& offset) override {
        wg::RayTraceRecord trace{};
        bool hit = false;
        // The trace ran between a push and a pop of the view: the view comes back with its quantised camera.
        projection.around([&] {
            hit = wg::trace(trace, sh::world::Vec3{from.x, from.y, from.z}, sh::world::Vec3{offset.x, offset.y, offset.z},
                            wg::TraceKind::Chara, &player());
        });
        projection.vp().requantize();

        fx::BeamHit out;
        out.hit = hit;
        out.distance = trace.distance;
        out.target = fx::Vec3{trace.point.x, trace.point.y, trace.point.z};
        return out;
    }

    fx::ScreenPoint project(const fx::Vec3s& rel) override { return project_point(projection.vp(), rel, true); }

    void emit(const fx::EffectPrim& p) override { PrimWriter::link(p, projection); }
};

}  // namespace

extern "C" void WeaponBeam_HyperBlasterDraw(const std::int32_t* from, const std::int16_t* yaw,
                                            const std::int16_t* pitch) {
    const fx::WeaponBeams* b = beams();
    if (!beams_on_this_map(b)) {
        return;
    }

    BeamHostImpl host;
    view_at_harry(host.projection);

    fx::HyperBlasterShot shot;
    shot.from = fx::Vec3{from[0], from[1], from[2]};
    shot.origin = vec(player().position);
    shot.range = sh::chara::game::link::attack_row(sys().combat.weapon_attack).reach_q12;
    shot.yaw = *yaw;
    shot.pitch = *pitch;
    shot.color_option = sh::items::hyper_blaster_beam_color(sh::chara::game::link::special_items_picked_up());

    fx::draw_hyper_blaster_beam(b->hyper_blaster, shot, host);
}

extern "C" void WeaponBeam_SparkLineDraw(const std::int32_t* from, const std::int32_t* to) {
    const fx::WeaponBeams* b = beams();
    if (!beams_on_this_map(b)) {
        return;
    }

    BeamHostImpl host;
    view_at_harry(host.projection);

    const sh::game::EnvironmentMix& env = sys().environment;
    fx::BeamLights lights;
    lights.map_light_mode = sh::maps::map_runtime().light_mode();
    lights.flashlight_allowed = (env.current.preset.flags & (1 << 1)) != 0;
    lights.flashlight_on = env.flashlight_on != 0;
    lights.dark_flashlight_off = (env.transition[0].preset.flags & 1) != 0;
    lights.dark_flashlight_on = (env.transition[1].preset.flags & 1) != 0;

    fx::draw_spark_line(b->spark_line, fx::Vec3{from[0], from[1], from[2]}, fx::Vec3{to[0], to[1], to[2]},
                        vec(player().position), lights, host);
}

// ---------------------------------------------------------------------------------------
// Snow and rain
// ---------------------------------------------------------------------------------------

namespace {

std::unique_ptr<fx::WeatherSystem> s_weather;
std::string s_weatherMap;
bool s_weatherLoaded = false;

// The engine's weather for the loaded map; replaced when another map is loaded.
fx::WeatherSystem* weather() {
    const std::string map = loaded_map();
    if (s_weatherLoaded && map == s_weatherMap) {
        return s_weather.get();
    }
    s_weatherLoaded = true;
    s_weatherMap = map;
    s_weather.reset();

    fx::WeatherParams params;
    if (!map.empty()) {
        std::string source;
        bool found = false;
        sh::Result<fx::WeatherParams> loaded =
            fx::load_weather_params(PcPort_GetGameDataPath(), mods_dir(), map, &source, &found);
        if (loaded) {
            params = std::move(loaded.value());
            if (found) {
                SH_LOG("[FX] weather of %s from %s", map.c_str(), source.c_str());
            }
        } else {
            SH_ERR("[FX] %s - %s has no weather", loaded.error().c_str(), map.c_str());
        }
    }
    s_weather = std::make_unique<fx::WeatherSystem>(std::move(params));
    return s_weather.get();
}

class WeatherHostImpl final : public fx::WeatherHost {
public:
    std::int32_t rand16() override { return sh::loop::game_random().next15(); }
    std::int32_t sin(std::int32_t angle) override { return sh::world::fixed_sin(angle); }
    std::int32_t cos(std::int32_t angle) override { return sh::world::fixed_cos(angle); }
    std::int32_t square_root(std::int32_t v) override { return sh::world::fixed_sqrt(v); }
    std::int32_t delta_time() override { return sh::game_state::delta_time(); }
    int sim_hz() override { return g_PcConfig.weatherSimHz; }
    int room() override { return sh::game::save_record().room; }
    fx::Vec3 player() override { return vec(::player().position); }
    int event_param() override { return static_cast<int>(sh::gameflow::map_event_state().param); }
    int event_step() override { return sys().flow.sys_steps[0]; }
    int light_mode() override { return fg::weather_light_mode(); }
    bool flashlight_on() override { return fx::map_lighting().flashlight_on(); }
    bool match_anim() override { return sys().player_match_anim != 0; }
    int header_density() override { return sh::maps::map_runtime().weather(); }

    int density_mode() override { return sys().particles.density_mode; }
    void set_density_mode(int v) override {
        sys().particles.density_mode = static_cast<std::int8_t>(v);
        sys().particles.density_applied = static_cast<std::int8_t>(v);
    }
    bool transition_ready() override { return sys().particles.transition_ready != 0; }
    void set_transition_ready(bool v) override { sys().particles.transition_ready = v ? 1 : 0; }

    void begin_view(std::int32_t x_q8, std::int32_t z_q8) override {
        anchor_view(projection_, kWeatherAnchor, x_q8, 0, z_q8);
    }

    fx::ScreenPoint project(const fx::Vec3s& rel) override {
        const sh::math::PreciseVertex r = projection_.vp().project({rel.x, rel.y, rel.z});
        last_view_ = {r.view_x, r.view_y, r.view_z, r.h};
        return fx::ScreenPoint{r.v.sx, r.v.sy, projection_.vp().ordering_depth()};
    }

    void note_flake(int size) override {
        // The flake's view position and half its drawn size at its depth, for the scene's flake shadows.
        constexpr std::size_t kMaxFlakes = 4096;
        auto& flakes = sh::scene::current_frame().flakes;
        if (flakes.size() >= kMaxFlakes || last_view_[2] <= 0.0f || last_view_[3] <= 0.0f) return;
        flakes.push_back({last_view_[0], last_view_[1], last_view_[2],
                          0.5f * static_cast<float>(size) * last_view_[2] / last_view_[3]});
    }

    fx::GroundSample ground(std::int32_t x, std::int32_t z) override {
        wg::SurfaceRecord surface{};
        // The query ran between a push and a pop of the view: the view comes back with its quantised camera.
        projection_.around([&] { surface = wg::surface_at(x, z); });
        if (projection_.active()) {
            projection_.vp().requantize();
        }
        return fx::GroundSample{surface.height, surface.ground_type};
    }

    std::int32_t fog(std::int32_t depth) override { return sh::world::atmosphere().fog_ramp_u8(depth); }

    void emit(const fx::EffectPrim& p) override { PrimWriter::link(p, projection_, true); }

    void rain_loop(int volume) override {
        Sfx_WithFalloffAndPitchPlay(kRainLoopSound, &::player().position, volume, 0, 0);
    }
    void rain_start() override { SD_Call(kRainLoopSound); }
    void rain_stop() override { Sd_SfxStop(kRainLoopSound); }

private:
    std::array<float, 4> last_view_{};  // view xyz and the projection distance of the last projected point
    sh::effects::drawer::DrawerProjection projection_;
};

}  // namespace


// ---------------------------------------------------------------------------------------
// Acid spit of the Floatstinger and the Twinfeeler
// ---------------------------------------------------------------------------------------

namespace {

std::optional<fx::AcidEffects> s_acid;
std::uint32_t s_acidEpoch = sh::mods::kNeverRead;

const fx::AcidEffects* acid() {
    if (sh::mods::data_stale(s_acidEpoch)) {
        s_acid.reset();
        std::string source;
        sh::Result<fx::AcidConfig> loaded = fx::load_acid_config(PcPort_GetGameDataPath(), mods_dir(), &source);
        if (loaded) {
            s_acid.emplace(std::move(loaded.value()));
            SH_LOG("[FX] acid from %s", source.c_str());
        } else {
            SH_ERR("[FX] %s - there is no acid", loaded.error().c_str());
        }
    }
    return s_acid ? &*s_acid : nullptr;
}

// The map's numbers for the loaded map and difficulty; zeros (every spit dies at once) for a map
// without acid, as the original behaves with its unset table.
fx::AcidParams acid_params(const fx::AcidEffects& a) {
    constexpr int kHard = 1;
    const fx::AcidMapParams* p = a.config().find(loaded_map());
    if (p == nullptr) {
        return fx::AcidParams{};
    }
    return sh::game::save_record().difficulty() == kHard ? p->hard : p->normal;
}

sh::maps::EffectSlotRecord& effect_slot(int idx) { return sh::maps::map_runtime().effect_slots()[idx]; }

fx::AcidParticle load_particle(int idx) {
    const sh::maps::EffectSlotRecord& s = effect_slot(idx);
    fx::AcidParticle p;
    p.x = s.x;
    p.z = s.z;
    p.y = s.y;
    p.state = s.kind;
    p.flag = s.flags;
    p.c0 = fg::low_half(s.a);
    p.c1 = fg::high_half(s.a);
    p.t0 = fg::low_half(s.b);
    p.t1 = fg::high_half(s.b);
    return p;
}

class AcidHostImpl final : public fx::AcidHost {
public:
    // `cursor` is the map effect frame's packet cursor; the records are placed there.
    explicit AcidHostImpl(unsigned char** cursor) : cursor_(cursor) {}

    std::int32_t rand16() override { return sh::loop::game_random().next15(); }
    std::int32_t sin(std::int32_t angle) override { return sh::world::fixed_sin(angle); }
    std::int32_t cos(std::int32_t angle) override { return sh::world::fixed_cos(angle); }
    std::int32_t delta_time() override { return sh::game_state::delta_time(); }
    std::int32_t atan2(std::int32_t a, std::int32_t b) override { return sh::world::fixed_atan2(a, b); }
    // The length of (a, b) through Q6 squares, safe from overflow.
    std::int32_t length2_q6(std::int32_t a, std::int32_t b) override {
        const std::int32_t x = a >> 6;
        const std::int32_t z = b >> 6;
        return sh::world::fixed_sqrt(x * x + z * z) << 6;
    }
    std::int32_t darkness(const fx::Vec3& at) override { return sh::world::atmosphere().darkness_at(at.x, at.y, at.z); }

    fx::ScreenPoint project(const fx::Vec3s& rel) override { return project_point(view(), rel, false); }

    std::int32_t geometry_screen() override { return sh::effects::map_effect_view().view.screen_distance; }
    fx::Vec3s view_origin() override {
        const sh::effects::Vec3& o = sh::effects::map_effect_view().view.origin;  // whole metres, Q8
        return fx::Vec3s{static_cast<std::int16_t>(o.x), static_cast<std::int16_t>(o.y), static_cast<std::int16_t>(o.z)};
    }

    int alloc(std::uint8_t state, fx::AcidParticle* p) override {
        const int idx = fg::slot_alloc(state);
        if (idx >= 0) {
            *p = load_particle(idx);
        }
        return idx;
    }

    void store(int idx, const fx::AcidParticle& p) override {
        sh::maps::EffectSlotRecord& s = effect_slot(idx);
        s.x = p.x;
        s.z = p.z;
        s.y = p.y;
        s.kind = p.state;
        s.flags = p.flag;
        s.a = fg::with_low_half(s.a, p.c0);
        s.a = fg::with_high_half(s.a, p.c1);
        s.b = fg::with_low_half(s.b, p.t0);
        s.b = fg::with_high_half(s.b, p.t1);
    }

    fx::AcidProbe probe(const fx::Vec3& from, const fx::Vec3& offset) override {
        wg::RayTraceRecord trace{};
        wg::SurfaceRecord surface{};

        projection_.around([&] {
            wg::trace(trace, sh::world::Vec3{from.x, from.y, from.z}, sh::world::Vec3{offset.x, offset.y, offset.z},
                      wg::TraceKind::Chara, &sys().characters.npcs[0]);
            surface = wg::surface_at(from.x + offset.x, from.z + offset.z);
        });

        // The slot drawers after this one still project with the geometry state's view.
        sh::camera::Matrix& view = sh::effects::map_effect_view().world_screen;
        sh::effects::drawer::load_view(&view);
        if (projection_.active()) {
            set_view(projection_.vp(), view);
        }

        fx::AcidProbe out;
        out.hit = trace.hit != 0;
        out.hit_player = trace.hit != 0 && trace.character == &::player();
        out.target = fx::Vec3{trace.point.x, trace.point.y, trace.point.z};
        out.ground_height = surface.height;
        return out;
    }

    void strike_player() override {
        // The packed attack the acid hits the player with: the spitter type's character.json "acid_attack".
        const auto attack_of = [&](const CharacterRecord& c) -> std::int32_t {
            const sh::chara::CharacterDef* def = sh::chara::type_def(c.model.chara_id);
            if (def == nullptr) {
                return 0;
            }
            if (!def->acid_attack_name.empty()) {
                const int number = sh::chara::game::link::attack_number(def->acid_attack_name);
                return number > 0 ? number : 0;
            }
            return def->acid_attack;
        };
        std::int32_t attack = attack_of(sys().characters.npcs[0]);
        for (int i = kMapNpcSlots; i < sh::entities::kNpcSlotCount && attack == 0; ++i) {
            attack = attack_of(sys().characters.npcs[i]);
        }
        if (attack != 0) {
            ::player().attack_received = attack;
        }
    }

    void emit(const fx::EffectPrim& p) override { PrimWriter::link_at(cursor_, p, false, projection_); }

private:
    // The view the map effect frame loaded before the slot drawers ran.
    sh::math::ViewProjection& view() {
        if (!projection_.active()) {
            sh::effects::drawer::set_current(projection_.vp());
        }
        return projection_.vp();
    }

    unsigned char** cursor_;
    sh::effects::drawer::DrawerProjection projection_;
};

}  // namespace

extern "C" void Acid_Spit(const std::int32_t* from, const std::int32_t* to) {
    const fx::AcidEffects* a = acid();
    if (a == nullptr) {
        return;
    }
    AcidHostImpl host(nullptr);
    a->spawn_spit(host, acid_params(*a), fx::Vec3{from[0], from[1], from[2]}, fx::Vec3{to[0], to[1], to[2]});
}

// The acid slots' drawers; `cursor` is the map effect frame's packet cursor.
extern "C" std::int32_t Acid_SpitDraw(unsigned char** cursor, std::int32_t idx) {
    const fx::AcidEffects* a = acid();
    if (a == nullptr) {
        return 0;
    }
    AcidHostImpl host(cursor);
    fx::AcidParticle p = load_particle(idx);
    return a->update_spit(host, acid_params(*a), idx, p) ? 1 : 0;
}

extern "C" std::int32_t Acid_DropDraw(unsigned char** cursor, std::int32_t idx) {
    const fx::AcidEffects* a = acid();
    if (a == nullptr) {
        return 0;
    }
    AcidHostImpl host(cursor);
    fx::AcidParticle p = load_particle(idx);
    return a->update_drop(host, idx, p) ? 1 : 0;
}

// ---------------------------------------------------------------------------------------
// Glass shards of the burst window panes
// ---------------------------------------------------------------------------------------

namespace {

std::optional<fx::GlassEffects> s_glass;
std::uint32_t s_glassEpoch = sh::mods::kNeverRead;

const fx::GlassEffects* glass() {
    if (sh::mods::data_stale(s_glassEpoch)) {
        s_glass.reset();
        std::string source;
        sh::Result<fx::GlassParams> loaded = fx::load_glass_params(PcPort_GetGameDataPath(), mods_dir(), &source);
        if (loaded) {
            s_glass.emplace(loaded.value());
            SH_LOG("[FX] glass from %s", source.c_str());
        } else {
            SH_ERR("[FX] %s - the glass shards are not drawn", loaded.error().c_str());
        }
    }
    return s_glass ? &*s_glass : nullptr;
}

fx::GlassShard load_shard(int idx) {
    const sh::maps::EffectSlotRecord& s = effect_slot(idx);
    fx::GlassShard shard;
    shard.x = fg::low_word(s.x);
    shard.yaw = fg::high_word(s.x);
    shard.z = fg::low_word(s.z);
    shard.pitch = fg::high_word(s.z);
    shard.y = s.y;
    shard.state = s.kind;
    shard.variant = s.flags;
    shard.vel_x = fg::low_half(s.a);
    shard.vel_y = fg::high_half(s.a);
    shard.vel_z = fg::low_half(s.b);
    shard.spin = static_cast<std::uint16_t>(fg::high_half(s.b));
    return shard;
}

void store_shard(int idx, const fx::GlassShard& shard) {
    sh::maps::EffectSlotRecord& s = effect_slot(idx);
    s.x = fg::with_low_word(s.x, shard.x);
    s.z = fg::with_low_word(s.z, shard.z);
    s.y = static_cast<std::int16_t>(shard.y);
    s.kind = static_cast<std::uint8_t>(shard.state);
    s.flags = static_cast<std::uint8_t>(shard.variant);
    s.a = fg::with_low_half(s.a, shard.vel_x);
    s.a = fg::with_high_half(s.a, shard.vel_y);
    s.b = fg::with_low_half(s.b, shard.vel_z);
    s.x = fg::with_high_word(s.x, shard.yaw);
    s.z = fg::with_high_word(s.z, shard.pitch);
    s.b = fg::with_high_half(s.b, shard.spin);
}

sh::camera::Matrix rotation_matrix(std::int16_t x, std::int16_t y, std::int32_t tx, std::int32_t tz) {
    const sh::math::Matrix3 rot = sh::math::rot_matrix_zxy_neg(sh::math::Vec3s{x, y, 0});
    sh::camera::Matrix m;
    for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 3; c++) {
            m.m[r][c] = rot.m[r][c];
        }
    }
    m.t[0] = tx;
    m.t[1] = 0;
    m.t[2] = tz;
    return m;
}

// What the shards need from the game: the slot table, the projection, the ground, the fog and the packet cursor.
class GlassHostImpl final : public fx::GlassHost {
public:
    // The map effect frame's packet cursor (null while spawning, nothing is drawn then).
    explicit GlassHostImpl(unsigned char** cursor) : cursor_(cursor) {}

    std::int32_t rand_uint(std::int32_t low, std::int32_t high) override {
        return sh::loop::game_random().next_uint(low, high);
    }
    std::int32_t rand_add(std::int32_t base, std::int32_t low, std::int32_t high) override {
        return base + sh::loop::game_random().next_uint(low, high);
    }
    std::int32_t rand16() override { return sh::loop::game_random().next15(); }
    std::int32_t sin(std::int32_t a) override { return sh::world::fixed_sin(a); }
    std::int32_t cos(std::int32_t a) override { return sh::world::fixed_cos(a); }
    std::int32_t atan2(std::int32_t a, std::int32_t b) override { return sh::world::fixed_atan2(a, b); }
    std::int32_t sqrt(std::int32_t v) override { return sh::world::fixed_sqrt(v); }
    std::int32_t delta_time() override { return sh::game_state::delta_time(); }
    std::int32_t gravity() override { return sh::loop::frame_gravity(); }

    void rotate_setup(std::int16_t pitch, std::int16_t yaw) override {
        matrix_ = rotation_matrix(pitch, yaw, 0, 0);
        set_view(projection_.vp(), matrix_);
    }

    fx::Vec3 rotate(const fx::Vec3s& v) override {
        const sh::math::Vec3i out = projection_.vp().transform({v.x, v.y, v.z});
        return fx::Vec3{out.x, out.y, out.z};
    }

    void frame_setup(std::int16_t yaw, std::int16_t pitch, std::int32_t tx, std::int32_t tz) override {
        matrix_ = rotation_matrix(yaw, pitch, tx, tz);
        set_view(projection_.vp(), matrix_);
    }

    void use_view() override { set_view(projection_.vp(), sh::effects::map_effect_view().world_screen); }

    fx::Vec3s view_origin() override {
        const sh::effects::Vec3& o = sh::effects::map_effect_view().view.origin;  // whole metres, Q8
        return fx::Vec3s{static_cast<std::int16_t>(o.x), static_cast<std::int16_t>(o.y), static_cast<std::int16_t>(o.z)};
    }

    void project3(const fx::Vec3s& a, const fx::Vec3s& b, const fx::Vec3s& c, fx::ScreenPoint out[3]) override {
        const auto r = projection_.vp().project3(
            {sh::math::Vec3s{a.x, a.y, a.z}, sh::math::Vec3s{b.x, b.y, b.z}, sh::math::Vec3s{c.x, c.y, c.z}});
        for (int i = 0; i < 3; i++) {
            out[i] = fx::ScreenPoint{r[i].v.sx, r[i].v.sy, r[i].v.sz};
        }
    }

    fx::ScreenPoint project(const fx::Vec3s& v) override { return project_point(projection_.vp(), v, false); }

    fx::Rgb fog(std::int32_t depth_times_16) override {
        const sh::world::FogTint t =
            sh::world::atmosphere().fog_tint(static_cast<std::uint8_t>(glass()->params().fog_mix), depth_times_16);
        return fx::Rgb{t.tint.r, t.tint.g, t.tint.b};
    }

    GlassHost::Ground ground(std::int32_t x, std::int32_t z) override {
        const wg::SurfaceRecord surface = wg::surface_at(x, z);
        return GlassHost::Ground{surface.height, surface.ground_type};
    }

    int alloc(std::uint8_t group) override { return fg::slot_alloc(group); }

    fx::GlassShard load(int slot) override { return load_shard(slot); }
    void store(int slot, const fx::GlassShard& shard) override { store_shard(slot, shard); }

    void set_pane(int group, const fx::GlassPane& pane) override {
        fg::GlassPaneRecord& p = pane_table()[group];
        p.x = pane.x;
        p.z = pane.z;
        p.size = static_cast<std::int16_t>(pane.size);
    }

    fx::GlassPane pane(int group) override {
        const fg::GlassPaneRecord& p = pane_table()[group];
        return fx::GlassPane{p.x, p.z, p.size};
    }

    void emit(const Quad& q) override {
        namespace d3 = sh::draw3d;
        // The variant byte picks the shard's texture cell; the words below are the original's raw packing of the
        // u/v fields (u, v and the palette or page of a vertex in one word).
        const std::uint8_t look = q.variant;
        const std::uint32_t w0 = ((look & 3) << 4) + ((look << 0xA) & 0x3000) + 0xE0000;
        const std::uint32_t w1 = ((look & 3) << 4) + 15 + ((look << 0xA) & 0x3000) + 0x2D0000;
        const std::uint16_t w2 = static_cast<std::uint16_t>(((look & 3) << 4) + ((((look * 4) & 0x30) + 15) << 8));
        const std::uint16_t w3 =
            static_cast<std::uint16_t>(((look & 3) << 4) + 15 + ((((look * 4) & 0x30) + 15) << 8));

        d3::Polygon out;
        out.count = 4;
        out.textured = true;
        out.shade = {true, false};  // the semi-transparent flat textured quad
        out.palette = d3::Palette::from_word(static_cast<std::uint16_t>(w0 >> 16));
        out.page = d3::TexturePage::from_word(static_cast<std::uint16_t>(w1 >> 16));
        const std::uint32_t uv[4] = {w0, w1, w2, w3};
        for (int i = 0; i < 4; i++) {
            out.v[i].xy = {q.corners[i].x, q.corners[i].y};
            out.v[i].uv = {static_cast<std::uint8_t>(uv[i]), static_cast<std::uint8_t>(uv[i] >> 8)};
        }
        out.v[0].color = {static_cast<std::uint8_t>(q.color.r), static_cast<std::uint8_t>(q.color.g),
                          static_cast<std::uint8_t>(q.color.b)};

        projection_.place(out, {sh::frame::Layer::World, static_cast<std::uint32_t>(q.slot)});
        *cursor_ += sizeof(fg::packet::QuadTextured);
    }

private:
    // The pane table belongs to the loaded map (MapRuntime points at it).
    static fg::GlassPaneRecord* pane_table() {
        return static_cast<fg::GlassPaneRecord*>(sh::maps::map_runtime().effect_block(sh::maps::EffectBlock::Glass));
    }

    unsigned char** cursor_ = nullptr;
    sh::camera::Matrix matrix_{};
    sh::effects::drawer::DrawerProjection projection_;
};

}  // namespace

// A pane bursts: the call's numbers (pane corners, speed, size factor, half sizes, cell spread, pane group).
extern "C" void Glass_Shatter(const std::int32_t* from, const std::int32_t* to, std::int16_t speed, std::int32_t size,
                              std::int16_t half_a, std::int16_t half_b, std::int32_t spread, std::int32_t group) {
    const fx::GlassEffects* g = glass();
    if (g == nullptr) {
        return;
    }
    GlassHostImpl host(nullptr);
    g->spawn(host, fx::Vec3{from[0], from[1], from[2]}, fx::Vec3{to[0], to[1], to[2]}, speed, size, half_a, half_b,
             spread, group);
}

extern "C" std::int32_t Glass_Draw(unsigned char** cursor, std::int32_t idx) {
    const fx::GlassEffects* g = glass();
    if (g == nullptr) {
        return 0;
    }
    GlassHostImpl host(cursor);
    fx::GlassShard shard = load_shard(idx);
    return g->update(host, idx, shard) ? 1 : 0;
}

// ---------------------------------------------------------------------------------------
// The insects of map0_s00
// ---------------------------------------------------------------------------------------

namespace {

std::optional<fx::InsectSwarms> s_insects;
std::uint32_t s_insectsEpoch = sh::mods::kNeverRead;

fx::InsectSwarms* insects() {
    if (sh::mods::data_stale(s_insectsEpoch)) {
        s_insects.reset();
        std::string source;
        sh::Result<fx::InsectConfig> loaded = fx::load_insect_config(PcPort_GetGameDataPath(), mods_dir(), &source);
        if (loaded) {
            s_insects.emplace(std::move(loaded.value()));
            SH_LOG("[FX] insects from %s", source.c_str());
        } else {
            SH_ERR("[FX] %s - there are no insects", loaded.error().c_str());
        }
    }
    return s_insects ? &*s_insects : nullptr;
}

// What one insect update sees: the run mode the start returned (0 off, 1 always, 2 near), the weather's pause, the
// light colour and the flashlight cone (size Q12, origin, rotation).
struct InsectFrame {
    int mode = 0;
    bool paused = false;
    fx::Rgb light;
    std::int32_t cone_size = 0;
    fx::Vec3 cone_origin;
    std::int16_t cone_rotation[3] = {};
};

class InsectHostImpl final : public fx::InsectHost {
public:
    InsectHostImpl(const InsectFrame* frame, void (*start_weather)(void))
        : frame_(frame), start_weather_(start_weather) {}

    std::int32_t rand16() override { return sh::loop::game_random().next15(); }
    std::int32_t sin(std::int32_t angle) override { return sh::world::fixed_sin(angle); }
    std::int32_t cos(std::int32_t angle) override { return sh::world::fixed_cos(angle); }
    std::int32_t atan2(std::int32_t a, std::int32_t b) override { return sh::world::fixed_atan2(a, b); }
    std::int32_t sqrt(std::int32_t v) override { return sh::world::fixed_sqrt(v); }
    std::int32_t delta_time() override { return sh::game_state::delta_time(); }
    bool paused() override { return frame_ != nullptr && frame_->paused; }
    std::int32_t frame_counter() override { return sys().flow.frame_counters[0]; }
    fx::Vec3 player() override { return vec(::player().position); }
    bool hand_follows() override { return sys().player_match_anim != 0; }
    fx::Vec3 hand_q8() override {
        const std::int32_t* t = sys().characters.player_bones[sh::game::harry::bone::kRightHand].world.t;
        return fx::Vec3{t[0], t[1], t[2]};
    }
    bool flashlight_on() override { return fx::map_lighting().flashlight_on(); }
    std::int32_t light_mode() override { return fg::weather_light_mode(); }
    fx::Rgb light_color() override {
        return frame_->light;
    }
    std::int32_t darkness(const fx::Vec3& at) override { return sh::world::atmosphere().darkness_at(at.x, at.y, at.z); }
    void start_weather() override {
        if (start_weather_ != nullptr) {
            start_weather_();
        }
    }

    // The view of the world itself: positions are projected as they are, Q8 from the origin.
    void begin_view() override { anchor_view(projection_, kBeamAnchor, 0, 0, 0); }

    fx::ScreenPoint project(const fx::Vec3s& rel) override { return project_point(projection_.vp(), rel, true); }

    std::int32_t cone_size() override { return frame_->cone_size; }
    fx::Vec3 cone_origin() override {
        return frame_->cone_origin;
    }
    void cone_begin() override {
        const std::int16_t* r = frame_->cone_rotation;
        cone_ = sh::math::rot_matrix_zxy_neg(sh::math::Vec3s{r[0], r[1], r[2]});
    }
    fx::Vec3s cone_rotate(const fx::Vec3s& c) override {
        // The original's drawer rotated through the geometry state between a push and a pop of the view, which came back
        // with its quantised camera.
        if (projection_.active()) {
            projection_.vp().requantize();
        }
        const sh::math::MatVec out =
            sh::math::matrix_vector(cone_, {c.x, c.y, c.z});
        return fx::Vec3s{static_cast<std::int16_t>(out.saturated.x), static_cast<std::int16_t>(out.saturated.y),
                         static_cast<std::int16_t>(out.saturated.z)};
    }

    void emit(const fx::EffectPrim& p) override { PrimWriter::link(p, projection_); }

private:
    sh::effects::drawer::DrawerProjection projection_;
    const InsectFrame* frame_;
    void (*start_weather_)(void);
    sh::math::Matrix3 cone_{};
};

}  // namespace

// ---------------------------------------------------------------------------------------
// The weather of every map: the map scripts' requests and the update of the snow, rain and
// insects the game loop runs each frame
// ---------------------------------------------------------------------------------------

namespace {

// The insects' run mode lives in the high nibble of the session's particle byte.
int insect_mode() { return sys().particles.cue_and_insects >> 4; }
void set_insect_mode(int mode) {
    std::uint8_t& b = sys().particles.cue_and_insects;
    b = static_cast<std::uint8_t>((b & 0x0F) | ((mode & 0x0F) << 4));
}

class WeatherControlHostImpl final : public sh::maps::WeatherControlHost {
public:
    int light_mode() override { return fg::weather_light_mode(); }
    void set_light_mode(int mode) override { fg::set_weather_light_mode(mode); }
    bool transition_ready() override { return sys().particles.transition_ready != 0; }
    void set_transition_ready(bool ready) override { sys().particles.transition_ready = ready ? 1 : 0; }
    void set_density_mode(int level) override {
        sys().particles.density_mode = static_cast<std::int8_t>(level);
        sys().particles.density_applied = static_cast<std::int8_t>(level);
    }
};

sh::maps::WeatherFeatures weather_features(const fx::WeatherParams& p) {
    return sh::maps::WeatherFeatures{p.snow_on, p.rain_on};
}

// The insects' start runs the light mode setter first, with the mode the map already has.
void insects_weather_light() {
    WeatherControlHostImpl host;
    sh::maps::map_runtime().weather_control().light_mode_set(host, host.light_mode());
}

fx::Rgb rgb(const sh::maps::WeatherLight& l) { return fx::Rgb{l.r, l.g, l.b}; }

}  // namespace

// The map's environment (0 clear, 1/2 snow, 5/6 rain) and light request (127 keeps the light mode).
extern "C" void Map_WeatherEnvironmentSet(std::int8_t environment, std::uint32_t lightRequest) {
    WeatherControlHostImpl host;
    sh::maps::map_runtime().weather_control().environment_set(host, weather_features(weather()->params()),
                                                               environment, lightRequest);
}

// A new density level; 1 when it was taken (no transition running).
extern "C" std::int32_t Map_WeatherDensitySet(std::int32_t level) {
    WeatherControlHostImpl host;
    return sh::maps::map_runtime().weather_control().density_set(host, weather_features(weather()->params()), level)
               ? 1
               : 0;
}

// One weather update; `phase` 0 or -1 starts the map's weather, anything else steps it.
extern "C" void Map_WeatherUpdate(std::int32_t, std::int32_t, std::int32_t phase) {
    WeatherHostImpl weatherHost;
    fx::WeatherSystem* system = weather();
    if (system->suspended(weatherHost)) {
        return;
    }

    sh::maps::MapRuntime& runtime = sh::maps::map_runtime();
    sh::maps::WeatherControl& control = runtime.weather_control();
    sh::maps::WeatherLight& snowLight = runtime.snow_light();
    sh::maps::WeatherLight& rainLight = runtime.rain_light();
    const fx::WeatherParams& params = system->params();
    const sh::maps::WeatherFeatures features = weather_features(params);
    const bool starting = phase == 0 || phase == -1;
    if (starting) {
        if (features.particles()) {
            fg::reset_particle_anchor(kWeatherAnchor);
        }
    } else if (features.particles()) {
        // The snow and rain take the light colours of the map's lighting presets.
        const fx::MapLighting lighting = fx::map_lighting();
        const sh::game::Rgbx snow = lighting.snow_color();
        const sh::game::Rgbx rain = lighting.rain_color();
        snowLight = sh::maps::WeatherLight{snow.r, snow.g, snow.b, snow.x};
        rainLight = sh::maps::WeatherLight{rain.r, rain.g, rain.b, rain.x};
    }

    control.prepare_update(features);
    const sh::camera::ViewSystem& view = sh::camera::view_system();
    fx::WeatherFrame frame;
    frame.view_position = fx::Vec3{view.position().x, view.position().y, view.position().z};
    frame.view_yaw = view.angles().y;
    frame.snow_color = rgb(snowLight);
    frame.rain_color = rgb(rainLight);
    system->update(weatherHost, control.words(), frame, phase);
    // The wind the blood splats drift with: on the PS1 the particle code's own wind, so the splats drifted with the
    // snow and rain.
    runtime.set_wind(system->wind_x(), system->wind_z());

    if (!params.insects) {
        return;
    }
    fx::InsectSwarms* swarms = insects();
    if (starting) {
        int mode = 0;
        if (swarms != nullptr) {
            fg::reset_particle_anchor(kBeamAnchor);
            InsectHostImpl host(nullptr, insects_weather_light);
            mode = swarms->start(host);
        }
        set_insect_mode(mode);
        return;
    }
    InsectFrame bugs;
    bugs.mode = insect_mode();
    if (swarms == nullptr || bugs.mode == 0) {
        return;
    }
    bugs.paused = control.words().paused;
    bugs.light = rgb(snowLight);
    // The flashlight cone of the swarm: nothing in the game writes its size (a zero word of the first map's data), so
    // the insects never see it.
    bugs.cone_size = 0;
    const sh::world::AtmosphereState& atmosphere = sh::world::atmosphere().state();
    bugs.cone_origin = fx::Vec3{atmosphere.light_position.x, atmosphere.light_position.y, atmosphere.light_position.z};
    bugs.cone_rotation[0] = atmosphere.light_direction.x;
    bugs.cone_rotation[1] = atmosphere.light_direction.y;
    bugs.cone_rotation[2] = atmosphere.light_direction.z;
    InsectHostImpl host(&bugs, nullptr);
    swarms->update(host, bugs.mode);
}
