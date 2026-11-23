find_package(Threads REQUIRED)

add_library(sh_import STATIC
    sha256.cpp
    input.cpp
    index.cpp
    import.cpp)

target_compile_features(sh_import PUBLIC cxx_std_20)
target_include_directories(sh_import PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(sh_import PUBLIC sh_common sh_disc sh_iso9660 sh_archive Threads::Threads)
target_compile_options(sh_import PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:-Wall -Wextra -Wpedantic>)
