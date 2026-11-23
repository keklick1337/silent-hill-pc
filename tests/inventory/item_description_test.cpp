// SPDX-License-Identifier: GPL-3.0-or-later
// Unit tests for the inventory screen's text (sh::inventory::update_description).

#include <array>
#include <string>
#include <vector>

#include "engine/inventory/item_description.h"

#include "framework/sh_test.h"

using namespace sh::inventory;

namespace {

class FakeHost final : public DescriptionHost {
public:
    Selection sel = Selection::Items;
    std::int32_t selected = 0, weapon = -1, shown = 0, count = 8, phase_value = 0;
    int hand = kNoItem;
    std::array<Slot, 40> slots{};
    std::vector<std::string> log;
    int name_letters_needed = 3, restarts = 0;

    Selection selection() override { return sel; }
    std::int32_t selected_slot() override { return selected; }
    std::int32_t weapon_slot() override { return weapon; }
    std::int32_t shown_slot() override { return shown; }
    std::int32_t slot_count() override { return count; }
    Slot slot(std::int32_t i) override { return slots[i]; }
    int item_in_hand() override { return hand; }
    std::int32_t phase() override { return phase_value; }
    void next_phase() override { phase_value++; }
    void restart() override { phase_value = 0; restarts++; }
    ItemTag tag(int item) override { return item == 0x20 ? ItemTag::Stock : ItemTag::None; }
    bool shows_ammo_in_hand(int item) override { return item == 0x80; }
    bool switched_on(int) override { return false; }
    int hyper_blaster_state() override { return 0; }
    void draw_slot_number(std::int32_t x, std::int32_t y, std::int32_t n) override { log.push_back("no " + std::to_string(x) + "," + std::to_string(y) + "=" + std::to_string(n)); }
    void draw_hand_stock_label(std::int32_t, std::int32_t) override { log.push_back("hand"); }
    void draw_hand_stock(std::int32_t x, std::int32_t, std::int32_t c) override { log.push_back("hand " + std::to_string(x) + "=" + std::to_string(c)); }
    std::int32_t place_tag(TagText, std::int32_t, int) override { return 200; }
    void draw_tag(TagText) override { log.push_back("tag"); }
    void draw_stock(std::int32_t x, std::int32_t, std::int32_t c) override { log.push_back("stock " + std::to_string(x) + "=" + std::to_string(c)); }
    void text_white() override {}
    bool draw_name(int, std::int32_t letters) override { log.push_back("name " + std::to_string(letters)); return letters >= name_letters_needed; }
    bool draw_body(BodyText t, int, std::int32_t letters) override { log.push_back("body " + std::to_string(static_cast<int>(t)) + " " + std::to_string(letters)); return letters >= 4; }
};

}  // namespace

SH_TEST(inventory_description_types_name_then_rolls) {
    FakeHost host;
    host.slots[0] = Slot{0x20, 12, 0, 0};
    host.slots[3] = Slot{0x80, 7, 0, 0};
    host.hand = 0x80;
    host.shown = 5;
    DescriptionState state;
    state.last_group = state.last_index = 0xFF;  // as the screen leaves it on opening
    update_description(state, host);
    // A new item restarts the text; the slot number, the gun's rounds, the stock tag and count.
    SH_CHECK(host.restarts == 1 && host.log.size() == 6);
    SH_CHECK(host.log[0] == "no 55,184=1" && host.log[2] == "hand 188=7" && host.log[4] == "stock 270=12");
    SH_CHECK(host.log[5] == "name 1" && state.last_group == 0 && state.last_index == 0);
    for (int i = 0; i < 2; i++)
        update_description(state, host);
    SH_CHECK(host.phase_value == 1 && state.name_timer == 100 && host.restarts == 1);
    update_description(state, host);
    update_description(state, host);
    SH_CHECK(host.phase_value == 2 && state.roll_timer == 100 && host.log.back() == "body 0 4");
    host.phase_value = 3;
    host.shown = 0;
    update_description(state, host);
    SH_CHECK(host.log.back() == "body 1 100" && state.name_timer == 0);

    // The Exit button: no slot number, no tag, no text; the ammo in hand stays.
    host.log.clear();
    host.sel = Selection::Exit;
    host.weapon = 3;
    update_description(state, host);
    SH_CHECK(host.restarts == 2 && host.log.size() == 2 && host.log[0] == "hand");
}

// English layout: the tag sits where retail puts it, even when it touches the name (gap equal to
// the English one); a translation that overlaps more drops the tag to the row below.
SH_TEST(inventory_name_tag_keeps_english_layout) {
    using namespace sh::inventory;
    SH_CHECK(name_x(0) == 68);
    SH_CHECK(name_x(9) == 77);

    TagPlace english = place_name_tag(200, 0, 0, 150, 150);
    SH_CHECK(english.x == 200 && english.y == 200);
    english = place_name_tag(200, 0, 0, 140, 140);  // the English name runs 8 into the tag
    SH_CHECK(english.x == 200 && english.y == 200);

    TagPlace wide = place_name_tag(200, 10, 4, 100, 140);  // tag at 190, name ends at 212
    SH_CHECK(wide.x == 190 && wide.y == 216);
    TagPlace fits = place_name_tag(200, 10, 4, 100, 110);  // name ends at 182
    SH_CHECK(fits.x == 190 && fits.y == 200);
}
