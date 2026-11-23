# sh_draw3d: the engine's world-space effect draw list (docs/RENDER.md section 2.2). Engine types only; the
# game's effect drawers record polygons, the frame draws them at their place through the native backend.

add_library(sh_draw3d STATIC draw3d.cpp draw3d_stream.cpp)
target_compile_features(sh_draw3d PUBLIC cxx_std_20)
target_include_directories(sh_draw3d PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(sh_draw3d PUBLIC sh_draw2d sh_frame)
target_compile_options(sh_draw3d PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>:-Wall -Wextra -Wpedantic>)
set_target_properties(sh_draw3d PROPERTIES POSITION_INDEPENDENT_CODE ON)
