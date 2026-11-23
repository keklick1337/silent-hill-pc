// SPDX-License-Identifier: GPL-3.0-or-later
// The item view (sh::inventory::ItemView, item_view.h): which slot each model shows, the map pack
// lookup, the screen fit of the item draw, the Unknown Liquid model fix and the results awards.

#include "engine/inventory/item_models.h"
#include "engine/inventory/item_view.h"
#include "engine/items/item_library.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <vector>

#include "framework/sh_test.h"

namespace {

namespace inv = sh::inventory;

}  // namespace

SH_TEST(item_view_open_carousel_rings_the_selection) {
    inv::ItemView view;
    view.shown[9] = 5;
    inv::open_carousel(view, 1, 8);
    const std::array<std::int32_t, 7> row = {6, 7, 0, 1, 2, 3, 4};
    for (int i = 0; i < 7; ++i)
        SH_CHECK(view.shown[i] == row[i]);
    SH_CHECK(view.equipped() == inv::kNoModelSlot && view.shown[9] == inv::kNoModelSlot);
}

SH_TEST(item_view_slot_and_pack_lookup) {
    inv::Slots slots;
    slots[2].item = 0x80;
    slots[5].item = 0x80;
    slots[9].item = 0x81;
    SH_CHECK(inv::slot_of_item(slots, 8, 0x80) == 2);
    SH_CHECK(inv::slot_of_item(slots, 8, 0x81) == inv::kNoModelSlot);  // past the slot count
    SH_CHECK(inv::slot_of_item(slots, 10, 0x81) == 9);

    std::array<std::int32_t, 48> pack{};
    pack[0] = 0x20;
    pack[3] = 0x80;
    pack[41] = 0x90;
    SH_CHECK(inv::pack_index(pack, 0x80) == 3);
    SH_CHECK(inv::pack_index(pack, 0x90) == -1);  // only the first 40 entries
    SH_CHECK(inv::pack_index(pack, 0) == 1);      // the end of the list matches like any entry
}

SH_TEST(item_view_link_count) {
    inv::ItemView view;
    for (int i = 0; i < 12; ++i)
        view.count_link();
    SH_CHECK(view.linked == inv::kViewModels);
}

SH_TEST(item_view_screen_fit) {
    inv::ScreenMatrix ls;
    ls.m = {{{4096, 0, 0}, {0, 4096, 0}, {0, 0, 4096}}};
    ls.t = {100, 200, 3000};
    inv::ScreenFit fit;
    fit.stretched = true;
    fit.display_aspect = 16.0f / 9.0f;
    fit.square = true;
    fit.frame_width = 320;
    fit.frame_height = 224;
    fit.square_percent = 100;
    fit.in_menu = true;
    fit.row_offset = 50;
    fit.hand_offset = -50;
    inv::ScreenMatrix row = ls;
    inv::fit_to_screen(row, 3, fit);
    SH_CHECK(row.m[0][0] == 3072 && row.t[0] == 75);  // 4:3 over 16:9
    SH_CHECK(row.m[1][1] == 3822);                    // 224/240 of the height
    SH_CHECK(row.t[1] == 225 && row.t[2] == 3000);     // 50 * 224 / 448 down
    inv::ScreenMatrix hand = ls;
    inv::fit_to_screen(hand, 7, fit);
    SH_CHECK(hand.t[1] == 175);
    inv::ScreenMatrix pickup = ls;
    inv::fit_to_screen(pickup, 9, fit);
    SH_CHECK(pickup.t[1] == 200);
    fit.in_menu = false;
    fit.stretched = false;
    fit.square = false;
    inv::ScreenMatrix world = ls;
    inv::fit_to_screen(world, 3, fit);
    SH_CHECK(world.m == ls.m && world.t == ls.t);
}

SH_TEST(item_view_carousel_dim) {
    SH_CHECK(inv::carousel_dim(3, -1024, 30) == 256);
    SH_CHECK(inv::carousel_dim(2, -1024 + 1567, 30) == 256 - (256 * 30) / 100);
    SH_CHECK(inv::carousel_dim(0, -1024 + 3784, 50) == 0);
    SH_CHECK(inv::carousel_dim(7, 5000, 30) == 256);
    SH_CHECK(inv::carousel_dim(1, 5000, 0) == 256);
}

SH_TEST(item_view_liquid_fix_only_on_the_stock_layout) {
    std::vector<std::uint8_t> prims(220 * 28, 0);
    for (std::size_t p = 0; p < 220; ++p)
        prims[p * 28 + 1] = 6;
    // Not the stock bottle: nothing changes.
    const std::vector<std::uint8_t> before = prims;
    SH_CHECK(!inv::fix_liquid_uvs(prims, 59, 220));
    SH_CHECK(prims == before);
    SH_CHECK(!inv::fix_liquid_uvs(prims, 58, 220));
}

SH_TEST(item_view_shipped_results_items) {
    auto files = inv::load_item_models(std::filesystem::path(SH_PROJECT_ROOT) / "gamedata", {});
    SH_CHECK(files);
    if (!files)
        return;
    const std::vector<std::string> awards = {"GasolineTank", "Chainsaw", "RockDrill",
                                             "Katana", "ChannelingStone", "HyperBlaster"};
    SH_CHECK(files.value().results_items == awards);
    SH_CHECK(!inv::parse_item_models(R"({"results_items": ["A","B","C","D","E","F","G"]})"));
    SH_CHECK(!inv::parse_item_models(R"({"results_items": [1]})"));
}
