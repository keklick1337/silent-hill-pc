// SPDX-License-Identifier: GPL-3.0-or-later
// The glow fields by name (named_effects.h, gamedata/effects/glow_fields.json): the field module's views,
// projections and primitives on the engine projection and the frame's world layer.

#include "engine/camera/view_system.h"
#include <cstdint>
#include <string>

#include "engine/camera/view_space.h"
#include "engine/effects/effect_world.h"
#include "engine/effects/glow_field.h"
#include "engine/effects/named_effects.h"
#include "engine/effects/named_effects_host.h"
#include "engine/loop/game_random.h"
#include "engine/math/precise_capture.h"
#include "render/game_view.h"

namespace sh::effects {

namespace {

namespace ew = effect_world;

named::Registry<GlowField>& registry() {
    static named::Registry<GlowField> r;
    return r;
}

GlowField* field(const char* name) {
    return registry().find(name, "glow_fields.json", "glow fields",
                           [](const std::string& g, const std::string& m, std::string* s) {
                               return load_glow_fields(g, m, s);
                           });
}

const void* ref(GlowAnchor a) { return reinterpret_cast<const void*>(a); }

std::int16_t q8(std::int32_t q12) { return static_cast<std::int16_t>(q12 >> 4); }

// A glow field covers the whole 320x224 frame but is drawn in the world layer, so it inherits the world pass's Hor+
// widening and vertical crop (both there for 3D geometry) and would come out as a 4:3 box that stops short of the
// bottom. Its corners are mapped onto the world ortho's own extent instead: every cell edge goes through the same
// affine map, so shared edges stay shared. Identity at 4:3, where the ortho is the frame itself.
struct ScreenMap {
    float sx = 1.0f, ox = 0.0f, sy = 1.0f, oy = 0.0f;

    ScreenMap() {
        float ortho[4];
        float disp[2];
        if (!ew::world_ortho(ortho, disp) || disp[0] <= 0.0f || disp[1] <= 0.0f) return;
        // Primitive coordinates are centre-origin, the ortho is in display coordinates.
        sx = (ortho[1] - ortho[0]) / disp[0];
        ox = ortho[0] + (disp[0] * 0.5f) * sx - (disp[0] * 0.5f);
        sy = (ortho[3] - ortho[2]) / disp[1];
        oy = ortho[2] + (disp[1] * 0.5f) * sy - (disp[1] * 0.5f);
    }

    static std::int16_t map(std::int16_t v, float s, float o) {
        const float f = static_cast<float>(v) * s + o;
        return static_cast<std::int16_t>((f >= 0.0f) ? (f + 0.5f) : (f - 0.5f));
    }
};

class Host final : public GlowFieldHost {
public:
    Host() : view_(named::begin_view()), ws_(named::world_screen()) {}
    ~Host() override { named::end_view(view_); }

    std::int32_t rand16() override { return loop::game_random().next15(); }

    void emit_quad(const GlowQuad& q, std::int32_t slot) override {
        if (!q.lit || slot < 0 || slot >= ew::kWorldSlots) return;
        // The first colour word's top byte is the quad's code; the others' are the bytes the renderer reads the
        // corners' fog from (corner 0 shares corner 1's).
        const std::uint32_t code = q.color[0] >> 24;
        draw3d::Polygon p;
        p.count = 4;
        p.gouraud = true;
        p.shade = {(code & 2) != 0, (code & 1) != 0};
        for (int i = 0; i < 4; i++) {
            p.v[i].xy = q.projected ? draw3d::Point{q.x[i], q.y[i]}
                                    : draw3d::Point{ScreenMap::map(q.x[i], map_.sx, map_.ox),
                                                    ScreenMap::map(q.y[i], map_.sy, map_.oy)};
            p.v[i].color = named::rgb(q.color[i]);
            p.v[i].fog = static_cast<std::uint8_t>(q.color[i == 0 ? 1 : i] >> 24);
        }
        named::submit(p, view_, slot);
    }

