add_library(sh_png STATIC palette_variant.cpp sh_png.cpp zlib_codec.cpp stb_png.cpp)

target_compile_features(sh_png PUBLIC cxx_std_20)
target_include_directories(sh_png PUBLIC ${PROJECT_SOURCE_DIR}/src)
# stb_png.cpp includes "stb/include/stb_image.h"; miniz (deflate) is the shared sh_miniz of the root CMakeLists.txt.
target_include_directories(sh_png PRIVATE ${PROJECT_SOURCE_DIR}/third_party)
target_link_libraries(sh_png PUBLIC sh_common PRIVATE sh_miniz)

# Warnings are for our own sources; the vendored C files are built as they are.
set_source_files_properties(stb_png.cpp PROPERTIES COMPILE_OPTIONS "-w")
target_compile_options(sh_png PRIVATE
    $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>>:-Wall -Wextra -Wpedantic>)
