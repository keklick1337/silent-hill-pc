# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 Vladislav Tislenko (keklick1337)
#
# sh_save: saves as files in .savegames/ next to the executable.
#   sh_save    the file format, the store, the memory card image import and the console byte form of the progress
#              record and the settings (console_records.cpp; the records themselves are header-only game types)
#   sh_save_c  C facade (save_c.h) and the game's saves over the session (game_saves.h), journal through sh_log
add_library(sh_save STATIC
    save_file.cpp
    save_game.cpp
    save_store.cpp
    save_version.cpp
    save_catalog.cpp
    boot_check.cpp
    memory_card.cpp
    console_records.cpp)

target_compile_features(sh_save PUBLIC cxx_std_20)
target_include_directories(sh_save PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(sh_save PUBLIC sh_common)
target_compile_options(sh_save PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:-Wall -Wextra -Wpedantic>)

# The C++20 requirement stays private, so the exe keeps its own language settings.
add_library(sh_save_c STATIC save_c.cpp game_saves.cpp)
target_compile_features(sh_save_c PRIVATE cxx_std_20)
target_include_directories(sh_save_c PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(sh_save_c PRIVATE sh_save sh_log sh_ids sh_engine_version)
target_compile_options(sh_save_c PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:-Wall -Wextra -Wpedantic>)
