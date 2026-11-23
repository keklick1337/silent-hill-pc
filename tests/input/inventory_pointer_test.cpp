// The mouse on the inventory screen (input/inventory_pointer.h): what each place and click asks of the screen.
#include "input/inventory_pointer.h"

#include "framework/sh_test.h"

using sh::input::InventoryFocus;
using sh::input::InventoryPointer;
using Press = InventoryPointer::Press;

namespace {

InventoryPointer::View at(int x, int y) {
    InventoryPointer::View v;
    v.x = x;
    v.y = y;
    v.item_count = 10;
    v.selected_item = 4;
    return v;
}

}  // namespace

SH_TEST(inventory_pointer_selects_what_it_points_at) {
    InventoryPointer p;
    InventoryPointer::View v = at(0, 200);  // Exit
    SH_CHECK(!p.update(v).focus.has_value());  // a pointer at rest selects nothing
    v.moved = true;
    InventoryPointer::Action a = p.update(v);
    SH_CHECK(a.focus == InventoryFocus::Exit && a.press == Press::None);
    v.focus = InventoryFocus::Exit;
    SH_CHECK(!p.update(v).focus.has_value());  // already there
    v.clicked = true;
    SH_CHECK(p.update(v).press == Press::Enter);

    SH_CHECK(p.update(at(-100, 200)).press == Press::None);
    v = at(-100, 200);
    v.clicked = true;
    a = p.update(v);
    SH_CHECK(a.focus == InventoryFocus::Settings && a.press == Press::Enter);
    v = at(100, 181);
    v.moved = true;
    SH_CHECK(p.update(v).focus == InventoryFocus::Map);
    v = at(0, 150);  // between the ring and the bottom row
    v.clicked = true;
    a = p.update(v);
    SH_CHECK(!a.focus.has_value() && a.press == Press::None);
}

SH_TEST(inventory_pointer_equipped_item_needs_one) {
    InventoryPointer p;
    InventoryPointer::View v = at(0, -100);
    v.clicked = true;
    InventoryPointer::Action a = p.update(v);
    SH_CHECK(!a.focus.has_value() && a.press == Press::None);
    v.has_equipped = true;
    a = p.update(v);
    SH_CHECK(a.focus == InventoryFocus::EquippedItem && a.press == Press::Enter);
}

SH_TEST(inventory_pointer_ring_clicks_and_wheel) {
    InventoryPointer p;
    InventoryPointer::View v = at(0, 0);
    v.focus = InventoryFocus::Exit;
    v.clicked = true;
    InventoryPointer::Action a = p.update(v);
    SH_CHECK(a.focus == InventoryFocus::Item && a.press == Press::Enter);

    // The wheel turns by one and wins over a click.
    v = at(0, 0);
    v.wheel = 1;
    v.clicked = true;
    SH_CHECK(p.update(v).press == Press::RingLeft);
    v.wheel = -1;
    SH_CHECK(p.update(v).press == Press::RingRight);

    // Two places to the right: walked one press at a time until the ring is there.
    v = at(90, 0);
    v.clicked = true;
    SH_CHECK(p.update(v).press == Press::None);
    v = at(-300, -300);  // the pointer may leave meanwhile
    SH_CHECK(p.update(v).press == Press::RingRight);
    v.selected_item = 5;
    SH_CHECK(p.update(v).press == Press::RingRight);
    v.selected_item = 6;
    SH_CHECK(p.update(v).press == Press::None);

    // The places of the ring: the item next to the selected one on the left, and the far ends.
    v = at(-40, 0);
    v.clicked = true;
    p.update(v);
    v = at(-300, -300);
    SH_CHECK(p.update(v).press == Press::RingLeft);  // from 4 to 3
    p.reset();
    SH_CHECK(p.update(v).press == Press::None);
    v = at(-150, 0);
    v.clicked = true;
    p.update(v);
    v.clicked = false;
    v.selected_item = 2;
    SH_CHECK(p.update(v).press == Press::RingLeft);  // the target is 1
    v.selected_item = 1;
    SH_CHECK(p.update(v).press == Press::None);

    // A stretched picture pulls the items inwards: the same pointer is one place further out.
    v = at(-90, 0);
    v.selected_item = 4;
    v.clicked = true;
    v.ring_scale = 0.75f;
    p.update(v);
    v.clicked = false;
    v.selected_item = 2;
    SH_CHECK(p.update(v).press == Press::RingLeft);  // -90 is beyond -101 * 0.75: three places, to 1
    p.reset();

    // Above or below the ring is not the ring.
    v = at(0, 81);
    v.clicked = true;
    SH_CHECK(p.update(v).press == Press::None);
}

SH_TEST(inventory_pointer_turn_stops_when_the_selection_leaves) {
    InventoryPointer p;
    InventoryPointer::View v = at(90, 0);
    v.clicked = true;
    p.update(v);
    v = at(-300, -300);
    v.focus = InventoryFocus::Exit;
    SH_CHECK(p.update(v).press == Press::None);
    v.focus = InventoryFocus::Item;
    SH_CHECK(p.update(v).press == Press::None);  // and it does not resume
}

SH_TEST(inventory_pointer_commands_are_modal) {
    InventoryPointer p;
    InventoryPointer::View v = at(0, 0);  // over the ring
    v.focus = InventoryFocus::ItemCommands;
    v.command_count = 2;
    v.moved = true;
    InventoryPointer::Action a = p.update(v);
    SH_CHECK(!a.focus.has_value() && a.press == Press::None);
    v.clicked = true;
    SH_CHECK(p.update(v).press == Press::Cancel);  // a click beside the commands
    v = at(100, -150);
    v.focus = InventoryFocus::ItemCommands;
    v.command_count = 2;
    v.right_clicked = true;
    SH_CHECK(p.update(v).press == Press::Cancel);

    // Two rows from -154, sixteen apart, twelve tall.
    v.right_clicked = false;
    v.command_row = 1;
    v.moved = true;
    a = p.update(v);
    SH_CHECK(a.command_row == 0 && a.press == Press::None);
    v.y = -138;
    v.command_row = 0;
    v.moved = false;
    v.clicked = true;
    a = p.update(v);
    SH_CHECK(a.command_row == 1 && a.press == Press::Enter);
    v.y = -141;  // the gap between the rows
    a = p.update(v);
    SH_CHECK(!a.command_row.has_value() && a.press == Press::None);
    // A lone command sits at -146.
    v.command_count = 1;
    v.y = -146;
    v.command_row = 0;
    a = p.update(v);
    SH_CHECK(!a.command_row.has_value() && a.press == Press::Enter);
    v.y = -154;
    SH_CHECK(p.update(v).press == Press::None);
}

SH_TEST(inventory_pointer_empty_inventory) {
    InventoryPointer p;
    InventoryPointer::View v = at(0, 200);
    v.item_count = 0;
    v.clicked = true;
    const InventoryPointer::Action a = p.update(v);
    SH_CHECK(!a.focus.has_value() && a.press == Press::None);
}
