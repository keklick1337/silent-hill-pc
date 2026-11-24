# The quick setup shown before the first start: its own window, before the
# disc import, with nothing of the game loaded. A library of its own, like the first-run import beside it.
add_library(sh_quick_setup STATIC setup_model.cpp setup_text.cpp hardware_probe.cpp setup_screen.cpp)

target_compile_features(sh_quick_setup PUBLIC cxx_std_20)
target_include_directories(sh_quick_setup PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(sh_quick_setup PUBLIC sh_common sh_screen_kit PRIVATE sh_i18n sh_rhi sh_gpu sh_desktop ${SH_SDL3_TARGET})
target_compile_options(sh_quick_setup PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:-Wall -Wextra -Wpedantic>)
