// SPDX-License-Identifier: GPL-3.0-or-later
// The lens flare and water reflection (engine/effects/water.h) and the drain's water drops and ribbon
// (engine/effects/water_stream.h): their hosts over the world environment, the camera, the effect frame record
// and the map's water block, placing polygons by order key. The world drawing calls the flare's frame entry points
// (shLensFlare*), the map effect frame the drops and the ribbon, the map's Lua binds the stream's start and stop.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

#include "engine/app/pc_paths.h"
#include "engine/camera/camera_host.h"
#include "engine/camera/view_space.h"
#include "engine/effects/drawer_projection.h"
#include "engine/effects/game/effect_link.h"
#include "engine/effects/map_effect_view.h"
#include "engine/effects/water.h"
#include "engine/effects/water_stream.h"
#include "engine/game_state/game_state.h"
#include "engine/game_state/session.h"
#include "engine/log/sh_log.h"
#include "engine/loop/frame_view.h"
#include "engine/loop/game_random.h"
#include "engine/math/geometry_state.h"
#include "engine/maps/map_runtime.h"
#include "engine/world/fixed_trig.h"
#include "engine/world/world_atmosphere.h"
#include "render/draw2d/draw2d.h"
#include "render/frame/frame.h"
#include "render/scene/scene.h"
#include "engine/mods/data_reload.h"

extern "C" {
const char* PcPort_GetGameDataPath(void);
// The flashlight colour the player picked (options), applied to the flare.
extern std::int32_t g_PcFlashlightColorActive;
extern unsigned char g_PcFlashlightColorR, g_PcFlashlightColorG, g_PcFlashlightColorB;
// The screen point and angles of a light at `pos` (Q12) facing `rot` (the world drawing's billboard helper).
void Screen_BillboardAnglesCalc(std::int32_t* screen, std::int32_t* azimuth, std::int32_t* altitude,
                                const std::int16_t* rot, const std::int32_t* pos);
// The camera's smooth move mode (0: chasing the player).
std::int32_t vcRetCamMvSmoothF(void);
}

