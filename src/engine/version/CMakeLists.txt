# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 Vladislav Tislenko (keklick1337)
#
# sh_engine_version: the engine version of the VERSION file (project(... VERSION) in the root) as constants, and
# the build metadata of sh_build_info.h (gen_build_info, rewritten per commit) behind functions.
configure_file(engine_version_numbers.h.in
    "${CMAKE_BINARY_DIR}/generated/engine/version/engine_version_numbers.h" @ONLY)

add_library(sh_engine_version STATIC engine_version.cpp)
target_compile_features(sh_engine_version PUBLIC cxx_std_20)
target_include_directories(sh_engine_version
    PUBLIC ${PROJECT_SOURCE_DIR}/src "${CMAKE_BINARY_DIR}/generated"
    PRIVATE ${CMAKE_BINARY_DIR})
add_dependencies(sh_engine_version gen_build_info)
# sh_build_info.h is a byproduct of gen_build_info: name it so a clean build writes it before this file compiles.
set_source_files_properties(engine_version.cpp PROPERTIES OBJECT_DEPENDS "${CMAKE_BINARY_DIR}/sh_build_info.h")
target_compile_options(sh_engine_version PRIVATE $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall;-Wextra;-Wpedantic>)
