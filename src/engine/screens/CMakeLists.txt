# SPDX-License-Identifier: GPL-3.0-or-later
#
# sh_screens: the game's full-screen UI written on the engine: the staff roll's
# timing, layout and screen steps and its 24-pixel font; the menus' selection widgets;
# the options screen (main and extra lists, brightness panel, controller entry) and the
# settings model behind the Options menu's rows; the boot logo screens and the movie game
# states.
# The module computes WHAT to draw and reports draw records through a sink; the game
# screen bridges turn the records into primitives. Data: gamedata/menu/
# staff_roll.json, menu_style.json, brightness.json, options.json, settings.json,
# boot_logos.json, movies.json, controls_screen.json, quick_options.json, map_screen.json,
# item_screens.json.
add_library(sh_screens STATIC
    roll_text.cpp
    staff_roll.cpp
    staff_roll_data.cpp
    staff_roll_screen.cpp
    staff_roll_clock.cpp
    menu_widgets.cpp
    brightness_panel.cpp
    save_layout.cpp
    warning_layout.cpp
    screen_data.cpp
    controls_layout.cpp
    controls_names.cpp
    map_screen_data.cpp
    item_screen_style.cpp
    controls_screen.cpp
    options_menu.cpp
    settings_model.cpp
    boot_logos.cpp
    movie_states.cpp
    settings_presets.cpp
    screen_fade.cpp
    cursor_shapes.cpp
    ranking.cpp
    background.cpp
    paper_map.cpp
    health_cues.cpp)

target_compile_features(sh_screens PRIVATE cxx_std_20)
target_include_directories(sh_screens PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(sh_screens PRIVATE sh_common sh_mods)
target_link_libraries(sh_screens PUBLIC sh_draw2d)
target_compile_options(sh_screens PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:-Wall -Wextra -Wpedantic>)