namespace {

namespace fx = sh::effects;
namespace fg = sh::effects::game_link;

constexpr int kCameraChasing = 0;
constexpr std::uint32_t kWaterRunning = 1u << 3;  // map effect flag: the drain's water runs

std::optional<fx::WaterEffects> s_water;
std::uint32_t s_waterEpoch = sh::mods::kNeverRead;

// The scene renderer draws the water's surface: it takes the surface's numbers whenever the file is read.
void give_surface(const fx::WaterParams& p) {
    const auto colour = [](const std::array<std::uint8_t, 3>& c) {
        return std::array<float, 3>{c[0] / 255.0f, c[1] / 255.0f, c[2] / 255.0f};
    };
    sh::scene::WaterSurface w;
    w.material = p.surface_material;
    w.base_color = colour(p.surface_base);
    w.lit_color = colour(p.surface_lit);
    w.wave_length = static_cast<float>(p.wave_length);
    w.wave_contrast = static_cast<float>(p.wave_contrast);
    w.wave_distortion = static_cast<float>(p.wave_distortion);
    w.reflection_strength = static_cast<float>(p.reflection_strength);
    w.reflection_tint = colour(p.reflection_tint);
    w.reflection_blur = static_cast<float>(p.reflection_blur);
    w.plane_offset = static_cast<float>(p.reflection_plane_offset);
    w.glow_strength = static_cast<float>(p.glow_strength);
    w.glow_color = colour(p.glow_color);
    w.glow_size = static_cast<float>(p.glow_size);
    for (std::size_t i = 0; i < w.quality.size(); i++) {
        const fx::WaterQuality& q = p.quality[i];
        w.quality[i] = {q.wave_layers, static_cast<float>(q.wave_grid), q.color_steps, q.phase_steps,
                        static_cast<float>(q.distortion), q.reflection_lines, q.reflection_smooth, q.blur_taps};
    }
    sh::scene::set_water_surface(std::move(w));
}

fx::WaterEffects* water() {
    if (sh::mods::data_stale(s_waterEpoch)) {
        s_water.reset();
        char mods[1024];
        PcPaths_ExeFile(mods, sizeof(mods), "mods");
        std::string source;
        sh::Result<fx::WaterParams> loaded = fx::load_water_params(PcPort_GetGameDataPath(), mods, &source);
        if (loaded) {
            s_water.emplace(std::move(loaded.value()));
            give_surface(s_water->params());
            SH_LOG("[FX] water from %s", source.c_str());
        } else {
            SH_ERR("[FX] %s - there is no flare or water reflection", loaded.error().c_str());
        }
    }
    return s_water ? &*s_water : nullptr;
}

// The flare takes the flashlight's tint (luma-preserving) when the player set one.
fx::Rgb tinted(fx::Rgb c) {
    if (!g_PcFlashlightColorActive) {
        return c;
    }
    const int luma = (c.r + c.g + c.b) / 3;
    return fx::Rgb{static_cast<std::uint8_t>((luma * g_PcFlashlightColorR) / 255),
                   static_cast<std::uint8_t>((luma * g_PcFlashlightColorG) / 255),
                   static_cast<std::uint8_t>((luma * g_PcFlashlightColorB) / 255)};
}

sh::draw3d::Palette palette_of(const fx::TextureRef& t) {
    return sh::draw3d::Palette::from_word(
        static_cast<std::uint16_t>((t.palette_y << 6) | ((t.palette_x >> 4) & 0x3F)));
}

// Texel coordinates and a palette or page packed as the original's 32-bit stores wrote them (added, not or-ed).
struct Texel {
    std::uint8_t u, v;
    std::uint16_t extra;
};
Texel texel(std::uint32_t word) {
    return {static_cast<std::uint8_t>(word), static_cast<std::uint8_t>(word >> 8), static_cast<std::uint16_t>(word >> 16)};
}

sh::frame::OrderKey world_key(std::int32_t slot) { return {sh::frame::Layer::World, static_cast<std::uint32_t>(slot)}; }

class WaterHostImpl final : public fx::WaterHost {
public:
    WaterHostImpl(const fx::WaterParams& /*params*/, bool tint) : tint_(tint) {}

    std::int32_t sin(std::int32_t a) override { return sh::world::fixed_sin(a); }
    std::int32_t cos(std::int32_t a) override { return sh::world::fixed_cos(a); }
    std::int32_t atan2(std::int32_t a, std::int32_t b) override { return sh::world::fixed_atan2(a, b); }
    std::int32_t delta_time() override { return sh::game_state::delta_time(); }
    void screen_size(std::int32_t& w, std::int32_t& h) override {
        w = sh::game::session().game.screen_width;
        h = sh::game::session().game.screen_height;
    }

    fx::FlareView flare_view(const fx::Vec3& pos, const fx::Vec3s& rot) override {
        std::int32_t screen[3] = {};
        std::int32_t azimuth = 0;
        std::int32_t altitude = 0;
        const std::int16_t r[4] = {rot.x, rot.y, rot.z, 0};
        const std::int32_t p[3] = {pos.x, pos.y, pos.z};
        Screen_BillboardAnglesCalc(screen, &azimuth, &altitude, r, p);
        return fx::FlareView{fx::Vec3{screen[0], screen[1], screen[2]}, azimuth, altitude};
    }

    std::int32_t projection_distance() override { return sh::math::geometry().screen_distance(); }

    fx::FacingProbe facing_probe() override {
        // The lamp's beam, probed four units ahead; both lengths in the Q8 of the view matrix.
        const sh::world::AtmosphereState& env = sh::world::detail::g_atmosphere_state;
        const sh::world::Vec3s16& dir = env.light_direction;
        const sh::world::Vec3i32& at = env.light_position;
        const sh::camera::Matrix& ws = sh::camera::camera_host().world_screen();
        const sh::camera::Matrix m0 = sh::camera::world_screen_at(ws, sh::camera::Vec3{at.x, at.y, at.z});
        const sh::camera::Matrix m1 = sh::camera::world_screen_at(
            ws, sh::camera::Vec3{at.x + (static_cast<std::int32_t>(dir.x) << 2), at.y + (static_cast<std::int32_t>(dir.y) << 2),
                                 at.z + (static_cast<std::int32_t>(dir.z) << 2)});
        fx::FacingProbe probe;
        probe.delta_z = m0.t[2] - m1.t[2];
        probe.step_len = sh::world::fixed_sqrt(static_cast<std::int32_t>(dir.x) * dir.x +
                                               static_cast<std::int32_t>(dir.y) * dir.y +
                                               static_cast<std::int32_t>(dir.z) * dir.z) >>
                         2;
        return probe;
    }

