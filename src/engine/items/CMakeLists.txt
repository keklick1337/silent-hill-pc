# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 Vladislav Tislenko (keklick1337)
#
# sh_items: the inventory items and weapons of the game as data, gamedata/items/.
add_library(sh_items STATIC
    item_library.cpp
    quick_heal.cpp
    powered_weapon.cpp
    weapon_cycle.cpp
    quick_actions.cpp)

target_compile_features(sh_items PRIVATE cxx_std_20)
target_include_directories(sh_items PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(sh_items PRIVATE sh_common sh_mods)
target_compile_options(sh_items PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:-Wall -Wextra -Wpedantic>)
