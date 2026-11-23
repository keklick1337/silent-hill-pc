// SPDX-License-Identifier: GPL-3.0-or-later
// Heat fields, lens flares, fire pools and bolt pools by name (named_effects.h; gamedata/effects/heat_fields.json,
// lens_flares.json, fire_pools.json, bolt_pools.json): the modules' views, projections and primitives on the engine
// projection and the frame's world layer.

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>

#include "engine/effects/bolt_pool.h"
#include "engine/effects/effect_world.h"
#include "engine/effects/fire_pool.h"
#include "engine/effects/heat_field.h"
#include "engine/effects/lens_flare.h"
#include "engine/effects/map_lights.h"
#include "engine/effects/named_effects.h"
#include "engine/effects/named_effects_host.h"
#include "engine/game_state/game_state.h"
#include "engine/loop/game_random.h"
#include "engine/world/fixed_trig.h"

namespace sh::effects {

namespace {

namespace ew = effect_world;
namespace d3 = draw3d;

// A Gouraud quad; the low bits of the primitive code pick the shading. With `fog_bytes` the colour words' high bytes
// are the corners' fog amounts (the colours were written whole after the code, into the bytes the renderer reads
// the fog from; corner 0 shares corner 1's).
d3::Polygon gouraud_quad(const SplashQuad& q, std::int32_t prim_code, bool fog_bytes) {
    const std::uint32_t code = static_cast<std::uint8_t>(prim_code);
    d3::Polygon p;
    p.count = 4;
    p.gouraud = true;
    p.shade = {(code & 2) != 0, (code & 1) != 0};
    for (int i = 0; i < 4; i++) {
        p.v[i].xy = {static_cast<std::int16_t>(q.x[i]), static_cast<std::int16_t>(q.y[i])};
        p.v[i].color = named::rgb(q.color[i]);
        if (fog_bytes) p.v[i].fog = static_cast<std::uint8_t>(q.color[i == 0 ? 1 : i] >> 24);
    }
    return p;
}

// A semi-transparent flat textured quad (the colour word's high byte is the code's, not the colour's).
d3::Polygon sprite_quad(const std::array<std::int16_t, 4>& x, const std::array<std::int16_t, 4>& y,
                        const std::array<std::uint8_t, 4>& u, const std::array<std::uint8_t, 4>& v,
                        std::uint32_t color, std::uint16_t tpage, std::uint16_t clut) {
    d3::Polygon p;
    p.count = 4;
    p.textured = true;
    p.shade = {true, false};
    p.page = d3::TexturePage::from_word(tpage);
    p.palette = d3::Palette::from_word(clut);
    for (int i = 0; i < 4; i++) {
        p.v[i].xy = {x[static_cast<std::size_t>(i)], y[static_cast<std::size_t>(i)]};
        p.v[i].uv = {u[static_cast<std::size_t>(i)], v[static_cast<std::size_t>(i)]};
    }
    p.v[0].color = named::rgb(color);
    return p;
}

// A slot past the world layer (a depth the original linked beyond the table's end) is not drawn.
bool in_layer(std::int32_t slot) { return slot >= 0 && slot < ew::kWorldSlots; }

std::int32_t clamp_slot(std::int32_t slot) { return std::clamp(slot, 0, ew::kWorldSlots - 1); }

// The frame of an effect drawn around a point: the world-screen frame moved to the origin (Q8 world units) and
// turned by yaw.
void view_around(EffectView& view, const Vec3& origin_q8, std::int32_t yaw) {
    view.set_frame(frame_at_long(named::world_screen(), {origin_q8.x, origin_q8.y, origin_q8.z}));
    view.multiply_rotation(math::rot_matrix_zxy_neg({0, static_cast<std::int16_t>(yaw), 0}));
}

ScreenPoint project_origin(EffectView& view) {
    const math::PreciseVertex r = view.project({0, 0, 0});
    return ScreenPoint{r.v.sx, r.v.sy, view.order_depth()};
}

// ---- heat fields ----

named::Registry<HeatField>& heat_registry() {
    static named::Registry<HeatField> r;
    return r;
}

HeatField* heat(const char* name) {
    return heat_registry().find(name, "heat_fields.json", "heat fields",
                                [](const std::string& g, const std::string& m, std::string* s) {
                                    return load_heat_fields(g, m, s);
                                });
}

class HeatHost final : public HeatFieldHost {
public:
    explicit HeatHost(const HeatField& f) : field_(f), view_(named::begin_view()) {}
    ~HeatHost() override { named::end_view(view_); }

