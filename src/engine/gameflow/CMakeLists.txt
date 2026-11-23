# sh_gameflow: the game flow of the engine: the map event triggers
# that pick the event the game state machine runs next, the in-game state machine and the music
# layer mix. The game is connected through the game's event bridges; sh_world supplies the
# bit-exact fixed-point trigonometry.
add_library(sh_gameflow STATIC
    ../game_state/session.cpp
    event_triggers.cpp
    music_mix.cpp
    arrival.cpp
    event_tweens.cpp
    flow_data.cpp
    game_states.cpp
    game_over.cpp
    area_load.cpp
    event_steps.cpp
    title_fog.cpp
    title_menu.cpp
    menu_states.cpp
    event_commands.cpp
    event_composites.cpp
    composite_data.cpp
    status_screen.cpp
    pad_vibration.cpp
    npc_spawn.cpp
    anim_slots.cpp
    map_start.cpp
    map_load.cpp
    warm_boot.cpp
    scene_owner.cpp
    boot_machine.cpp
    frame_gate.cpp
    packet_arena.cpp
    freeze_latch.cpp
    frame_cheats.cpp
    debug_camera.cpp
    clear_color.cpp
    load_screen_pose.cpp
    load_boot.cpp
    load_screen_run.cpp
    collision_zones.cpp
    world_frame.cpp
    main_frame.cpp
    state_names.cpp
    soft_reset.cpp
    warp_story.cpp
    demo_player.cpp)

target_compile_features(sh_gameflow PRIVATE cxx_std_20)
target_include_directories(sh_gameflow PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(sh_gameflow PRIVATE sh_world sh_mods sh_common)
target_compile_options(sh_gameflow PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:-Wall -Wextra -Wpedantic>)
