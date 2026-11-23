# CMP (LZSS) and demo DAT converters, both lossless.
add_library(sh_convert_archive STATIC lzss.cpp archive_converters.cpp)

target_compile_features(sh_convert_archive PUBLIC cxx_std_20)
target_include_directories(sh_convert_archive PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(sh_convert_archive PUBLIC sh_common)
target_compile_options(sh_convert_archive PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:-Wall -Wextra -Wpedantic>)
