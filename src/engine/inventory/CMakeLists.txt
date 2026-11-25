# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 Vladislav Tislenko (keklick1337)
#
# sh_inventory: the player's inventory slots and the inventory screen:
# tidy-up (sort, merge, pack), taking units, slot commands, pickup tallies, reload arithmetic; the
# screen's menu, flow, item carousel and drawing records. Item behaviour comes in through
# ItemRules and host interfaces implemented by the game's inventory bridges. Standard
# library, sh_world's fixed-point trigonometry and the shared JSON/mod-discovery helpers
# (gamedata/menu/results_info.json).
add_library(sh_inventory STATIC
    inventory.cpp
    inventory_menu.cpp
    item_screen_flow.cpp
    inventory_draw.cpp
    screen_data.cpp
    inventory_background.cpp
    item_carousel.cpp
    health_status.cpp
    item_description.cpp
    item_actions.cpp
    results_save.cpp
    results_info.cpp
    pickup_anim.cpp
    item_models.cpp
    exit_anim.cpp
    screen_input.cpp
    item_view.cpp
    item_catalog.cpp
    item_screen_text.cpp
    wide_layout.cpp)

target_compile_features(sh_inventory PRIVATE cxx_std_20)
target_include_directories(sh_inventory PUBLIC ${PROJECT_SOURCE_DIR}/src)
# The game's fixed-point sine and cosine (one formula in the engine: sh_world's fixed_trig).
target_link_libraries(sh_inventory PRIVATE sh_world sh_common sh_mods sh_log sh_text sh_screens)
# The item catalog answers from the item library (engine/items).
target_link_libraries(sh_inventory PUBLIC sh_items)
target_compile_options(sh_inventory PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:-Wall -Wextra -Wpedantic>)
