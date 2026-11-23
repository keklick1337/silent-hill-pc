# sh_shaderstate: frame-wide shading inputs the game sets for the renderer (world fog ramp, per-pixel light).
add_library(sh_shaderstate STATIC shaderstate.cpp)
target_compile_features(sh_shaderstate PUBLIC cxx_std_20)
target_include_directories(sh_shaderstate PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_compile_options(sh_shaderstate PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>:-Wall -Wextra -Wpedantic>)
set_target_properties(sh_shaderstate PROPERTIES POSITION_INDEPENDENT_CODE ON)
