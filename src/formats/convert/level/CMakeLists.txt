add_library(sh_convert_level STATIC
    level_parse.cpp
    level_json.cpp
    level_flat.cpp
    level_textures.cpp
    level_gltf.cpp
    map_areas.cpp
    level_converter.cpp
    map_unified.cpp
    map_unified_files.cpp)

target_compile_features(sh_convert_level PUBLIC cxx_std_20)
target_include_directories(sh_convert_level PUBLIC ${PROJECT_SOURCE_DIR}/src)
# sh_convert_mapdata: the map scenes of the unified map file (map_unified.h).
target_link_libraries(sh_convert_level PUBLIC sh_common sh_gltf sh_convert_mapdata)
target_compile_options(sh_convert_level PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:-Wall -Wextra -Wpedantic>)
