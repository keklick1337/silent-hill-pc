# DMS cutscenes <-> sh-cutscene JSON (lossless).
add_library(sh_convert_cutscene STATIC cutscene_converter.cpp)

target_compile_features(sh_convert_cutscene PUBLIC cxx_std_20)
target_include_directories(sh_convert_cutscene PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(sh_convert_cutscene PUBLIC sh_common)
target_compile_options(sh_convert_cutscene PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:-Wall -Wextra -Wpedantic>)