    bool camera_chasing() override { return vcRetCamMvSmoothF() == kCameraChasing; }

    fx::ScreenPoint project_mirror(const fx::Vec3& q8) override {
        // The world-screen view moved to the mirrored point, which is projected as the view's origin.
        const sh::camera::Matrix& ws = fg::world_screen_matrix();
        sh::math::ViewProjection& vp = projection_.vp();
        sh::effects::drawer::set_matrix(vp, &ws);
        sh::effects::drawer::move_to(vp, &ws, {q8.x, q8.y, q8.z});
        const sh::math::PreciseVertex r = vp.project({0, 0, 0});
        return fx::ScreenPoint{r.v.sx, r.v.sy, vp.ordering_depth()};
    }

    void emit(const fx::EffectPrim& p) override {
        // A textured quad with one colour, in the packet space a textured quad packet took.
        const fx::Rgb c = tint_ ? tinted(p.rgb[0]) : p.rgb[0];
        sh::draw3d::Polygon q;
        q.count = 4;
        q.textured = true;
        q.shade = {p.semi_transparent, false};
        q.page = sh::draw3d::TexturePage::from_word(p.texture.page);
        q.palette = palette_of(p.texture);
        q.v[0].color = {c.r, c.g, c.b};
        for (int i = 0; i < 4; i++) {
            q.v[i].xy = {p.x[i], p.y[i]};
            q.v[i].uv = {p.u[i], p.v[i]};
        }
        projection_.place(q, world_key(p.slot));
        fg::packet_cursor() += sizeof(fg::packet::QuadTextured);
    }

    void surface_clock(std::int32_t time, std::int32_t period) override {
        sh::scene::set_water_phase(period > 0 ? static_cast<float>(time) / static_cast<float>(period) : 0.0f);
    }

    bool surface_shaded() override { return sh::scene::water_surface_placed(); }

    void surface_glow(const fx::Vec3& lamp, std::int32_t strength) override {
        sh::scene::Frame& f = sh::scene::current_frame();
        f.water_lamp = {static_cast<float>(lamp.x) / 16.0f, static_cast<float>(lamp.y) / 16.0f,
                        static_cast<float>(lamp.z) / 16.0f};
        f.water_glow = static_cast<float>(strength) / 4096.0f;
    }

