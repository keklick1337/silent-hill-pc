add_library(sh_hooks STATIC hook_bus.cpp catalog.cpp hook_trace.cpp)
target_compile_features(sh_hooks PUBLIC cxx_std_20)
target_include_directories(sh_hooks PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_compile_options(sh_hooks PRIVATE $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall;-Wextra;-Wpedantic>)

# The `hooks` / `hooks_trace` console commands, apart so sh_hooks does not need the console.
add_library(sh_hooks_console STATIC hooks_console.cpp)
target_compile_features(sh_hooks_console PUBLIC cxx_std_20)
target_link_libraries(sh_hooks_console PUBLIC sh_hooks sh_console PRIVATE sh_log)
target_compile_options(sh_hooks_console PRIVATE $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall;-Wextra;-Wpedantic>)
