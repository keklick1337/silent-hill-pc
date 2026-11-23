add_library(sh_archive STATIC sh_archive.cpp)

target_compile_features(sh_archive PUBLIC cxx_std_20)
target_include_directories(sh_archive PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(sh_archive PUBLIC sh_common sh_disc)
target_compile_options(sh_archive PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:-Wall -Wextra -Wpedantic>)