    void sheen(const fx::SheenDraw& d) override {
        // Each segment is a fan (a gouraud quad and triangle) round the hub and a translucent band (the same pair)
        // past it, in the packet space the four packets took. The band and fan corners are the projected corners
        // with their projection records, so every corner of the sheen shares one view depth and the per-pixel
        // flashlight sees one flat normal.
        constexpr std::size_t kSegmentBytes = 2 * sizeof(fg::packet::QuadGouraud) + 2 * sizeof(fg::packet::TriangleGouraud);

        // The sheen's frame: a turn about y by the yaw, at its place (Q8), under the world-screen view.
        sh::camera::Matrix local{{{static_cast<std::int16_t>(d.cos_yaw), 0, static_cast<std::int16_t>(d.sin_yaw)},
                                  {0, 4096, 0},
                                  {static_cast<std::int16_t>(-d.sin_yaw), 0, static_cast<std::int16_t>(d.cos_yaw)}},
                                 {d.place.x, d.place.y, d.place.z}};
        const sh::camera::Matrix view = sh::camera::view_matrix(sh::camera::view_system().world_screen(), local);
        sh::math::ViewProjection& vp = projection_.vp();
        sh::effects::drawer::set_matrix(vp, &view);
        const sh::math::PreciseVertex hub = vp.project({d.hub.x, d.hub.y, d.hub.z});

        const sh::frame::OrderKey ring = world_key(d.ring_slot);
        const sh::frame::OrderKey band = world_key(d.band_slot);
        unsigned char*& cursor = fg::packet_cursor();
        const auto& col = *d.colors;

        for (int i = 0; i < 8; i++) {
            std::array<sh::math::Vec3s, 4> v;
            for (int k = 0; k < 4; k++) {
                v[k] = {d.corners[i][k].x, d.corners[i][k].y, d.corners[i][k].z};
            }
            const std::array<sh::math::PreciseVertex, 4> xy = vp.project4(v);

            // Corner sources: the fan's triangle is (hub, xy0, xy1); the quads use xy0..xy3.
            const sh::math::PreciseVertex* const quadSrc[4] = {&xy[0], &xy[1], &xy[2], &xy[3]};
            const sh::math::PreciseVertex* const triSrc[3] = {&hub, &xy[0], &xy[1]};
            const auto make = [&](int n, bool translucent, const std::array<fx::Rgb, 4>& c,
                                  const sh::math::PreciseVertex* const* src) {
                sh::draw3d::Polygon q;
                q.count = static_cast<std::uint8_t>(n);
                q.gouraud = true;
                q.shade = {translucent, false};
                // The sheen draws affine and without the per-pixel flashlight (its albedo would blow out white
                // under it).
                q.affine = true;
                q.no_flashlight = true;
                for (int k = 0; k < n; k++) {
                    q.v[k].xy = {src[k]->v.sx, src[k]->v.sy};
                    q.v[k].color = {c[k].r, c[k].g, c[k].b};
                    sh::effects::drawer::corner_records(*src[k], q.v[k]);
                }
                return q;
            };
            sh::draw3d::Polygon fanQuad = make(4, false, col[0], quadSrc);
            sh::draw3d::Polygon fanTri = make(3, false, col[1], triSrc);
            sh::draw3d::Polygon bandQuad = make(4, true, col[2], quadSrc);
            sh::draw3d::Polygon bandTri = make(3, true, col[3], triSrc);
            projection_.place(fanQuad, ring, sh::effects::drawer::SceneDepth::Slot);
            projection_.place(fanTri, ring, sh::effects::drawer::SceneDepth::Slot);
            projection_.place(bandQuad, band, sh::effects::drawer::SceneDepth::Slot);
            projection_.place(bandTri, band, sh::effects::drawer::SceneDepth::Slot);
            cursor += kSegmentBytes;
        }

        sh::draw2d::DrawMode mode;
        mode.page = sh::draw2d::TexturePage::from_word(32);
        mode.dither = true;
        sh::draw2d::submit(mode, band);
        cursor += sizeof(fg::packet::TexturePage);
    }

private:
    bool tint_;
    sh::effects::drawer::DrawerProjection projection_;
};

}  // namespace

extern "C" {

void shLensFlareInit(void) {
    if (fx::WaterEffects* w = water()) {
        w->reset();
    }
}

void Water_EffectEnable(void) {
    if (fx::WaterEffects* w = water()) {
        w->set_flare_suppressed(true);
    }
}

void Water_EffectDisable(void) {
    if (fx::WaterEffects* w = water()) {
        w->set_flare_suppressed(false);
    }
}

void shLensFlareExec(std::int16_t lensFlareIntensity, const std::int16_t* rot, const std::int32_t* pos,
                     const fg::WaterZoneRecord* waterZones) {
    fx::WaterEffects* w = water();
    if (w == nullptr) {
        return;
    }

    fx::WaterZone zones[32];
    std::size_t count = 0;
    for (const fg::WaterZoneRecord* z = waterZones; z != nullptr && z->enabled && count < 32; z++, count++) {
        zones[count] = fx::WaterZone{true, z->illumination, z->min_x, z->max_x, z->min_z, z->max_z};
    }

    WaterHostImpl host(w->params(), false);
    w->frame(host, lensFlareIntensity, fx::Vec3s{rot[0], rot[1], rot[2]}, fx::Vec3{pos[0], pos[1], pos[2]}, zones,
             count);
}

void shLensFlareDraw(void) {
    if (fx::WaterEffects* w = water()) {
        WaterHostImpl host(w->params(), true);
        w->end_frame(host);
    }
}

}  // extern "C"

