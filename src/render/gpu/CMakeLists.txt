# sh_gpu: the engine's primitive renderer on the RHI (docs/RENDER.md section 3). Every zone adds its .cpp here;
# CONFIGURE_DEPENDS picks new files up without editing this list.
file(GLOB SH_GPU_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

# Built-in shaders of the zones (shaders/*.vert, *.frag), embedded like the RHI's (cmake/rhi_embed_shader.cmake):
# shaders/<name>.<stage> becomes sh::rhi::embedded::gpu_<name>_<stage>(), declared by the zone that uses it.
file(GLOB SH_GPU_SHADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.vert"
     "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.frag")
set(_gpu_gen "${CMAKE_CURRENT_BINARY_DIR}/gen")
file(MAKE_DIRECTORY "${_gpu_gen}")
set(_gpu_shader_sources "")
foreach(_src IN LISTS SH_GPU_SHADERS)
    get_filename_component(_file "${_src}" NAME)
    string(MAKE_C_IDENTIFIER "gpu_${_file}" _symbol)
    set(_spv "${_gpu_gen}/${_file}.spv")
    set(_cpp "${_gpu_gen}/${_file}.cpp")
    # SDL3 GPU: Vulkan resource sets and Z in [0, 1].
    if(SH_GLSLC)
        set(_compile "${SH_GLSLC}" --target-env=vulkan1.0 -DSH_RHI_VULKAN -DSH_RHI_CLIP_Z01 "${_src}" -o "${_spv}")
    elseif(SH_GLSLANG)
        set(_compile "${SH_GLSLANG}" -V --target-env vulkan1.0 -DSH_RHI_VULKAN -DSH_RHI_CLIP_Z01 "${_src}" -o "${_spv}")
    else()
        set(_compile "${CMAKE_COMMAND}" -E touch "${_spv}")
    endif()
    add_custom_command(OUTPUT "${_spv}" COMMAND ${_compile} DEPENDS "${_src}"
        COMMENT "SPIR-V ${_file}" VERBATIM)
    add_custom_command(OUTPUT "${_cpp}"
        COMMAND "${CMAKE_COMMAND}" -DGLSL=${_src} -DSPIRV=${_spv} -DSYMBOL=${_symbol} -DOUT=${_cpp}
                -P "${PROJECT_SOURCE_DIR}/cmake/rhi_embed_shader.cmake"
        DEPENDS "${_src}" "${_spv}" "${PROJECT_SOURCE_DIR}/cmake/rhi_embed_shader.cmake"
        COMMENT "Embed ${_file}" VERBATIM)
    list(APPEND _gpu_shader_sources "${_cpp}")
endforeach()

add_library(sh_gpu STATIC ${SH_GPU_SOURCES} ${_gpu_shader_sources})
target_compile_features(sh_gpu PUBLIC cxx_std_20)
target_include_directories(sh_gpu PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(sh_gpu PUBLIC sh_rhi sh_rhi_gl sh_scene sh_frame sh_draw2d sh_draw3d sh_shaderstate sh_post sh_present sh_feedback
                      sh_offscreen sh_override_tex sh_log sh_desktop)
