// SPDX-License-Identifier: GPL-3.0-or-later
// The ground effect sets by name (named_effects.h, gamedata/effects/ground_fx.json; map4_s03: the Twinfeeler's holes
// and dust): the module's frames, lighting, palettes and primitives on the engine projection and the frame's world
// layer.

#include <array>
#include <cstdint>
#include <string>

#include "engine/effects/effect_world.h"
#include "engine/effects/ground_fx.h"
#include "engine/effects/named_effects.h"
#include "engine/effects/named_effects_host.h"
#include "engine/game_state/game_state.h"
#include "engine/loop/game_random.h"
#include "engine/world/fixed_trig.h"
#include "engine/world/world_atmosphere.h"
#include "render/frame/frame.h"

namespace sh::effects {

namespace {

namespace ew = effect_world;
namespace d3 = draw3d;

// A projected grid point as the quads that take it need it: the screen position, the precise and view-space record.
struct GridPoint {
    math::PreciseVertex p;
    bool projected = false;
    bool casts_shadow = true;
    float fade = 0.f;
};

struct Instance {
    explicit Instance(GroundFxParams p) : fx(std::move(p)) {}
    GroundFx fx;
    math::Transform world{math::Matrix3{{{0, 0, 0}, {0, 0, 0}, {0, 0, 0}}}, {}};  // world-screen at the grid origin
    math::Transform local{math::Matrix3{{{0, 0, 0}, {0, 0, 0}, {0, 0, 0}}}, {}};  // lighting frame of the last point
    // The points of the last grid projection; one spare entry past the nine.
    std::array<GridPoint, 10> grid{};
};

named::Registry<Instance>& registry() {
    static named::Registry<Instance> r;
    return r;
}

Instance* set(const char* name) {
    return registry().find(name, "ground_fx.json", "ground effect sets",
                           [](const std::string& g, const std::string& m, std::string* s) {
                               return load_ground_fx(g, m, s);
                           });
}

math::Vec3s sv(const Vec3s& v) { return {v.x, v.y, v.z}; }

class Host final : public GroundFxHost {
public:
    explicit Host(Instance& in) : in_(in), view_(named::begin_view()) {}
    ~Host() override { named::end_view(view_); }

    std::int32_t rand16() override { return loop::game_random().next15(); }
    std::int32_t sin(std::int32_t a) override { return world::fixed_sin(a); }
    std::int32_t cos(std::int32_t a) override { return world::fixed_cos(a); }
    std::int32_t delta_time() override { return game_state::delta_time(); }
    std::int32_t ordering_size() override { return ew::kWorldSlots; }

    bool texture(const std::string& name, std::uint16_t& page, std::uint16_t& px, std::uint16_t& py) override {
        return ew::texture_place(name, page, px, py);
    }
    void read_palette_row(int x, int y, std::uint16_t out[16]) override { ew::read_palette_row(x, y, out); }
    void write_palette_rows(int x, int y, int rows, const std::uint16_t* data) override {
        ew::write_palette_rows(x, y, rows, data);
    }

    std::int32_t ground_height(std::int32_t x, std::int32_t z) override { return game_state::ground_height(x, z); }
    std::int32_t owner_heading(std::uint64_t owner) override {
        return ew::chara_heading(reinterpret_cast<const void*>(static_cast<std::uintptr_t>(owner)));
    }
    std::int32_t view_yaw() override { return ew::view_yaw(); }

    void begin_frame(std::int32_t ox, std::int32_t oz, const Vec3s& rotation) override {
        in_.world = named::world_screen_at(Vec3{ox, 0, oz});
        view_.set_frame(in_.world);
        view_.multiply_rotation(math::rot_matrix_xyz(sv(rotation)));
    }

    void frame_view() override { view_.set_frame(in_.world); }

    void local_frame(const Vec3s& at, std::int32_t yaw) override {
        in_.local.rot = math::rot_matrix_xyz({0, static_cast<std::int16_t>(yaw), 0});
        in_.local.trans = {(in_.fx.origin_x() >> 4) + at.x, 0, (in_.fx.origin_z() >> 4) + at.z};
    }

    void view_at(std::int32_t yaw, const Vec3s& at) override {
        view_.set_frame(frame_at(in_.world, sv(at)));
        view_.multiply_rotation(math::rot_matrix_zxy_neg({0, static_cast<std::int16_t>(yaw), 0}));
    }

    void push_view() override { view_.push(); }
    void pop_view() override { view_.pop(); }

    // The lighting loads its own depth cue into the game's projection; the following projections take it.
    std::uint8_t light(const Vec3s& point, const Vec3s& normal) override {
        const std::uint8_t amount = ew::ground_light(point, normal, in_.local);
        view_.set_screen(math::game_view_projection().screen());
        return amount;
    }

    std::uint32_t tint(std::uint8_t amount) override {
        const world::Color8 c = world::atmosphere().faded_tint(0, amount);
        return static_cast<std::uint32_t>(c.r) | (static_cast<std::uint32_t>(c.g) << 8) |
               (static_cast<std::uint32_t>(c.b) << 16);
    }

    std::int32_t geom_screen() override { return view_.screen_distance(); }

    GroundProjected project_origin() override {
        const math::PreciseVertex r = view_.project({0, 0, 0});
        return GroundProjected{r.v.sx, r.v.sy, view_.order_depth(), view_.clamps().too_near};
    }

