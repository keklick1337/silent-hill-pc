// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/inventory/item_description.h"

namespace sh::inventory {

namespace {

constexpr std::int32_t kAllLetters = 100;
constexpr std::int32_t kTagX = 208;
constexpr std::int32_t kBlasterTagX = kTagX - 16;
constexpr std::int32_t kNumberY = 184;
constexpr std::int32_t kHandStockX = 122;
constexpr std::int32_t kHandStockY = 30;

bool shows_slot_item(Selection selection)
{
    switch (selection) {
        case Selection::Items:
        case Selection::ItemCommands:
        case Selection::Health:
        case Selection::Examine: return true;
        default: return false;
    }
}

bool is_button(Selection selection)
{
    return selection == Selection::Exit || selection == Selection::Option || selection == Selection::Map;
}

// The name row's tag and the description only show for the item row, the item in hand and
// the actions on them.
bool shows_details(Selection selection)
{
    const auto value = static_cast<std::uint32_t>(selection);
    return value < 2 || (value >= 5 && value < 9);
}

TagText blaster_text(int state)
{
    return state == 2 ? TagText::UseOk : state == 1 ? TagText::UseOkMaybe : TagText::UseNg;
}

}  // namespace

void update_description(DescriptionState& state, DescriptionHost& host)
{
    const Selection selection = host.selection();
    const std::int32_t index = shows_slot_item(selection) ? host.selected_slot() : host.weapon_slot();
    const int item = host.slot(index).item;

    if (state.last_group != (item >> 5) - 1 || state.last_index != (item & 31)) {
        host.restart();
        state.name_timer = 0;
        state.roll_timer = 0;
    }

    if (index != -1 && !is_button(selection) && index >= 0 && index < host.slot_count())
        host.draw_slot_number(index + 1 >= 10 ? 45 : 55, kNumberY, index + 1);

    const int in_hand = host.item_in_hand();
    if (host.shows_ammo_in_hand(in_hand)) {
        host.draw_hand_stock_label(kHandStockX, kHandStockY);
        // Every slot of the item in hand reports (there is normally one).
        for (std::int32_t i = 0; i < host.slot_count(); i++) {
            const Slot slot = host.slot(i);
            if (slot.item == in_hand)
                host.draw_hand_stock(slot.count >= 10 ? 178 : 188, kHandStockY, slot.count);
        }
    }

    if (!shows_details(selection) || item == kNoItem)
        return;

    switch (host.tag(item)) {
        case ItemTag::Switch: {
            const TagText text = host.switched_on(item) ? TagText::On : TagText::Off;
            host.place_tag(text, kTagX, item);
            host.draw_tag(host.switched_on(item) ? TagText::On : TagText::Off);
            break;
        }
        case ItemTag::HyperBlaster: {
            // The game asks the HyperBlaster twice: once to place the tag, once to draw it.
            host.place_tag(blaster_text(host.hyper_blaster_state()), kBlasterTagX, item);
            const int blaster = host.hyper_blaster_state();
            if (blaster >= 0 && blaster <= 2)
                host.draw_tag(blaster_text(blaster));
            break;
        }
        case ItemTag::Stock: {
            const std::int32_t y = host.place_tag(TagText::Stock, kTagX, item);
            host.draw_tag(TagText::Stock);
            const std::int32_t count = host.slot(index).count;
            host.draw_stock(count >= 100 ? 260 : count >= 10 ? 270 : 280, y, count);
            break;
        }
        case ItemTag::None: break;
    }

    host.text_white();
    state.last_group = (item >> 5) - 1;
    state.last_index = static_cast<std::uint8_t>(item & 31);

    switch (host.phase()) {
        case 0:
            state.name_timer++;
            if (host.draw_name(item, state.name_timer)) {
                state.name_timer = kAllLetters;
                host.next_phase();
            }
            break;

        case 1:
            state.roll_timer += 2;
            host.draw_name(item, kAllLetters);
            // The name types out again when the carousel comes back to this slot.
            if (index == host.shown_slot())
                state.name_timer = 0;
            if (host.draw_body(BodyText::Description, item, state.roll_timer)) {
                state.roll_timer = kAllLetters;
                host.next_phase();
            }
            break;

        case 2:
        case 3:
        case 4: {
            const std::int32_t phase = host.phase();
            host.draw_name(item, kAllLetters);
            host.draw_body(phase == 2 ? BodyText::Description : phase == 3 ? BodyText::CantUseHere : BodyText::TooDark,
                           item, kAllLetters);
            if (index == host.shown_slot())
                state.name_timer = 0;
            break;
        }

        default: break;
    }
}

std::int32_t name_x(std::int32_t name_label_grow) { return kNameLabelX + name_label_grow; }

TagPlace place_name_tag(std::int32_t x, std::int32_t tag_grow, std::int32_t name_label_grow,
                        std::int32_t english_name_width, std::int32_t name_width) {
    const std::int32_t english_gap = x - (kNameLabelX + english_name_width);
    TagPlace place;
    place.x = x - tag_grow;
    const std::int32_t gap = place.x - (name_x(name_label_grow) + name_width);
    place.y = gap < 0 && gap < english_gap ? kTagDropRowY : kNameRowY;
    return place;
}

}  // namespace sh::inventory
