// SPDX-License-Identifier: GPL-3.0-or-later
// The game's sprite groups and screen effects on the engine (engine/world/billboard.h and effects_2d.h, docs/
// RENDER.md sections 5.3 and 3): the billboard groups of the lamps and of the lens flare, and once per frame the
// per-pixel flashlight cone, the glow halo, the lens flare, the brightness and the fog overlays. The engine decides
// corner colours, projection, quad size, the cone and the dark-room debounce; this host takes the atmosphere, the
// camera and the light registers and places the commands in the frame with their own depths.

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>

#include "engine/camera/camera_host.h"
#include "engine/camera/view_system.h"
#include "engine/config/pc_config.h"
#include "engine/effects/game/effect_link.h"
#include "engine/effects/game/slot_hosts.h"
#include "engine/game_state/session.h"
#include "engine/log/sh_log.h"
#include "engine/math/geometry_state.h"
#include "engine/math/precise_capture.h"
#include "engine/runtime/lens_flare_table.h"
#include "engine/world/billboard.h"
#include "engine/world/effects_2d.h"
#include "engine/world/world_atmosphere.h"
#include "render/draw2d/draw2d.h"
#include "render/draw3d/draw3d.h"
#include "render/frame/frame.h"
#include "render/gpu/render_state.h"

extern "C" {
// The world view's stretch (the present's projection) and the display's pixel aspect.
float GR_LivePixelAspect(void);
// Per-pixel flashlight switches and the cone the fragment shader reads.
// The physical flashlight before a first-person eye takes over.
extern sh::world::Vec3i32 g_PcFlashlightShadowWorld;
int Pc_ScriptOwnsShot(void);
int Pc_WholeMapDrawActive(void);
std::int32_t Pc_FogFarDist(void);
// The light set-ups the sprites are lit with (mesh_light.cpp, atmosphere_host.cpp) and the effects drawn here.
void WorldEnv_RelLightPosCalc(sh::world::ModelFrame* frame, std::int32_t intensity, const void* rot, const void* pos);
void Mesh_DirLightMatrixSet(void* block, std::int32_t alpha);
void Mesh_LightVecScale(void* block, std::int32_t scale);
// The glow halo of the point light at `pos` (Q12 world, x y z words) pointing along `rot` (screen_glow_host.cpp).
void Screen_BillboardQuadDraw(void* ot, std::int32_t size, const sh::world::Vec3s16* rot, const sh::world::Vec3i32* pos);
void shLensFlareExec(std::int16_t lensFlareIntensity, const std::int16_t* rot, const std::int32_t* pos,
                     const sh::effects::game_link::WaterZoneRecord* waterZones);
}

namespace {

namespace sw = sh::world;
namespace sg = sh::math;
namespace d3 = sh::draw3d;
namespace cam = sh::camera;

constexpr std::int32_t kWorldSlots = 2048;
constexpr std::int32_t kOne = 4096;  // Q12 1.0
// Packet area the original's primitives took (the game's later drawers keep their addresses).
constexpr std::size_t kTexturedShadedQuad = sh::effects::slot_hosts::packet::kTag + 48;
constexpr std::size_t kShadedQuad = sh::effects::slot_hosts::packet::kShadedQuad;
constexpr std::size_t kDrawMode = sh::effects::slot_hosts::packet::kDrawMode;

// A billboard group: a run of the sprite table, its height offset (Q8) and the texel rect of its sprite.
struct BillboardRow {
    int first;
    int count;
    std::int16_t y_offset;
    std::uint8_t u0, v0, u1, v1;
};

// Row 0 is unused by the world (the groups are kinds 1 and 2); all sample the BG_ETC sprite at 0..63 x 128..191.
std::array<BillboardRow, 3> s_rows = {{
    {1, 3, 0, 0x00, 0x80, 0x3F, 0xBF},
    {0, 12, 0, 0x00, 0x80, 0x3F, 0xBF},
    {12, 5, -153, 0x00, 0x80, 0x3F, 0xBF},
}};

sg::Matrix3 rot_of(const cam::Matrix& m) {
    sg::Matrix3 out;
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) out.m[r][c] = m.m[r][c];
    }
    return out;
}

// A full-screen overlay quad of one colour (the high byte of the word is not used).
d3::Polygon overlay_quad(std::uint32_t color, std::int16_t halfW, std::int16_t halfH) {
    d3::Polygon p;
    p.count = 4;
    p.gouraud = true;
    p.shade = {true, false};
    const std::int16_t xs[4] = {static_cast<std::int16_t>(-halfW), halfW, static_cast<std::int16_t>(-halfW), halfW};
    const std::int16_t ys[4] = {static_cast<std::int16_t>(-halfH), static_cast<std::int16_t>(-halfH), halfH, halfH};
    for (int i = 0; i < 4; i++) {
        p.v[i].xy = {xs[i], ys[i]};
        p.v[i].color = {static_cast<std::uint8_t>(color), static_cast<std::uint8_t>(color >> 8),
                        static_cast<std::uint8_t>(color >> 16)};
    }
    return p;
}

