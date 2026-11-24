# SPDX-License-Identifier: GPL-3.0-or-later
#
# sh_menu: the engine's generic menu model. Renderer-free
# and game-agnostic; the views live in src/ui/menu, the Lua binding in src/engine/script/bind_menu.cpp.
add_library(sh_menu STATIC
    menu_layout.cpp
    menu_model.cpp
    touch_scroll.cpp
    menu_registry.cpp
    text_fit.cpp)

target_compile_features(sh_menu PRIVATE cxx_std_20)
target_include_directories(sh_menu PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(sh_menu PUBLIC sh_hooks)
target_compile_options(sh_menu PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:-Wall -Wextra -Wpedantic>)