    void emit_page(std::int32_t slot, std::int32_t tpage, std::int32_t dtd) override {
        if (slot >= 0 && slot < ew::kWorldSlots) named::submit_mode(slot, tpage, dtd != 0);
    }

    // A mesh with a transform: the world-screen frame moved to its point (Q8) and turned by its rotation; one
    // without keeps the frame the emitters left.
    void begin_mesh(const GlowMesh& m) override {
        if (!m.has_transform) return;
        view_.set_frame(frame_at(ws_, {m.position[0], m.position[1], m.position[2]}));
        view_.multiply_rotation(math::rot_matrix_zxy_neg({m.rotation[0], m.rotation[1], m.rotation[2]}));
    }

    void project4(const std::int16_t (&v)[4][3], std::int16_t (&x)[4], std::int16_t (&y)[4]) override {
        const auto r = view_.project4({math::Vec3s{v[0][0], v[0][1], v[0][2]}, math::Vec3s{v[1][0], v[1][1], v[1][2]},
                                       math::Vec3s{v[2][0], v[2][1], v[2][2]}, math::Vec3s{v[3][0], v[3][1], v[3][2]}});
        for (int k = 0; k < 4; k++) {
            x[k] = r[static_cast<std::size_t>(k)].v.sx;
            y[k] = r[static_cast<std::size_t>(k)].v.sy;
        }
    }

    // The emitters' views. Each leaves the drawing frame where the original left it, which a mesh without a
    // transform draws with.

    // A point: the world-screen frame moved to the owner's position (Q12 -> Q8) and turned by its rotation (Z X Y
    // negated) on top of the world-screen rotation. The drawing frame keeps the world-screen rotation.
    void view_point(GlowAnchor at, GlowAnchor turn) override {
        const Vec3 pos = named::position_at(ref(at));
        const math::Transform f = frame_at(ws_, {q8(pos.x), q8(pos.y), q8(pos.z)});
        emitter_.trans = f.trans;
        const math::Matrix3 turned = math::rot_matrix_zxy_neg(named::angles_at(ref(turn)));
        emitter_.rot = math::mul_matrix(ws_.rot, turned);
        math::capture::register_camera_turn(ws_, turned, emitter_);
        view_.set_frame(f);
    }

