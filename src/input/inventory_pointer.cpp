// SPDX-License-Identifier: GPL-3.0-or-later
#include "input/inventory_pointer.h"

namespace sh::input {

namespace {

// A region of the screen, edges included.
struct Region {
    int left, top, right, bottom;

    constexpr bool contains(int x, int y) const { return x >= left && x <= right && y >= top && y <= bottom; }
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

std::optional<int> command_row_at(int x, int y, int rows) {
    if (x < kRowLeft || x > kRowRight) {
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

}  // namespace

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

    // Open commands own the pointer: passing over the ring on the way to them must not take the selection away.
    if (on_commands(view.focus)) {
        if (view.right_clicked || (view.clicked && !kCommands.contains(view.x, view.y))) {
            action.press = Press::Cancel;
            return action;
        }
        const std::optional<int> row = command_row_at(view.x, view.y, view.command_count);
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
    } else if (kSettings.contains(view.x, view.y)) {
        target = InventoryFocus::Settings;
    } else if (kExit.contains(view.x, view.y)) {
        target = InventoryFocus::Exit;
    } else if (kMap.contains(view.x, view.y)) {
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
