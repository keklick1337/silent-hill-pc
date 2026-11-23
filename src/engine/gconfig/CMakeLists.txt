# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 Vladislav Tislenko (keklick1337)
#
# sh_gconfig: gameplay parameters as data, gamedata/config/.
add_library(sh_gconfig STATIC
    gameplay_params.cpp
    difficulties.cpp
    map_info.cpp
    controller_layouts.cpp
    sound_tables.cpp)

target_compile_features(sh_gconfig PRIVATE cxx_std_20)
target_include_directories(sh_gconfig PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(sh_gconfig PRIVATE sh_common sh_mods)
target_compile_options(sh_gconfig PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:-Wall -Wextra -Wpedantic>)
