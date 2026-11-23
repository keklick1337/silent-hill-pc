# sh_convert_mapdata: data of the map overlays as JSON in gamedata/maps/.
# map_layout.inc is a generated table: where each overlay keeps its map points and events.
add_library(sh_convert_mapdata STATIC
    mapdata_binary.cpp
    mapdata_json.cpp
    mapdata_files.cpp)

target_compile_features(sh_convert_mapdata PUBLIC cxx_std_20)
target_include_directories(sh_convert_mapdata PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(sh_convert_mapdata PUBLIC sh_common)
target_compile_options(sh_convert_mapdata PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:-Wall -Wextra -Wpedantic>)
