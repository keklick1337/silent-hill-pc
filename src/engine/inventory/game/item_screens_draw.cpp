// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The item views on the item stage (item_stage.h): the item screens' models (the carousel, the item in hand, the
// results screen's awards) and the world pickup. The entry points keep the names the game calls them by; the 2D parts
// of the screen are the screen's bridges, declared below.

#include <cstdint>
#include <optional>
#include <string>

#include "engine/camera/view_system.h"
#include "engine/game_state/game_state.h"
#include "engine/game_state/game_state_inventory.h"
#include "engine/game_state/save_record.h"
#include "engine/game_state/session.h"
#include "engine/inventory/game/item_stage.h"
#include "engine/inventory/item_carousel.h"
#include "engine/inventory/item_catalog.h"
#include "engine/inventory/item_view.h"
#include "engine/inventory/pickup_anim.h"
#include "engine/text/game_text.h"
#include "render/gpu/render_state.h"

// Set every frame the pickup model is on screen: the main loop brackets the scene with the item depth and releases the
// frozen frame when the pickup ends.
int g_PcPickupItemActive = 0;
// Set every frame a full-screen puzzle draws its models on the stage in the world (the map5_s01 lock): the main loop
// gives the scene the item depth like the pickup's.
int g_PcPuzzleItemDepth = 0;

// The screen's 2D parts and the results text.
extern "C" {
void Gfx_ItemScreens_LabelsDraw(void);
void Inventory_PlayerItemScroll(std::uint32_t* selection);
void Gfx_Inventory_ItemDescriptionDraw(std::int32_t* selection);
void Gfx_Inventory_2dBackgroundDraw(std::int32_t* selection);
void Gfx_Inventory_HealthStatusReset(void);
void Gfx_ItemScreen_RectDraw(void);
void Results_DisplayInfo(std::uint32_t* selection);
void Gfx_Results_ItemsPosition(void);
void Gfx_Results_ItemsDisplay(void);
}

namespace {

namespace inv = sh::inventory;

constexpr std::int32_t kResultsStep = 21;  // item screen steps from here on are the results screen
constexpr std::int32_t kHandHeight = -1408;  // the item in hand, 5.5 units up (Q8)
constexpr std::int32_t kPickupHeight = 64;   // 0.25 (Q8)
constexpr std::int32_t kPickupDepth = -8368; // -32.6875 (Q8)
constexpr int kEmptyItem = 0xFF;
constexpr std::int32_t kWhiteText = 7;  // the game's text colour id

inv::PickupAnim s_pickup = inv::pickup_begin();

bool in_inventory() { return sh::game::session().game.game_steps[1] < kResultsStep; }

void pack_link(int place, int item) { inv::item_stage().link_from_pack(place, item); }

void display_tilt(int item, inv::ModelTransform& transform) {
    const inv::DisplayTilt tilt = inv::item_catalog().display_tilt(item);
    transform.rotate[0] = tilt.x;
    transform.rotate[2] = tilt.z;
}

}  // namespace

