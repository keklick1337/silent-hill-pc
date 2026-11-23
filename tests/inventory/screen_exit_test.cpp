// SPDX-License-Identifier: GPL-3.0-or-later
// Unit tests for the item screens' directions, the carousel scroll and leaving the screen
// (sh::inventory: screen_input, scroll_in_slot, store_hand_ammo, weapon_on_exit).

#include "engine/inventory/item_actions.h"
#include "engine/inventory/item_carousel.h"
#include "engine/inventory/screen_input.h"

#include "framework/sh_test.h"

using namespace sh::inventory;

namespace {

const DirectionBits kAnalog{1u << 16, 1u << 17, 1u << 18, 1u << 19};
const DirectionBits kDirection{1u << 24, 1u << 25, 1u << 26, 1u << 27};

constexpr std::uint8_t kPistol = 160, kBullets = 192, kKnife = 128;

ItemRules gun_rules()
{
    ItemRules rules;
    rules[kPistol].ammo_item = kBullets;
    rules[kPistol].keeps_empty_slot = true;
    rules[kBullets].is_ammo = true;
    return rules;
}

Slots empty_slots()
{
    Slots slots{};
    for (Slot& s : slots)
        s.item = kNoItem;
    return slots;
}

}  // namespace

SH_TEST(inventory_directions_pick_the_set_by_the_stick) {
    PadFrame pad;
    pad.clicked = (1u << 24) | (1u << 17);  // shared up, analog right
    pad.held = (1u << 27) | (1u << 19);
    pad.pulsed = 1u << 26;
    // Stick inside the zone: the shared set.
    Directions d = read_directions(pad, kAnalog, kDirection);
    SH_CHECK(d.up_clicked && !d.right_clicked && d.left_held && d.down_pulsed);
    // -65 and 64 leave the zone, -64 and 63 do not.
    for (int v : {-65, 64}) {
        pad.stick_x = v;
        d = read_directions(pad, kAnalog, kDirection);
        SH_CHECK(!d.up_clicked && d.right_clicked && d.left_held && !d.down_pulsed);
        pad.stick_x = 0;
        pad.stick_y = v;
        SH_CHECK(read_directions(pad, kAnalog, kDirection) == d);
        pad.stick_y = 0;
    }
    pad.stick_x = -64;
    pad.stick_y = 63;
    SH_CHECK(read_directions(pad, kAnalog, kDirection).up_clicked);
}

SH_TEST(inventory_scroll_replaces_the_far_display) {
    // Ten slots, displays centred on slot 3 (slots 0..6).
    std::array<std::int32_t, kCarouselModels> displays{0, 1, 2, 3, 4, 5, 6};
    // Scrolling right brings in slot 7; the display of slot 0 (7 - 7) takes it.
    SH_CHECK(scroll_in_slot(displays, true, 7, 10) == 0 && displays[0] == 7);
    // Scrolling left brings in slot 9; the display of (9 + 7) % 10 = 6 takes it.
    SH_CHECK(scroll_in_slot(displays, false, 9, 10) == 6 && displays[6] == 9);
    // Nobody shows the leaving slot: display 0 takes it.
    displays = {-1, -1, -1, -1, -1, -1, -1};
    SH_CHECK(scroll_in_slot(displays, true, 2, 8) == 0 && displays[0] == 2);
    // Fewer slots than seven: the truncating remainder can stay negative and match nothing.
    // (1 - 7 + 4) % 4 = -2: no display shows it, display 0 takes the slot.
    displays = {3, 0, 1, 2, -1, -1, -1};
    SH_CHECK(scroll_in_slot(displays, true, 1, 4) == 0 && displays[0] == 1);
    // Now {1, 0, 1, 2, ...}: (3 + 7 + 4) % 4 = 2, shown by display 3.
    SH_CHECK(scroll_in_slot(displays, false, 3, 4) == 3 && displays[3] == 3);
    // The first display showing the slot takes it: (1 + 7 + 4) % 4 = 0 is display 1.
    SH_CHECK(scroll_in_slot(displays, false, 1, 4) == 1 && displays[1] == 1);
}

SH_TEST(inventory_store_hand_ammo) {
    const ItemRules rules = gun_rules();
    Slots slots = empty_slots();
    slots[0] = Slot{kPistol, 3, 0, 0};
    slots[1] = Slot{kBullets, 10, 0, 0};
    slots[2] = Slot{kBullets, 7, 0, 0};
    // The clip goes to the gun, the reserve to the first ammo slot only.
    SH_CHECK(store_hand_ammo(slots, 8, 0, HandAmmo{15, 4}, rules));
    SH_CHECK(slots[0].count == 15 && slots[1].count == 4 && slots[2].count == 7);
    // A reserve of 0 frees the slot.
    SH_CHECK(store_hand_ammo(slots, 8, 0, HandAmmo{15, 0}, rules));
    SH_CHECK(slots[1].item == kNoItem && slots[1].count == 0);
    // Nothing in hand: nothing written.
    const Slots before = slots;
    SH_CHECK(!store_hand_ammo(slots, 8, -1, HandAmmo{1, 1}, rules));
    SH_CHECK(slots == before);
    // A melee weapon in hand: its count is written, no ammo slot.
    slots[3] = Slot{kKnife, 0, 0, 0};
    SH_CHECK(!store_hand_ammo(slots, 8, 3, HandAmmo{1, 9}, rules));
    SH_CHECK(slots[3].count == 1 && slots[2].count == 7);
    // Ammo past the scanned slot count is not found.
    SH_CHECK(!store_hand_ammo(slots, 2, 0, HandAmmo{5, 5}, rules) && slots[2].count == 7);
}

SH_TEST(inventory_weapon_on_exit) {
    ExitWeapon none = weapon_on_exit(0, 12);
    SH_CHECK(none.attack == -1 && none.stop_aiming);
    ExitWeapon gun = weapon_on_exit(160, 32);
    SH_CHECK(gun.attack == 32 && !gun.stop_aiming);
    // A packed attack past the console's signed byte (a mod's weapon) stays as it is.
    SH_CHECK(weapon_on_exit(1, 200).attack == 200);
}
