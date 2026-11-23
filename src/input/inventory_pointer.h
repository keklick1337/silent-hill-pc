// SPDX-License-Identifier: GPL-3.0-or-later
//
// The mouse on the inventory screen. The pointer is tested against the screen's own layout and answered with the
// pad presses the screen already understands, so scrolling, sounds and commands stay the screen's:
//   - pointing at a region (the item ring, the equipped item, Option / Exit / Map) selects it;
//   - a click on the selected item opens its commands, a click on another item of the ring turns the ring to it;
//   - a click on a command runs it; a right click, or a click beside the open commands, cancels;
//   - the wheel over the ring turns it by one item.
#pragma once

#include <optional>

namespace sh::input {

// What the selection is on.
enum class InventoryFocus { Item, EquippedItem, Exit, Settings, Map, ItemCommands, EquippedCommands };

class InventoryPointer {
public:
    // One frame of the pointer and of the screen it points at.
    struct View {
        // The pointer in the screen's own space: pixels of the 320x240 picture from its centre.
        int x = 0;
        int y = 0;
        bool moved = false;
        bool clicked = false;
        bool right_clicked = false;
        int wheel = 0;  // > 0: away from the user

        InventoryFocus focus = InventoryFocus::Item;
        int selected_item = 0;   // its place in the ring
        int item_count = 0;      // places in the ring
        int command_count = 0;   // rows of the open command list
        int command_row = 0;     // the highlighted one
        bool has_equipped = false;
        // How far the picture is squeezed sideways when it is stretched over a wide window (1 = not at all):
        // the ring's items move towards the centre by it.
        float ring_scale = 1.0f;
    };

    enum class Press { None, RingLeft, RingRight, Enter, Cancel };

    // What to tell the screen, applied in this order: the selection, the command row, the press.
    struct Action {
        std::optional<InventoryFocus> focus;
        std::optional<int> command_row;
        Press press = Press::None;
    };

    Action update(const View& view);
    // Forgets a ring turn in progress (the screen was left).
    void reset() { turn_to_ = kNowhere; }

private:
    static constexpr int kNowhere = -1;

    // The place in the ring a click asked for. The screen moves the ring one place per step, so a turn of several
    // places is walked there one press at a time.
    int turn_to_ = kNowhere;
};

// The pointer on the running game (inventory_pointer_host.cpp). Called by the inventory screen at the start of
// its frame, before it reads the pad: the presses made here are what it then finds.
void update_inventory_pointer();

}  // namespace sh::input
