# sh_memory: the work buffers the game loads its files into (work_buffers.h).
add_library(sh_memory STATIC work_buffers.cpp)
target_compile_features(sh_memory PUBLIC cxx_std_20)
target_include_directories(sh_memory PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_compile_options(sh_memory PRIVATE $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall;-Wextra;-Wpedantic>)