    GroundProjected project4(const std::array<Vec3s, 4>& pts, std::array<GroundProjected, 4>& out) override {
        const auto r = view_.project4({sv(pts[0]), sv(pts[1]), sv(pts[2]), sv(pts[3])});
        for (std::size_t i = 0; i < 4; i++) out[i] = GroundProjected{r[i].v.sx, r[i].v.sy, 0, false};
        return GroundProjected{0, 0, view_.order_depth(), view_.clamps().too_near};
    }

    std::int32_t project_grid(const std::array<Vec3s, 9>& pts) override {
        const frame::State& st = frame::state();
        for (std::size_t j = 0; j < 3; j++) {
            const auto r = view_.project3({sv(pts[j * 3]), sv(pts[j * 3 + 1]), sv(pts[j * 3 + 2])});
            for (std::size_t k = 0; k < 3; k++) in_.grid[j * 3 + k] = GridPoint{r[k], true, !st.no_shadow_cast, st.chara_fade};
        }
        return view_.order_depth();
    }

    void box(std::int16_t x, std::int16_t y, std::int16_t w, std::int16_t h, const std::array<std::uint32_t, 4>& c,
             std::int32_t slot, bool drawn) override {
        if (!drawn) return;
        const std::int16_t xs[4] = {x, static_cast<std::int16_t>(x + w), x, static_cast<std::int16_t>(x + w)};
        const std::int16_t ys[4] = {y, y, static_cast<std::int16_t>(y + h), static_cast<std::int16_t>(y + h)};
        d3::Polygon q;
        q.count = 4;
        q.gouraud = true;
        q.shade = {true, false};
        for (std::size_t i = 0; i < 4; i++) {
            q.v[i].xy = {xs[i], ys[i]};
            q.v[i].color = named::rgb(c[i]);
        }
        named::submit(q, view_, slot);
    }

    void draw_page(std::int32_t slot, int, int dtd, std::uint16_t page) override {
        named::submit_mode(slot, page, dtd != 0);
    }

    void mark(const std::array<GroundProjected, 4>& p, std::uint32_t colour, const std::array<std::uint8_t, 4>& uv,
              std::uint16_t page, std::uint16_t palette, std::int32_t slot) override {
        const d3::TexCoord st[4] = {{uv[0], uv[1]}, {uv[2], uv[1]}, {uv[0], uv[3]}, {uv[2], uv[3]}};
        d3::Polygon q;
        q.count = 4;
        q.textured = true;
        q.shade = {true, false};
        q.page = d3::TexturePage::from_word(page);
        q.palette = d3::Palette::from_word(palette);
        for (std::size_t i = 0; i < 4; i++) {
            q.v[i].xy = {p[i].x, p[i].y};
            q.v[i].uv = st[i];
        }
        q.v[0].color = named::rgb(colour);
        named::submit(q, view_, slot);
    }

    // The corners take the screen position, the precise position and the view-space position their grid point was
    // projected to.
    void grid_quad(const GroundGridQuad& q, std::int32_t slot) override {
        d3::Polygon p;
        p.count = 4;
        p.textured = true;
        p.gouraud = true;
        p.shade = {true, false};
        p.page = d3::TexturePage::from_word(q.page);
        p.palette = d3::Palette::from_word(q.palette);
        for (std::size_t i = 0; i < 4; i++) {
            const GridPoint& g = in_.grid[q.corner[i]];
            d3::Vertex& v = p.v[i];
            v.xy = {g.p.v.sx, g.p.v.sy};
            v.uv = {static_cast<std::uint8_t>(q.uv[i]), static_cast<std::uint8_t>(q.uv[i] >> 8)};
            v.color = named::rgb(q.color[i]);
            if (g.projected) {
                v.precise = d3::Precise{g.p.sx, g.p.sy, g.p.w};
                v.view = d3::ViewPosition{g.p.view_x, g.p.view_y, g.p.view_z, g.casts_shadow, g.fade,
                                          g.p.ofx,    g.p.ofy,    g.p.h};
            }
        }
        named::submit(p, view_, slot);
    }

private:
    Instance& in_;
    EffectView view_;
};

}  // namespace

void GroundFxSets::reset(const char* name, const Vec3& at) {
    if (Instance* in = set(name)) {
        Host host(*in);
        in->fx.reset(host, at);
    }
}

void GroundFxSets::dust_at(const char* name, const Vec3& at, std::int32_t index) {
    if (Instance* in = set(name)) in->fx.dust_at(at, index);
}

std::int32_t GroundFxSets::dust_free(const char* name, const Vec3& at, std::int32_t kind) {
    if (Instance* in = set(name)) return in->fx.dust_free(at, kind);
    return -1;
}

void GroundFxSets::mark_at(const char* name, const Vec3& at, std::int32_t index) {
    if (Instance* in = set(name)) in->fx.mark_at(at, index);
}

bool GroundFxSets::hole(const char* name, std::int32_t timeline, const void* chara, const Vec3& position) {
    Instance* in = set(name);
    if (in == nullptr || chara == nullptr) return false;
    Host host(*in);
    return in->fx.hole(host, timeline, static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(chara)), position);
}

void GroundFxSets::frame(const char* name, const Vec3s& rotation) {
    if (Instance* in = set(name)) {
        Host host(*in);
        in->fx.frame(host, rotation);
    }
}

GroundFxSets& ground_fx_sets() {
    static GroundFxSets s;
    return s;
}

}  // namespace sh::effects
