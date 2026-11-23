# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 Vladislav Tislenko (keklick1337)
#
# sh_console: the debug console as an engine module: tokenizer, command registry with
# typed arguments, completion engine, history, prompt editor, console model, view layout and the font atlas.
# Game-agnostic and SDL/GL-free; the exe-side glue is in src/engine/debug/.
file(GLOB SH_CONSOLE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
add_library(sh_console STATIC ${SH_CONSOLE_SOURCES})

target_compile_features(sh_console PRIVATE cxx_std_20)
target_include_directories(sh_console PUBLIC ${PROJECT_SOURCE_DIR}/src)
# stb_truetype is header-only; font_atlas.cpp compiles its own static copy (STB_TRUETYPE_STATIC).
target_include_directories(sh_console PRIVATE ${PROJECT_SOURCE_DIR}/third_party)
target_link_libraries(sh_console PUBLIC sh_common)
target_compile_options(sh_console PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:-Wall -Wextra -Wpedantic>)
