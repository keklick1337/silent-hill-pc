# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 Vladislav Tislenko (keklick1337)
#
# sh_desktop: the application's face in the desktop - the window icon from assets/icon/, and the Linux menu entry
# the user asks for with --install-desktop (assets/linux/silent-hill-pc.desktop.in, compiled in as text so the
# option works without the template file beside the exe).
file(READ "${PROJECT_SOURCE_DIR}/assets/linux/silent-hill-pc.desktop.in" SH_DESKTOP_ENTRY_TEMPLATE)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/assets/linux/silent-hill-pc.desktop.in")
configure_file(desktop_entry_template.h.in
    "${CMAKE_BINARY_DIR}/generated/engine/desktop/desktop_entry_template.h" @ONLY)

add_library(sh_desktop STATIC desktop_entry.cpp window_icon.cpp)
target_compile_features(sh_desktop PUBLIC cxx_std_20)
target_include_directories(sh_desktop PUBLIC ${PROJECT_SOURCE_DIR}/src PRIVATE "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(sh_desktop PUBLIC sh_common PRIVATE sh_png ${SH_SDL3_TARGET})
target_compile_options(sh_desktop PRIVATE $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall;-Wextra;-Wpedantic>)
