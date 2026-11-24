# What the screens shown before the game (the quick setup, the disc import) draw with: text from the TrueType fonts
# of gamedata/font and the system's, the menus' widgets on an SDL renderer, and the layout arithmetic beside them.
add_library(sh_screen_kit STATIC text_raster.cpp text_layout.cpp line_edit.cpp painter.cpp script.cpp)

target_compile_features(sh_screen_kit PUBLIC cxx_std_20)
target_include_directories(sh_screen_kit PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(sh_screen_kit PUBLIC sh_i18n PRIVATE sh_common ${SH_SDL3_TARGET})
target_compile_options(sh_screen_kit PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:-Wall -Wextra -Wpedantic>)
