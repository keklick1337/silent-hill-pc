# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 Vladislav Tislenko (keklick1337)
#
# sh_combat: attacks, hits and damage between characters, their movement step and the combat math.
# Engine types only; the game is connected through src/engine/chara/game/combat_host.cpp.
add_library(sh_combat STATIC
    attack.cpp
    combat_math.cpp
    combat_script.cpp
    movement.cpp
    session_combat_view.cpp)

target_compile_features(sh_combat PRIVATE cxx_std_20)
target_include_directories(sh_combat PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(sh_combat PRIVATE sh_common sh_world)
target_compile_options(sh_combat PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:-Wall -Wextra -Wpedantic>)
