# sh_offscreen: RGBA render-target to VRAM word packing (docs/RENDER.md).
add_library(sh_offscreen STATIC rgba_to_vram.cpp)
target_compile_features(sh_offscreen PUBLIC cxx_std_20)
target_include_directories(sh_offscreen PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_compile_options(sh_offscreen PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>:-Wall -Wextra -Wpedantic>)
set_target_properties(sh_offscreen PROPERTIES POSITION_INDEPENDENT_CODE ON)
