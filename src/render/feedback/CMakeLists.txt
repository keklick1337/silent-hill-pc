# sh_feedback: framebuffer feedback geometry and decisions (capture rects, display buffers, sample
# selection) plus the pack shader text. Pure logic; the GL back end makes the GL calls.
add_library(sh_feedback STATIC feedback_geometry.cpp)
target_compile_features(sh_feedback PUBLIC cxx_std_20)
target_include_directories(sh_feedback PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_compile_options(sh_feedback PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>:-Wall -Wextra -Wpedantic>)
set_target_properties(sh_feedback PROPERTIES POSITION_INDEPENDENT_CODE ON)
