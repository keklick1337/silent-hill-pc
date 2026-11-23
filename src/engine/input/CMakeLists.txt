# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 Vladislav Tislenko (keklick1337)
#
# sh_input: semantic input actions, their config format and the translation to
# the game's pad bits. Pure C++: the SDL/game bridge lives in src/input/.
add_library(sh_input STATIC
    input_actions.cpp
    input_primitives.cpp
    frame_edge.cpp
    controls_ref.cpp)

target_compile_features(sh_input PRIVATE cxx_std_20)
target_include_directories(sh_input PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(sh_input PRIVATE sh_common)
target_compile_options(sh_input PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:-Wall -Wextra -Wpedantic>)
