# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 Vladislav Tislenko (keklick1337)
#
# sh_i18n: what the game's text needs before it is drawn, without the game: the languages
# found in the text directories, Unicode properties, bidirectional ordering, Arabic shaping, line breaking and the
# preparation of the drawer's strings, and the fonts a character missing from the game font is drawn with (the
# catalogue of mod, game and system fonts, and the cell drawing of their glyphs). The exe side is src/i18n/*.cpp.
add_library(sh_i18n STATIC
    arabic.cpp
    bidi.cpp
    bitmap_font.cpp
    font_catalog.cpp
    font_file.cpp
    language_set.cpp
    outline_font.cpp
    text_prepare.cpp
    unicode.cpp)

target_compile_features(sh_i18n PUBLIC cxx_std_20)
target_include_directories(sh_i18n PUBLIC ${PROJECT_SOURCE_DIR}/src)
# stb_truetype is header-only; outline_font.cpp compiles its own static copy.
target_include_directories(sh_i18n PRIVATE ${PROJECT_SOURCE_DIR}/third_party)
target_link_libraries(sh_i18n PUBLIC sh_common PRIVATE sh_convert_text)
target_compile_options(sh_i18n PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:-Wall -Wextra -Wpedantic>)
