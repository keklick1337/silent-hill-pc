# sh-upscale: the AI texture upscaler (docs/RENDER.md section 12), a tool of its own so the game does not
# link ncnn. sh_upscale_tool holds the picture work without the network and is tested without a GPU.
add_library(sh_upscale_tool STATIC upscaler.cpp)
target_compile_features(sh_upscale_tool PUBLIC cxx_std_20)
target_include_directories(sh_upscale_tool PUBLIC ${PROJECT_SOURCE_DIR}/tools ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(sh_upscale_tool PUBLIC sh_common)
target_compile_options(sh_upscale_tool PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:-Wall -Wextra -Wpedantic>)

# ncnn from tools/ncnn/build.sh (the dependency prefix of this platform); without it only the game is built.
get_filename_component(SH_UPSCALE_NCNN_PREFIX "${SH_UPSCALE_NCNN_PREFIX}" ABSOLUTE BASE_DIR "${PROJECT_SOURCE_DIR}")
if(NOT TARGET ncnn)
    # The prefix is a host path: a cross toolchain's find root must not be put in front of it.
    find_package(ncnn CONFIG QUIET PATHS "${SH_UPSCALE_NCNN_PREFIX}" NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
endif()
if(NOT TARGET ncnn)
    # A warning, not a status line: without the tool texture_upscale does nothing, and the game can only say so.
    message(WARNING "sh-upscale: no ncnn in ${SH_UPSCALE_NCNN_PREFIX}; AI texture upscaling (texture_upscale) is "
                    "not built. Run tools/ncnn/build.sh once (or set SH_UPSCALE_NCNN_PREFIX) and configure again.")
    return()
endif()

add_executable(sh-upscale main.cpp)
# Beside the game, which starts it from its own folder.
set_target_properties(sh-upscale PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
target_link_libraries(sh-upscale PRIVATE sh_upscale_tool sh_png ncnn)
target_compile_options(sh-upscale PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:-Wall -Wextra -Wpedantic>)
if(APPLE)
    # ncnn calls vkGetInstanceProcAddr directly on Apple; MoltenVK is opened at run time (vulkan_loader_apple.cpp).
    target_sources(sh-upscale PRIVATE vulkan_loader_apple.cpp)
endif()
if(MINGW)
    # The tool carries its runtime: it runs beside the game without the MinGW DLLs.
    target_link_options(sh-upscale PRIVATE -static)
endif()
file(GLOB _sh_upscale_licenses "${SH_UPSCALE_NCNN_PREFIX}/share/sh-licenses/LICENSE-ncnn.txt"
    "${SH_UPSCALE_NCNN_PREFIX}/share/sh-licenses/LICENSE-Real-ESRGAN.txt")
if(_sh_upscale_licenses)
    add_custom_command(TARGET sh-upscale POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:sh-upscale>/licenses"
        COMMAND ${CMAKE_COMMAND} -E copy ${_sh_upscale_licenses} "$<TARGET_FILE_DIR:sh-upscale>/licenses"
        VERBATIM)
endif()
# The model beside the tool, where it looks by default.
set(_sh_upscale_models "${SH_UPSCALE_NCNN_PREFIX}/share/sh-upscale/models")
if(EXISTS "${_sh_upscale_models}")
    add_custom_command(TARGET sh-upscale POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${_sh_upscale_models}" "$<TARGET_FILE_DIR:sh-upscale>/upscale-models"
        VERBATIM)
endif()
