# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 Vladislav Tislenko (keklick1337)
#
# sh_ids: the numbers of the original game's things by name, read from gamedata/.
add_library(sh_ids STATIC
    game_numbers.cpp)

target_compile_features(sh_ids PRIVATE cxx_std_20)
target_include_directories(sh_ids PUBLIC ${PROJECT_SOURCE_DIR}/src)
# Tools and tests run from anywhere: the source tree's gamedata is the last place to look.
target_compile_definitions(sh_ids PRIVATE SH_IDS_SOURCE_GAMEDATA="${PROJECT_SOURCE_DIR}/gamedata")
target_link_libraries(sh_ids PRIVATE sh_common sh_log sh_flags sh_mods sh_items sh_chara sh_sound_registry
    sh_convert_mapdata)
target_compile_options(sh_ids PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:-Wall -Wextra -Wpedantic>)
