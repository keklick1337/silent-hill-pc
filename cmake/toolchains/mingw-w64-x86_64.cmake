# Cross-compile for Windows x64 with mingw-w64 (GCC).
#
#   cmake -G Ninja -B build-win \
#       -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-w64-x86_64.cmake
#
# Dependencies (SDL3, OpenAL Soft, libjpeg-turbo) come from
# tools/win-deps/build.sh, installed under third_party/deps/x86_64-w64-mingw32.
# SH_WIN_DEPS_PREFIX overrides that location.

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(SH_MINGW_TRIPLE x86_64-w64-mingw32)
set(CMAKE_C_COMPILER   ${SH_MINGW_TRIPLE}-gcc)
set(CMAKE_CXX_COMPILER ${SH_MINGW_TRIPLE}-g++)
set(CMAKE_RC_COMPILER  ${SH_MINGW_TRIPLE}-windres)
set(CMAKE_AR           ${SH_MINGW_TRIPLE}-gcc-ar CACHE FILEPATH "")
set(CMAKE_RANLIB       ${SH_MINGW_TRIPLE}-gcc-ranlib CACHE FILEPATH "")

get_filename_component(_sh_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
if(NOT SH_WIN_DEPS_PREFIX)
    if(DEFINED ENV{SH_WIN_DEPS_PREFIX})
        set(SH_WIN_DEPS_PREFIX "$ENV{SH_WIN_DEPS_PREFIX}")
    else()
        set(SH_WIN_DEPS_PREFIX "${_sh_root}/third_party/deps/${SH_MINGW_TRIPLE}")
    endif()
endif()
# try_compile projects re-read this file; pass the prefix through to them.
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES SH_WIN_DEPS_PREFIX)

execute_process(COMMAND ${CMAKE_C_COMPILER} -print-sysroot
    OUTPUT_VARIABLE _sh_sysroot OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
set(CMAKE_FIND_ROOT_PATH "${SH_WIN_DEPS_PREFIX}")
if(_sh_sysroot)
    list(APPEND CMAKE_FIND_ROOT_PATH "${_sh_sysroot}/${SH_MINGW_TRIPLE}" "${_sh_sysroot}")
endif()
list(APPEND CMAKE_PREFIX_PATH "${SH_WIN_DEPS_PREFIX}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Host pkg-config would hand back Linux SDL3/ffmpeg paths.
set(ENV{PKG_CONFIG_LIBDIR} "${SH_WIN_DEPS_PREFIX}/lib/pkgconfig")
set(ENV{PKG_CONFIG_PATH} "")
set(ENV{PKG_CONFIG_SYSROOT_DIR} "")

# A user's Windows has no libgcc_s_seh-1.dll, libstdc++-6.dll or
# libwinpthread-1.dll, so the GCC runtime goes into every binary statically.
# -static only switches -l lookups to archives; SDL3 and OpenAL are linked by
# the full path of their import libraries and stay DLLs.
set(CMAKE_EXE_LINKER_FLAGS_INIT    "-static -static-libgcc -static-libstdc++")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-static -static-libgcc -static-libstdc++")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "-static -static-libgcc -static-libstdc++")

# The exe and map DLLs run under wine on the build host for tests.
find_program(SH_WINE NAMES wine wine64)
if(SH_WINE AND NOT CMAKE_CROSSCOMPILING_EMULATOR)
    set(CMAKE_CROSSCOMPILING_EMULATOR "${SH_WINE}")
endif()
