# sh_maps: the game reads the data of the maps from gamedata/maps/ through the
# C face sh_maps.h.
# cxx_std_20 is private so that linking it does not raise the language level of the game executable.
add_library(sh_maps STATIC
    map_description.cpp
    map_globals.cpp
    map_rules.cpp
    map_runtime.cpp
    map_tables.cpp
    map_table_data.cpp
    map_store.cpp
    sh_maps.cpp
    spawn_overrides.cpp)

target_compile_features(sh_maps PRIVATE cxx_std_20)
target_include_directories(sh_maps PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(sh_maps PRIVATE sh_convert_mapdata sh_common sh_mods sh_log)
target_compile_options(sh_maps PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:-Wall -Wextra -Wpedantic>)

# sh_maps_unified: the VFS layer that serves BG/*.IPD and BG/*.PLM from gamedata/maps/areas/*.glb when
# map_format = unified (unified_layer.h). Linked by sh_vfs_c.
add_library(sh_maps_unified STATIC unified_layer.cpp)
target_compile_features(sh_maps_unified PUBLIC cxx_std_20)
target_include_directories(sh_maps_unified PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(sh_maps_unified PUBLIC sh_vfs sh_convert_level)
target_compile_options(sh_maps_unified PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:-Wall -Wextra -Wpedantic>)
