// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/inventory/game/item_stage.h"

#include "engine/inventory/game/wide_screen.h"

#include <array>
#include <span>
#include <string>

#include "engine/game_state/session.h"
#include "engine/inventory/item_catalog.h"
#include "engine/inventory/item_view.h"
#include "engine/log/sh_log.h"
#include "engine/math/fixed_geometry.h"
#include "engine/math/geometry_state.h"
#include "engine/platform/host_api.h"
#include "engine/platform/window.h"
#include "formats/convert/model/tmd.h"
#include "render/frame/frame.h"
#include "render/scene/item_model.h"
#include "render/gpu/render_state.h"


// The engine's tunables of the item draw (console: invaspect, invscale, invcary, inveqy, invdim).
//
// invaspect / invscale: the vertical correction of the 3D item. (4/3) * (224 / 320) = 224 / 240: a TV shows the
// 224-line frame letterboxed in its 240 lines, while the engine stretches those rows to the full 4:3 height; 100 % of
// the factor undoes exactly that, which is what DuckStation shows. Not persisted.
int g_PcInvAspectSquare = 1;
int g_PcInvAspectPct = 100;
// The row a touch down and the item in hand a touch up (view units; + is down), matched against DuckStation.
int g_PcInvCarouselYOff = 50;
int g_PcInvEquipYOff = -50;
// Darkening per slot off the centre of the row, percent (the depth cue the unlit models lack).
int g_PcInvDimStrength = 30;

namespace sh::inventory {

namespace {

constexpr std::int32_t kCameraDistance = 0x2800;
constexpr std::int16_t kItemScreenProjection = 1000;
constexpr std::int32_t kResultsStep = 21;  // item screen steps from here on are the results screen
constexpr std::uint32_t kSceneTableEntries = 1u << 11;  // the scene's ordering table
constexpr std::uint32_t kPickupTableEntries = 1u << 9;  // the pickup's own table

camera::Matrix model_matrix(const std::array<std::int16_t, 3>& rotate, const std::array<std::int32_t, 3>& scale,
                            const std::int32_t (&t)[3]) {
    const math::Matrix3 r = math::rot_matrix_zxy_neg({rotate[0], rotate[1], rotate[2]});
    const math::Matrix3 s = math::scale_matrix(r, {scale[0], scale[1], scale[2]});
    camera::Matrix m;
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) m.m[i][j] = s.m[i][j];
        m.t[i] = t[i];
    }
    return m;
}

// The picture on the key items' sheet: the key TIM the map group loaded (one resident at a time).
std::optional<scene::ItemSheet> resident_sheet(std::uint16_t tpage, std::uint16_t clut) {
    const std::uint8_t resident = game::session().system.inventory_load_flags;
    const std::vector<std::string>& keys = item_catalog().model_files().key_textures;
    std::string key_tim;
    for (std::size_t i = 0; i < keys.size() && i < kMaxKeyTextures; ++i)
        if (resident & (1u << (1 + i))) key_tim = keys[i];
    const auto texture = convert::model::resolve_key_sheet(tpage, clut, key_tim);
    if (!texture) return std::nullopt;
    return scene::ItemSheet{texture->tim, texture->palette, texture->u0, texture->v0};
}

const scene::ItemPacketFix& liquid_fix() {
    static const scene::ItemPacketFix fix = [](std::span<std::uint8_t> packets, std::uint32_t vertices,
                                               std::uint32_t count) { return fix_liquid_uvs(packets, vertices, count); };
    return fix;
}

}  // namespace

void ItemStage::camera_init(ItemCamera& camera) {
    camera.view.eye = {0, 0, 0};
    camera.view.target = {0, 0, 10};
    camera.view.roll = 0;
    camera.view.parent = &camera.frame;
    camera.frame.parent = nullptr;
    camera.rotation = {0, 0, 0};
    const std::int32_t t[3] = {0, 0, -kCameraDistance};
    camera.frame.local = model_matrix(camera.rotation, {4096, 4096, 4096}, t);
    camera.frame.world_current = 0;
    camera::view_system().set_reference_view(camera.view);
}

void ItemStage::view_set(ItemCamera& camera) {
    camera.frame.local = model_matrix(camera.rotation, {4096, 4096, 4096}, camera.frame.local.t);
    camera.frame.world_current = 0;
    camera::view_system().set_reference_view(camera.view);
}

void ItemStage::link(int index, const std::string& model, int object, int item) {
    StagePlace& p = place(index);
    p.model = model;
    p.object = object;
    p.item = item;
    p.shown = !model.empty() && object >= 0;
    p.frame = camera::FrameRecord{};
    p.frame.local = camera::Matrix{{{4096, 0, 0}, {0, 4096, 0}, {0, 0, 4096}}, {0, 0, 0}};
    p.framed = true;
    item_view().count_link();
}

bool ItemStage::link_from_pack(int index, int item) {
    const std::int32_t k = pack_index(pack, item);
    if (k < 0) return false;
    link(index, pack_model, k, pack[static_cast<std::size_t>(k)]);
    return true;
}

void ItemStage::unlink(int index) {
    StagePlace& p = place(index);
    p.shown = false;
    p.model.clear();
    p.object = -1;
}