// Half-extent of the full-screen overlay quads: they must cover the widescreen margins and the vertical crop.
std::int16_t overlay_half_height() {
    const float vs = (sh::render::picture().world_v_scale > 1.0f) ? sh::render::picture().world_v_scale : 1.0f;
    const float shift = (sh::render::picture().world_v_shift < 0.0f) ? -sh::render::picture().world_v_shift
                                                                     : sh::render::picture().world_v_shift;
    return static_cast<std::int16_t>(112.0f + 224.0f * (vs - 1.0f) + shift + 8.0f);
}

std::int16_t overlay_half_width() {
    const float hs = (sh::render::picture().world_h_scale > 0.01f) ? sh::render::picture().world_h_scale : 1.0f;
    float halfW = (PcConfig_RenderHeight() > 0)
                      ? (static_cast<float>(sh::game::session().game.screen_height) *
                         static_cast<float>(PcConfig_RenderWidth()) / (2.0f * static_cast<float>(PcConfig_RenderHeight()))) *
                            GR_LivePixelAspect() / hs
                      : 160.0f;
    if (halfW < 160.0f) halfW = 160.0f;
    return static_cast<std::int16_t>(halfW + 10.0f);
}

// "This room is lit by the flashlight rather than by its own lighting": the game's own dark-room test, without the
// term that asks whether the flashlight is out. Deliberately not the glow-halo enable, which reads 0 in genuinely dark
// rooms. It gates a binary whole-scene dim, so a single frame where the raw bits disagree must not reach it.
bool room_lit_raw() {
    const sh::game::EnvironmentMix& env = sh::game::session().system.environment;
    return (env.current.preset.flags & (1 << 1)) &&
           ((env.transition[0].preset.flags & (1 << 0)) || (env.transition[1].preset.flags & (1 << 0)));
}

}  // namespace