extern "C" {

void Gfx_Items_Draw(void) {
    inv::ItemStage& stage = inv::item_stage();
    inv::ItemView& view = inv::item_view();

    stage.open_projection();
    inv::item_screen().carousel.glare_time = 0;
    stage.camera_init(stage.camera());

    view.shown.fill(inv::kNoModelSlot);
    for (int i = 0; i < inv::kStagePlaces; ++i) stage.place(i).transform = inv::ModelTransform{};
    // Every place but the pickup's starts with nothing: a place only an item of this map's pack refreshes would keep a
    // stale model of an earlier item (a cheat-added item not in the pack).
    for (int i = 0; i < inv::kPickupModel; ++i) stage.unlink(i);

    sh::game::SaveRecord& save = sh::game::save_record();
    if (in_inventory()) {
        const inv::Slots slots = sh::game_state::inventory_slots();
        const std::int32_t shown = sh::game_state::inventory_shown_slots();
        inv::open_carousel(view, sh::game::session().system.inventory_selected, shown);
        for (int i = 0; i < inv::kCarouselModels; ++i) {
            const int item = slots[static_cast<std::size_t>(view.shown[i])].item;
            if (item != kEmptyItem) pack_link(i, item);
        }
        inv::inventory_menu().equipped_item = save.equipped_weapon;
        view.equipped() = inv::slot_of_item(slots, shown, save.equipped_weapon);
        if (view.equipped() != inv::kNoModelSlot) pack_link(inv::kEquippedModel, save.equipped_weapon);
        stage.place(inv::kEquippedModel).frame.local.t[0] = 0;
        stage.place(inv::kEquippedModel).frame.local.t[1] = kHandHeight;
        stage.place(inv::kEquippedModel).frame.local.t[2] = 0;
    } else {
        // The results screen: its awards, one model each.
        if (save.clear_count == 0) save.clear_count = 1;
        for (int i = 0; i < inv::kResultsModels; ++i) {
            view.shown[i] = i;
            if (const std::optional<int> item = inv::item_catalog().results_item(i)) pack_link(i, *item);
        }
    }
}

void Gfx_Items_DrawInit(void) {
    inv::ItemStage& stage = inv::item_stage();
    inv::item_view().linked = 0;
    inv::item_view().pickup_item = inv::kNoViewItem;
    Gfx_Inventory_HealthStatusReset();
    inv::item_screen().description.name_timer = 0;
    inv::item_screen().description.roll_timer = 0;
    inv::item_screen().carousel.timer = 0;
    inv::item_screen().carousel.finger = false;

    const std::uint8_t* list = sh::game_state::map_loadable_items();
    std::size_t i = 0;
    for (; list != nullptr && list[i] != 0 && i + 1 < stage.pack.size(); ++i) stage.pack[i] = list[i];
    stage.pack[i] = 0;

    Gfx_Items_Draw();
}

void Gfx_ItemScreens_DrawInit(std::uint32_t* selection) {
    inv::ItemStage& stage = inv::item_stage();
    const inv::ItemView& view = inv::item_view();
    sh::text::game_text().set_color(kWhiteText);

    if (in_inventory()) {
        Gfx_ItemScreens_LabelsDraw();
        Inventory_PlayerItemScroll(selection);

        // The row, then the item in hand.
        for (int i = 0; i <= inv::kEquippedModel; ++i) {
            const std::int32_t slot = view.shown[i];
            if (slot == inv::kNoModelSlot) continue;
            const int item = sh::game_state::inventory_slot(slot).item;
            if (item == kEmptyItem || !stage.place(i).framed) continue;
            display_tilt(item, stage.place(i).transform);
            stage.rotate(i);
            stage.draw(i, inv::StageDraw::Screen);
        }

        Gfx_Inventory_ItemDescriptionDraw(reinterpret_cast<std::int32_t*>(selection));
        Gfx_Inventory_2dBackgroundDraw(reinterpret_cast<std::int32_t*>(selection));
    } else {
        Gfx_Results_ItemsPosition();
        Gfx_Results_ItemsDisplay();
        Results_DisplayInfo(selection);
    }
    Gfx_ItemScreen_RectDraw();
}

void Gfx_Results_ItemsPosition(void) {
    sh::game::SaveRecord& save = sh::game::save_record();
    if (save.clear_count == 0) save.clear_count = 1;
    inv::ItemStage& stage = inv::item_stage();
    const auto& poses = inv::results_poses();
    for (int i = 0; i < inv::kResultsModels; ++i) {
        // Only the models of the items this ending awarded.
        if (!((stage.results_mask >> i) & 1)) continue;
        inv::StagePlace& p = stage.place(i);
        p.frame.local.t[0] = poses[static_cast<std::size_t>(i)].x;
        p.frame.local.t[1] = poses[static_cast<std::size_t>(i)].y;
        p.frame.local.t[2] = poses[static_cast<std::size_t>(i)].z;
        p.transform.rotate[0] = inv::kResultsTilt;
        p.transform.rotate[2] = inv::kResultsTilt;
        p.transform.scale = {4096, 4096, 4096};
        p.transform.rotate[1] = static_cast<std::int16_t>(p.transform.rotate[1] + inv::kResultsSpin);
    }
}

void Gfx_Results_ItemsDisplay(void) {
    inv::ItemStage& stage = inv::item_stage();
    for (int i = 0; i < inv::kResultsModels; ++i) {
        if (!((stage.results_mask >> i) & 1) || !stage.place(i).framed) continue;
        stage.rotate(i);
        stage.draw(i, inv::StageDraw::Results);
    }
}

void Gfx_Items_PickupInit(int item) {
    inv::ItemStage& stage = inv::item_stage();
    inv::item_view().pickup_item = item;
    inv::item_view().linked = 0;
    s_pickup = inv::pickup_begin();

    inv::StagePlace& p = stage.place(inv::kPickupModel);
    p.transform.rotate = {0, 0, 0};
    p.transform.translate = {0, 0, 0};
    stage.link(inv::kPickupModel, stage.close_up_model, 0, item);
    p.transform.scale = {4096, 4096, 4096};
    stage.camera_init(stage.camera());
}

std::int32_t Gfx_PickupItemAnimate(int item) {
    inv::ItemStage& stage = inv::item_stage();
    // The world is frozen behind the item (the pause flag holds the world's draw, the last frame is presented again)
    // and the item is the scene's only model: its own front faces hide its back faces. Set every frame (the main
    // loop clears them).
    sh::game::session().system.process.music_status |= sh::game::music_status::kPause;
    sh::render::feedback_control().present_last_frame = 1;
    g_PcPickupItemActive = 1;

    inv::StagePlace& p = stage.place(inv::kPickupModel);
    p.frame.local.t[0] = 0;
    p.frame.local.t[1] = kPickupHeight;
    p.frame.local.t[2] = kPickupDepth;

    const inv::DisplayTilt tilt = inv::item_catalog().display_tilt(item);
    inv::pickup_step(s_pickup, sh::game_state::delta_time_raw(), tilt.x, tilt.z);
    p.transform.scale = {s_pickup.scale, s_pickup.scale, s_pickup.scale};
    p.transform.rotate = {s_pickup.rot_x, s_pickup.rot_y, s_pickup.rot_z};

    stage.view_set(stage.camera());
    stage.rotate(inv::kPickupModel);

    // Half a turn per second.
    inv::pickup_spin(s_pickup, sh::game_state::delta_time_raw());
    p.transform.rotate[1] = s_pickup.rot_y;

    // The game's own projection (H = the frame height), whatever the alternate cameras' field of view set.
    const std::int16_t saved = sh::camera::projection_now().h;
    sh::camera::set_projection_distance(static_cast<std::int16_t>(sh::game::session().game.screen_height));
    stage.draw(inv::kPickupModel, inv::StageDraw::Pickup);
    sh::camera::set_projection_distance(saved);

    return s_pickup.state > 0 ? 1 : 0;
}

}  // extern "C"
