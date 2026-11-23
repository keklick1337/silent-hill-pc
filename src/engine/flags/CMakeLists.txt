# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 Vladislav Tislenko (keklick1337)
#
# sh_flags: event flags by name, gamedata/flags/*.json and the mods' flags.json / flags/*.json.
add_library(sh_flags STATIC
    flag_registry.cpp)

target_compile_features(sh_flags PRIVATE cxx_std_20)
target_include_directories(sh_flags PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(sh_flags PRIVATE sh_common sh_mods)
target_compile_options(sh_flags PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:-Wall -Wextra -Wpedantic>)
