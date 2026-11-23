# sh_post: post-process, shadow light and frozen-frame policy of the renderer.
add_library(sh_post STATIC post.cpp hdr.cpp temporal.cpp)
target_compile_features(sh_post PUBLIC cxx_std_20)
target_include_directories(sh_post PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_compile_options(sh_post PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>:-Wall -Wextra -Wpedantic>)
set_target_properties(sh_post PROPERTIES POSITION_INDEPENDENT_CODE ON)