    std::int32_t rand16() override { return loop::game_random().next15(); }
    std::int32_t sin(std::int32_t a) override { return world::fixed_sin(a); }
    std::int32_t cos(std::int32_t a) override { return world::fixed_cos(a); }
    std::int32_t atan2(std::int32_t y, std::int32_t x) override { return world::fixed_atan2(y, x); }
    std::int32_t delta_time() override { return game_state::delta_time(); }
    std::int32_t sqrt12(std::int32_t v) override { return sqrt_q12(v); }
    Vec3 normalize(const Vec3& v) override {
        const math::Vec3i n = math::vector_normal({v.x, v.y, v.z});
        return Vec3{n.x, n.y, n.z};
    }

    void begin_view(const Vec3& origin_q8, std::int32_t yaw) override { view_around(view_, origin_q8, yaw); }
    ScreenPoint project_origin() override { return sh::effects::project_origin(view_); }
    ScreenPoint project(const Vec3s& v) override {
        const math::PreciseVertex r = view_.project({v.x, v.y, v.z});
        return ScreenPoint{r.v.sx, r.v.sy, 0};
    }

    void emit_quad(const SplashQuad& q, std::int32_t slot) override {
        if (in_layer(slot)) named::submit(gouraud_quad(q, field_.params().prim_code, false), view_, slot);
    }
    void emit_mode(std::int32_t slot, std::int32_t tpage, std::int32_t, std::int32_t dtd) override {
        if (in_layer(slot)) named::submit_mode(slot, tpage, dtd != 0);
    }

private:
    const HeatField& field_;
    EffectView view_;
};

// ---- lens flares ----

named::Registry<LensFlare>& flare_registry() {
    static named::Registry<LensFlare> r;
    return r;
}

LensFlare* flare(const char* name) {
    return flare_registry().find(name, "lens_flares.json", "lens flares",
                                 [](const std::string& g, const std::string& m, std::string* s) {
                                     return load_lens_flares(g, m, s);
                                 });
}

class FlareHost final : public LensFlareHost {
public:
    explicit FlareHost(const LensFlare& f) : flare_(f), view_(named::begin_view()) {}
    ~FlareHost() override { named::end_view(view_); }

    std::int32_t sin(std::int32_t a) override { return world::fixed_sin(a); }
    std::int32_t cos(std::int32_t a) override { return world::fixed_cos(a); }
    std::int32_t geom_screen() override { return view_.screen_distance(); }
    void begin_view(const Vec3& origin_q8, std::int32_t yaw) override { view_around(view_, origin_q8, yaw); }
    ScreenPoint project_origin() override { return sh::effects::project_origin(view_); }

    void emit_quad(const SplashQuad& q, std::int32_t slot) override {
        if (in_layer(slot)) named::submit(gouraud_quad(q, flare_.params().prim_code, true), view_, slot);
    }
    void emit_mode(std::int32_t slot, std::int32_t tpage, std::int32_t, std::int32_t dtd) override {
        if (in_layer(slot)) named::submit_mode(slot, tpage, dtd != 0);
    }

private:
    const LensFlare& flare_;
    EffectView view_;
};

// ---- fire pools ----

named::Registry<FirePool>& fire_registry() {
    static named::Registry<FirePool> r;
    return r;
}

FirePool* pool(const char* name) {
    return fire_registry().find(name, "fire_pools.json", "fire pools",
                                [](const std::string& g, const std::string& m, std::string* s) {
                                    return load_fire_pools(g, m, s);
                                });
}

// The world-screen frame at the pool origin of the frame; the rolled quads start from it. Kept between calls, as
// the original's.
math::Transform& fire_frame() {
    static math::Transform f{math::Matrix3{{{0, 0, 0}, {0, 0, 0}, {0, 0, 0}}}, {}};
    return f;
}

class FireHost final : public FirePoolHost {
public:
    explicit FireHost(int (*sound_gate)() = nullptr, const void* skeleton = nullptr)
        : gate_(sound_gate), skeleton_(skeleton), view_(named::begin_view()) {}
    ~FireHost() override { named::end_view(view_); }

    std::int32_t rand16() override { return loop::game_random().next15(); }
    void reseed(std::uint32_t seed) override { loop::game_random().set_seed(seed); }
    std::int32_t sin(std::int32_t a) override { return world::fixed_sin(a); }
    std::int32_t cos(std::int32_t a) override { return world::fixed_cos(a); }
    std::int32_t delta_time() override { return game_state::delta_time(); }
    std::int32_t geom_screen() override { return view_.screen_distance(); }
    void flame_shown(const std::array<float, 3>& position, float amount) override {
        fire_particle_shown("FirePool", position, amount);
    }

