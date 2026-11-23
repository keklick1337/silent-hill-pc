// SPDX-License-Identifier: GPL-3.0-or-later
//
// The inventory pointer (inventory_pointer.h) on the running game: reads the mouse and the inventory screen's
// state, and hands the answer to the screen as held pad buttons.
#include "input/inventory_pointer.h"

#include <cstdint>

#include "engine/config/pc_config.h"
#include "engine/game_state/game_state.h"
#include "engine/game_state/game_state_ui.h"
#include "engine/platform/window.h"
#include "input/mouse_pointer.h"
#include "render/gpu/render_state.h"

namespace sh::input {

namespace {

namespace gs = sh::game_state;

// The pointer is reported in the space the screen's text is placed in; its layout is measured from the centre of
// the picture, which lies here in that space.
constexpr int kCentreX = 160;
constexpr int kCentreY = 112;

// The inventory proper among the steps of its screen state; the other steps (options, results, fading) use the
// same state for other things.
constexpr int kLiveStep = 1;

constexpr int kSoundVolume = 64;
constexpr int kSoundCentre = 64;

InventoryFocus focus_of(int selection) {
    switch (static_cast<gs::InventorySelection>(selection)) {
        case gs::InventorySelection::EquippedItem: return InventoryFocus::EquippedItem;
        case gs::InventorySelection::Exit: return InventoryFocus::Exit;
        case gs::InventorySelection::Settings: return InventoryFocus::Settings;
        case gs::InventorySelection::Map: return InventoryFocus::Map;
        case gs::InventorySelection::ItemCmd: return InventoryFocus::ItemCommands;
        case gs::InventorySelection::EquippedItemCmd: return InventoryFocus::EquippedCommands;
        case gs::InventorySelection::Item: break;
    }
    return InventoryFocus::Item;
}

gs::InventorySelection selection_of(InventoryFocus focus) {
    switch (focus) {
        case InventoryFocus::EquippedItem: return gs::InventorySelection::EquippedItem;
        case InventoryFocus::Exit: return gs::InventorySelection::Exit;
        case InventoryFocus::Settings: return gs::InventorySelection::Settings;
        case InventoryFocus::Map: return gs::InventorySelection::Map;
        case InventoryFocus::ItemCommands: return gs::InventorySelection::ItemCmd;
        case InventoryFocus::EquippedCommands: return gs::InventorySelection::EquippedItemCmd;
        case InventoryFocus::Item: break;
    }
    return gs::InventorySelection::Item;
}

// The item screen's camera squeezes the ring sideways when the 4:3 picture is stretched over a wider window
// (widescreen by stretching, or the menus' side bars switched off).
float ring_scale() {
    const auto& picture = sh::render::picture();
    const bool stretched = picture.hor_plus ? picture.widescreen_mode == 2 : !picture.menu_pillarbox;
    if (!stretched) {
        return 1.0f;
    }
    int width = 0;
    int height = 0;
    sh::platform::window_size(&width, &height);
    if (height <= 0) {
        return 1.0f;
    }
    const float game = 320.0f / 240.0f;
    const float window = static_cast<float>(width) / static_cast<float>(height);
    return window <= game ? 1.0f : game / window;
}

// The button the player gave a menu action; 0 before the game has its settings.
std::uint32_t menu_button(int action) {
    std::uint16_t binds[gs::kPadBindCount];
    return gs::pad_binds(binds) ? binds[action] : 0u;
}

std::uint32_t buttons_of(InventoryPointer::Press press) {
    switch (press) {
        case InventoryPointer::Press::RingLeft: return gs::pad_stick_left_bit();
        case InventoryPointer::Press::RingRight: return gs::pad_stick_right_bit();
        case InventoryPointer::Press::Enter: return menu_button(gs::kPadEnter);
        case InventoryPointer::Press::Cancel: return menu_button(gs::kPadCancel);
        case InventoryPointer::Press::None: break;
    }
    return 0;
}

}  // namespace

void update_inventory_pointer() {
    static InventoryPointer pointer;
    if (!g_PcConfig.mouseCursor) {
        return;
    }
    const MousePointer& mouse = mouse_pointer();
    int text_x = 0;
    int text_y = 0;
    if (gs::inventory_screen_step() != kLiveStep || !mouse.menu_position(&text_x, &text_y)) {
        pointer.reset();
        return;
    }

    InventoryPointer::View view;
    view.x = text_x - kCentreX;
    view.y = text_y - kCentreY;
    view.moved = mouse.moved();
    view.clicked = mouse.left_clicked();
    view.right_clicked = mouse.right_clicked();
    view.wheel = mouse.wheel_step();
    view.focus = focus_of(gs::inventory_selection());
    view.selected_item = gs::inventory_selected_item();
    view.item_count = gs::inventory_slot_count();
    if (view.focus == InventoryFocus::ItemCommands) {
        view.command_count = gs::inventory_command_count(view.selected_item);
    } else if (view.focus == InventoryFocus::EquippedCommands) {
        view.command_count = gs::inventory_command_count(gs::inventory_equipped_item());
    }
    view.command_row = gs::inventory_command_row();
    view.has_equipped = gs::player_weapon_attack() >= gs::inventory_first_weapon_attack();
    view.ring_scale = ring_scale();

    const InventoryPointer::Action action = pointer.update(view);
    if (action.focus) {
        // The screen ignores input for a few frames after its selection moves; this move comes already settled,
        // so the click that follows the pointer is not lost.
        gs::inventory_snap_selection(static_cast<int>(selection_of(*action.focus)));
        gs::sfx_play(gs::sfx::kMenuMove, kSoundVolume);
    }
    if (action.command_row) {
        gs::set_inventory_command_row(*action.command_row);
        gs::sfx_play_balanced(gs::sfx::kMenuMove, kSoundVolume, kSoundCentre);
    }
    if (action.press != InventoryPointer::Press::None) {
        gs::pad_press_held(buttons_of(action.press));
    }
}

}  // namespace sh::input
