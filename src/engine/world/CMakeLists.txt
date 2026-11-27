# sh_world: map collision from the unified map files and the world queries,
# and the world objects of a map (world_objects.json; the running set is src/engine/runtime/world_object_runtime.cpp).
# Engine types only; the game is connected through its collision bridge.
# cxx_std_20 is private so that linking it does not raise the language level of the game executable.
add_library(sh_world STATIC
    area_collision.cpp
    aim_assist.cpp
    chara_collision.cpp
    chara_volume.cpp
    chunk_fx.cpp
    mesh_draw.cpp
    bone_matrix.cpp
    bone_order.cpp
    skeleton.cpp
    lm_materials.cpp
    part_variants.cpp
    held_items.cpp
    held_item.cpp
    object_queue.cpp
    chara_vram.cpp
    map_wall.cpp
    collision_triggers.cpp
    collision_entry.cpp
    ground_query.cpp
    ground_point.cpp
    point_math.cpp
    line_of_sight.cpp
    ray_query.cpp
    move_query.cpp
    mover_setup.cpp
    wall_detect.cpp
    fixed_trig.cpp
    map_env.cpp
    world_env.cpp
    world_atmosphere.cpp
    screen_glow.cpp
    billboard.cpp
    mesh_prep.cpp
    bone_light.cpp
    effects_2d.cpp
    blood_color.cpp
    ../math/fixed_geometry.cpp
    ../math/precise_projection.cpp
    ../math/view_projection.cpp
    ../math/geometry_state.cpp
    ../math/precise_capture.cpp
    world.cpp
    world_objects.cpp
    solid_objects.cpp
    disc_poses.cpp
    runtime_pickups.cpp
    script_origin.cpp
    carried_lights.cpp
    script_event.cpp
    world_script.cpp
    cell_grid.cpp
    cell_stream.cpp
    screen_glow_overlay.cpp)

target_compile_features(sh_world PRIVATE cxx_std_20)
target_include_directories(sh_world PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(sh_world PRIVATE sh_common)
target_compile_options(sh_world PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:-Wall -Wextra -Wpedantic>)