    void begin_view(const Vec3& origin) override {
        fire_frame() = named::world_screen_at(origin);
        view_.set_frame(fire_frame());
    }

    FireProjection project(const Vec3s& v) override {
        const math::PreciseVertex r = view_.project({v.x, v.y, v.z});
        const math::ProjectionClamps& clamps = view_.clamps();
        return FireProjection{r.v.sx, r.v.sy, view_.order_depth(), clamps.too_near, clamps.depth_clamped};
    }

    // The quad's frame: moved to the centre (through the current rotation, from the pool frame's translation) and
    // turned to face the view, rolled; the frame before it is kept.
    FireRolledProjection project_rolled(const Vec3s& centre, std::int32_t roll,
                                        const std::array<Vec3s, 4>& c) override {
        view_.push();
        const math::Vec3i offset = view_.rotate({centre.x, centre.y, centre.z});
        const math::Vec3i& base = fire_frame().trans;
        view_.set_translation({offset.x + base.x, offset.y + base.y, offset.z + base.z});
        view_.multiply_rotation(math::rot_matrix_zxy(
            {0, static_cast<std::int16_t>(ew::view_yaw()), static_cast<std::int16_t>(-roll)}));
        const auto r = view_.project4({math::Vec3s{c[0].x, c[0].y, c[0].z}, math::Vec3s{c[1].x, c[1].y, c[1].z},
                                       math::Vec3s{c[2].x, c[2].y, c[2].z}, math::Vec3s{c[3].x, c[3].y, c[3].z}});
        FireRolledProjection out;
        for (std::size_t i = 0; i < 4; i++) {
            out.x[i] = r[i].v.sx;
            out.y[i] = r[i].v.sy;
        }
        out.depth = view_.order_depth();
        out.near = view_.clamps().too_near;
        out.far = view_.clamps().depth_clamped;
        view_.pop();
        return out;
    }

    std::int32_t ground_height(std::int32_t x, std::int32_t z) override { return game_state::ground_height(x, z); }
    bool sound_allowed() override { return gate_ == nullptr || gate_() != 0; }
    void play_sound(std::int32_t id, const Vec3& world, std::int32_t volume) override {
        ew::play_sound_at(id, world, volume);
    }

    Vec3 bone_position(int bone) override {
        const math::Vec3i t = named::bone_world(skeleton_, bone).trans;
        return Vec3{t.x << 4, t.y << 4, t.z << 4};
    }

    void emit_sprite(const FireQuad& q) override {
        named::submit(sprite_quad(q.x, q.y, q.u, q.v, q.color, q.tpage, q.clut), view_, clamp_slot(q.slot));
    }

private:
    int (*gate_)();
    const void* skeleton_;
    EffectView view_;
};

// ---- bolt pools ----

named::Registry<BoltPool>& bolt_registry() {
    static named::Registry<BoltPool> r;
    return r;
}

BoltPool* bolts(const char* name) {
    return bolt_registry().find(name, "bolt_pools.json", "bolt pools",
                                [](const std::string& g, const std::string& m, std::string* s) {
                                    return load_bolt_pools(g, m, s);
                                });
}

// The world-screen frame at the bolt pool origin of the frame; each segment's frame starts from it.
math::Transform& bolt_frame() {
    static math::Transform f{math::Matrix3{{{0, 0, 0}, {0, 0, 0}, {0, 0, 0}}}, {}};
    return f;
}

class BoltHost final : public BoltPoolHost {
public:
    BoltHost() : view_(named::begin_view()) {}
    ~BoltHost() override { named::end_view(view_); }

    std::int32_t rand16() override { return loop::game_random().next15(); }
    std::int32_t sin(std::int32_t a) override { return world::fixed_sin(a); }
    std::int32_t cos(std::int32_t a) override { return world::fixed_cos(a); }
    std::int32_t atan2(std::int32_t y, std::int32_t x) override { return world::fixed_atan2(y, x); }
    std::int32_t sqrt12(std::int32_t v) override { return sqrt_q12(v); }
    std::int32_t delta_time() override { return game_state::delta_time(); }
    bool easy() override { return game_state::save_difficulty() == -1; }

    // The turn becomes the drawing rotation, as the original loaded it to apply it.
    Vec3 turn_xyz(std::int16_t ax, std::int16_t ay, std::int16_t length) override {
        view_.set_rotation(math::rot_matrix_xyz({ax, ay, 0}));
        const math::Vec3i out = view_.rotate({0, 0, length});
        return Vec3{out.x, out.y, out.z};
    }

