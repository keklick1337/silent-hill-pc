# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 Vladislav Tislenko (keklick1337)
#
# sh_player: the player of the engine - Harry's resets, set-up, queries, weapon, tick and pose.
# The game's records are reached only through PlayerRecord.
add_library(sh_player STATIC
    player.cpp
    player_behaviour.cpp
    player_math.cpp
    player_state.cpp
    player_tick.cpp
    player_vars.cpp)

target_compile_features(sh_player PUBLIC cxx_std_20)
target_include_directories(sh_player PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(sh_player PUBLIC sh_world)
target_compile_options(sh_player PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:-Wall -Wextra -Wpedantic>)

# sh_player_script: Harry's Lua entry points and the sh.entity.harry* / sh.input.* binds over the player
# (player_script.h). Built with the Lua script host.
if(TARGET sh_script)
    add_library(sh_player_script STATIC player_script.cpp)
    target_compile_features(sh_player_script PUBLIC cxx_std_20)
    target_include_directories(sh_player_script PUBLIC ${PROJECT_SOURCE_DIR}/src)
    target_link_libraries(sh_player_script PUBLIC sh_player PRIVATE sh_script sh_input sh_log)
    target_compile_options(sh_player_script PRIVATE
        $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:-Wall -Wextra -Wpedantic>)
endif()