// ---------------------------------------------------------------------------------------
// The drain's water drops and ribbon (map1_s02, map1_s03; engine: water_stream.h). The maps' header
// slots (gamedata/maps/<map>/overlay.json "effect_draws": "water") and the Lua binds call these; the
// stream table is the one the map header points at, the particle slots are the map-wide table.
// ---------------------------------------------------------------------------------------

namespace {

std::optional<fx::WaterStream> s_stream;
std::uint32_t s_streamEpoch = sh::mods::kNeverRead;

const fx::WaterStream* stream() {
    if (sh::mods::data_stale(s_streamEpoch)) {
        s_stream.reset();
        char mods[1024];
        PcPaths_ExeFile(mods, sizeof(mods), "mods");
        std::string source;
        sh::Result<fx::WaterStreamParams> loaded =
            fx::load_water_stream_params(PcPort_GetGameDataPath(), mods, &source);
        if (loaded) {
            s_stream.emplace(loaded.value());
            SH_LOG("[FX] water stream from %s", source.c_str());
        } else {
            SH_ERR("[FX] %s - the drain's water is not drawn", loaded.error().c_str());
        }
    }
    return s_stream ? &*s_stream : nullptr;
}

// Reads element n of an array of the block, past the array's end too: the original indexed one
// element past the last segment, into whatever the next array holds.
template <typename T>
std::int32_t table_word(const fg::WaterStreamBlock& h, std::size_t offset, int n) {
    T v;
    std::memcpy(&v, reinterpret_cast<const char*>(&h) + offset + sizeof(T) * n, sizeof(T));
    return static_cast<std::int32_t>(v);
}

fx::StreamTable load_table(const fg::WaterStreamBlock& h) {
    using B = fg::WaterStreamBlock;
    fx::StreamTable t;
    for (int i = 0; i <= fx::kWaterSegments; i++) {
        t.x[i] = table_word<std::int32_t>(h, offsetof(B, x), i);
        t.y[i] = table_word<std::int16_t>(h, offsetof(B, y), i);
        t.z[i] = table_word<std::int32_t>(h, offsetof(B, z), i);
        t.length[i] = table_word<std::int16_t>(h, offsetof(B, length), i);
        t.mode[i] = table_word<std::uint8_t>(h, offsetof(B, mode), i);
        t.angle[i] = table_word<std::int16_t>(h, offsetof(B, angle), i);
        t.width[i] = table_word<std::int16_t>(h, offsetof(B, width), i);
        t.step[i] = table_word<std::int16_t>(h, offsetof(B, step), i);
        t.flow[i] = table_word<std::int16_t>(h, offsetof(B, flow), i);
        t.phase[i] = table_word<std::uint16_t>(h, offsetof(B, phase), i);
        t.spread[i] = table_word<std::int16_t>(h, offsetof(B, spread), i);
        t.ramp[i] = table_word<std::int16_t>(h, offsetof(B, ramp), i);
        t.jitter[i] = table_word<std::int16_t>(h, offsetof(B, jitter), i);
    }
    t.drops = h.drops;
    t.count = std::min<int>(h.count, fx::kWaterSegments);
    return t;
}

void store_phases(fg::WaterStreamBlock& h, const fx::StreamTable& t) {
    for (int i = 0; i < t.count; i++) {
        h.phase[i] = static_cast<std::uint16_t>(t.phase[i]);
    }
}

fg::WaterStreamBlock* water_block() {
    return static_cast<fg::WaterStreamBlock*>(sh::maps::map_runtime().effect_block(sh::maps::EffectBlock::Water));
}
sh::maps::EffectSlotRecord& drop_slot(int idx) { return sh::maps::map_runtime().effect_slots()[idx]; }

class StreamHostImpl final : public fx::WaterStreamHost {
public:
    // The packet cursor the map effect frame passes (null when nothing is drawn).
    explicit StreamHostImpl(unsigned char** cursor = nullptr) : cursor_(cursor) {}

