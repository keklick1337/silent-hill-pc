# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 Vladislav Tislenko (keklick1337)
#
# sh_text: cell-font text layout (the 12x16 atlas strings, the map messages with their
# markup, the 8x8 debug font and the number formatting) and the game's text drawer
# (game_text: the strings' state, glyph records turned into draw2d commands) with its style
# (text_style: gamedata/menu/text_style.json).
add_library(sh_text STATIC
    debug_font.cpp
    game_text.cpp
    glyph_emit.cpp
    message_box.cpp
    message_layout.cpp
    number_format.cpp
    string_layout.cpp
    text_style.cpp)

target_compile_features(sh_text PRIVATE cxx_std_20)
target_include_directories(sh_text PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(sh_text PUBLIC sh_draw2d)
target_link_libraries(sh_text PRIVATE sh_common sh_mods)
target_compile_options(sh_text PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:-Wall -Wextra -Wpedantic>)
