# sh_draw2d: the engine's 2D draw list (docs/RENDER.md section 5). Engine types only; the game's drawers
# record commands and place them in the engine frame (render/frame) by order key.

add_library(sh_draw2d STATIC draw2d.cpp draw2d_stream.cpp)
target_compile_features(sh_draw2d PUBLIC cxx_std_20)
target_include_directories(sh_draw2d PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(sh_draw2d PUBLIC sh_frame)
target_compile_options(sh_draw2d PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>:-Wall -Wextra -Wpedantic>)
set_target_properties(sh_draw2d PROPERTIES POSITION_INDEPENDENT_CODE ON)
