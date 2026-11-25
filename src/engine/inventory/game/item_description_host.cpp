// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The inventory screen's text over the session: sh::inventory::update_description
// decides what is shown where and when; this host owns the strings, the translation-aware positions
// (item_screen_text.h) and the text phase (the in-game step 1). Gfx_Inventory_ItemDescriptionDraw keeps the name the
// screen draw calls it by.

#include <cstdint>

#include "engine/effects/map_lighting.h"
#include "engine/game_state/game_state_inventory.h"
#include "engine/game_state/save_record.h"
#include "engine/game_state/session.h"
#include "engine/inventory/game/inventory_host.h"
#include "engine/inventory/game/wide_screen.h"
#include "engine/inventory/item_description.h"
#include "engine/inventory/item_screen_text.h"
#include "engine/inventory/item_view.h"
#include "engine/text/game_text.h"

namespace {

namespace inv = sh::inventory;
using sh::game::session;
using sh::text::game_text;

constexpr std::int16_t kWhiteText = 7;  // the game's text colour id

const char* tag_text(inv::TagText text)
{
    static const char* const kText[] = {"Stock:", "==On==", "==Off==", "==Use_OK==", "==Use_OK?==", "==Use_NG=="};
    return kText[static_cast<int>(text)];
}

class DescriptionHost final : public inv::DescriptionHost {
public:
    explicit DescriptionHost(std::int32_t* selection) : selection_(selection) {}

    inv::Selection selection() override { return static_cast<inv::Selection>(*selection_); }
    std::int32_t selected_slot() override { return session().system.inventory_selected; }
    std::int32_t weapon_slot() override { return session().system.combat.weapon_slot; }
    std::int32_t shown_slot() override { return inv::item_screen().carousel.shown_slot; }
    std::int32_t slot_count() override { return sh::game::save_record().slot_count; }

    inv::Slot slot(std::int32_t index) override { return sh::game_state::inventory_slot(index); }

    int item_in_hand() override { return inv::inventory_menu().equipped_item; }

    std::int32_t phase() override { return session().system.flow.sys_steps[1]; }
    void next_phase() override { session().advance_sys_step(1); }

    void restart() override
    {
        // (The game also clears two counters only its Japanese build reads.)
        session().set_sys_step(1, 0);
        inv::inventory_menu().scrolled = 0;
    }

    inv::ItemTag tag(int item) override { return inv::game::item_rules()[item].tag; }
    bool shows_ammo_in_hand(int item) override { return inv::game::item_rules()[item].stock_in_hand; }

    bool switched_on(int item) override
    {
        const sh::items::ItemDef* def = inv::game::item_def(item);
        if (def != nullptr && def->inventory.switches == "flashlight")
            return sh::effects::map_lighting().flashlight_on();
        return (sh::game::save_record().item_toggles & sh::game::save_bits::kRadioOn) != 0;
    }

    int hyper_blaster_state() override { return inv::game::unlock_function(); }

    void draw_slot_number(std::int32_t x, std::int32_t y, std::int32_t number) override
    {
        game_text().set_position(wide_.x(inv::Column::Left, x + inv::label_growth("No.")), y);
        game_text().draw_int(number);
    }

    // The ammo in hand stays centred over the gun, label and count together.
    void draw_hand_stock_label(std::int32_t x, std::int32_t y) override
    {
        stock_grow_ = inv::label_growth(tag_text(inv::TagText::Stock));
        game_text().set_position(x - stock_grow_ / 2, y);
        game_text().draw(tag_text(inv::TagText::Stock), 10);
    }

    void draw_hand_stock(std::int32_t x, std::int32_t y, std::int32_t count) override
    {
        game_text().set_position(x + stock_grow_ - stock_grow_ / 2, y);
        game_text().draw_int(count);
    }

    std::int32_t place_tag(inv::TagText text, std::int32_t x, int item) override
    {
        return inv::place_name_row_tag(x, tag_text(text), item, wide_.x(inv::Column::Right, 0));
    }

    void draw_tag(inv::TagText text) override { game_text().draw(tag_text(text), 10); }

    void draw_stock(std::int32_t x, std::int32_t y, std::int32_t count) override
    {
        game_text().set_position(wide_.x(inv::Column::Right, x), y);
        game_text().draw_int(count);
    }

    void text_white() override { game_text().set_color(kWhiteText); }

    bool draw_name(int item, std::int32_t letters) override
    {
        game_text().set_position(wide_.x(inv::Column::Left, inv::item_name_x()), 200);
        return game_text().draw(inv::item_name_text(item), letters) == true;
    }

    bool draw_body(inv::BodyText text, int item, std::int32_t letters) override
    {
        static const char* const kNotices[] = {"Can't_use_it_here.", "Too_dark_to_look_at\n\t\tthe_item_here."};
        game_text().set_position(wide_.x(inv::Column::Left, 30), 232);
        const char* str = text == inv::BodyText::Description ? inv::item_description_text(item)
                                                              : kNotices[static_cast<int>(text) - 1];
        return game_text().draw(str, letters) == true;
    }

private:
    std::int32_t* selection_;
    const inv::WideLayout wide_ = inv::wide_screen();
    std::int32_t stock_grow_ = 0;
};

}  // namespace

extern "C" void Gfx_Inventory_ItemDescriptionDraw(std::int32_t* selection)
{
    DescriptionHost host(selection);
    inv::update_description(inv::item_screen().description, host);
}
