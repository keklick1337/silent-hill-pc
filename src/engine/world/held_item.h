// SPDX-License-Identifier: GPL-3.0-or-later
//
// What the player holds in a hand right now (docs/RENDER.md section 6): the item, whether a cutscene has
// hidden it, and the file-queue ticket of its last read (scripts wait on it). Which files an item reads and which
// hand it goes in is the table of held_items.h; the reads and the draw are in src/render/world/world_objects_draw.cpp.

#pragma once

#include <cstdint>

namespace sh::world {

class HeldItemState {
public:
    static constexpr std::int32_t kNone = -1;

    std::int32_t item() const { return item_; }
    bool hidden() const { return hidden_; }
    std::int32_t ticket() const { return ticket_; }

    // Takes `item` in hand; false when it already is (nothing to read).
    bool change(std::int32_t item) {
        if (item == item_) return false;
        item_ = item;
        return true;
    }
    // The ticket of the read just queued; `model` when it reads a model file.
    void set_ticket(std::int32_t ticket, bool model) {
        ticket_ = ticket;
        model_pending_ = model_pending_ || model;
    }
    // The draw saw the ticket loaded: a model read in flight is now in hand. The hand has a model to draw once
    // any model read has landed since it was last emptied (an item without its own model keeps the last one).
    bool settle_loaded() {
        if (model_pending_) {
            model_pending_ = false;
            model_ready_ = true;
        }
        return model_ready_;
    }
    // Empty hand, shown again.
    void free() {
        item_ = kNone;
        hidden_ = false;
        model_pending_ = false;
        model_ready_ = false;
    }
    void show() { hidden_ = false; }
    void hide() { hidden_ = true; }

private:
    // A fresh game holds item 0 until the boot frees the hand.
    std::int32_t item_ = 0;
    bool hidden_ = false;
    std::int32_t ticket_ = 0;
    bool model_pending_ = false;
    bool model_ready_ = false;
};

HeldItemState& held_item();

}  // namespace sh::world