    Vec3 player_position() override {
        const entities::Vec3 p = game_state::chara_position(0);
        return Vec3{p.x, p.y, p.z};
    }

    std::array<std::int32_t, 3> player_chest() override {
        const math::Vec3i t = named::bone_world(ew::player_bones(), 1).trans;
        return {t.x, t.y, t.z};
    }

    std::array<std::int32_t, 3> bone(BoltAnchor bones, int bone) override {
        const math::Vec3i t = named::bone_world(reinterpret_cast<const void*>(bones), bone).trans;
        return {t.x, t.y, t.z};
    }

    bool npc_is(int slot, const std::string& chara) override { return ew::npc_is(slot, chara); }

    void begin_view(std::int32_t x, std::int32_t z) override { bolt_frame() = named::world_screen_at(Vec3{x, 0, z}); }

    BoltPair project_pair(const BoltSegment& seg, const std::array<Vec3s, 2>& v) override {
        math::Transform local;
        for (int r = 0; r < 3; r++) {
            for (int c = 0; c < 3; c++) local.rot.m[r][c] = seg.m[r][c];
        }
        local.trans = {seg.t[0], seg.t[1], seg.t[2]};
        view_.set_frame(math::comp_matrix(bolt_frame(), local));

        BoltPair out;
        const math::PreciseVertex a = view_.project({v[0].x, v[0].y, v[0].z});
        const std::int32_t z0 = view_.order_depth();
        out.xy0 = math::screen_word(a.v);
        if (view_.clamps().too_near) return out;
        const math::PreciseVertex b = view_.project({v[1].x, v[1].y, v[1].z});
        const std::int32_t z1 = view_.order_depth();
        out.xy1 = math::screen_word(b.v);
        out.second = true;
        if (view_.clamps().too_near) return out;
        out.depth = (z0 + z1) / 2;
        return out;
    }

    void emit_quad(const BoltQuad& q) override {
        std::array<std::int16_t, 4> x;
        std::array<std::int16_t, 4> y;
        for (std::size_t i = 0; i < 4; i++) {
            x[i] = static_cast<std::int16_t>(q.xy[i] & 0xFFFF);
            y[i] = static_cast<std::int16_t>(q.xy[i] >> 16);
        }
        named::submit(sprite_quad(x, y, q.u, q.v, q.color, q.tpage, q.clut), view_, clamp_slot(q.slot));
    }

    void hurt_player(std::int32_t attack, std::int32_t amount) override { ew::hurt_player(attack, amount); }
    void play_sound(std::int32_t id, const Vec3& at, std::int32_t volume) override {
        ew::play_sound_at(id, at, volume);
    }

