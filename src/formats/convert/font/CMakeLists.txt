# sh_convert_font: the 12x16 game font -- the glyph sheet format, the builder the importer
# runs on 1ST/FONT16.TIM (the disc's part), and the reader the game uses (that part plus the sheets of gamedata/font
# and of the mods).
add_library(sh_convert_font STATIC
    game_font.cpp
    font_converter.cpp)

target_compile_features(sh_convert_font PUBLIC cxx_std_20)
target_include_directories(sh_convert_font PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(sh_convert_font PUBLIC sh_common PRIVATE sh_convert_text sh_convert_tim sh_png)
target_compile_options(sh_convert_font PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:-Wall -Wextra -Wpedantic>)
