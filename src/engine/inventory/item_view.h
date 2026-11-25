// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The item view: the 3D models of the item screens (the inventory carousel, the item in hand, the
// results screen's awards) and of the world pickup.
// ItemView says which inventory slot each model shows; the functions below are the decisions of
// the view: which slots open around the selection, where an item sits in the map's item pack,
// and how a model's screen matrix is fitted to the display. The item stage (game/item_stage.h) draws
// them. Standard library.

#pragma once

#include <array>
#include <cstdint>
#include <span>

#include "engine/inventory/inventory.h"
#include "engine/inventory/item_carousel.h"
#include "engine/inventory/item_description.h"
#include "engine/inventory/screen_input.h"

namespace sh::inventory {

inline constexpr int kViewModels = 10;      // 0..6 the item row, 7 the item in hand, 9 the pickup
inline constexpr int kPickupModel = 9;
inline constexpr std::int32_t kNoModelSlot = -1;
inline constexpr std::int32_t kNoViewItem = 0;  // "nothing": the game's unequipped item number
inline constexpr std::size_t kPackSearch = 40;  // entries of the pack list the game compares

struct ItemView {
    // The inventory slot each model shows (kNoModelSlot: none). The results screen puts the award
    // number there instead.
    std::array<std::int32_t, kViewModels> shown{kNoModelSlot, kNoModelSlot, kNoModelSlot, kNoModelSlot,
                                                kNoModelSlot, kNoModelSlot, kNoModelSlot, kNoModelSlot,
                                                kNoModelSlot, kNoModelSlot};
    // Models linked since an item screen opened, up to kViewModels. Nothing reads it (kept as the game
    // counts it).
    std::int32_t linked = 0;
    // The item the pickup view shows; kNoViewItem outside a pickup.
    std::int32_t pickup_item = kNoViewItem;

    std::int32_t& equipped() { return shown[7]; }
    std::int32_t equipped() const { return shown[7]; }

    // A model was linked: counts up to kViewModels.
    void count_link();
};

// The view of the running game (the item screens and the pickup share it).
ItemView& item_view();

// What the item screens keep between frames: the carousel (its timer, centred slot, examine glare,
// last reload's ammo slot), the item text (typing and rolling timers, item shown last) and the
// directions read this frame.
struct ItemScreenState {
    CarouselState carousel{0, 0, 0, 0, false, 0};  // the reload's ammo slot starts at 0, as the game's does
    DescriptionState description;
    Directions directions;
};

ItemScreenState& item_screen();

// The inventory menu between frames: the command and the row or box selected, the outline that follows the
// selection, the item in hand. Zero when the game starts.
struct InventoryMenuState {
    std::int32_t command = 0;           // entry of the selected item's command list
    std::uint32_t selection = 0;        // the selected row or box (the game's selection ids)
    std::int32_t scrolled = 0;          // a scroll started this frame (the description clears it)
    std::int32_t equipped_item = 0;     // item number in hand
    std::uint32_t scrolling = 0;        // 1: a held left or right keeps the carousel turning (a 4-byte flag)
    std::int32_t difficulty = 0;        // the game's difficulty as the screen shows it
    std::int32_t outline_age = 0;       // frames since the outline started moving (drawn whole at 8)
    std::int32_t outline_target = 0;    // the selection the outline moves to (a frame behind `selection`)
};

namespace detail {
// Static storage with a constant address, so the C names can be bound to it before any code runs.
inline InventoryMenuState g_inventory_menu;
}  // namespace detail

inline InventoryMenuState& inventory_menu() { return detail::g_inventory_menu; }

// The inventory opens: every model shows nothing, then the seven row models show the slots from
// three before `selected` to three after it, round the ring of `slot_count` slots.
void open_carousel(ItemView& view, std::int32_t selected, std::int32_t slot_count);

// The first of the first `slot_count` slots holding `item`, kNoModelSlot when none does.
std::int32_t slot_of_item(const Slots& slots, std::int32_t slot_count, int item);

// Where `item` is in the map's item pack list: the first of its first kPackSearch entries equal to it,
// -1 when none is (entries past the list's end are compared too, as the game does).
std::int32_t pack_index(std::span<const std::int32_t> pack, int item);

// --- Fitting a model's screen matrix to the display (the engine's corrections of the item draw).

struct ScreenMatrix {
    std::array<std::array<std::int16_t, 3>, 3> m{};
    std::array<std::int32_t, 3> t{};
};

struct ScreenFit {
    bool stretched = false;        // a 4:3 picture is stretched to a wider display
    float display_aspect = 0.0f;   // width / height of the display
    bool square = false;           // undo the frame's 224 -> 240 line stretch (invaspect)
    std::int32_t frame_width = 0;  // the game's frame
    std::int32_t frame_height = 0;
    std::int32_t square_percent = 100;  // invscale
    bool in_menu = false;               // the item screens (not the world: the map5_s01 lock reels)
    std::int32_t row_offset = 0;        // invcary: the item row a touch down
    std::int32_t hand_offset = 0;       // inveqy: the item in hand a touch up
    float row_spread = 1.0f;            // the item row spreads sideways on a wide window (wide_layout.h)
};

inline constexpr std::int32_t kOffsetFrameHeight = 448;  // frame height the offsets were tuned at

// Fits the local-to-screen matrix of model `display`: narrows a stretched picture, makes the
// model's height true in square mode, nudges the row and the item in hand, spreads the row on a wide window (its
// models move apart, none grows).
void fit_to_screen(ScreenMatrix& ls, int display, const ScreenFit& fit);

// The brightness (256: full) of model `display` at ring depth `z`: each slot off the centre darkens
// by `strength` percent, the item in hand and the other models stay full.
std::int32_t carousel_dim(int display, std::int32_t z, std::int32_t strength);

// --- The close-up model of the Unknown Liquid (items/<Name>.json "close_up_fix": "liquid_uvs").

// Corrects the stock bottle: four liquid faces sample outside the red patch and sixteen overlapping
// liquid faces blend like the shell. `prims` is the model's primitive data (28 bytes each). Only a
// model whose layout matches the stock one byte for byte (texture page and palette aside) changes;
// returns whether it did.
bool fix_liquid_uvs(std::span<std::uint8_t> prims, std::uint32_t vertex_count, std::uint32_t prim_count);

}  // namespace sh::inventory
