add_library(sh_convert_model STATIC
    model_converter.cpp
    model_common.cpp
    ilm.cpp
    ilm_build.cpp
    tmd.cpp
    tmd_build.cpp
    plm_build.cpp)

target_compile_features(sh_convert_model PUBLIC cxx_std_20)
target_include_directories(sh_convert_model PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(sh_convert_model PUBLIC sh_common sh_gltf sh_convert_level sh_convert_anim)
target_compile_options(sh_convert_model PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:-Wall -Wextra -Wpedantic>)
