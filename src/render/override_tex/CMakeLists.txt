# sh_override_tex: texel terms of replacement textures; engine types only.
add_library(sh_override_tex STATIC override_tex.cpp)
target_compile_features(sh_override_tex PUBLIC cxx_std_20)
target_include_directories(sh_override_tex PUBLIC ${PROJECT_SOURCE_DIR}/src)
set_target_properties(sh_override_tex PROPERTIES POSITION_INDEPENDENT_CODE ON)
