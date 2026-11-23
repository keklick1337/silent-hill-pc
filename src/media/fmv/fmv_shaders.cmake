# FMV picture shaders (src/media/fmv/shaders), embedded like the RHI's (cmake/rhi_embed_shader.cmake): GLSL 450
# text for the GL backend and SPIR-V for SDL3 GPU when glslc or glslang is there. Adds the generated sources to
# the exe; included from the top-level CMakeLists.txt after the SilentHillPC target exists.

find_program(SH_GLSLC glslc)
find_program(SH_GLSLANG NAMES glslangValidator glslang)

set(_fmv_dir "${PROJECT_SOURCE_DIR}/src/media/fmv/shaders")
set(_fmv_gen "${CMAKE_BINARY_DIR}/fmv_shaders")
file(MAKE_DIRECTORY "${_fmv_gen}")
foreach(_pair IN ITEMS "fmv_blit.vert:fmvBlitVert" "fmv_blit.frag:fmvBlitFrag")
    string(REPLACE ":" ";" _pair "${_pair}")
    list(GET _pair 0 _file)
    list(GET _pair 1 _symbol)
    set(_src "${_fmv_dir}/${_file}")
    set(_spv "${_fmv_gen}/${_file}.spv")
    set(_cpp "${_fmv_gen}/${_file}.cpp")
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
    target_sources(SilentHillPC PRIVATE "${_cpp}")
endforeach()
