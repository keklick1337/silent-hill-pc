# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 Vladislav Tislenko (keklick1337)
#
# sh_utf8: UTF-8 core (strict decode/count/advance) for the engine's future i18n.

add_library(sh_utf8 STATIC
    utf8.cpp)

target_compile_features(sh_utf8 PRIVATE cxx_std_20)
target_include_directories(sh_utf8 PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(sh_utf8 PRIVATE sh_common)
target_compile_options(sh_utf8 PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:-Wall -Wextra -Wpedantic>)