// Billboard group `kind` at a Q12 world point, into the world layer at its depth shifted by `shift`.
extern "C" void Gfx_BillboardDraw(std::int32_t kind, std::int32_t posX, std::int32_t posY, std::int32_t posZ,
                                  void* /*ot: the world layer*/, std::int32_t shift) {
    const BillboardRow& row = s_rows[static_cast<std::size_t>(kind)];
    const sw::AtmosphereState& air = sw::atmosphere().state();

    const std::int32_t scaledBase =
        static_cast<std::int32_t>(0x79C * ((g_PcConfig.drawDistancePct <= 0) ? 100 : g_PcConfig.drawDistancePct) / 100)
        << (shift + 2);
    const bool wholeMapDraw = Pc_WholeMapDrawActive() != 0;
    const std::int32_t depthLimit =
        sw::billboard_depth_limit(scaledBase, air.fog_enabled != 0, Pc_FogFarDist(), wholeMapDraw);
    cam::Matrix toScreen = cam::world_screen_at(cam::camera_host().world_screen(), cam::Vec3{posX, posY, posZ});

    sw::BillboardLightIn lightIn;
    lightIn.mode = air.light_mode;
    lightIn.tint = {air.world_tint.r, air.world_tint.g, air.world_tint.b};
    lightIn.map_light = air.ambient;

    if (air.light_mode != 0) {
        cam::Matrix hier = cam::frame_world(static_cast<cam::FrameRecord*>(sh::runtime::lens_flare_view_frame()));
        hier.t[0] = posX >> 4;
        hier.t[1] = (posY >> 4) + row.y_offset;
        hier.t[2] = posZ >> 4;
        WorldEnv_RelLightPosCalc(reinterpret_cast<sw::ModelFrame*>(&hier), air.light_intensity, &air.light_direction,
                                 &air.light_position);

        // The light block the set-ups write besides the registers (unused here).
        alignas(4) unsigned char block[32] = {};
        switch (air.light_mode) {
            case 0:
            case 1:
                Mesh_DirLightMatrixSet(block, kOne);
                sg::geometry().load_color_matrix(sg::transform_of(air.light_color).rot);
                sg::geometry().set_depth_cue_slope(air.light_depth_cue);
                sg::geometry().set_depth_cue_base(0);
                break;
            case 2:
                Mesh_LightVecScale(block, kOne);
                sg::geometry().load_color_matrix(sg::transform_of(air.light_color).rot);
                break;
        }
        lightIn.light = sg::geometry().light_matrix();
        const sg::Vec3i bk = sg::geometry().background();
        lightIn.background = {bk.x, bk.y, bk.z};
        lightIn.color_matrix = sg::transform_of(air.light_color).rot;
    }

    const sw::BillboardLightOut lit = sw::billboard_light(lightIn);

    if (air.light_mode != 0) {
        // Registers other code reads later: the background colour, as scaled, and the light matrix as used.
        if (lit.background.x != lightIn.background.x || lit.background.y != lightIn.background.y ||
            lit.background.z != lightIn.background.z) {
            sg::geometry().set_background({lit.background.x, lit.background.y, lit.background.z});
        }
        sg::geometry().load_light_matrix(lit.light);
    }

    sg::geometry().load_rotation(sg::transform_of(toScreen));
    sg::geometry().load_translation(sg::transform_of(toScreen));

    sw::BillboardView view;
    view.projection.rot = rot_of(toScreen);
    view.projection.trans = {toScreen.t[0], toScreen.t[1], toScreen.t[2]};
    {
        const sg::Projection screen = sg::geometry().projection();
        view.projection.h = screen.h;
        view.projection.ofx = screen.ofx;
        view.projection.ofy = screen.ofy;
    }
    view.whole_map_far = sh::render::whole_town().reproject_far != 0;
    view.whole_map_draw = wholeMapDraw;
    view.last_depth = sh::render::whole_town().last_depth;
    view.depth_limit = depthLimit;

    const sh::runtime::LensSprite* sprites = sh::runtime::lens_sprites() + row.first;
    unsigned char*& cursor = sh::effects::game_link::packet_cursor();
    for (int i = 0; i < row.count; ++i) {
        const sh::runtime::LensSprite& s = sprites[i];
        const sw::BillboardPoint point{s.position.x, s.position.y, s.position.z, s.half_width, s.sprite_height};
        const sw::BillboardQuad quad = sw::billboard_quad(view, point);
        if (!quad.visible) continue;

        d3::Polygon p;
        p.count = 4;
        p.textured = true;
        p.gouraud = true;
        p.page = d3::TexturePage::from_word(0x2C);
        p.palette = d3::Palette::from_word(0x8C);
        const d3::TexCoord uv[4] = {{row.u0, row.v0}, {row.u1, row.v0}, {row.u0, row.v1}, {row.u1, row.v1}};
        // Fog amount of every corner; the shader mixes the fog colour in by it.
        const std::uint8_t fog =
            air.fog_enabled ? sw::billboard_fog_byte(sw::atmosphere().fog_ramp_u8(quad.depth << 6), air.fog.intensity)
                            : 0;
        for (int k = 0; k < 4; k++) {
            p.v[k].xy = {quad.x[k], quad.y[k]};
            p.v[k].uv = uv[k];
            p.v[k].color = {static_cast<std::uint8_t>(lit.corners[k].r), static_cast<std::uint8_t>(lit.corners[k].g),
                            static_cast<std::uint8_t>(lit.corners[k].b)};
            p.v[k].fog = fog;
        }
        // A flat camera-facing quad built around one projected centre: affine is correct (it only matters with the
        // precise projection on).
        p.affine = sh::render::precise().enabled != 0;

        const sh::frame::OrderKey key{sh::frame::Layer::World,
                                      static_cast<std::uint32_t>(std::min(quad.depth >> shift, kWorldSlots - 1))};
        if (wholeMapDraw) {
            // The far town is depth-tested; a sprite at its slot depth would cover every far block.
            d3::Depth d;
            const auto full = static_cast<std::uint32_t>(quad.full_depth);
            d.sz = {full, full, full, full};
            d.source = d3::DepthSource::Exact;
            p.depth = d;
        }
        d3::submit_placed(p, sg::geometry().depth_history(), key);
        cursor += kTexturedShadedQuad;
    }

    if (view.whole_map_far) sh::render::whole_town().last_depth = view.last_depth;
}