    // The fire pool draws with its own view: this one is handed back to the game around it and taken up again.
    void fire_flash(const std::string& name, const Vec3& at, std::int32_t palette) override {
        named::end_view(view_);
        fire_pools().glow_burst(name.c_str(), at, palette);
        view_ = named::begin_view();
    }
    void fire_burst(const std::string& name, const Vec3& at, std::int32_t palette) override {
        named::end_view(view_);
        fire_pools().burst(name.c_str(), at, palette);
        view_ = named::begin_view();
    }

private:
    EffectView view_;
};

}  // namespace

// ---- heat fields ----

void HeatFields::start(const char* name, const Vec3& at) {
    if (HeatField* f = heat(name)) {
        HeatHost host(*f);
        f->start(host, at);
    }
}

void HeatFields::start_between(const char* name, const Vec3& from, const Vec3& to) {
    if (HeatField* f = heat(name)) {
        HeatHost host(*f);
        f->start_between(host, from, to);
    }
}

void HeatFields::arm(const char* name, const Vec3& at, std::int32_t yaw) {
    if (HeatField* f = heat(name)) f->arm(at, yaw);
}

void HeatFields::stop(const char* name) {
    if (HeatField* f = heat(name)) f->stop();
}

void HeatFields::clear(const char* name) {
    if (HeatField* f = heat(name)) f->clear();
}

void HeatFields::spawn(const char* name, const Vec3& around, std::int32_t count) {
    if (HeatField* f = heat(name)) {
        HeatHost host(*f);
        f->spawn(host, around, count);
    }
}

void HeatFields::update(const char* name, bool advance) {
    if (HeatField* f = heat(name)) {
        HeatHost host(*f);
        f->update(host, advance);
    }
}

std::optional<Vec3> HeatFields::origin(const char* name) {
    if (HeatField* f = heat(name)) return f->state().origin_q8;
    return std::nullopt;
}

HeatFields& heat_fields() {
    static HeatFields f;
    return f;
}

// ---- lens flares ----

void LensFlares::draw(const char* name, std::int32_t origin) {
    if (LensFlare* f = flare(name)) {
        FlareHost host(*f);
        f->draw(host, origin);
    }
}

LensFlares& lens_flares() {
    static LensFlares f;
    return f;
}

// ---- fire pools ----

void FirePools::reset(const char* name) {
    if (FirePool* p = pool(name)) p->reset();
}

void FirePools::pause(const char* name, bool paused) {
    if (FirePool* p = pool(name)) p->set_paused(paused);
}

void FirePools::cue(const char* name, const std::int32_t* record) {
    FirePool* p = pool(name);
    if (p == nullptr || record == nullptr) return;
    FireCue c;
    c.position = Vec3{record[0], record[1], record[2]};
    c.near = record[3];
    c.far = record[4];
    c.lead_min = record[5];
    c.lead_max = record[6];
    c.scale_min = record[7];
    c.scale_max = record[8];
    c.shape = record[9];
    c.kind = record[10];
    c.z_drift = record[11];
    c.fade = record[12];
    c.rate = record[13];
    c.flags = record[14];
    FireHost host;
    p->cue(host, c);
}

void FirePools::glow(const char* name, const Vec3& at, std::int32_t palette, std::int32_t delay,
                     std::int32_t scale) {
    if (FirePool* p = pool(name)) {
        FireHost host;
        p->glow(host, at, palette, delay, scale);
    }
}

void FirePools::burst(const char* name, const Vec3& at, std::int32_t palette) {
    if (FirePool* p = pool(name)) {
        FireHost host;
        p->burst(host, at, palette);
    }
}

void FirePools::glow_burst(const char* name, const Vec3& at, std::int32_t palette) {
    if (FirePool* p = pool(name)) {
        FireHost host;
        p->glow_burst(host, at, palette);
    }
}

void FirePools::update(const char* name, int (*sound_gate)()) {
    if (FirePool* p = pool(name)) {
        FireHost host(sound_gate);
        p->update(host);
    }
}

void FirePools::attached_reset(const char* name) {
    if (FirePool* p = pool(name)) p->attached_reset();
}

void FirePools::attach(const char* name) {
    if (FirePool* p = pool(name)) {
        FireHost host;
        p->attach(host);
    }
}

void FirePools::draw_attached(const char* name, const void* skeleton) {
    FirePool* p = pool(name);
    if (p == nullptr || skeleton == nullptr) return;
    FireHost host(nullptr, skeleton);
    p->draw_attached(host);
}

FirePools& fire_pools() {
    static FirePools f;
    return f;
}

// ---- bolt pools ----

void BoltPools::load(const char* name) {
    if (BoltPool* p = bolts(name)) p->restore_sounds();
}

void BoltPools::attach(const char* name, const char* owner, const Vec3& at, const void* skeleton) {
    BoltPool* p = bolts(name);
    if (p == nullptr || owner == nullptr) return;
    BoltHost host;
    if (!p->attach(host, owner, at.x, at.z, reinterpret_cast<BoltAnchor>(skeleton))) {
        SH_ERR("[FX] bolt pool \"%s\" has no owner \"%s\"", name, owner);
    }
}

std::vector<std::string> BoltPools::owners(const char* name) {
    std::vector<std::string> out;
    if (BoltPool* p = bolts(name)) {
        for (const BoltOwner& o : p->params().owners) {
            out.push_back(o.name);
        }
    }
    return out;
}

void BoltPools::track(const char* name, std::int32_t* lead_x, std::int32_t* lead_z) {
    if (BoltPool* p = bolts(name)) {
        BoltHost host;
        p->track(host);
        if (lead_x != nullptr) *lead_x = p->lead_x();
        if (lead_z != nullptr) *lead_z = p->lead_z();
    }
}

void BoltPools::spawn(const char* name, const char* spawner, const Vec3& from, const Vec3& to, std::int32_t bone,
                      std::int32_t step, std::int32_t life) {
    BoltPool* p = bolts(name);
    if (p == nullptr || spawner == nullptr) return;
    BoltHost host;
    p->spawn(host, spawner, from, to, bone, step, life);
}

void BoltPools::update(const char* name) {
    if (BoltPool* p = bolts(name)) {
        BoltHost host;
        p->update(host);
    }
}

BoltPools& bolt_pools() {
    static BoltPools f;
    return f;
}

}  // namespace sh::effects