    std::int32_t rand16() override { return sh::loop::game_random().next15(); }
    std::int32_t rand_uint(std::int32_t low, std::int32_t high) override {
        return sh::loop::game_random().next_uint(low, high);
    }
    std::int32_t sin(std::int32_t a) override { return sh::world::fixed_sin(a); }
    std::int32_t cos(std::int32_t a) override { return sh::world::fixed_cos(a); }
    std::int32_t delta_time() override { return sh::game_state::delta_time(); }
    std::int32_t gravity() override { return sh::loop::frame_gravity(); }

    fx::Vec3 view_origin() override { return sh::effects::map_effect_view().view.origin; }
    std::int32_t view_yaw() override { return sh::effects::map_effect_view().view.yaw; }

    const fx::WaterSegmentTables& segments() override { return sh::effects::map_effect_view().water; }

    std::int32_t darkness(std::int32_t x, std::int32_t y, std::int32_t z) override {
        return sh::world::atmosphere().darkness_at(x, y, z);
    }

    // The three corners go into the packet, as the original stored them; the drop drawn in that packet keeps their
    // projection records.
    void project3(const fx::Vec3s& a, const fx::Vec3s& b, const fx::Vec3s& c, fx::ScreenPoint out[3]) override {
        const auto r = view().project3(
            {sh::math::Vec3s{a.x, a.y, a.z}, sh::math::Vec3s{b.x, b.y, b.z}, sh::math::Vec3s{c.x, c.y, c.z}});
        for (int i = 0; i < 3; i++) {
            out[i] = fx::ScreenPoint{r[i].v.sx, r[i].v.sy, r[i].v.sz};
            corners_[i] = r[i];
        }
        if (cursor_ != nullptr) {
            fg::packet::QuadTextured* p = reinterpret_cast<fg::packet::QuadTextured*>(*cursor_);
            p->length = fg::packet::kQuadTexturedLength;
            p->code = fg::packet::kQuadTexturedCode;
            p->x0 = out[0].x;
            p->y0 = out[0].y;
            p->x1 = out[1].x;
            p->y1 = out[1].y;
            p->x2 = out[2].x;
            p->y2 = out[2].y;
            pending_ = true;
        }
    }

    fx::ScreenPoint project(const fx::Vec3s& v) override {
        const sh::math::PreciseVertex r = view().project({v.x, v.y, v.z});
        records_.push_back(r);
        return fx::ScreenPoint{r.v.sx, r.v.sy, r.v.sz};
    }

    int alloc(std::uint8_t group) override { return fg::slot_alloc(group); }

    fx::WaterDrop load(int slot) override {
        const sh::maps::EffectSlotRecord& s = drop_slot(slot);
        fx::WaterDrop d;
        d.x = s.x;
        d.z = s.z;
        d.y = s.y;
        d.state = s.kind;
        d.flags = s.flags;
        d.cell = fg::byte_of(s.a, 0);
        d.cell_hi = fg::byte_of(s.a, 1);
        d.fall = static_cast<std::uint16_t>(fg::high_half(s.a));
        d.elapsed = static_cast<std::uint16_t>(fg::low_half(s.b));
        d.scale = static_cast<std::uint16_t>(fg::high_half(s.b));
        return d;
    }

    void store(int slot, const fx::WaterDrop& d) override {
        sh::maps::EffectSlotRecord& s = drop_slot(slot);
        s.x = d.x;
        s.z = d.z;
        s.y = static_cast<std::int16_t>(d.y);
        s.kind = d.state;
        s.flags = d.flags;
        s.a = fg::with_byte(s.a, 0, d.cell);
        s.a = fg::with_byte(s.a, 1, d.cell_hi);
        s.a = fg::with_high_half(s.a, d.fall);
        s.b = fg::with_low_half(s.b, d.elapsed);
        s.b = fg::with_high_half(s.b, d.scale);
    }

    void set_running(bool on) override {
        const std::uint32_t flags = fg::map_effect_flags();
        fg::set_map_effect_flags(on ? (flags | kWaterRunning) : (flags & ~kWaterRunning));
    }

