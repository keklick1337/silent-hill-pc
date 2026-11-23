add_library(sh_convert_audio STATIC
    wav.cpp
    spu_adpcm.cpp
    xa_adpcm.cpp
    json_writer.cpp
    vab_converter.cpp
    xa_converter.cpp
    kdt_converter.cpp)

target_compile_features(sh_convert_audio PUBLIC cxx_std_20)
target_include_directories(sh_convert_audio PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(sh_convert_audio PUBLIC sh_common)
target_compile_options(sh_convert_audio PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:-Wall -Wextra -Wpedantic>)
