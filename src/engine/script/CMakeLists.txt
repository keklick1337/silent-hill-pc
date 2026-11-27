# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 Vladislav Tislenko (keklick1337)
#
# sh_script: the game's Lua script host (gamedata/scripts/game.lua).
# Only built when the vendored Lua is there (lua_static, from the root CMakeLists).
if(NOT TARGET lua_static)
    message(STATUS "sh_script: no vendored Lua, the script host is not built")
    return()
endif()

add_library(sh_script STATIC
    api_registry.cpp
    bind_config.cpp
    bind_console.cpp
    bind_engine.cpp
    bind_flags.cpp
    bind_hooks.cpp
    bind_lights.cpp
    bind_menu.cpp
    bind_mods.cpp
    bind_player.cpp
    bind_sound.cpp
    bind_timers.cpp
    bind_world.cpp
    bind_world_pickups.cpp
    caller_mod.cpp
    game_binders.cpp
    mod_settings.cpp
    named_binds.cpp
    named_api.cpp
    script_host.cpp
    sh_script.cpp)

target_compile_features(sh_script PRIVATE cxx_std_20)
target_include_directories(sh_script PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(sh_script PUBLIC lua_static sh_hooks PRIVATE sh_common sh_menu sh_mods sh_engine_version sh_log sh_sound_registry sh_console sh_world sh_gameflow)
target_compile_options(sh_script PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:-Wall -Wextra -Wpedantic>)