    void clear_groups(std::uint8_t first, std::uint8_t second) override {
        for (int i = 0; i < sh::maps::map_runtime().effect_slot_count(); i++) {
            sh::maps::EffectSlotRecord& s = drop_slot(i);
            if (s.kind == first || s.kind == second) {
                s.kind = 0;
            }
        }
    }

    void emit_drop(const DropQuad& q) override {
        fg::packet::QuadTextured* poly = reinterpret_cast<fg::packet::QuadTextured*>(*cursor_);
        const fx::WaterStreamParams& pr = stream()->params();
        const std::uint8_t cell = static_cast<std::uint8_t>(q.cell);

        poly->length = fg::packet::kQuadTexturedLength;
        poly->code = fg::packet::kQuadTexturedCode;
        poly->x0 = q.corners[0].x;
        poly->y0 = q.corners[0].y;
        poly->x1 = q.corners[1].x;
        poly->y1 = q.corners[1].y;
        poly->x2 = q.corners[2].x;
        poly->y2 = q.corners[2].y;
        poly->x3 = q.corners[3].x;
        poly->y3 = q.corners[3].y;

        const std::uint32_t st[4] = {
            static_cast<std::uint32_t>(((cell & 0x2) * 16) + 0x40 + (q.v_top << 8) + (pr.drop_clut << 16)),
            static_cast<std::uint32_t>(((cell & 0x2) * 16) + 0x5F + (q.v_top << 8) + (pr.drop_page << 16)),
            static_cast<std::uint16_t>(((cell & 0x2) * 16) + 0x40 + (q.v_bottom << 8)),
            static_cast<std::uint16_t>(((cell & 0x2) * 16) + 0x5F + (q.v_bottom << 8))};

        sh::draw3d::Polygon d;
        d.count = 4;
        d.textured = true;
        d.shade = {true, false};
        d.palette = sh::draw3d::Palette::from_word(texel(st[0]).extra);
        d.page = sh::draw3d::TexturePage::from_word(texel(st[1]).extra);
        for (int i = 0; i < 4; i++) {
            d.v[i].uv = {texel(st[i]).u, texel(st[i]).v};
            d.v[i].xy = {q.corners[i].x, q.corners[i].y};
        }
        d.v[0].color = {static_cast<std::uint8_t>(q.intensity), static_cast<std::uint8_t>(q.intensity),
                        static_cast<std::uint8_t>(q.intensity)};
        if (pending_) {
            // A corner keeps the record of the projection stored into this packet while its position is that one.
            for (int k = 0; k < 3; k++) {
                if (d.v[k].xy.x == corners_[k].v.sx && d.v[k].xy.y == corners_[k].v.sy) {
                    sh::effects::drawer::corner_records(corners_[k], d.v[k]);
                }
            }
            pending_ = false;
        }
        own_depth(d, q.corners);
        // The drops fall inside the same trench as the ribbon (emit_ribbon): its walls and grates hide them too.
        projection_.place(d, world_key(q.slot), sh::effects::drawer::SceneDepth::Exact);
        *cursor_ += sizeof(fg::packet::QuadTextured);
    }

