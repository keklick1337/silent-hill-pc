# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 Vladislav Tislenko (keklick1337)
#
# sh_cutscene: cutscenes as data, convert/cutscenes/**/*.dms.json.
#   sh_cutscene    the model, sampling and the slot director (no file access)
#   sh_cutscene_c  C facade for the game over the VFS (cutscene_c.h)
add_library(sh_cutscene STATIC
    cutscene.cpp
    cutscene_json.cpp
    cutscene_director.cpp)

target_compile_features(sh_cutscene PUBLIC cxx_std_20)
target_include_directories(sh_cutscene PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(sh_cutscene PUBLIC sh_common)
target_compile_options(sh_cutscene PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:-Wall -Wextra -Wpedantic>)

# The C++20 requirement stays private, so the exe keeps its own language settings.
add_library(sh_cutscene_c STATIC cutscene_c.cpp)
target_compile_features(sh_cutscene_c PRIVATE cxx_std_20)
target_include_directories(sh_cutscene_c PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(sh_cutscene_c PRIVATE sh_cutscene sh_convert_cutscene sh_vfs_c)
target_compile_options(sh_cutscene_c PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:-Wall -Wextra -Wpedantic>)
