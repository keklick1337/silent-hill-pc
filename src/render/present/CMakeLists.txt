# sh_present: pure present-path logic (display pixel aspect, scaled render size, present rectangle, point-sampled
# texture set). No GL: the renderer (render/gpu) feeds it numbers and applies the result.
add_library(sh_present STATIC present_math.cpp)
target_compile_features(sh_present PUBLIC cxx_std_20)
target_include_directories(sh_present PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_compile_options(sh_present PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>:-Wall -Wextra -Wpedantic>)
set_target_properties(sh_present PROPERTIES POSITION_INDEPENDENT_CODE ON)
