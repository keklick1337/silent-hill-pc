add_library(sh_convert_tim STATIC tim_converter.cpp tim_file.cpp)

target_compile_features(sh_convert_tim PUBLIC cxx_std_20)
target_include_directories(sh_convert_tim PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(sh_convert_tim PUBLIC sh_common sh_png)
target_compile_options(sh_convert_tim PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:-Wall -Wextra -Wpedantic>)