    // A skeleton: its root bone's world frame relative to the eye, scaled (Q12), in the eye's world-to-view space.
    void view_skeleton(GlowAnchor skeleton, std::int32_t scale) override {
        math::Transform m = named::bone_world(ref(skeleton), 1);
        const camera::WorldScreen ws = camera::view_system().world_screen();
        m.trans = {m.trans.x - ws.eye.t[0], m.trans.y - ws.eye.t[1], m.trans.z - ws.eye.t[2]};
        m.rot = math::scale_matrix(m.rot, {scale, scale, scale});
        camera::Matrix in;
        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 3; ++c) in.m[r][c] = m.rot.m[r][c];
        }
        in.t[0] = m.trans.x;
        in.t[1] = m.trans.y;
        in.t[2] = m.trans.z;
        const camera::Matrix out = camera::compose(ws.world_to_view, in);
        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 3; ++c) emitter_.rot.m[r][c] = out.m[r][c];
        }
        emitter_.trans = {out.t[0], out.t[1], out.t[2]};
        math::Transform key;
        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 3; ++c) key.rot.m[r][c] = ws.world_screen.m[r][c];
        }
        key.trans = {ws.world_screen.t[0], ws.world_screen.t[1], ws.world_screen.t[2]};
        if (const math::capture::CameraTwin* twin = math::capture::camera_of(key.rot, key.trans)) {
            // The same frame seen by the unquantised camera, from the bone before the eye's integer translation.
            const math::Transform bone = named::bone_world(ref(skeleton), 1);
            const double d[3] = {bone.trans.x - twin->eye[0], bone.trans.y - twin->eye[1], bone.trans.z - twin->eye[2]};
            double rot[9], trans[3];
            for (int r = 0; r < 3; ++r) {
                for (int c = 0; c < 3; ++c) {
                    rot[r * 3 + c] = (twin->rot[r * 3] * m.rot.m[0][c] + twin->rot[r * 3 + 1] * m.rot.m[1][c] +
                                      twin->rot[r * 3 + 2] * m.rot.m[2][c]) /
                                     4096.0;
                }
                trans[r] = twin->rot[r * 3] * d[0] + twin->rot[r * 3 + 1] * d[1] + twin->rot[r * 3 + 2] * d[2];
            }
            math::capture::register_derived(emitter_, rot, trans);
        }
    }

    // A ray: placed at its owner (Q12 -> Q8 through the world-screen frame), its axes aimed at the target (yaw from
    // x/z, pitch from the height), rolled 75 degrees about z, then put in the world-screen rotation. The drawing
    // frame becomes the ray's.
    void view_ray(GlowAnchor at, GlowAnchor target) override {
        const Vec3 from = named::position_at(ref(at));
        const Vec3 to = named::position_at(ref(target));
        const math::Transform f = frame_at(ws_, {q8(from.x), q8(from.y), q8(from.z)});
        emitter_.trans = f.trans;
        math::Matrix3 rot = aim(from, to);
        rot = math::mul_matrix(rot, math::rot_matrix_xyz({0, 0, kRayRoll}));
        emitter_.rot = math::mul_matrix(ws_.rot, rot);
        math::capture::register_camera_turn(ws_, rot, emitter_);
        view_.set_frame(emitter_);
    }

    Projected project_view(const std::int16_t (&v)[3]) override {
        view_.set_frame(emitter_);
        const math::PreciseVertex r = view_.project({v[0], v[1], v[2]});
        return Projected{r.v.sx, r.v.sy, static_cast<std::int16_t>(view_.order_depth())};
    }

    void bone_translation(GlowAnchor skeleton, int bone, std::int32_t (&t)[3]) override {
        const math::Vec3i v = named::bone_local_translation(ref(skeleton), bone);
        t[0] = v.x;
        t[1] = v.y;
        t[2] = v.z;
    }

    std::int32_t timer(GlowAnchor timer) override { return named::timer_at(ref(timer)); }

    void flash_cue(std::int32_t sound, std::int32_t rumble) override {
        ew::play_sound_flat(sound, 0xFF);
        ew::rumble(rumble);
    }

private:
    // 75 degrees as a Q12 angle (truncated, as the game's constant).
    static constexpr std::int16_t kRayRoll = 853;

    // The rotation aimed from `from` at `to`: a yaw about y (its sine and cosine from the x/z offset) times a pitch
    // about x (from the height over the full distance); Q12 products wrap at 32 bits as the original's.
    static math::Matrix3 aim(const Vec3& from, const Vec3& to) {
        const auto sq = [](std::int32_t a) {
            return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) * static_cast<std::uint32_t>(a)) >> 12;
        };
        const auto shl12 = [](std::int32_t a) {
            return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) << 12);
        };
        const std::int32_t dx = to.x - from.x;
        const std::int32_t dy = to.y - from.y;
        const std::int32_t dz = to.z - from.z;
        const std::int32_t xx = sq(dx), zz = sq(dz), yy = sq(dy);

        const std::int32_t flat = sqrt_q12(xx + zz);
        const std::int32_t yaw_s = flat != 0 ? shl12(dx) / flat : 0;
        const std::int32_t yaw_c = flat != 0 ? shl12(dz) / flat : 4096;
        const std::int32_t full = sqrt_q12(yy + xx + zz);
        const std::int32_t pitch_s = full != 0 ? -shl12(dy) / full : 0;
        const std::int32_t pitch_c = full != 0 ? shl12(flat) / full : 4096;

        math::Matrix3 yaw;
        math::Matrix3 pitch;
        yaw.m[0][0] = static_cast<std::int16_t>(yaw_c);
        yaw.m[0][1] = 0;
        yaw.m[0][2] = static_cast<std::int16_t>(yaw_s);
        yaw.m[1][0] = 0;
        yaw.m[1][1] = 4096;
        yaw.m[1][2] = 0;
        yaw.m[2][0] = static_cast<std::int16_t>(-yaw_s);
        yaw.m[2][1] = 0;
        yaw.m[2][2] = static_cast<std::int16_t>(yaw_c);
        pitch.m[0][0] = 4096;
        pitch.m[0][1] = 0;
        pitch.m[0][2] = 0;
        pitch.m[1][0] = 0;
        pitch.m[1][1] = static_cast<std::int16_t>(pitch_c);
        pitch.m[1][2] = static_cast<std::int16_t>(-pitch_s);
        pitch.m[2][0] = 0;
        pitch.m[2][1] = static_cast<std::int16_t>(pitch_s);
        pitch.m[2][2] = static_cast<std::int16_t>(pitch_c);
        return math::mul_matrix(yaw, pitch);
    }

    EffectView view_;
    math::Transform ws_;
    ScreenMap map_;
    // The frame the emitter views project with; an unset one is zero, as the original's.
    math::Transform emitter_{math::Matrix3{{{0, 0, 0}, {0, 0, 0}, {0, 0, 0}}}, {}};
};

}  // namespace

