# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 Vladislav Tislenko (keklick1337)
#
# sh_convert_materials: the texture bake (convert/texture_bake.json, baked convert/textures/) and
# convert/materials.json — which texture PNGs each model/level uses.
# draw_usage.json (CLUT rows of pictures drawn without geometry) is compiled in as a byte array.
set(SH_DRAW_USAGE_JSON ${CMAKE_CURRENT_SOURCE_DIR}/draw_usage.json)
set(SH_DRAW_USAGE_CPP ${CMAKE_CURRENT_BINARY_DIR}/draw_usage_data.cpp)
add_custom_command(
    OUTPUT ${SH_DRAW_USAGE_CPP}
    COMMAND ${CMAKE_COMMAND} -DIN=${SH_DRAW_USAGE_JSON} -DOUT=${SH_DRAW_USAGE_CPP}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/embed_usage.cmake
    DEPENDS ${SH_DRAW_USAGE_JSON} ${CMAKE_CURRENT_SOURCE_DIR}/embed_usage.cmake
    VERBATIM)

add_library(sh_convert_materials STATIC
    materials_manifest.cpp
    surface_inference.cpp
    texture_bake.cpp
    ${SH_DRAW_USAGE_CPP})

target_compile_features(sh_convert_materials PRIVATE cxx_std_20)
target_include_directories(sh_convert_materials PUBLIC ${PROJECT_SOURCE_DIR}/src)
# texture_bake.cpp reads the consumer glbs with cgltf; its symbols come from sh_gltf (cgltf_prefixed.h).
target_include_directories(sh_convert_materials PRIVATE ${PROJECT_SOURCE_DIR}/third_party/cgltf/include)
target_link_libraries(sh_convert_materials PUBLIC sh_gltf PRIVATE sh_png sh_common)
target_compile_options(sh_convert_materials PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:-Wall -Wextra -Wpedantic>)
