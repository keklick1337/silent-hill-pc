# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 Vladislav Tislenko (keklick1337)
#
# sh_chara: the characters of the game as data, gamedata/chara/.
add_library(sh_chara STATIC
    behavior_state.cpp
    chara_library.cpp
    converted_sources.cpp
    footsteps.cpp
    harry_reactions.cpp)

target_compile_features(sh_chara PRIVATE cxx_std_20)
target_include_directories(sh_chara PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(sh_chara PUBLIC sh_sound_registry PRIVATE sh_common sh_mods)
target_compile_options(sh_chara PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:-Wall -Wextra -Wpedantic>)

# sh_chara_actions: the Lua action hooks of a character (chara/<Name>/actions.lua). Apart from sh_chara so the data
# library does not pull the script host into tools that only read characters.
add_library(sh_chara_actions STATIC chara_actions.cpp chara_script.cpp behavior_host.cpp)
target_compile_features(sh_chara_actions PRIVATE cxx_std_20)
target_include_directories(sh_chara_actions PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(sh_chara_actions PUBLIC sh_chara sh_script)
target_compile_options(sh_chara_actions PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:-Wall -Wextra -Wpedantic>)