    void emit_ribbon(const RibbonQuad& q) override {
        if (q.slot < 0 || q.slot >= fx::effect_world::kWorldSlots) {
            return;  // the original wrote outside the ordering table here
        }
        const fx::WaterStreamParams& pr = stream()->params();

        // A translucent textured quad shaded per corner, in the packet space a textured gouraud quad packet took.
        const std::uint32_t st[4] = {static_cast<std::uint32_t>(((q.v_top << 8) + 64) + (pr.ribbon_clut << 16)),
                                     static_cast<std::uint32_t>(((q.v_top << 8) + 95) + (pr.ribbon_page << 16)),
                                     static_cast<std::uint16_t>(((q.v_top + q.v_span) << 8) + 64),
                                     static_cast<std::uint16_t>(((q.v_top + q.v_span) << 8) + 95)};
        sh::draw3d::Polygon r;
        r.count = 4;
        r.textured = true;
        r.gouraud = true;
        r.shade = {true, false};
        r.palette = sh::draw3d::Palette::from_word(texel(st[0]).extra);
        r.page = sh::draw3d::TexturePage::from_word(texel(st[1]).extra);
        for (int i = 0; i < 4; i++) {
            const std::uint8_t g = static_cast<std::uint8_t>(q.grey[i]);
            r.v[i].xy = {q.corners[i].x, q.corners[i].y};
            r.v[i].uv = {texel(st[i]).u, texel(st[i]).v};
            r.v[i].color = {g, g, g};
            // The ribbon's rows slide along the stream every frame: corners snapped to whole pixels made the water
            // shake and the floor show through it, so each corner keeps the record it was projected with.
            for (auto it = records_.rbegin(); it != records_.rend(); ++it) {
                if (it->v.sx == q.corners[i].x && it->v.sy == q.corners[i].y && it->v.sz == q.corners[i].depth) {
                    sh::effects::drawer::corner_records(*it, r.v[i]);
                    break;
                }
            }
        }
        own_depth(r, q.corners);
        // The ribbon runs along the trench walls and its segments reach under them at the corners; the original's
        // slot a metre behind the water put it under the walls and ledges in front. With the usual effect margin
        // (a third of a metre counting the map's own) their tops lost to it and the segment ends showed through.
        // The water floats above the trench floor, so it needs no margin to win against that.
        projection_.place(r, world_key(q.slot), sh::effects::drawer::SceneDepth::Exact);
        fg::packet_cursor() += sizeof(fg::packet::QuadGouraudTextured);
    }

private:
    // The stream's ordering slot lies slot_depth_offset (a metre) behind the water, which put it under whatever
    // stands in it; tested against the scene's depth at that slot the water would be under its own floor. Each
    // corner takes the depth it was projected at instead.
    static void own_depth(sh::draw3d::Polygon& polygon, const fx::ScreenPoint* corners) {
        for (int i = 0; i < 4; i++) {
            if (!polygon.v[i].precise && corners[i].depth > 0) {
                polygon.v[i].precise = sh::draw3d::Precise{static_cast<float>(corners[i].x),
                                                           static_cast<float>(corners[i].y),
                                                           static_cast<float>(corners[i].depth)};
            }
        }
    }

    // The view the map effect frame loaded before the slot drawers ran.
    sh::math::ViewProjection& view() {
        if (!projection_.active()) {
            sh::effects::drawer::set_current(projection_.vp());
        }
        return projection_.vp();
    }

    unsigned char** cursor_ = nullptr;
    sh::effects::drawer::DrawerProjection projection_;
    sh::math::PreciseVertex corners_[3];
    std::vector<sh::math::PreciseVertex> records_;  // the projections of this host's single points, in order
    bool pending_ = false;  // corners_ were stored into the packet at *cursor_
};

}  // namespace

extern "C" void WaterStream_Start(void) {
    const fx::WaterStream* w = stream();
    if (w == nullptr || water_block() == nullptr) {
        return;
    }
    fx::StreamTable table = load_table(*water_block());
    StreamHostImpl host;
    w->start(host, table);
    store_phases(*water_block(), table);
}

extern "C" void WaterStream_Stop(void) {
    const fx::WaterStream* w = stream();
    if (w == nullptr) {
        return;
    }
    StreamHostImpl host;
    w->stop(host);
}

// One drop of slot `idx`; `cursor` is the map effect frame's packet cursor.
extern "C" std::int32_t WaterStream_DropDraw(unsigned char** cursor, std::int32_t idx) {
    const fx::WaterStream* w = stream();
    if (w == nullptr || water_block() == nullptr) {
        return 0;
    }
    fx::StreamTable table = load_table(*water_block());
    StreamHostImpl host(cursor);
    return w->update_drop(host, table, idx) ? 1 : 0;
}

extern "C" void WaterStream_Ribbon(void) {
    const fx::WaterStream* w = stream();
    if (w == nullptr || water_block() == nullptr) {
        return;
    }
    fx::StreamTable table = load_table(*water_block());
    StreamHostImpl host;
    w->draw_ribbon(host, table);
    store_phases(*water_block(), table);
}
