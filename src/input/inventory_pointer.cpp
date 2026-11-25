// SPDX-License-Identifier: GPL-3.0-or-later
#include "input/inventory_pointer.h"

namespace sh::input {

namespace {

// A region of the screen, edges included.
struct Region {
    int left, top, right, bottom;

    constexpr bool contains(int x, int y) const { return x >= left && x <= right && y >= top && y <= bottom; }
    constexpr Region moved(int dx) const { return {left + dx, top, right + dx, bottom}; }
};

// The brackets the screen draws around what is selected.
constexpr Region kRingItem{-34, -56, 34, 80};
constexpr Region kEquipped{-50, -204, 50, -52};
constexpr Region kSettings{-146, 181, -46, 213};
constexpr Region kExit{-50, 181, 50, 213};
constexpr Region kMap{46, 181, 146, 213};
constexpr Region kCommands{45, -204, 146, -68};

// The ring shows seven items, the selected one in the middle. They are models on a circle seen in perspective, so
// the outer ones crowd together; each entry is the right edge of an item's share of the row, halfway to its
// neighbour, from the third on the left to the third on the right. The scroll arrows lie inside the shares of the
// two nearest neighbours and so turn the ring by one place, as they should.
constexpr int kRingSpan = 3;
constexpr int kRingRightEdge[2 * kRingSpan + 1] = {-101, -80, -31, 31, 80, 101, 160};

// The command rows: highlight bars one under another. A lone command is centred where two would meet.
constexpr int kRowLeft = 56;
constexpr int kRowRight = 136;
constexpr int kRowPitch = 16;
constexpr int kRowHeight = 12;
constexpr int kFirstRowOfTwo = -154;
constexpr int kOnlyRow = -146;

// The ring place under the pointer, counted from the selected item (-3..3).
std::optional<int> ring_offset_at(int x, int y, float scale) {
    if (y < kRingItem.top || y > kRingItem.bottom) {
        return std::nullopt;
    }
    int offset = -kRingSpan;
    for (const int edge : kRingRightEdge) {
        if (x <= static_cast<int>(static_cast<float>(edge) * scale)) {
            return offset;
        }
        ++offset;
    }
    return kRingSpan;
}

std::optional<int> command_row_at(int x, int y, int rows, int shift) {
    if (x < kRowLeft + shift || x > kRowRight + shift) {
        return std::nullopt;
    }
    const int first = rows >= 2 ? kFirstRowOfTwo : kOnlyRow;
    for (int row = 0; row < rows; ++row) {
        const int top = first + row * kRowPitch;
        if (y >= top && y < top + kRowHeight) {
            return row;
        }
    }
    return std::nullopt;
}

bool on_commands(InventoryFocus focus) {
    return focus == InventoryFocus::ItemCommands || focus == InventoryFocus::EquippedCommands;
}

// How far a finger moves the ring by one item, in screen pixels at ring scale 1: from the centre item to its
// neighbour, so the item under the finger stays under it.
constexpr double kItemPitch = 62.0;

bool in_ring_band(int y) { return y >= kRingItem.top && y <= kRingItem.bottom; }

// Places from `from` to `to` the shorter way round (positive: towards higher places).
int ring_distance(int from, int to, int count) {
    int ahead = ((to - from) % count + count) % count;
    if (ahead * 2 > count) ahead -= count;
    return ahead;
}

}  // namespace

void InventoryPointer::reset() {
    turn_to_ = kNowhere;
    carousel_ = RingCarousel{};
    ring_owned_ = false;
    finger_down_ = false;
    frames_since_lift_ = kTapFrames;
    down_on_ring_ = false;
}

bool InventoryPointer::follow_finger(const View& view, Action& action) {
    carousel_.set_count(view.item_count);
    carousel_.set_pitch(kItemPitch * static_cast<double>(view.ring_scale));
    const bool landed = view.finger_down && !finger_down_;
    if (finger_down_ && !view.finger_down) {
        frames_since_lift_ = 0;
    } else if (frames_since_lift_ < kTapFrames) {
        ++frames_since_lift_;
    }
    finger_down_ = view.finger_down;

    if (on_commands(view.focus)) {
        // Commands opened by other means (the pad): the ring stops on the selected item.
        ring_owned_ = false;
        down_on_ring_ = false;
        carousel_.release();
        carousel_.jump(view.selected_item);
        return false;
    }
    if (landed) {
        down_x_ = view.x;
        down_on_ring_ = in_ring_band(view.y);
        if (down_on_ring_ && ring_owned_) {
            carousel_.touch();
        }
    }
    // A ring at rest is where the screen keeps it (the pad turns it too).
    if (!ring_owned_ && !carousel_.held()) {
        carousel_.jump(view.selected_item);
    }

    if (view.finger_dragging && view.finger_horizontal && down_on_ring_) {
        if (!carousel_.grabbed()) {
            ring_owned_ = true;
            // From where the finger landed: the ring catches up the distance a drag needs to start.
            carousel_.press(down_x_);
        }
        carousel_.drag(view.x, view.seconds);
    } else if (carousel_.held() && (!view.finger_down || view.finger_dragging)) {
        carousel_.release();
    }

    // A click a finger made (a mouse clicking a neighbour walks the ring there as before).
    if (view.clicked && !carousel_.held() && frames_since_lift_ < kTapFrames) {
        const std::optional<int> offset = ring_offset_at(view.x, view.y, view.ring_scale);
        // A tap on the centre item of a ring at rest opens its commands as a click does (below); one on a ring
        // still settling only lets it settle.
        if (offset && *offset != 0) {
            ring_owned_ = true;
            carousel_.turn_by(*offset);
        }
    }

    carousel_.update(view.seconds);
    if (!ring_owned_) {
        return false;
    }
    if (!carousel_.active() && carousel_.item() == view.selected_item) {
        // At rest on the chosen item, and the screen agrees: the ring is the screen's again.
        ring_owned_ = false;
        return true;
    }
    if (view.focus != InventoryFocus::Item) {
        action.focus = InventoryFocus::Item;
    }
    action.ring_position = carousel_.position();
    const int ahead = ring_distance(view.selected_item, carousel_.item(), view.item_count);
    if (ahead != 0) {
        action.press = ahead > 0 ? Press::RingRight : Press::RingLeft;
        action.settle = true;
    }
    return true;
}

InventoryPointer::Action InventoryPointer::update(const View& view) {
    Action action;
    if (view.item_count <= 0) {
        return action;
    }
    const auto select = [&](InventoryFocus focus) {
        if (view.focus != focus) {
            action.focus = focus;
        }
    };

    if (turn_to_ != kNowhere) {
        if (view.selected_item == turn_to_ || view.focus != InventoryFocus::Item) {
            turn_to_ = kNowhere;
        } else {
            // The shorter way round.
            const int ahead = (turn_to_ - view.selected_item + view.item_count) % view.item_count;
            action.press = ahead * 2 <= view.item_count ? Press::RingRight : Press::RingLeft;
            return action;
        }
    }

    if (!view.touch) {
        ring_owned_ = false;
    } else if (follow_finger(view, action)) {
        return action;
    }

    // Open commands own the pointer: passing over the ring on the way to them must not take the selection away.
    const Region commands = kCommands.moved(view.column_shift);
    if (on_commands(view.focus)) {
        if (view.right_clicked || (view.clicked && !commands.contains(view.x, view.y))) {
            action.press = Press::Cancel;
            return action;
        }
        const std::optional<int> row = command_row_at(view.x, view.y, view.command_count, view.column_shift);
        if (!row) {
            return action;
        }
        // Set directly: stepping there with up and down would restart the screen's settle time and lose the click.
        if (view.command_row != *row && (view.moved || view.clicked)) {
            action.command_row = *row;
        }
        if (view.clicked) {
            action.press = Press::Enter;
        }
        return action;
    }

    if (const std::optional<int> offset = ring_offset_at(view.x, view.y, view.ring_scale)) {
        if (view.moved || view.clicked || view.wheel != 0) {
            select(InventoryFocus::Item);
        }
        if (view.wheel != 0) {
            action.press = view.wheel > 0 ? Press::RingLeft : Press::RingRight;
        } else if (view.clicked) {
            if (*offset == 0) {
                action.press = Press::Enter;
            } else {
                turn_to_ = (view.selected_item + *offset + view.item_count) % view.item_count;
            }
        }
        return action;
    }

    std::optional<InventoryFocus> target;
    if (kEquipped.contains(view.x, view.y)) {
        // Nothing equipped: the pad cannot reach this place either.
        if (!view.has_equipped) {
            return action;
        }
        target = InventoryFocus::EquippedItem;
    } else if (kSettings.moved(-view.column_shift).contains(view.x, view.y)) {
        target = InventoryFocus::Settings;
    } else if (kExit.contains(view.x, view.y)) {
        target = InventoryFocus::Exit;
    } else if (kMap.moved(view.column_shift).contains(view.x, view.y)) {
        target = InventoryFocus::Map;
    }
    if (target) {
        if (view.moved || view.clicked) {
            select(*target);
        }
        if (view.clicked) {
            action.press = Press::Enter;
        }
    }
    return action;
}

}  // namespace sh::input