void ItemStage::rotate(int index) {
    StagePlace& p = place(index);
    p.frame.local = model_matrix(p.transform.rotate, p.transform.scale, p.frame.local.t);
    p.frame.world_current = 0;
}

void ItemStage::draw(int index, StageDraw mode) {
    StagePlace& p = place(index);
    // A place never framed, a model scaled to nothing (the unequip animation's first frame) or no model: nothing.
    if (!p.framed || !p.shown || p.transform.scale[0] == 0) return;

    const std::string& fix_name = item_catalog().close_up_fix(p.item);
    static const scene::ItemPacketFix kNoFix;
    if (!fix_name.empty() && fix_name != "liquid_uvs") {
        static bool s_warned = false;
        if (!s_warned) SH_WARN("[ITEMS] item %u: unknown close_up_fix '%s'", static_cast<unsigned>(p.item), fix_name.c_str());
        s_warned = true;
    }
    const bool fixed = fix_name == "liquid_uvs";
    const std::shared_ptr<const scene::ItemMesh> mesh =
        scene::find_item_mesh(p.model, p.object, fixed ? fix_name : std::string(), fixed ? liquid_fix() : kNoFix);
    if (!mesh) return;

    const camera::Matrix world = camera::frame_world(&p.frame);
    camera::Matrix ls = camera::view_matrix(camera::view_system().world_screen(), world);

    // The engine's display corrections (fit_to_screen): a 4:3 picture stretched to a wider viewport with no
    // compensation (anamorphic mode, or a 2D screen without the menu pillarbox) narrows the model; Hor+ widens the
    // projection itself and a pillarboxed viewport is 4:3, so neither is corrected.
    int screen_w = 0, screen_h = 0;
    sh::platform::window_size(&screen_w, &screen_h);
    ScreenFit fit;
    fit.stretched      = (sh::render::picture().hor_plus && sh::render::picture().widescreen_mode == 2) ||
                         (!sh::render::picture().hor_plus && !sh::render::picture().menu_pillarbox);
    fit.display_aspect = screen_h > 0 ? static_cast<float>(screen_w) / static_cast<float>(screen_h) : 0.0f;
    fit.square = g_PcInvAspectSquare != 0;
    fit.frame_width = game::session().game.screen_width;
    fit.frame_height = game::session().game.screen_height;
    fit.square_percent = g_PcInvAspectPct;
    // The lock reels draw in the world and keep their place.
    fit.in_menu = game::session().game.game_state != game::game_state::kInGame;
    fit.row_offset = g_PcInvCarouselYOff;
    fit.hand_offset = g_PcInvEquipYOff;
    // Only the inventory's row spreads; the results screen's awards keep their places.
    if (game::session().game.game_state == game::game_state::kInventoryScreen &&
        game::session().game.game_steps[1] < kResultsStep)
        fit.row_spread = wide_screen().ring;
    ScreenMatrix screen;
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) screen.m[i][j] = ls.m[i][j];
        screen.t[i] = ls.t[i];
    }
    fit_to_screen(screen, index, fit);

    scene::ItemModelPose pose;
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) pose.m[i][j] = screen.m[i][j];
        pose.t[i] = screen.t[i];
    }
    const camera::ScreenProjection proj = camera::projection_now();
    const scene::ItemProjection projection{static_cast<float>(proj.h), static_cast<float>(proj.ofx) / 65536.0f,
                                           static_cast<float>(proj.ofy) / 65536.0f};
    // Off-centre row models are darker.
    const std::int32_t dim = carousel_dim(index, p.frame.local.t[2], g_PcInvDimStrength);
    const bool pickup = mode == StageDraw::Pickup;
    const scene::ItemModelDraw drawn = scene::add_item_model(
        *mesh, pose, projection, dim, 1, static_cast<int>(pickup ? kPickupTableEntries : kSceneTableEntries),
        resident_sheet);
    if (!drawn.drawn) return;
    // The pickup's own table hangs at the scene's slot 0; a slot draws the entry submitted last first, so the meshes
    // go in reverse to draw in list order.
    const frame::OrderKey key{frame::Layer::World, pickup ? 0u : static_cast<std::uint32_t>(drawn.bucket)};
    for (auto it = drawn.meshes.rbegin(); it != drawn.meshes.rend(); ++it)
        frame::submit_keyed(frame::Kind::Scene, static_cast<std::uint32_t>(*it) + 1u, key);
}

void ItemStage::open_projection() {
    saved_view_ = math::geometry().view();
    saved_screen_ = math::geometry().screen();
    camera::set_projection_distance(kItemScreenProjection);
    weapon_on_open = game::session().system.combat.weapon_attack;
}

void ItemStage::close_projection() {
    math::geometry().set_rotation(saved_view_.rot);
    math::geometry().set_translation(saved_view_.trans);
    camera::set_projection_distance(saved_screen_.h);
    // The centre comes back in whole screen units, as the game read and wrote it.
    math::geometry().set_screen_offset(saved_screen_.ofx >> 16, saved_screen_.ofy >> 16);
}

ItemStage& item_stage() {
    static ItemStage s_stage;
    return s_stage;
}

}  // namespace sh::inventory
