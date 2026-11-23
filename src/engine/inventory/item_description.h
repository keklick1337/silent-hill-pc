// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The text part of the inventory screen: the slot
// number, the ammo of the gun in hand, the tag after the item's name (stock, on/off, the
// HyperBlaster's state), and the name typed out letter by letter followed by the rolling
// description or a notice. The module decides what is shown where and when; the host owns the
// text (translations, widths, fonts) and the game rules of the tags. Standard library only.

#pragma once

#include <cstdint>

#include "engine/inventory/inventory.h"
#include "engine/inventory/inventory_menu.h"

namespace sh::inventory {

enum class TagText : std::uint8_t { Stock, On, Off, UseOk, UseOkMaybe, UseNg };

// The lower text: the description, or a notice in its place.
enum class BodyText : std::uint8_t { Description, CantUseHere, TooDark };

struct DescriptionState {
    std::int32_t name_timer = 0;  // letters of the name typed so far (100: all)
    std::int32_t roll_timer = 0;  // letters of the description rolled so far (100: all)
    // The item shown last frame, as group - 1 and index in group (the game's encoding).
    std::int32_t last_group = 0;
    std::uint8_t last_index = 0;
};

class DescriptionHost {
public:
    virtual ~DescriptionHost() = default;

    virtual Selection selection() = 0;
    virtual std::int32_t selected_slot() = 0;
    virtual std::int32_t weapon_slot() = 0;
    virtual std::int32_t shown_slot() = 0;  // the carousel's centre slot
    virtual std::int32_t slot_count() = 0;
    virtual Slot slot(std::int32_t index) = 0;
    virtual int item_in_hand() = 0;

    // The text phase (0 name, 1 description, 2 shown, 3/4 notices) lives with the game state.
    virtual std::int32_t phase() = 0;
    virtual void next_phase() = 0;
    virtual void restart() = 0;  // phase 0 and everything that restarts with a new item

    // Game rules.
    virtual ItemTag tag(int item) = 0;
    virtual bool shows_ammo_in_hand(int item) = 0;
    virtual bool switched_on(int item) = 0;
    virtual int hyper_blaster_state() = 0;  // 2 usable, 1 usable on the second port, 0 locked

    // Text. Positions are the English layout; the host widens them for translations.
    virtual void draw_slot_number(std::int32_t x, std::int32_t y, std::int32_t number) = 0;
    virtual void draw_hand_stock_label(std::int32_t x, std::int32_t y) = 0;
    virtual void draw_hand_stock(std::int32_t x, std::int32_t y, std::int32_t count) = 0;
    virtual std::int32_t place_tag(TagText text, std::int32_t x, int item) = 0;  // returns y
    virtual void draw_tag(TagText text) = 0;
    virtual void draw_stock(std::int32_t x, std::int32_t y, std::int32_t count) = 0;
    virtual void text_white() = 0;
    virtual bool draw_name(int item, std::int32_t letters) = 0;  // true once complete
    virtual bool draw_body(BodyText text, int item, std::int32_t letters) = 0;
};

// One frame of the text (the game's Gfx_Inventory_ItemDescriptionDraw).
void update_description(DescriptionState& state, DescriptionHost& host);

// Name row layout for translations. The NTSC positions fit the English words; a translation
// moves a label right by how much wider it is than its English word (`grow`, never negative).
constexpr std::int32_t kNameLabelX = 68;  // "Name:"
constexpr std::int32_t kNameRowY = 200;
constexpr std::int32_t kTagDropRowY = 216;  // the empty row under the name

struct TagPlace {
    std::int32_t x = 0;
    std::int32_t y = kNameRowY;
};

// The x where the item name starts: after "Name:", widened as that label is.
std::int32_t name_x(std::int32_t name_label_grow);

// A tag after the name (Stock:, ==On==, ...) at English x `x` keeps the English right edge (it
// moves left by its own growth). It drops to the row below when it would overlap the translated
// name, and more than the English tag overlaps the English name, so the English layout stays as
// it was.
TagPlace place_name_tag(std::int32_t x, std::int32_t tag_grow, std::int32_t name_label_grow,
                        std::int32_t english_name_width, std::int32_t name_width);

}  // namespace sh::inventory
