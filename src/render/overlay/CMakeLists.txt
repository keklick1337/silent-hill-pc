# sh_overlay: the engine's 2D overlay (UI and debug drawers) on the RHI. The shaders are embedded like the RHI's
# (cmake/rhi_embed_shader.cmake): GLSL 450 text for the GL backend, SPIR-V for SDL3 GPU when glslc or glslang is there.

set(_overlay_gen "${CMAKE_CURRENT_BINARY_DIR}/gen")
file(MAKE_DIRECTORY "${_overlay_gen}")
set(_overlay_shader_sources "")
foreach(_pair IN ITEMS "overlay.vert:overlayVert" "overlay.frag:overlayFrag")
    string(REPLACE ":" ";" _pair "${_pair}")
    list(GET _pair 0 _file)
    list(GET _pair 1 _symbol)
    set(_src "${CMAKE_CURRENT_SOURCE_DIR}/shaders/${_file}")
    set(_spv "${_overlay_gen}/${_file}.spv")
    set(_cpp "${_overlay_gen}/${_file}.cpp")
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
    list(APPEND _overlay_shader_sources "${_cpp}")
endforeach()

add_library(sh_overlay STATIC overlay.cpp overlay_gpu.cpp ${_overlay_shader_sources})
target_compile_features(sh_overlay PUBLIC cxx_std_20)
target_include_directories(sh_overlay PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(sh_overlay PUBLIC sh_rhi_gl sh_common)
target_compile_options(sh_overlay PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>:-Wall -Wextra -Wpedantic>)
set_target_properties(sh_overlay PROPERTIES POSITION_INDEPENDENT_CODE ON)

