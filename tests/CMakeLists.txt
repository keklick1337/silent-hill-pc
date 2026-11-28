# One runner (sh_tests): framework/main.cpp plus the *_test.cpp files whose module target exists.
# Added from the root with add_subdirectory(tests), after the module subdirectories.

file(GLOB_RECURSE SH_TEST_GLOB CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*/*_test.cpp")

# tests/<module>/ is built only when sh_<module> exists: its tests need that library's API.
set(SH_TEST_SOURCES "")
foreach(sh_file IN LISTS SH_TEST_GLOB)
    file(RELATIVE_PATH sh_rel "${CMAKE_CURRENT_SOURCE_DIR}" "${sh_file}")
    string(REGEX REPLACE "/.*$" "" sh_module "${sh_rel}")
    if(TARGET sh_${sh_module})
        list(APPEND SH_TEST_SOURCES "${sh_file}")
    else()
        message(STATUS "sh_tests: ${sh_rel} is not built, no sh_${sh_module} target yet")
    endif()
endforeach()

add_executable(sh_tests framework/main.cpp ${SH_TEST_SOURCES})
target_compile_features(sh_tests PRIVATE cxx_std_20)
if(NOT MSVC)
    target_compile_options(sh_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
# Tests include the framework as "framework/sh_test.h", so only the tests/ root is added.
target_include_directories(sh_tests PRIVATE
    "${PROJECT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(sh_tests PRIVATE "SH_PROJECT_ROOT=\"${PROJECT_SOURCE_DIR}\"")

# A module that is not part of the build has no target, so its link is skipped.
foreach(sh_lib IN ITEMS sh_common sh_disc sh_miniz sh_iso9660 sh_archive sh_vfs sh_mods sh_hooks sh_mods_console sh_tables sh_log sh_crash sh_memory
        sh_png sh_gltf sh_import sh_import_ui sh_quick_setup sh_screen_kit sh_import_tool sh_upscale_tool sh_desktop
        sh_convert_tim sh_convert_model sh_convert_audio sh_convert_text sh_i18n sh_convert_font sh_convert_level
        sh_convert_anim sh_convert_cutscene sh_convert_archive
        sh_convert_mapdata sh_convert_filetable sh_convert_gamefiles sh_maps sh_maps_unified sh_assets sh_items sh_script lua_static sh_convert_materials sh_chara sh_chara_actions sh_sound_registry sh_gconfig sh_utf8 sh_input sh_audio sh_entities sh_player sh_player_script sh_world sh_combat sh_cutscene sh_save sh_save_c sh_render sh_frame sh_present sh_flags sh_console sh_text sh_camera sh_inventory sh_screens sh_menu sh_loop sh_devtools sh_gameflow sh_effects sh_offscreen sh_feedback sh_loader sh_gpu sh_fmv)
    if(TARGET ${sh_lib})
        target_link_libraries(sh_tests PRIVATE ${sh_lib})
    endif()
endforeach()

# The game's numbering of named things (src/engine/ids/game_numbers.h): the
# libraries number what they load through it, as in the game (tests/flags/flags_test.cpp calls it, so its
# installer is linked and runs before main()).
if(TARGET sh_ids)
    target_link_libraries(sh_tests PRIVATE sh_ids)
endif()

# The map event binds are compiled into the game (src/engine/scripting); their test builds the module itself.
set(SH_TEST_MAP_EVENT_API "${CMAKE_CURRENT_SOURCE_DIR}/script/map_event_api_test.cpp")
if(SH_TEST_MAP_EVENT_API IN_LIST SH_TEST_SOURCES)
    target_sources(sh_tests PRIVATE "${PROJECT_SOURCE_DIR}/src/engine/scripting/map_event_api.cpp")
endif()

# The attack table is compiled into the game (src/engine/inventory/game); its test builds the file itself.
set(SH_TEST_ATTACK_TABLE "${CMAKE_CURRENT_SOURCE_DIR}/inventory/attack_table_test.cpp")
if(SH_TEST_ATTACK_TABLE IN_LIST SH_TEST_SOURCES)
    target_sources(sh_tests PRIVATE "${PROJECT_SOURCE_DIR}/src/engine/inventory/game/attack_table.cpp")
endif()

# The game's command line is compiled into the game (src/engine/app); its test builds the file itself.
set(SH_TEST_COMMAND_LINE "${CMAKE_CURRENT_SOURCE_DIR}/console/command_line_test.cpp")
if(SH_TEST_COMMAND_LINE IN_LIST SH_TEST_SOURCES)
    target_sources(sh_tests PRIVATE "${PROJECT_SOURCE_DIR}/src/engine/app/command_line.cpp")
endif()

# The text panels are compiled into the game (src/features/hud); their test builds the parts that need no overlay.
set(SH_TEST_TEXT_PANEL "${CMAKE_CURRENT_SOURCE_DIR}/render/text_panel_test.cpp")
if(SH_TEST_TEXT_PANEL IN_LIST SH_TEST_SOURCES)
    target_sources(sh_tests PRIVATE "${PROJECT_SOURCE_DIR}/src/features/hud/text_panel.cpp"
                                    "${PROJECT_SOURCE_DIR}/src/features/hud/frame_stats.cpp"
                                    "${PROJECT_SOURCE_DIR}/src/ui/stb_truetype_impl.cpp")
    target_include_directories(sh_tests PRIVATE "${PROJECT_SOURCE_DIR}/third_party")
endif()

# The retired config keys are compiled into the game (src/engine/config); their test builds the file itself.
set(SH_TEST_RETIRED_KEYS "${CMAKE_CURRENT_SOURCE_DIR}/gconfig/retired_keys_test.cpp")
if(SH_TEST_RETIRED_KEYS IN_LIST SH_TEST_SOURCES)
    target_sources(sh_tests PRIVATE "${PROJECT_SOURCE_DIR}/src/engine/config/retired_keys.cpp")
endif()

# The character hosts are compiled into the game (src/engine/chara/game); their test builds the parts that need no
# game and stubs the link.
set(SH_TEST_CHARA_GAME "${CMAKE_CURRENT_SOURCE_DIR}/chara/chara_game_test.cpp")
if(SH_TEST_CHARA_GAME IN_LIST SH_TEST_SOURCES)
    target_sources(sh_tests PRIVATE "${PROJECT_SOURCE_DIR}/src/engine/chara/game/bone_ops.cpp"
                                    "${PROJECT_SOURCE_DIR}/src/engine/chara/game/map_cast.cpp")
endif()

# The bind table, the console key binds, the prompt table, the pointer arithmetic, the console's selection, the
# camera styles, the phone's rumble, the stick zones and the touch controls' plain parts are compiled into the game
# (src/input); their tests build the parts that need neither SDL nor the game (input/touch_test.cpp and
# input/touch_dpad_test.cpp use the touch sources below).
set(SH_TEST_INPUT_BIND_TABLE "${CMAKE_CURRENT_SOURCE_DIR}/input/bind_table_test.cpp")
if(SH_TEST_INPUT_BIND_TABLE IN_LIST SH_TEST_SOURCES)
    target_sources(sh_tests PRIVATE "${PROJECT_SOURCE_DIR}/src/input/bind/bind_table.cpp"
                                    "${PROJECT_SOURCE_DIR}/src/input/bind/bind_capture.cpp"
                                    "${PROJECT_SOURCE_DIR}/src/input/bind/pad_chord.cpp"
                                    "${PROJECT_SOURCE_DIR}/src/input/bind/console_binds.cpp"
                                    "${PROJECT_SOURCE_DIR}/src/input/glyphs/glyph_table.cpp"
                                    "${PROJECT_SOURCE_DIR}/src/input/pointer_math.cpp"
                                    "${PROJECT_SOURCE_DIR}/src/input/phone_rumble.cpp"
                                    "${PROJECT_SOURCE_DIR}/src/input/stick_shape.cpp"
                                    "${PROJECT_SOURCE_DIR}/src/input/inventory_pointer.cpp"
                                    "${PROJECT_SOURCE_DIR}/src/input/ring_carousel.cpp"
                                    "${PROJECT_SOURCE_DIR}/src/input/object_pick.cpp"
                                    "${PROJECT_SOURCE_DIR}/src/input/camera_style.cpp"
                                    "${PROJECT_SOURCE_DIR}/src/input/touch/touch_layout.cpp"
                                    "${PROJECT_SOURCE_DIR}/src/input/touch/touch_gestures.cpp"
                                    "${PROJECT_SOURCE_DIR}/src/input/touch/action_kinds.cpp"
                                    "${PROJECT_SOURCE_DIR}/src/input/touch/touch_icons.cpp"
                                    "${PROJECT_SOURCE_DIR}/src/input/touch/touch_sprites.cpp")
endif()

# The debug layer is compiled into the game (src/engine/debug); its test builds the parts that need no game.
set(SH_TEST_DEBUG_PARTS "${CMAKE_CURRENT_SOURCE_DIR}/console/debug_parts_test.cpp")
if(SH_TEST_DEBUG_PARTS IN_LIST SH_TEST_SOURCES)
    target_sources(sh_tests PRIVATE "${PROJECT_SOURCE_DIR}/src/engine/debug/overlay/toast_queue.cpp"
                                    "${PROJECT_SOURCE_DIR}/src/engine/debug/overlay/bitmap_text.cpp"
                                    "${PROJECT_SOURCE_DIR}/src/engine/debug/overlay/pointer_sprite.cpp"
                                    "${PROJECT_SOURCE_DIR}/src/engine/debug/commands/scripted_run.cpp"
                                    "${PROJECT_SOURCE_DIR}/src/engine/debug/commands/remembered_flags.cpp"
                                    "${PROJECT_SOURCE_DIR}/src/engine/debug/commands/frame_image.cpp"
                                    "${PROJECT_SOURCE_DIR}/src/engine/debug/commands/command_kit.cpp")
endif()

# The scene of a frame seen from another camera is compiled into the game (src/engine/app/loop); its test builds the
# file itself.
set(SH_TEST_SCENE_AIM "${CMAKE_CURRENT_SOURCE_DIR}/render/scene_aim_test.cpp")
if(SH_TEST_SCENE_AIM IN_LIST SH_TEST_SOURCES)
    target_sources(sh_tests PRIVATE "${PROJECT_SOURCE_DIR}/src/engine/app/loop/scene_aim.cpp")
endif()

# The bullet hole sheet is compiled into the game (src/render/decals); its test builds the file itself.
set(SH_TEST_BULLET_HOLE "${CMAKE_CURRENT_SOURCE_DIR}/render/bullet_hole_test.cpp")
if(SH_TEST_BULLET_HOLE IN_LIST SH_TEST_SOURCES)
    target_sources(sh_tests PRIVATE "${PROJECT_SOURCE_DIR}/src/render/decals/bullet_hole.cpp")
endif()

# The replacement pictures are compiled into the game (src/render/textures); their test builds the store itself.
set(SH_TEST_REPLACEMENT_STORE "${CMAKE_CURRENT_SOURCE_DIR}/render/replacement_store_test.cpp")
if(SH_TEST_REPLACEMENT_STORE IN_LIST SH_TEST_SOURCES)
    target_sources(sh_tests PRIVATE "${PROJECT_SOURCE_DIR}/src/render/textures/replacement_store.cpp")
endif()

# The readers of a mod's picture files are compiled into the game (src/formats/texture); their test builds them
# itself. bc7_dds.cpp includes the BC7 decoder as "bcdec/include/bcdec.h".
set(SH_TEST_PICTURE "${CMAKE_CURRENT_SOURCE_DIR}/png/picture_test.cpp")
if(SH_TEST_PICTURE IN_LIST SH_TEST_SOURCES)
    target_sources(sh_tests PRIVATE "${PROJECT_SOURCE_DIR}/src/formats/texture/bc7_dds.cpp"
                                    "${PROJECT_SOURCE_DIR}/src/formats/texture/picture.cpp")
    set_property(SOURCE "${PROJECT_SOURCE_DIR}/src/formats/texture/bc7_dds.cpp" APPEND PROPERTY
                 INCLUDE_DIRECTORIES "${PROJECT_SOURCE_DIR}/third_party")
endif()

# The check of a mod's model file and the registry of oversized ones are compiled into the game (src/formats/game,
# src/render/geometry); their tests build the files themselves (the registry's game host stays out).
set(SH_TEST_MODEL_FILE_CHECK "${CMAKE_CURRENT_SOURCE_DIR}/render/model_file_check_test.cpp")
set(SH_TEST_OVERSIZED_MODELS "${CMAKE_CURRENT_SOURCE_DIR}/render/oversized_models_test.cpp")
if(SH_TEST_MODEL_FILE_CHECK IN_LIST SH_TEST_SOURCES OR SH_TEST_OVERSIZED_MODELS IN_LIST SH_TEST_SOURCES)
    target_sources(sh_tests PRIVATE "${PROJECT_SOURCE_DIR}/src/formats/game/model_file_check.cpp")
endif()
if(SH_TEST_OVERSIZED_MODELS IN_LIST SH_TEST_SOURCES)
    target_sources(sh_tests PRIVATE "${PROJECT_SOURCE_DIR}/src/render/geometry/oversized_models.cpp")
endif()

# sh_tests imports SDL3.dll: on Windows it must sit beside the exe (tests/ is
# not the game's directory, where the root CMakeLists.txt puts it).
if(WIN32 AND SH_WIN_DEPS_PREFIX AND EXISTS "${SH_WIN_DEPS_PREFIX}/bin/SDL3.dll")
    add_custom_command(TARGET sh_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${SH_WIN_DEPS_PREFIX}/bin/SDL3.dll" "$<TARGET_FILE_DIR:sh_tests>"
        COMMENT "Copying SDL3.dll beside sh_tests")
endif()

enable_testing()
add_test(NAME sh_tests COMMAND sh_tests)

# Replay harness: replay_tools and replay_check.
add_subdirectory(replay)
