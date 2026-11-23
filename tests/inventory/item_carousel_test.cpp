// SPDX-License-Identifier: GPL-3.0-or-later
// Unit tests for the inventory item carousel (sh::inventory::update_carousel); expected values
// are the original screen's.

#include <array>

#include "engine/inventory/item_carousel.h"
#include "engine/inventory/inventory_menu.h"

#include "framework/sh_test.h"

using namespace sh::inventory;

namespace {

struct Pose {
    std::int32_t scale = 0, x = 0, y = 0, z = 0, angle = 0;
};

class FakeHost final : public CarouselHost {
public:
    std::int32_t step_value = 1, substep = 7, selected = 0, count = 10, weapon = -1;
    std::array<std::int32_t, 8> displays{7, 8, 9, 0, 1, 2, 3, -1};
    std::array<Slot, 41> slots{};
    std::array<Pose, 8> poses{};
    int selection_resets = 0, commands = 0, equips = 0, ages = 0;
    bool loaded = true, clicked = false;

    std::int32_t step() override { return step_value; }
    std::int32_t selected_slot() override { return selected; }
    std::int32_t slot_count() override { return count; }
    std::int32_t display_slot(int display) override { return displays[display]; }
    Slot slot(std::int32_t index) override { return slots[index]; }
    std::int32_t weapon_slot() override { return weapon; }
    void scale_model(int d, std::int32_t s) override { poses[d].scale = s; }
    void place_model(int d, std::int32_t x, std::int32_t y, std::int32_t z) override { poses[d].x = x; poses[d].y = y; poses[d].z = z; }
    void turn_model(int d, std::int32_t a) override { poses[d].angle = a; }
    void spin_model(int d, std::int32_t a) override { poses[d].angle += a; }
    void set_step(std::int32_t s, bool reset) override { step_value = s; if (reset) substep = 0; }
    void select_item_row() override { selection_resets++; }
    void reset_selection_age() override { ages++; }
    bool files_loaded() override { return loaded; }
    bool confirm_clicked() override { return clicked; }
    void set_slot_count(std::int32_t i, std::int32_t c) override { slots[i].count = c; }
    void empty_slot(std::int32_t i) override { slots[i].item = kNoItem; }
    void refresh_commands() override { commands++; }
    bool is_ammo(std::int32_t i) override { return slots[i].item >= 0xC0 && slots[i].item < 0xE0; }
    bool used_up_on_use(std::int32_t) override { return true; }
    void equip_start() override { equips++; }
    void equip_finish() override { equips += 10; }
    void unequip_finish() override {}
    std::int32_t reload_equipped() override { return -1; }
    ReloadResult reload_selected() override { return {}; }
    void use_health() override { slots[selected].count--; }
};

}  // namespace

SH_TEST(inventory_carousel_settled_layout) {
    FakeHost host;
    CarouselState state;
    update_carousel(state, host);
    SH_CHECK(host.poses[3].x == 0 && host.poses[3].y == 64 && host.poses[3].z == -1024);
    SH_CHECK(host.poses[3].angle == -16 && host.poses[7].angle == 32);
    SH_CHECK(host.poses[4].x == 682 && host.poses[4].z == 543 && host.poses[4].angle == -341);
    // Slot 7 is three slots to the left the short way round.
    SH_CHECK(host.poses[0].x == -1365 && host.poses[0].z == 2760 && host.poses[0].angle == 1024);
    SH_CHECK(host.poses[0].scale == 4096 && state.timer == 0);
}

SH_TEST(inventory_carousel_scroll_right) {
    FakeHost host;
    host.selected = 1;
    CarouselState state;
    update_carousel(state, host);
    SH_CHECK(state.timer == 1 && state.shown_slot == 0);
    SH_CHECK(host.poses[4].x == 602 && host.poses[4].z == 355 && host.poses[4].angle == -299 - 16);
    for (int i = 0; i < 7; i++)
        update_carousel(state, host);
    SH_CHECK(state.timer == 0 && state.shown_slot == 1);
    SH_CHECK(host.poses[4].x == 0 && host.poses[4].z == -1024);
}

SH_TEST(inventory_carousel_equip_and_health) {
    FakeHost host;
    host.step_value = static_cast<std::int32_t>(ScreenStep::Equip);
    CarouselState state;
    for (int i = 0; i < 8; i++)
        update_carousel(state, host);
    SH_CHECK(host.poses[7].scale == 8 * 512 && host.poses[7].y == -1408 && host.equips == 1);
    update_carousel(state, host);
    SH_CHECK(host.equips == 11 && host.step_value == 1 && state.timer == 0 && host.selection_resets == 1);

    // The last health drink is used: its model shrinks and the slot empties on frame 9.
    host.step_value = static_cast<std::int32_t>(ScreenStep::UseHealth);
    host.selected = 0;
    host.slots[0] = Slot{0x20, 1, 0, 0};
    update_carousel(state, host);
    SH_CHECK(host.slots[0].count == 0 && host.poses[3].scale == 4096 - 512);
    for (int i = 0; i < 8; i++)
        update_carousel(state, host);
    SH_CHECK(host.slots[0].item == kNoItem && host.commands == 1 && host.step_value == 1);
}

SH_TEST(inventory_carousel_examine_glare) {
    FakeHost host;
    host.step_value = static_cast<std::int32_t>(ScreenStep::Examine);
    CarouselState state;
    for (int i = 0; i < 32; i++)
        update_carousel(state, host);
    SH_CHECK(host.step_value == 14 && host.substep == 0 && state.glare_time == 32);
    for (int i = 0; i < 40; i++)
        update_carousel(state, host);
    SH_CHECK(host.step_value == 14 && state.glare_time == 64);
    host.clicked = true;
    update_carousel(state, host);
    SH_CHECK(host.step_value == 15 && state.glare_time == 0 && host.ages == 1);
    for (int i = 0; i < 17; i++)
        update_carousel(state, host);
    SH_CHECK(host.step_value == 1 && state.glare_time == 0 && host.ages == 2 && host.selection_resets == 1);
}
