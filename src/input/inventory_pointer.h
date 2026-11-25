// SPDX-License-Identifier: GPL-3.0-or-later
//
// The mouse on the inventory screen. The pointer is tested against the screen's own layout and answered with the
// pad presses the screen already understands, so scrolling, sounds and commands stay the screen's:
//   - pointing at a region (the item ring, the equipped item, Option / Exit / Map) selects it;
//   - a click on the selected item opens its commands, a click on another item of the ring turns the ring to it;
//   - a click on a command runs it; a right click, or a click beside the open commands, cancels;
//   - the wheel over the ring turns it by one item;
//   - on a touch screen a finger turns the ring as a carousel (ring_carousel.h): the ring follows a sideways drag,
//     glides on and settles on the item nearest the centre, the screen stepping its selection after it one item at
//     a time; a tap on a neighbour turns the ring there softly.
#pragma once

#include <optional>

#include "input/ring_carousel.h"

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
        // How far the ring's items move sideways from the centre (1 = not at all): below 1 when the picture is
        // stretched over a wide window, above 1 when the screen spreads over it.
        float ring_scale = 1.0f;
        // A screen spread over a wide window moves its side columns (Option; Map and the commands) out by this.
        int column_shift = 0;

        // A touch screen: the pointer is a finger (touch_controls.h ListFinger), and `seconds` passed since the
        // last frame.
        bool touch = false;
        bool finger_down = false;
        bool finger_dragging = false;
        bool finger_horizontal = false;
        double seconds = 0.0;
    };

    enum class Press { None, RingLeft, RingRight, Enter, Cancel };

    // What to tell the screen, applied in this order: the selection, the command row, the press.
    struct Action {
        std::optional<InventoryFocus> focus;
        std::optional<int> command_row;
        Press press = Press::None;
        // The finger has the ring: the screen draws its centre here (places, 0 .. item_count), whichever item it
        // has selected by then.
        std::optional<double> ring_position;
        // The ring press is to be taken at once: the screen's selection settles first, as the finger's ring does
        // not wait for the screen's own scroll.
        bool settle = false;
    };

    Action update(const View& view);
    // Forgets a ring turn in progress (the screen was left).
    void reset();
    // The ring glides fast: a finger that lands only stops it.
    bool ring_flinging() const { return carousel_.flinging(); }

private:
    static constexpr int kNowhere = -1;

    // True when the finger's ring took the frame.
    bool follow_finger(const View& view, Action& action);

    // The place in the ring a click asked for. The screen moves the ring one place per step, so a turn of several
    // places is walked there one press at a time.
    int turn_to_ = kNowhere;

    // The ring under the finger. While it owns the ring (dragged, gliding, settling, or the screen still stepping
    // after it) the screen draws it; then the ring is the screen's again.
    RingCarousel carousel_;
    bool ring_owned_ = false;
    bool finger_down_ = false;
    // A tap's click comes as the finger lifts or just after.
    static constexpr int kTapFrames = 4;
    int frames_since_lift_ = kTapFrames;
    bool down_on_ring_ = false;
    int down_x_ = 0;
};

// The pointer on the running game (inventory_pointer_host.cpp). Called by the inventory screen at the start of
// its frame, before it reads the pad: the presses made here are what it then finds.
void update_inventory_pointer();

}  // namespace sh::input
