# sh_scene: the engine's scene renderer (docs/RENDER.md section 6): map cells from maps/areas/*.glb and
# posed meshes through the RHI. Engine types only; the game describes the frame through scene_c.h, the GPU renderer
# (render/gpu) draws it at the scene point of the frame.

set(_scene_gen "${CMAKE_CURRENT_BINARY_DIR}/gen")
file(MAKE_DIRECTORY "${_scene_gen}")
set(_scene_shader_sources "")
# Model upscaling's later stages include scene.vert as a library (SH_SCENE_LIB): the same text without its #version
# line, which an included file may not have.
set(_scene_vert "${CMAKE_CURRENT_SOURCE_DIR}/shaders/scene.vert")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_scene_vert}")
file(READ "${_scene_vert}" _scene_lib)
string(REGEX REPLACE "^#version[^\n]*\n" "" _scene_lib "${_scene_lib}")
file(CONFIGURE OUTPUT "${_scene_gen}/scene_lib.glsl" CONTENT "${_scene_lib}" @ONLY)
set(_scene_includes "${_scene_gen}/scene_lib.glsl" "${CMAKE_CURRENT_SOURCE_DIR}/shaders/scene_pn.glsl"
                    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/scene_pn_data.glsl")
# file:symbol:variant. A variant is the same source compiled with one more define (the GL backend adds it to the
# text itself, scene_renderer.cpp); "-" is the plain shader. The scene_pn stages exist on the native Vulkan backend
# only (RENDER.md section 7.11); task and mesh shaders need SPIR-V 1.4, hence Vulkan 1.2.
foreach(_pair IN ITEMS "scene.vert:sceneVert:-" "scene.frag:sceneFrag:-"
                       "scene.frag:sceneFragExactDepth:SH_SCENE_EXACT_DEPTH" "scene.vert:sceneVertPn:SH_SCENE_PN"
                       "scene_pn.tesc:scenePnTesc:-" "scene_pn.tese:scenePnTese:-" "scene_pn.task:scenePnTask:-"
                       "scene_pn.mesh:scenePnMesh:-")
    string(REPLACE ":" ";" _pair "${_pair}")
    list(GET _pair 0 _file)
    list(GET _pair 1 _symbol)
    list(GET _pair 2 _variant)
    set(_src "${CMAKE_CURRENT_SOURCE_DIR}/shaders/${_file}")
    set(_spv "${_scene_gen}/${_symbol}.spv")
    set(_cpp "${_scene_gen}/${_symbol}.cpp")
    set(_defines -DSH_RHI_VULKAN -DSH_RHI_CLIP_Z01 "-I${CMAKE_CURRENT_SOURCE_DIR}/shaders" "-I${_scene_gen}")
    if(NOT _variant STREQUAL "-")
        list(APPEND _defines -D${_variant})
    endif()
    set(_env vulkan1.0)
    if(_file MATCHES "\\.(task|mesh)$")
        set(_env vulkan1.2)
    endif()
    if(SH_GLSLC)
        set(_compile "${SH_GLSLC}" --target-env=${_env} ${_defines} "${_src}" -o "${_spv}")
    elseif(SH_GLSLANG)
        set(_compile "${SH_GLSLANG}" -V --target-env ${_env} ${_defines} "${_src}" -o "${_spv}")
    else()
        set(_compile "${CMAKE_COMMAND}" -E touch "${_spv}")
    endif()
    add_custom_command(OUTPUT "${_spv}" COMMAND ${_compile} DEPENDS "${_src}" ${_scene_includes}
        COMMENT "SPIR-V ${_symbol}" VERBATIM)
    add_custom_command(OUTPUT "${_cpp}"
        COMMAND "${CMAKE_COMMAND}" -DGLSL=${_src} -DSPIRV=${_spv} -DSYMBOL=${_symbol} -DOUT=${_cpp}
                -P "${PROJECT_SOURCE_DIR}/cmake/rhi_embed_shader.cmake"
        DEPENDS "${_src}" "${_spv}" "${PROJECT_SOURCE_DIR}/cmake/rhi_embed_shader.cmake"
        COMMENT "Embed ${_symbol}" VERBATIM)
    list(APPEND _scene_shader_sources "${_cpp}")
endforeach()

add_library(sh_scene STATIC area_cells.cpp frame_blend.cpp item_model.cpp materials.cpp model_upscale.cpp preview.cpp scene_renderer.cpp scene_store.cpp surface_query.cpp ${_scene_shader_sources})
target_compile_features(sh_scene PUBLIC cxx_std_20)
target_include_directories(sh_scene PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(sh_scene PUBLIC sh_rhi_gl sh_gltf sh_png sh_common)
target_compile_options(sh_scene PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>:-Wall -Wextra -Wpedantic>)
set_target_properties(sh_scene PROPERTIES POSITION_INDEPENDENT_CODE ON)
# cgltf's header for the area reader; its implementation (renamed symbols) is sh_gltf's (cgltf_prefixed.h).
target_include_directories(sh_scene PRIVATE ${PROJECT_SOURCE_DIR}/third_party/cgltf/include)

# tests/render/ builds against sh_render (tests/CMakeLists.txt: one target per tests/<module>).
add_library(sh_render INTERFACE)
target_link_libraries(sh_render INTERFACE sh_scene)
