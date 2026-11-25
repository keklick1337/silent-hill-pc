# RHI (docs/RENDER.md section 2): interface, GL 3.3 backend, SDL3 GPU and native Vulkan backends and the
# primitive batch shaders.
# sh_rhi_gl (interface, GL backend, primitive batches) is always built: the GPU renderer (render/gpu) draws the
# frame's splits through it. sh_rhi adds the native Vulkan backend (renderer = vulkan) and the SDL3 GPU backend
# (renderer = sdlgpu).

# Built-in shaders: GLSL 450 text for the GL backend, plus SPIR-V for SDL3 GPU when glslc or glslang is
# there. A GL-only build without either embeds an empty SPIR-V blob.
find_program(SH_GLSLC glslc)
find_program(SH_GLSLANG NAMES glslangValidator glslang)
if(SH_RHI_SDL3 AND NOT SH_GLSLC AND NOT SH_GLSLANG)
    message(FATAL_ERROR "SH_RHI_SDL3: glslc or glslangValidator is needed to build SPIR-V")
endif()

set(_rhi_gen "${CMAKE_CURRENT_BINARY_DIR}/gen")
file(MAKE_DIRECTORY "${_rhi_gen}")
set(_rhi_shader_sources "")
foreach(_pair IN ITEMS "prim_batch.vert:primBatchVert" "prim_batch.frag:primBatchFrag")
    string(REPLACE ":" ";" _pair "${_pair}")
    list(GET _pair 0 _file)
    list(GET _pair 1 _symbol)
    set(_src "${CMAKE_CURRENT_SOURCE_DIR}/shaders/${_file}")
    set(_spv "${_rhi_gen}/${_file}.spv")
    set(_cpp "${_rhi_gen}/${_file}.cpp")
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
    list(APPEND _rhi_shader_sources "${_cpp}")
endforeach()

