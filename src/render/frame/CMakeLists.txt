# sh_frame: the engine frame, an ordered list of draw commands (docs/RENDER.md section 4).
add_library(sh_frame STATIC frame.cpp)
target_compile_features(sh_frame PUBLIC cxx_std_20)
target_include_directories(sh_frame PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_compile_options(sh_frame PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>:-Wall -Wextra -Wpedantic>)
set_target_properties(sh_frame PROPERTIES POSITION_INDEPENDENT_CODE ON)