void GlowFields::reset(const char* name) {
    if (GlowField* f = field(name)) f->reset();
}

void GlowFields::decay(const char* name) {
    if (GlowField* f = field(name)) {
        Host host;
        f->decay(host);
    }
}

void GlowFields::draw(const char* name) {
    if (GlowField* f = field(name)) {
        Host host;
        f->draw(host);
    }
}

void GlowFields::draw_mesh(const char* name, const char* mesh) {
    GlowField* f = field(name);
    if (f == nullptr || mesh == nullptr) return;
    Host host;
    if (!f->draw_mesh(host, mesh)) {
        static std::string s_warned;
        if (s_warned != mesh) {
            s_warned = mesh;
            SH_ERR("[FX] glow field \"%s\" has no mesh \"%s\"", name, mesh);
        }
    }
}

void GlowFields::add_point(const char* name, const void* at, const void* turn) {
    if (GlowField* f = field(name)) f->add_point(reinterpret_cast<GlowAnchor>(at), reinterpret_cast<GlowAnchor>(turn));
}

void GlowFields::add_bones(const char* name, const void* at, const void* skeleton, const void* timer) {
    if (GlowField* f = field(name)) {
        Host host;
        f->add_bones(host, reinterpret_cast<GlowAnchor>(at), reinterpret_cast<GlowAnchor>(skeleton),
                     reinterpret_cast<GlowAnchor>(timer));
    }
}

void GlowFields::add_ray(const char* name, const void* at, const void* target) {
    if (GlowField* f = field(name)) {
        Host host;
        f->add_ray(host, reinterpret_cast<GlowAnchor>(at), reinterpret_cast<GlowAnchor>(target));
    }
}

void GlowFields::add_flash(const char* name, const void* timer) {
    if (GlowField* f = field(name)) f->add_flash(reinterpret_cast<GlowAnchor>(timer));
}

void GlowFields::remove(const char* name, const char* kind) {
    static const struct {
        const char* name;
        GlowKind kind;
    } kKinds[] = {{"Point", GlowKind::Point}, {"Bones", GlowKind::Bones}, {"Ray", GlowKind::Ray},
                  {"Flash", GlowKind::Flash}};
    GlowField* f = field(name);
    if (f == nullptr || kind == nullptr) return;
    for (const auto& k : kKinds) {
        if (std::string(kind) == k.name) {
            f->remove(k.kind);
            return;
        }
    }
    SH_ERR("[FX] no glow emitter kind \"%s\"", kind);
}

void GlowFields::clear_emitters(const char* name) {
    if (GlowField* f = field(name)) f->clear_emitters();
}

void GlowFields::emit(const char* name) {
    if (GlowField* f = field(name)) {
        Host host;
        f->emit(host);
    }
}

GlowFields& glow_fields() {
    static GlowFields f;
    return f;
}

}  // namespace sh::effects