add_library(sh_rhi_gl STATIC rhi_gl.cpp prim_batch.cpp shader_overrides.cpp ${_rhi_shader_sources})
target_compile_features(sh_rhi_gl PUBLIC cxx_std_20)
target_include_directories(sh_rhi_gl PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(sh_rhi_gl PUBLIC sh_common)
target_compile_options(sh_rhi_gl PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>:-Wall -Wextra -Wpedantic>)
# Position independent: it also links into shared objects.
set_target_properties(sh_rhi_gl PROPERTIES POSITION_INDEPENDENT_CODE ON)

# The native Vulkan backend's dependencies, vendored and built from source (COPYRIGHT.md): Vulkan-Headers, volk (opens
# the system's Vulkan loader at run time, so nothing links against vulkan-1 / libvulkan) and Vulkan Memory Allocator.
add_library(sh_vulkan_deps STATIC "${PROJECT_SOURCE_DIR}/third_party/volk/volk.c" vk_mem_alloc.cpp)
target_include_directories(sh_vulkan_deps SYSTEM PUBLIC
    "${PROJECT_SOURCE_DIR}/third_party/vulkan-headers/include"
    "${PROJECT_SOURCE_DIR}/third_party/volk"
    "${PROJECT_SOURCE_DIR}/third_party/vma/include")
target_compile_definitions(sh_vulkan_deps PUBLIC VK_NO_PROTOTYPES VMA_STATIC_VULKAN_FUNCTIONS=0
                                                 VMA_DYNAMIC_VULKAN_FUNCTIONS=0)
target_compile_features(sh_vulkan_deps PUBLIC cxx_std_20)
target_link_libraries(sh_vulkan_deps PUBLIC ${CMAKE_DL_LIBS})
set_target_properties(sh_vulkan_deps PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Ray tracing's compute shaders (rhi_vulkan_rt.cpp): SPIR-V 1.5 for ray queries, Vulkan only.
set(_rt_shader_sources "")
foreach(_pair IN ITEMS "rt_trace.comp:rtTraceComp" "rt_denoise.comp:rtDenoiseComp")
    string(REPLACE ":" ";" _pair "${_pair}")
    list(GET _pair 0 _file)
    list(GET _pair 1 _symbol)
    set(_src "${CMAKE_CURRENT_SOURCE_DIR}/shaders/${_file}")
    set(_spv "${_rhi_gen}/${_file}.spv")
    set(_cpp "${_rhi_gen}/${_file}.cpp")
    if(SH_GLSLC)
        set(_compile "${SH_GLSLC}" --target-env=vulkan1.2 "${_src}" -o "${_spv}")
    elseif(SH_GLSLANG)
        set(_compile "${SH_GLSLANG}" -V --target-env vulkan1.2 "${_src}" -o "${_spv}")
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
    list(APPEND _rt_shader_sources "${_cpp}")
endforeach()

# The native Vulkan backend's own passes (vk_atmosphere.cpp): compute and graphics shaders sharing common.glsl, SPIR-V
# only (no GL text is used).
set(_atmo_dir "${CMAKE_CURRENT_SOURCE_DIR}/shaders/atmosphere")
set(_atmo_sources "")
foreach(_pair IN ITEMS "ao.comp:atmosphereAoComp" "inject.comp:atmosphereInjectComp"
                       "integrate.comp:atmosphereIntegrateComp" "composite.vert:atmosphereCompositeVert"
                       "composite.frag:atmosphereCompositeFrag")
    string(REPLACE ":" ";" _pair "${_pair}")
    list(GET _pair 0 _file)
    list(GET _pair 1 _symbol)
    set(_src "${_atmo_dir}/${_file}")
    set(_spv "${_rhi_gen}/atmosphere_${_file}.spv")
    set(_cpp "${_rhi_gen}/atmosphere_${_file}.cpp")
    if(SH_GLSLC)
        set(_compile "${SH_GLSLC}" --target-env=vulkan1.1 -I "${_atmo_dir}" "${_src}" -o "${_spv}")
    elseif(SH_GLSLANG)
        set(_compile "${SH_GLSLANG}" -V --target-env vulkan1.1 -I"${_atmo_dir}" "${_src}" -o "${_spv}")
    else()
        set(_compile "${CMAKE_COMMAND}" -E touch "${_spv}")
    endif()
    add_custom_command(OUTPUT "${_spv}" COMMAND ${_compile} DEPENDS "${_src}" "${_atmo_dir}/common.glsl"
        COMMENT "SPIR-V atmosphere ${_file}" VERBATIM)
    add_custom_command(OUTPUT "${_cpp}"
        COMMAND "${CMAKE_COMMAND}" -DGLSL=${_src} -DSPIRV=${_spv} -DSYMBOL=${_symbol} -DOUT=${_cpp}
                -P "${PROJECT_SOURCE_DIR}/cmake/rhi_embed_shader.cmake"
        DEPENDS "${_src}" "${_spv}" "${PROJECT_SOURCE_DIR}/cmake/rhi_embed_shader.cmake"
        COMMENT "Embed atmosphere ${_file}" VERBATIM)
    list(APPEND _atmo_sources "${_cpp}")
endforeach()

# TAA, TAAU and the spatial upscale (vk_temporal.cpp), SPIR-V only like the atmosphere's.
set(_temporal_dir "${CMAKE_CURRENT_SOURCE_DIR}/shaders/temporal")
set(_temporal_sources "")
foreach(_pair IN ITEMS "motion.vert:temporalMotionVert" "motion.frag:temporalMotionFrag"
                       "resolve.comp:temporalResolveComp" "spatial.comp:temporalSpatialComp"
                       "sharpen.comp:temporalSharpenComp" "composite.vert:temporalCompositeVert"
                       "composite.frag:temporalCompositeFrag" "resolve.frag:temporalResolveFrag"
                       "spatial.frag:temporalSpatialFrag" "composite_sharpen.frag:temporalCompositeSharpenFrag")
    string(REPLACE ":" ";" _pair "${_pair}")
    list(GET _pair 0 _file)
    list(GET _pair 1 _symbol)
    set(_src "${_temporal_dir}/${_file}")
    set(_spv "${_rhi_gen}/temporal_${_file}.spv")
    set(_cpp "${_rhi_gen}/temporal_${_file}.cpp")
    if(SH_GLSLC)
        set(_compile "${SH_GLSLC}" --target-env=vulkan1.1 -I "${_temporal_dir}" "${_src}" -o "${_spv}")
    elseif(SH_GLSLANG)
        set(_compile "${SH_GLSLANG}" -V --target-env vulkan1.1 -I"${_temporal_dir}" "${_src}" -o "${_spv}")
    else()
        set(_compile "${CMAKE_COMMAND}" -E touch "${_spv}")
    endif()
    add_custom_command(OUTPUT "${_spv}" COMMAND ${_compile} DEPENDS "${_src}" "${_temporal_dir}/common.glsl"
        COMMENT "SPIR-V temporal ${_file}" VERBATIM)
    add_custom_command(OUTPUT "${_cpp}"
        COMMAND "${CMAKE_COMMAND}" -DGLSL=${_src} -DSPIRV=${_spv} -DSYMBOL=${_symbol} -DOUT=${_cpp}
                -P "${PROJECT_SOURCE_DIR}/cmake/rhi_embed_shader.cmake"
        DEPENDS "${_src}" "${_spv}" "${PROJECT_SOURCE_DIR}/cmake/rhi_embed_shader.cmake"
        COMMENT "Embed temporal ${_file}" VERBATIM)
    list(APPEND _temporal_sources "${_cpp}")
endforeach()

add_library(sh_rhi STATIC rhi_sdlgpu.cpp rhi_vulkan.cpp rhi_vulkan_rt.cpp vk_atmosphere.cpp vk_temporal.cpp vk_rendering.cpp
    ${_rt_shader_sources} ${_atmo_sources} ${_temporal_sources})
target_link_libraries(sh_rhi PUBLIC sh_rhi_gl ${SH_SDL3_TARGET} PRIVATE sh_vulkan_deps)
target_compile_options(sh_rhi PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>:-Wall -Wextra -Wpedantic>)
set_target_properties(sh_rhi PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(NOT SH_GLSLC AND NOT SH_GLSLANG)
    message(WARNING "No glslc or glslangValidator: the SDL3 GPU backend has no shaders, renderer = vulkan falls back to gl")
endif()
# Vulkan structures are written {VK_STRUCTURE_TYPE_...} with the rest zeroed on purpose.
set_source_files_properties(rhi_vulkan.cpp rhi_vulkan_rt.cpp vk_atmosphere.cpp vk_temporal.cpp vk_rendering.cpp PROPERTIES COMPILE_OPTIONS
    "$<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>:-Wno-missing-field-initializers>")