extern "C" void Gfx_2dEffectsDraw(void) {
    static sw::StableFlag roomLit(6);
    static int prevDim = -1;
    static int dimLogs = 0;

    const sw::AtmosphereState& air = sw::atmosphere().state();
    const sh::game::EnvironmentMix& env = sh::game::session().system.environment;

    // Per-pixel flashlight: once per frame the light is put in view space for the fragment-shader cone. Gated on
    // the flashlight itself being on (the room's dynamic-light mode is on even with it stowed), on no scripted shot
    // owning the camera and on the room being dark (the game's own test); otherwise the glow-halo path below runs.
    const bool lit = roomLit.update(room_lit_raw());
    const int dimOn = sh::render::flashlight().per_pixel && env.flashlight_on && !Pc_ScriptOwnsShot() && lit;

    if (dimOn != prevDim && dimLogs < 40) {
        dimLogs++;
        SH_DBG("[FLICKER] dim=%d master=%d flashlight=%d scripted=%d roomlit=%d raw=%d", dimOn,
               sh::render::flashlight().per_pixel, static_cast<int>(env.flashlight_on), Pc_ScriptOwnsShot(),
               static_cast<int>(lit), static_cast<int>(room_lit_raw()));
    }
    prevDim = dimOn;

    if (dimOn) {
        const cam::Matrix& ws = cam::camera_host().world_screen();
        sw::FlashlightConeIn in;
        in.view.rot = rot_of(ws);
        in.view.trans = {ws.t[0], ws.t[1], ws.t[2]};
        in.light_position = {air.light_position.x, air.light_position.y, air.light_position.z};
        // The physical flashlight, taken before the first-person eye override: the shadow lands where third person
        // shows it.
        in.shadow_position = {g_PcFlashlightShadowWorld.x, g_PcFlashlightShadowWorld.y, g_PcFlashlightShadowWorld.z};
        in.direction = {air.light_direction.x, air.light_direction.y, air.light_direction.z};
        in.color_matrix = sg::transform_of(air.light_color).rot;
        in.tinted_by_room = sh::render::flashlight().style != 0;
        sw::FlashlightCone cone = sw::flashlight_cone(in);
        if (const sh::math::capture::CameraTwin* twin = sh::math::capture::camera_of(in.view.rot, in.view.trans)) {
            // The beam in the unquantised camera's view space, which the scene and the shadow map are drawn with.
            const auto to_view = [&](const sh::math::Vec3i& q12, bool point) {
                const double p[3] = {static_cast<double>(point ? (q12.x >> 4) : q12.x),
                                     static_cast<double>(point ? (q12.y >> 4) : q12.y),
                                     static_cast<double>(point ? (q12.z >> 4) : q12.z)};
                std::array<float, 3> out{};
                for (int r = 0; r < 3; ++r) {
                    const double v =
                        twin->rot[r * 3] * p[0] + twin->rot[r * 3 + 1] * p[1] + twin->rot[r * 3 + 2] * p[2];
                    out[static_cast<std::size_t>(r)] = static_cast<float>(point ? v + twin->trans[r] : v);
                }
                return out;
            };
            cone.position = to_view(in.light_position, true);
            cone.shadow_position = to_view(in.shadow_position, true);
            cone.direction = to_view({in.direction.x, in.direction.y, in.direction.z}, false);
        }
        for (int i = 0; i < 3; ++i) {
            sh::render::flashlight().pos[i] = cone.position[i];
            sh::render::flashlight().shadow_pos[i] = cone.shadow_position[i];
            sh::render::flashlight().dir[i] = cone.direction[i];
            sh::render::flashlight().color[i] = cone.color[i];
        }
        sh::render::flashlight().active = 1;
    } else {
        sh::render::flashlight().active = 0;
    }

    if (air.tint_light_overlap != 0) {
        // Classic per-pixel style still needs the original subtractive outer mask; the modern style replaces the
        // whole glow compositor, so it is skipped there.
        if (!(sh::render::flashlight().active && sh::render::flashlight().style == 0)) {
            Screen_BillboardQuadDraw(nullptr, air.light_intensity, &air.light_direction, &air.light_position);
        }
    }

    // Flashlight lens flare (Harry's chest glare) and water reflections.
    if (air.light_mode == 1 && air.lens_flare_intensity != 0) {
        const std::int16_t rot[3] = {air.light_direction.x, air.light_direction.y, air.light_direction.z};
        const std::int32_t pos[3] = {air.light_position.x, air.light_position.y, air.light_position.z};
        shLensFlareExec(air.lens_flare_intensity, rot, pos,
                        static_cast<const sh::effects::game_link::WaterZoneRecord*>(air.water_zones));
    }

    const std::int16_t halfW = overlay_half_width();
    const std::int16_t halfH = overlay_half_height();
    unsigned char*& cursor = sh::effects::game_link::packet_cursor();

    if (air.screen_brightness > 0) {
        cursor += kShadedQuad + kDrawMode;
        d3::Polygon quad = overlay_quad(sw::overlay_gray(air.screen_brightness), halfW, halfH);
        d3::submit_placed(quad, sg::geometry().depth_history(), {sh::frame::Layer::World, 0});
        // Additive blending, dithered; placed after the quad, so drawn before it.
        sh::draw2d::DrawMode drawMode;
        drawMode.page = sh::draw2d::TexturePage::from_word(0x20);
        drawMode.dither = true;
        sh::draw2d::submit(drawMode, {sh::frame::Layer::World, 0}, sg::geometry().depth_history_max());
    }

    // Fog colour quad over the whole frame, semi-transparent on the polygon itself.
    {
        cursor += kShadedQuad;
        std::uint32_t fogColor;
        std::memcpy(&fogColor, &air.fog.color, 4);
        d3::Polygon quad = overlay_quad(fogColor, halfW, halfH);
        d3::submit_placed(quad, sg::geometry().depth_history(), {sh::frame::Layer::World, kWorldSlots - 1});
    }
}
