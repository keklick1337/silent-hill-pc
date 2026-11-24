cmake_minimum_required(VERSION 3.16)

# The engine version is set by hand in the VERSION file (MAJOR.MINOR.PATCH, the only place): mods require it with
# "min_engine_version", and a release tag vX.Y.Z must match it (tools/ci/package.sh).
file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/VERSION" SH_ENGINE_VERSION LIMIT_COUNT 1)
string(STRIP "${SH_ENGINE_VERSION}" SH_ENGINE_VERSION)
if(NOT SH_ENGINE_VERSION MATCHES "^(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)$")
    message(FATAL_ERROR "VERSION must hold MAJOR.MINOR.PATCH, found '${SH_ENGINE_VERSION}'")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/VERSION")
project(SilentHillPC VERSION ${SH_ENGINE_VERSION} HOMEPAGE_URL "https://github.com/keklick1337/silent-hill-pc" LANGUAGES C CXX)
# The numbers as a header (engine/version/engine_version_numbers.h, written by src/engine/version): every target
# sees it, since module headers that include it are read by targets that do not link the module.
include_directories("${CMAKE_BINARY_DIR}/generated")

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# No CMAKE_BUILD_TYPE means -O0 for the whole game. RelWithDebInfo keeps -g for
# crash investigations. An explicit -DCMAKE_BUILD_TYPE wins.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
    message(STATUS "CMAKE_BUILD_TYPE unset -> defaulting to RelWithDebInfo")
endif()

# The game's fixed-point rules expect signed arithmetic to wrap, and the readers
# of the disc formats look at the same bytes through several record types. Both
# are undefined behaviour an optimizer may exploit, so both flags are required.
add_compile_options(-fno-strict-aliasing -fwrapv)
# No fused multiply-add: arm64 (Apple silicon) would otherwise contract a*b+c into one rounding where x86-64 rounds
# twice, and float results, the replays' state included, would differ between the platforms.
add_compile_options(-ffp-contract=off)

# Build everything position-independent: the executable is a PIE and the static
# module libraries are linked into it.
# Harmless on Windows/MinGW (PE code is already position-independent).
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# The test and tool targets name some module libraries twice to resolve their cycles; Apple's linker warns about
# every repeated one.
if(APPLE)
    include(CheckLinkerFlag)
    check_linker_flag(CXX "LINKER:-no_warn_duplicate_libraries" SH_LINKER_NO_DUPLICATE_WARNING)
    if(SH_LINKER_NO_DUPLICATE_WARNING)
        add_link_options("LINKER:-no_warn_duplicate_libraries")
    endif()
endif()

# =============================================================================
# Options
# =============================================================================
option(SH_DEBUG "Enable debug output" OFF)
option(SH_ASAN "Enable AddressSanitizer for memory corruption detection" OFF)
# FMV ffmpeg fallback (native h264/h265/vp9/av1 in mp4/mkv/webm/avi override files).
# ON by default and free to be: ffmpeg is loaded at runtime, so this costs only the
# headers at build time and nothing is bundled or linked. See the ffmpeg section below.
option(SH_FMV_FFMPEG "Enable ffmpeg FMV fallback decoder (h264/h265/vp9/av1, mp4/mkv/webm)" ON)
# The SDL3 GPU (Vulkan) RHI backend is always built (SDL3 is the platform layer); without glslc or glslang its
# SPIR-V is empty and renderer = vulkan falls back to gl at run time. This option makes the shader compiler
# mandatory (docs/RENDER.md section 2).
option(SH_RHI_SDL3 "Require SPIR-V shaders for the SDL3 GPU backend" OFF)

# =============================================================================
# Android (docs/BUILDING.md section 5): the game is libmain.so, which SDL's Java activity loads; SDL3 and libjpeg come
# from tools/android-deps/build.sh, one prefix per ABI. Sound goes through SDL3, so OpenAL is not linked; FFmpeg (mod
# mp4 movies) is not part of the Android build.
# =============================================================================
if(ANDROID)
    set(SH_ANDROID_DEPS_PREFIX "${PROJECT_SOURCE_DIR}/third_party/deps/android-${ANDROID_ABI}" CACHE PATH
        "Prefix of the Android dependencies of one ABI (tools/android-deps/build.sh)")
    if(NOT EXISTS "${SH_ANDROID_DEPS_PREFIX}/lib/cmake/SDL3/SDL3Config.cmake")
        message(FATAL_ERROR "no SDL3 in ${SH_ANDROID_DEPS_PREFIX}: run tools/android-deps/build.sh ${ANDROID_ABI}")
    endif()
    set(SDL3_DIR "${SH_ANDROID_DEPS_PREFIX}/lib/cmake/SDL3" CACHE PATH "" FORCE)
    set(JPEG_LIBRARY "${SH_ANDROID_DEPS_PREFIX}/lib/libjpeg.a" CACHE FILEPATH "" FORCE)
    set(JPEG_INCLUDE_DIR "${SH_ANDROID_DEPS_PREFIX}/include" CACHE PATH "" FORCE)
    set(SH_FMV_FFMPEG OFF CACHE BOOL "" FORCE)
endif()

# =============================================================================
# AddressSanitizer (optional, for debugging memory corruption)
# =============================================================================
if(SH_ASAN)
    message(STATUS "AddressSanitizer ENABLED")
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address)
endif()

# =============================================================================
# miniz: one copy for the executable and the PNG writer (sh_png), so the two
# never carry two definitions of mz_*.
# =============================================================================
add_library(sh_miniz STATIC "${PROJECT_SOURCE_DIR}/third_party/miniz/src/miniz.c")
target_include_directories(sh_miniz PUBLIC "${PROJECT_SOURCE_DIR}/third_party")
if(NOT MSVC)
    target_compile_options(sh_miniz PRIVATE -w)
endif()

# =============================================================================
# SDL3: window, GL context, input, audio, threads and the SDL3 GPU backend of the RHI. The system SDL3
# (SDL3Config.cmake) when there is one, otherwise a source tree in third_party/SDL3 built inside the
# project; Windows gets it from tools/win-deps (SH_WIN_DEPS_PREFIX). Code includes <SDL3/...>.
# =============================================================================
find_package(SDL3 3.2 CONFIG QUIET)
if(SDL3_FOUND)
    set(SH_SDL3_TARGET SDL3::SDL3)
    message(STATUS "SDL3: ${SDL3_VERSION} (${SDL3_DIR})")
elseif(EXISTS "${PROJECT_SOURCE_DIR}/third_party/SDL3/CMakeLists.txt")
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    add_subdirectory("${PROJECT_SOURCE_DIR}/third_party/SDL3" "${CMAKE_BINARY_DIR}/third_party/SDL3" EXCLUDE_FROM_ALL)
    set(SH_SDL3_TARGET SDL3::SDL3-static)
    message(STATUS "SDL3: from third_party/SDL3")
else()
    message(FATAL_ERROR "SDL3 >= 3.2 not found: install the SDL3 development files, clone a release-3.2.x tag "
                        "into third_party/SDL3, or (Windows) run tools/win-deps/build.sh")
endif()

# Dear ImGui v1.92.5 (6d910d5487d1, MIT, third_party/imgui): the UI of the built-in modding tools
# (src/features/tools). Only the core and the SDL3 platform backend are built; the draw lists go
# through the engine's overlay, so it renders on every RHI backend.
add_library(sh_imgui STATIC
    "${PROJECT_SOURCE_DIR}/third_party/imgui/imgui.cpp"
    "${PROJECT_SOURCE_DIR}/third_party/imgui/imgui_draw.cpp"
    "${PROJECT_SOURCE_DIR}/third_party/imgui/imgui_tables.cpp"
    "${PROJECT_SOURCE_DIR}/third_party/imgui/imgui_widgets.cpp"
    "${PROJECT_SOURCE_DIR}/third_party/imgui/backends/imgui_impl_sdl3.cpp")
target_include_directories(sh_imgui PUBLIC "${PROJECT_SOURCE_DIR}/third_party/imgui"
                                           "${PROJECT_SOURCE_DIR}/third_party/imgui/backends")
# No demo window and no obsolete names; the engine's own font is loaded at run time.
target_compile_definitions(sh_imgui PUBLIC IMGUI_DISABLE_DEMO_WINDOWS IMGUI_DISABLE_DEBUG_TOOLS
                                           IMGUI_DISABLE_OBSOLETE_FUNCTIONS)
target_link_libraries(sh_imgui PUBLIC ${SH_SDL3_TARGET})
# The SDL3 backend supplies the clipboard and the IME: no imm32 / user32 calls of ImGui's own on Windows.
if(WIN32)
    target_compile_definitions(sh_imgui PUBLIC IMGUI_DISABLE_WIN32_FUNCTIONS)
endif()
if(NOT MSVC)
    target_compile_options(sh_imgui PRIVATE -w)
endif()

# Linux: the project's own build of the dependencies (tools/linux-deps/build.sh: OpenAL Soft shared, libjpeg-turbo
# static), when it is there, comes before the system's; a release package is built this way so it asks the
# user's system for neither. Without it the system libraries are linked, as a development build does.
if(UNIX AND NOT APPLE)
    string(TOLOWER "${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}" _sh_own_host)
    set(_sh_own_deps "${PROJECT_SOURCE_DIR}/third_party/deps/${_sh_own_host}")
    if(EXISTS "${_sh_own_deps}/lib/libopenal.so.1" AND EXISTS "${_sh_own_deps}/lib/libjpeg.a")
        list(PREPEND CMAKE_PREFIX_PATH "${_sh_own_deps}")
        set(SH_LINUX_DEPS_PREFIX "${_sh_own_deps}")
        message(STATUS "Linux dependencies: the project's own build, ${_sh_own_deps}")
    endif()
endif()
# macOS: the same for tools/macos-deps/build.sh (libjpeg-turbo static); OpenAL and OpenGL are the system's
# frameworks. Without it Homebrew's libraries are linked, as a development build does.
if(APPLE)
    set(_sh_own_deps "${PROJECT_SOURCE_DIR}/third_party/deps/darwin-${CMAKE_SYSTEM_PROCESSOR}")
    if(EXISTS "${_sh_own_deps}/lib/libjpeg.a")
        list(PREPEND CMAKE_PREFIX_PATH "${_sh_own_deps}")
        set(SH_MACOS_DEPS_PREFIX "${_sh_own_deps}")
        message(STATUS "macOS dependencies: the project's own build, ${_sh_own_deps}")
    endif()
endif()

# OpenAL Soft.
if(NOT ANDROID)
    find_package(OpenAL REQUIRED)
endif()

# =============================================================================
# Source files
# =============================================================================
# Every map is data (gamedata/maps/<map>/); no map's code is compiled into the exe.
# The executable's own sources (engine hosts, render, audio, ui, features).
# Only the directories listed here are compiled into the exe: the module libraries
# (formats/common, formats/disc, formats/iso9660, formats/archive, engine/vfs, engine/mods)
# are not in this list and come in through their own add_subdirectory targets.
# CONFIGURE_DEPENDS so a file added under these directories is picked up by a plain
# `cmake --build .`.
set(SH_EXE_SOURCE_DIRS
    engine/app engine/config engine/timing engine/debug
    engine/loader engine/scripting engine/runtime
    formats/game formats/texture
    media audio render i18n input platform ui features
    engine/audio/output/device engine/platform
    engine/chara/runtime  # the running game's character data and spawns (the rest of engine/chara is sh_chara)
    engine/chara/game  # the character types' behaviour API, combat view, pool and map cast on their link (chara_link.h)
    engine/gameflow/game engine/entities/game  # the game flow and NPC hosts on the game's link (game_link.h)
    engine/inventory/game  # the item stage and the item views (sh_camera, sh_scene, the frame)
    engine/world/game  # the world queries of the session characters over the streamed cell window (character_world.h)
    engine/camera/game  # the camera host of the running game (camera_game_host.cpp)
    engine/effects/game  # the effect hosts and the map light machine on the game's link (the rest is sh_effects)
)
set(SH_EXE_SOURCES "")
foreach(_dir IN LISTS SH_EXE_SOURCE_DIRS)
    file(GLOB_RECURSE _dir_sources CONFIGURE_DEPENDS
        "${PROJECT_SOURCE_DIR}/src/${_dir}/*.cpp"
    )
    list(APPEND SH_EXE_SOURCES ${_dir_sources})
endforeach()
# The state dump (console `dump`) and the performance recorder name the build: the revision at configure time.
execute_process(COMMAND git rev-parse --short HEAD WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    OUTPUT_VARIABLE SH_GIT_REVISION OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET RESULT_VARIABLE _sh_git_result)
if(NOT _sh_git_result EQUAL 0)
    set(SH_GIT_REVISION "")
endif()
set_source_files_properties("${PROJECT_SOURCE_DIR}/src/features/dump/game_dump.cpp"
    "${PROJECT_SOURCE_DIR}/src/features/dump/perf_recorder.cpp" PROPERTIES
    COMPILE_DEFINITIONS "SH_GIT_REVISION=\"${SH_GIT_REVISION}\"")
# The engine side of the game-state API (src/engine/game_state/game_state*.cpp; session.cpp is in sh_gameflow).
file(GLOB _game_state_sources CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/src/engine/game_state/game_state*.cpp")
list(APPEND SH_EXE_SOURCES ${_game_state_sources})
# The first-run library is C++20 and builds as its own target (src/engine/app/first_run).
list(FILTER SH_EXE_SOURCES EXCLUDE REGEX "/src/engine/app/first_run/")
# So is the quick setup shown before it (src/engine/app/quick_setup), and what both draw with (screen_kit).
list(FILTER SH_EXE_SOURCES EXCLUDE REGEX "/src/engine/app/quick_setup/")
list(FILTER SH_EXE_SOURCES EXCLUDE REGEX "/src/engine/app/screen_kit/")
# The text preparation and the fallback fonts are their own library (src/i18n/core, sh_i18n).
list(FILTER SH_EXE_SOURCES EXCLUDE REGEX "/src/i18n/core/")
# The RHI is its own library (src/render/rhi): sh_rhi_gl always, sh_rhi with SH_RHI_SDL3.
list(FILTER SH_EXE_SOURCES EXCLUDE REGEX "/src/render/rhi/")
# The movie formats are their own library (src/media/fmv/codec, sh_fmv); the player stays in the exe.
list(FILTER SH_EXE_SOURCES EXCLUDE REGEX "/src/media/fmv/codec/")
# The scene renderer is its own library too (src/render/scene, RENDER.md section 6).
list(FILTER SH_EXE_SOURCES EXCLUDE REGEX "/src/render/scene/")
# The 2D draw list is its own library too (src/render/draw2d, RENDER.md section 5).
list(FILTER SH_EXE_SOURCES EXCLUDE REGEX "/src/render/draw2d/")
list(FILTER SH_EXE_SOURCES EXCLUDE REGEX "/src/render/draw3d/")
list(FILTER SH_EXE_SOURCES EXCLUDE REGEX "/src/render/override_tex/")
list(FILTER SH_EXE_SOURCES EXCLUDE REGEX "/src/render/frame/")
list(FILTER SH_EXE_SOURCES EXCLUDE REGEX "/src/render/shaderstate/")
list(FILTER SH_EXE_SOURCES EXCLUDE REGEX "/src/render/offscreen/")
list(FILTER SH_EXE_SOURCES EXCLUDE REGEX "/src/render/post/")
# The framebuffer feedback logic too (src/render/feedback).
list(FILTER SH_EXE_SOURCES EXCLUDE REGEX "/src/render/feedback/")
list(FILTER SH_EXE_SOURCES EXCLUDE REGEX "/src/render/present/")
list(FILTER SH_EXE_SOURCES EXCLUDE REGEX "/src/render/direct/")
# The primitive renderer on the RHI too (src/render/gpu, RENDER.md section 3).
list(FILTER SH_EXE_SOURCES EXCLUDE REGEX "/src/render/gpu/")
# The 2D overlay and the minimal GL loader are their own libraries too (src/render/overlay, src/render/gl).
list(FILTER SH_EXE_SOURCES EXCLUDE REGEX "/src/render/overlay/")
list(FILTER SH_EXE_SOURCES EXCLUDE REGEX "/src/render/gl/")
list(FILTER SH_EXE_SOURCES EXCLUDE REGEX "/src/engine/loader/queue/")

# =============================================================================
# Executable
# =============================================================================

# Windows: compile the .rc so the exe carries an application icon (Explorer,
# taskbar, title bar). windres resolves the ICON path relative to the .rc dir.
if(WIN32)
    enable_language(RC)
    # windres resolves the ICON path relative to its include dirs, not the .rc
    # location: assets/icon/ for app.ico (the project's one icon directory,
    # assets/icon/generate.sh), the binary dir for the generated sh_version.h.
    set(CMAKE_RC_FLAGS "${CMAKE_RC_FLAGS} -I \"${PROJECT_SOURCE_DIR}/assets/icon\" -I \"${CMAKE_BINARY_DIR}\"")
    set(SH_APP_RESOURCES "${PROJECT_SOURCE_DIR}/assets/res/app.rc")
    # The .rc embeds app.ico and includes the generated sh_version.h;
    # neither is visible to the generator's dep scan, so list them explicitly.
    # sh_version.h is rewritten every build, so the version resource (and only
    # the resource) recompiles each build.
    set_source_files_properties(${SH_APP_RESOURCES} PROPERTIES
        OBJECT_DEPENDS "${PROJECT_SOURCE_DIR}/assets/icon/app.ico;${CMAKE_BINARY_DIR}/sh_version.h")
endif()

# On Linux the exe finds the shared libraries shipped beside it (the project's OpenAL Soft of
# tools/linux-deps/build.sh, when the build uses it) through $ORIGIN, in the build folder and in a package alike:
# no absolute path of the build machine is written into the binary.
if(UNIX AND NOT APPLE)
    set(CMAKE_BUILD_WITH_INSTALL_RPATH ON)
    set(CMAKE_INSTALL_RPATH "$ORIGIN")
endif()

# Lua 5.4 — vendored under third_party/lua/src, compiled into a static lib for
# the script host (src/engine/script). The standalone programs (lua.c/luac.c) and the
# amalgamation/test files are not vendored, so a plain src/*.c glob is safe. The
# generic ISO-C luaconf (no LUA_USE_* platform define) keeps it portable and
# sandboxes out dynamic C-library loading, which user scripts must not do.
set(SH_LUA_DIR "${PROJECT_SOURCE_DIR}/third_party/lua")
if(EXISTS "${SH_LUA_DIR}/src/lua.h")
    file(GLOB LUA_SOURCES "${SH_LUA_DIR}/src/*.c")
    add_library(lua_static STATIC ${LUA_SOURCES})
    target_include_directories(lua_static PUBLIC "${SH_LUA_DIR}/src")
    if(NOT MSVC)
        target_compile_options(lua_static PRIVATE -w)
    endif()
    set(SH_HAVE_LUA ON)
else()
    set(SH_HAVE_LUA OFF)
    message(STATUS "Lua scripting: disabled (third_party/lua missing)")
endif()

if(ANDROID)
    # SDL's activity (android/) loads libmain.so and calls SDL_main, which SDL_main.h makes of main().
    add_library(SilentHillPC SHARED ${SH_EXE_SOURCES})
    set_target_properties(SilentHillPC PROPERTIES OUTPUT_NAME main)
else()
    add_executable(SilentHillPC
        ${SH_EXE_SOURCES}
        ${SH_APP_RESOURCES}
    )
endif()

# The executable exports nothing: data names its records by readable names the engine binds at start
# (sh::maps::map_globals), never by a symbol of the program.
if(MINGW)
    # Build as a GUI (windows) subsystem app so Windows doesn't spawn a console
    # window at launch. We keep the standard C entry (SDL_main.h is not used, so
    # main() is the real entry); force -emainCRTStartup because the windows
    # subsystem default entry is WinMainCRTStartup, which we don't provide.
    # External-console mode calls AllocConsole() at runtime (see src/engine/app/game_main.cpp).
    target_link_options(SilentHillPC PRIVATE "-Wl,--subsystem,windows" "-Wl,-emainCRTStartup")
endif()

# Shipped runtime assets: assets/runtime/ -> <build>/assets/ (the engine's own
# files: fonts, languages, icons, builtin/), the other entries of assets/ next to
# the exe. The release packaging (tools/ci/package.sh) ships the same directory,
# so a custom asset is added to assets/, never to the build folder.
# The script skips config.cfg (copied at configure time below) and res/.
add_custom_command(TARGET SilentHillPC POST_BUILD
    COMMAND ${CMAKE_COMMAND}
            -DSRC_DIR=${PROJECT_SOURCE_DIR}/assets
            -DDST_DIR=$<TARGET_FILE_DIR:SilentHillPC>
            -P ${PROJECT_SOURCE_DIR}/cmake/copy_assets.cmake
    COMMENT "Copying shipped runtime assets (assets/)"
)

# The application icon (assets/icon/, the one place of everything about it): the sizes go to <exe>/assets/icon/
# for the window icon and the desktop entry. On Linux the menu entry's template and its install script go beside
# the exe (assets/linux/; SilentHillPC --install-desktop does the same from the compiled-in template).
file(GLOB SH_APP_ICON_FILES CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/assets/icon/app_*.png")
add_custom_command(TARGET SilentHillPC POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:SilentHillPC>/assets/icon
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${SH_APP_ICON_FILES} $<TARGET_FILE_DIR:SilentHillPC>/assets/icon
    COMMENT "Copying the application icon"
)
if(UNIX AND NOT APPLE)
    add_custom_command(TARGET SilentHillPC POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${PROJECT_SOURCE_DIR}/assets/linux/silent-hill-pc.desktop.in
                ${PROJECT_SOURCE_DIR}/assets/linux/install-desktop.sh
                $<TARGET_FILE_DIR:SilentHillPC>
        COMMENT "Copying the desktop entry template"
    )
endif()

# The project's own OpenAL Soft (LGPL, a replaceable shared library) goes beside the exe, which finds it through
# $ORIGIN, with the license texts of that build.
if(SH_LINUX_DEPS_PREFIX)
    file(GLOB _sh_linux_deps_licenses "${SH_LINUX_DEPS_PREFIX}/share/sh-licenses/*")
    add_custom_command(TARGET SilentHillPC POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${SH_LINUX_DEPS_PREFIX}/lib/libopenal.so.1" "$<TARGET_FILE_DIR:SilentHillPC>"
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:SilentHillPC>/licenses"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${_sh_linux_deps_licenses} "$<TARGET_FILE_DIR:SilentHillPC>/licenses"
        COMMENT "Copying the project's OpenAL Soft beside the exe"
    )
endif()

# The license texts of tools/macos-deps/build.sh (libjpeg-turbo is linked statically; FFmpeg and ncnn copy their
# own), and MoltenVK beside the exe, where renderer = vulkan opens it (rhi::loadVulkanLibrary).
if(SH_MACOS_DEPS_PREFIX)
    file(GLOB _sh_macos_deps_licenses "${SH_MACOS_DEPS_PREFIX}/share/sh-licenses/*libjpeg*"
         "${SH_MACOS_DEPS_PREFIX}/share/sh-licenses/*MoltenVK*")
    if(EXISTS "${SH_MACOS_DEPS_PREFIX}/lib/libMoltenVK.dylib")
        add_custom_command(TARGET SilentHillPC POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${SH_MACOS_DEPS_PREFIX}/lib/libMoltenVK.dylib" "$<TARGET_FILE_DIR:SilentHillPC>"
            COMMENT "Copying MoltenVK beside the exe"
        )
    endif()
    add_custom_command(TARGET SilentHillPC POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:SilentHillPC>/licenses"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${_sh_macos_deps_licenses} "$<TARGET_FILE_DIR:SilentHillPC>/licenses"
        COMMENT "Copying the licenses of the project's macOS dependencies"
    )
endif()

# The console's bundled monospace font (third_party/fonts/jetbrains-mono, SIL OFL 1.1) goes next to the exe.
add_custom_command(TARGET SilentHillPC POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:SilentHillPC>/fonts
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${PROJECT_SOURCE_DIR}/third_party/fonts/jetbrains-mono/JetBrainsMonoNL-Regular.ttf
            ${PROJECT_SOURCE_DIR}/third_party/fonts/jetbrains-mono/OFL.txt
            $<TARGET_FILE_DIR:SilentHillPC>/fonts
    COMMENT "Copying the console font"
)

# Base game data of the repository: gamedata/items/ etc. deploy into <build>/gamedata/,
# beside what the first-run import writes there (raw/, convert/, tables/, maps/). copy_if_different
# per file, so the import's files and hand edits are left alone. A target of its own that runs on
# every build: a POST_BUILD step would run only when the exe is relinked, so a data-only change
# (a menu, a character file) would never reach the build folder.
add_custom_target(deploy_gamedata ALL
    COMMAND ${CMAKE_COMMAND}
            -DSRC_DIR=${PROJECT_SOURCE_DIR}/gamedata
            -DDST_DIR=$<TARGET_FILE_DIR:SilentHillPC>/gamedata
            -P ${PROJECT_SOURCE_DIR}/cmake/copy_gamedata.cmake
    COMMENT "Copying base game data (gamedata/)"
    VERBATIM
)
add_dependencies(deploy_gamedata SilentHillPC)

# Windows dependency DLLs from tools/win-deps/build.sh (the mingw toolchain file
# sets SH_WIN_DEPS_PREFIX) go beside the exe with their license texts. The exe
# imports SDL3.dll and OpenAL32.dll and does not start without them.
if(WIN32 AND SH_WIN_DEPS_PREFIX AND EXISTS "${SH_WIN_DEPS_PREFIX}/bin")
    file(GLOB SH_WIN_DEPS_DLLS "${SH_WIN_DEPS_PREFIX}/bin/*.dll")
    file(GLOB SH_WIN_DEPS_LICENSES "${SH_WIN_DEPS_PREFIX}/share/sh-licenses/*")
    add_custom_command(TARGET SilentHillPC POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${SH_WIN_DEPS_DLLS} "$<TARGET_FILE_DIR:SilentHillPC>"
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:SilentHillPC>/licenses"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${SH_WIN_DEPS_LICENSES} "$<TARGET_FILE_DIR:SilentHillPC>/licenses"
        COMMENT "Copying Windows dependency DLLs (SDL3, OpenAL Soft, FFmpeg)"
    )
    install(FILES ${SH_WIN_DEPS_DLLS} DESTINATION bin)
    # Mesa llvmpipe (tools/win-deps/software_gl.sh) in a folder of its own: the game loads it only when the system
    # has no OpenGL 3.3 driver, so it must not sit beside the exe, where it would replace the system's opengl32.dll.
    if(EXISTS "${SH_WIN_DEPS_PREFIX}/software_gl/opengl32.dll")
        add_custom_command(TARGET SilentHillPC POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                    "${SH_WIN_DEPS_PREFIX}/software_gl" "$<TARGET_FILE_DIR:SilentHillPC>/software_gl"
            COMMENT "Copying Mesa llvmpipe into software_gl/"
        )
        install(DIRECTORY "${SH_WIN_DEPS_PREFIX}/software_gl" DESTINATION bin)
    else()
        message(WARNING "no ${SH_WIN_DEPS_PREFIX}/software_gl (tools/win-deps/software_gl.sh): "
                        "without an OpenGL 3.3 driver the game will not start")
    endif()
endif()

if(SH_DEBUG)
    target_compile_definitions(SilentHillPC PRIVATE SH_DEBUG=1)
endif()

# =============================================================================
# Build identification
# =============================================================================
# Regenerated on EVERY build so the running exe logs exactly which commit
# it is built from (the [SH] build line of the log): a crash log then
# identifies the game build.
add_custom_target(gen_build_info ALL
    COMMAND ${CMAKE_COMMAND}
        -DOUT_FILE=${CMAKE_BINARY_DIR}/sh_build_info.h
        -DVER_FILE=${CMAKE_BINARY_DIR}/sh_version.h
        -DENGINE_VERSION=${PROJECT_VERSION}
        -DGIT_DIR=${PROJECT_SOURCE_DIR}
        -P ${PROJECT_SOURCE_DIR}/cmake/gen_build_info.cmake
    BYPRODUCTS ${CMAKE_BINARY_DIR}/sh_build_info.h ${CMAKE_BINARY_DIR}/sh_version.h
    COMMENT "Generating sh_build_info.h + sh_version.h"
)
add_dependencies(SilentHillPC gen_build_info)
target_include_directories(SilentHillPC PRIVATE ${CMAKE_BINARY_DIR})

# =============================================================================
# Include paths
# =============================================================================
# Roots for #include: src/ and third_party/.
target_include_directories(SilentHillPC PRIVATE
    "${PROJECT_SOURCE_DIR}/src"
    "${PROJECT_SOURCE_DIR}/third_party"
)
# OpenAL headers (the voice player uses AL directly), when the caller points at them
# (macOS Homebrew openal-soft); unset otherwise (no-op).
if(OPENAL_INCLUDE_DIR)
    target_include_directories(SilentHillPC PRIVATE "${OPENAL_INCLUDE_DIR}")
endif()

# =============================================================================
# libjpeg (for FMV playback - MJPG decoding)
# =============================================================================
find_package(PkgConfig QUIET)
find_library(JPEG_LIBRARY NAMES jpeg turbojpeg)
find_path(JPEG_INCLUDE_DIR jpeglib.h)

# =============================================================================
# Linking
# =============================================================================
target_link_libraries(SilentHillPC PRIVATE
    sh_first_run
    sh_quick_setup  # the quick setup before the first start (src/engine/app/quick_setup)
    sh_miniz
    sh_tables  # animation tables from gamedata/tables/ (engine/chara/game/clip_tables.cpp)
    sh_maps    # map overlay data from gamedata/maps/ (src/engine/maps)
    sh_items   # item/weapon data from gamedata/items/ (Inventory_GamedataInit)
    sh_inventory # inventory slots: sort, stack, remove, commands (game hosts in src/engine/inventory/game)
    sh_flags   # event flags by name, gamedata/flags/*.json and the mods
    sh_ids     # the original numbers of named things, read from gamedata/ (engine/ids/game_numbers.h)
    sh_text    # cell-font text layout and the game's text drawer (engine/text/game_text.h)
    sh_screens # staff roll and menu screens, draw records for src/ui/screens
    sh_menu    # the generic menu model, shown by src/ui/menu (pc_debug_menu.cpp)
    sh_devtools # developer tools (free camera) (free camera host in src/engine/app/devtools)
    sh_loop    # the game loop frame clock (driven by src/engine/app/loop)
    sh_gameflow # map event triggers, music mix, arrival, event tweens (game hosts in src/engine/gameflow/game)
    sh_camera  # the engine camera and its C entry points; game host src/engine/camera/game
    sh_chara   # character data from gamedata/chara/
    sh_chara_actions # chara/<Name>/actions.lua hooks (Pc_CharaSfxNamed)
    sh_gconfig # gameplay params from gamedata/config/ (Gameplay_GamedataInit)
    sh_utf8    # UTF-8 core for i18n (decode/validate/advance, font-cell hook)
    sh_world   # map collision from maps/areas/*.glb (src/engine/world, game side src/engine/world/game)
    sh_combat  # attacks, hits, damage, movement step (src/engine/combat, game view in src/engine/chara/game/combat_host.cpp)
    sh_effects # particle effects: hosts in src/engine/effects/game
    sh_loader  # file queue
)
# Game files through the VFS: gamedata/, mods, and the disc host. Declared in
# src/engine/vfs, added further down with the other modules.
target_link_libraries(SilentHillPC PRIVATE sh_vfs_c)
# Cutscenes from convert/cutscenes/ (src/engine/cutscene/cutscene_c.h); declared with the modules below.
target_link_libraries(SilentHillPC PRIVATE sh_cutscene_c)
# Saves in .savegames/ (src/engine/save/save_c.h); declared with the modules below.
target_link_libraries(SilentHillPC PRIVATE sh_save_c sh_save)
# Skinned glb characters of the scene renderer (src/engine/assets/skin_model.h, render/skinning/gltf_chara.cpp).
target_link_libraries(SilentHillPC PRIVATE sh_assets)
# Characters of the engine: skeleton, animation, Character, Lua sh.entity (src/engine/entities).
target_link_libraries(SilentHillPC PRIVATE sh_entities)
# The player of the engine (src/engine/player; hosts in src/engine/entities/game).
target_link_libraries(SilentHillPC PRIVATE sh_player)

# Lua scripting: link the vendored interpreter + flag its presence.
if(SH_HAVE_LUA)
    target_link_libraries(SilentHillPC PRIVATE lua_static)
    target_compile_definitions(SilentHillPC PRIVATE SH_LUA=1)
endif()

if(ANDROID)
elseif(TARGET OpenAL::OpenAL)
    target_link_libraries(SilentHillPC PRIVATE OpenAL::OpenAL)
elseif(TARGET OpenAL)
    target_link_libraries(SilentHillPC PRIVATE OpenAL)
else()
    target_link_libraries(SilentHillPC PRIVATE ${OPENAL_LIBRARY})
endif()
if(OpenAL_SOURCE_DIR)
    target_include_directories(SilentHillPC PRIVATE ${OpenAL_BINARY_DIR} ${OpenAL_SOURCE_DIR}/include)
endif()
if(WIN32)
    target_link_libraries(SilentHillPC PRIVATE ole32 ksguid avrt propsys)
endif()

target_link_libraries(SilentHillPC PRIVATE ${SH_SDL3_TARGET})

# After the SDL lookup above: the first-run library links SDL the same way.
add_subdirectory(src/engine/app/screen_kit)
add_subdirectory(src/engine/app/first_run)
add_subdirectory(src/engine/app/quick_setup)

if(JPEG_LIBRARY)
    target_link_libraries(SilentHillPC PRIVATE ${JPEG_LIBRARY})
    if(JPEG_INCLUDE_DIR)
        target_include_directories(SilentHillPC PRIVATE ${JPEG_INCLUDE_DIR})
    endif()
else()
    target_link_libraries(SilentHillPC PRIVATE jpeg)
endif()

# =============================================================================
# ffmpeg (optional FMV fallback: h264/h265/vp9/av1 in mp4/mkv/webm/avi)
# =============================================================================
# Headers only — the libs are resolved at runtime by src/media/fmv/ffmpeg_library.cpp through
# engine/loader/shared_library, so nothing ffmpeg lands in the exe's import table (an import-table
# dependency would stop the exe launching for every user without the DLLs).
# The project's own LGPL build (tools/ffmpeg/build.sh; Windows: tools/win-deps/build.sh)
# is picked up below and its shared libs are copied next to the exe; without it a
# system ffmpeg whose majors match the accepted table works too.
# Missing headers only cost the feature, never the build, so a plain checkout
# compiles.
#
# ffmpeg 6.0 (libavcodec 60) is the floor: the decoder uses AVChannelLayout and
# swr_alloc_set_opts2, which do not exist before ffmpeg 5.1, and 6.0 is the
# oldest release we actually validate against. Newer majors are accepted.
set(SH_FFMPEG_MIN_AVCODEC_MAJOR 60)
set(SH_FFMPEG_PREFIX "" CACHE PATH
    "Prefix holding ffmpeg development headers to build against (uses <prefix>/include)")
# The project's own LGPL build (tools/ffmpeg/build.sh, tools/win-deps/build.sh) is the default
# prefix when it is there; its shared libraries then go beside the exe, where the player looks first.
if(SH_FMV_FFMPEG AND NOT SH_FFMPEG_PREFIX)
    if(WIN32 AND SH_WIN_DEPS_PREFIX)
        set(_sh_ffmpeg_own "${SH_WIN_DEPS_PREFIX}")
    else()
        string(TOLOWER "${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}" _sh_ffmpeg_host)
        set(_sh_ffmpeg_own "${PROJECT_SOURCE_DIR}/third_party/deps/${_sh_ffmpeg_host}")
    endif()
    if(EXISTS "${_sh_ffmpeg_own}/include/libavcodec/avcodec.h")
        set(SH_FFMPEG_PREFIX "${_sh_ffmpeg_own}")
        set(SH_FFMPEG_OWN_BUILD TRUE)
    endif()
endif()
if(SH_FMV_FFMPEG)
    set(FFMPEG_HEADER_DIRS "")
    set(FFMPEG_HEADERS_FOUND FALSE)
    if(SH_FFMPEG_PREFIX)
        # An explicit prefix is a deliberate override (side-installed headers that
        # must win over whatever the system offers), so pkg-config is skipped
        # entirely rather than allowed to contradict it.
        if(EXISTS "${SH_FFMPEG_PREFIX}/include/libavcodec/avcodec.h")
            set(FFMPEG_HEADER_DIRS "${SH_FFMPEG_PREFIX}/include")
            set(FFMPEG_HEADERS_FOUND TRUE)
        else()
            message(STATUS "SH_FFMPEG_PREFIX=${SH_FFMPEG_PREFIX} has no "
                "include/libavcodec/avcodec.h")
        endif()
    else()
        if(PkgConfig_FOUND OR PKG_CONFIG_FOUND)
            pkg_check_modules(FFMPEG
                libavformat>=60 libavcodec>=${SH_FFMPEG_MIN_AVCODEC_MAJOR}
                libavutil>=58 libswscale>=7 libswresample>=4)
        endif()
        if(FFMPEG_FOUND)
            # pkg-config omits -I for compiler-default include dirs, so an EMPTY
            # FFMPEG_INCLUDE_DIRS means "already on the search path", not "missing".
            set(FFMPEG_HEADER_DIRS ${FFMPEG_INCLUDE_DIRS})
            set(FFMPEG_HEADERS_FOUND TRUE)
        else()
            find_path(FFMPEG_INCLUDE_DIR libavcodec/avcodec.h)
            if(FFMPEG_INCLUDE_DIR)
                set(FFMPEG_HEADER_DIRS ${FFMPEG_INCLUDE_DIR})
                set(FFMPEG_HEADERS_FOUND TRUE)
            endif()
        endif()
    endif()

    # The major is ALWAYS read from the header the compiler will actually open,
    # never from pkg-config's claim: a .pc whose includedir points at a different
    # tree reports one version while the compiler silently gets another, and the
    # set the player loads at run time is checked against the header's layout, not the .pc.
    set(_ff_major 0)
    set(_ff_header_root "")
    if(FFMPEG_HEADERS_FOUND)
        foreach(_dir IN LISTS FFMPEG_HEADER_DIRS)
            if(EXISTS "${_dir}/libavcodec/avcodec.h")
                set(_ff_header_root "${_dir}")
                break()
            endif()
        endforeach()
        if(NOT _ff_header_root)
            find_path(FFMPEG_DEFAULT_INCLUDE_DIR libavcodec/avcodec.h)
            if(FFMPEG_DEFAULT_INCLUDE_DIR)
                set(_ff_header_root "${FFMPEG_DEFAULT_INCLUDE_DIR}")
            endif()
        endif()

        # version_major.h since ffmpeg 5.0, version.h before that.
        set(_ff_version_header "${_ff_header_root}/libavcodec/version_major.h")
        if(NOT EXISTS "${_ff_version_header}")
            set(_ff_version_header "${_ff_header_root}/libavcodec/version.h")
        endif()
        if(_ff_header_root AND EXISTS "${_ff_version_header}")
            file(STRINGS "${_ff_version_header}" _ff_major_lines
                 REGEX "^#define[ \t]+LIBAVCODEC_VERSION_MAJOR[ \t]+[0-9]+")
            if(_ff_major_lines)
                list(GET _ff_major_lines 0 _ff_major_line)
                string(REGEX REPLACE "^.*LIBAVCODEC_VERSION_MAJOR[ \t]+([0-9]+).*$" "\\1"
                       _ff_major "${_ff_major_line}")
            endif()
        endif()
        if(_ff_major LESS SH_FFMPEG_MIN_AVCODEC_MAJOR)
            message(STATUS "ffmpeg headers at ${_ff_header_root} are libavcodec major "
                "${_ff_major}; ${SH_FFMPEG_MIN_AVCODEC_MAJOR}+ (ffmpeg 6.0+) required")
            set(FFMPEG_HEADERS_FOUND FALSE)
        endif()
    endif()

    if(FFMPEG_HEADERS_FOUND)
        if(FFMPEG_HEADER_DIRS)
            target_include_directories(SilentHillPC PRIVATE ${FFMPEG_HEADER_DIRS})
        endif()
        target_compile_definitions(SilentHillPC PRIVATE SH_FMV_FFMPEG)
        message(STATUS "ffmpeg FMV fallback ENABLED (runtime-loaded, libavcodec major "
            "${_ff_major}, headers: ${_ff_header_root})")
        # Windows DLLs and licenses travel with the SH_WIN_DEPS_PREFIX block above.
        if(SH_FFMPEG_OWN_BUILD AND NOT WIN32)
            if(APPLE)
                file(GLOB _sh_ffmpeg_libs "${SH_FFMPEG_PREFIX}/lib/lib*.dylib")
                list(FILTER _sh_ffmpeg_libs INCLUDE REGEX
                     "/lib(avformat|avcodec|avutil|swscale|swresample)\\.[0-9]+\\.dylib$")
            else()
                file(GLOB _sh_ffmpeg_libs "${SH_FFMPEG_PREFIX}/lib/lib*.so.*")
                list(FILTER _sh_ffmpeg_libs INCLUDE REGEX
                     "/lib(avformat|avcodec|avutil|swscale|swresample)\\.so\\.[0-9]+$")
            endif()
            file(GLOB _sh_ffmpeg_licenses "${SH_FFMPEG_PREFIX}/share/sh-licenses/*FFmpeg*")
            add_custom_command(TARGET SilentHillPC POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        ${_sh_ffmpeg_libs} "$<TARGET_FILE_DIR:SilentHillPC>"
                COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:SilentHillPC>/licenses"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        ${_sh_ffmpeg_licenses} "$<TARGET_FILE_DIR:SilentHillPC>/licenses"
                COMMENT "Copying the project's FFmpeg (LGPL, mod mp4 FMVs) beside the exe"
            )
            install(FILES ${_sh_ffmpeg_libs} DESTINATION bin)
            message(STATUS "ffmpeg: the project's own build, ${SH_FFMPEG_PREFIX}")
        endif()
    else()
        message(WARNING "SH_FMV_FFMPEG=ON but ffmpeg 6.0+ development headers were not found "
            "(MSYS2: pacman -S mingw-w64-x86_64-ffmpeg; Debian/Ubuntu: apt install libavcodec-dev "
            "libavformat-dev libavutil-dev libswscale-dev libswresample-dev). Building WITHOUT the "
            "ffmpeg FMV fallback; mp4/mkv/webm overrides will be ignored.")
    endif()
endif()

# FMV picture shaders for the RHI (src/media/fmv/fmv_renderer.cpp).
include(${PROJECT_SOURCE_DIR}/src/media/fmv/fmv_shaders.cmake)

# The profiler's process and GPU figures on macOS (src/platform/process_stats.cpp).
if(APPLE)
    target_link_libraries(SilentHillPC PRIVATE "-framework IOKit" "-framework CoreFoundation")
endif()
if(WIN32)
    target_link_libraries(SilentHillPC PRIVATE opengl32 winmm m)
elseif(ANDROID)
    # The system log (stdout and stderr go there) and the native activity's asset and window calls.
    target_link_libraries(SilentHillPC PRIVATE android log)
elseif(UNIX)
    find_package(OpenGL REQUIRED)
    target_link_libraries(SilentHillPC PRIVATE OpenGL::GL)
endif()

# =============================================================================
# Compiler flags
# =============================================================================
if(MSVC)
    target_compile_options(SilentHillPC PRIVATE
        /W3
        /wd4996  # deprecated functions
        /wd4244  # conversion warnings
        /wd4267  # size_t conversions
    )
else()
    target_compile_options(SilentHillPC PRIVATE
        -Wall
    )
endif()

# =============================================================================
# Config and resources next to the exe
# =============================================================================
# Only copy config.cfg if it doesn't already exist (preserves user edits)
if(NOT EXISTS "${CMAKE_BINARY_DIR}/config.cfg")
    file(COPY "${PROJECT_SOURCE_DIR}/assets/config.cfg" DESTINATION "${CMAKE_BINARY_DIR}")
endif()

# =============================================================================
# Maps
# =============================================================================
# Every map is data (gamedata/maps/<map>/) and the global character pool's behaviour list is
# gamedata/npc/rules.json: no map library is built or loaded.

# =============================================================================
# Modules and tests
# =============================================================================
# Each module declares its own sh_<name> target in its CMakeLists.txt; the root
# only connects the ones that exist.
# The texture upscaler (tools/sh-upscale) finds ncnn in the dependency prefix of the platform (tools/ncnn/build.sh).
if(WIN32 AND SH_WIN_DEPS_PREFIX)
    set(_sh_ncnn_default "${SH_WIN_DEPS_PREFIX}")
else()
    string(TOLOWER "${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}" _sh_ncnn_host)
    set(_sh_ncnn_default "${PROJECT_SOURCE_DIR}/third_party/deps/${_sh_ncnn_host}")
endif()
set(SH_UPSCALE_NCNN_PREFIX "${_sh_ncnn_default}" CACHE PATH "Prefix holding ncnn for sh-upscale (tools/ncnn/build.sh)")
foreach(_module IN ITEMS
        src/formats/common src/formats/disc src/formats/iso9660 src/formats/archive
        src/engine/version src/engine/desktop src/engine/manifest src/engine/vfs src/engine/mods src/engine/hooks src/engine/tables src/engine/log src/engine/crash src/engine/memory
        src/formats/png src/formats/gltf
        src/import src/import/ui
        src/formats/convert/tim src/formats/convert/anim src/formats/convert/cutscene src/formats/convert/archive
        src/formats/convert/model src/formats/convert/audio src/engine/audio
        src/formats/convert/text src/i18n/core src/formats/convert/font src/formats/convert/level src/formats/convert/mapdata src/formats/convert/filetable src/engine/maps src/engine/world src/engine/combat
        src/engine/items src/engine/script src/formats/convert/materials src/engine/chara src/engine/gconfig src/engine/utf8 src/engine/input
        src/engine/assets src/engine/entities src/engine/player src/engine/camera src/engine/inventory
        src/engine/console src/engine/cutscene src/engine/save src/engine/flags src/engine/ids src/engine/text src/engine/screens src/engine/menu src/engine/loop src/engine/devtools src/engine/gameflow src/engine/effects src/engine/loader/queue
        src/media/fmv/codec
        tools/sh-import tools/sh-audio-check tools/sh-upscale)
    if(EXISTS "${PROJECT_SOURCE_DIR}/${_module}/CMakeLists.txt")
        add_subdirectory(${_module})
    endif()
endforeach()
# The movie formats (src/media/fmv/codec): the player in src/media/fmv plays through them.
target_link_libraries(SilentHillPC PRIVATE sh_fmv)
# The journal is a module of its own.
target_link_libraries(SilentHillPC PRIVATE sh_log sh_console sh_mods_console sh_hooks sh_hooks_console)
# The report of a fatal fault (src/engine/crash/crash_handler.h).
target_link_libraries(SilentHillPC PRIVATE sh_crash)
# The work buffers the game loads its files into (src/engine/memory/work_buffers.h).
target_link_libraries(SilentHillPC PRIVATE sh_memory)
# Config keys kept by name for the modding tools (src/engine/mods/extra_config.h).
target_link_libraries(SilentHillPC PRIVATE sh_extra_config)
# Keyed game text, gamedata/text/ and the mods' text/ (src/i18n/text_bridge.cpp).
target_link_libraries(SilentHillPC PRIVATE sh_convert_text sh_convert_font sh_mods sh_i18n)
# The engine version and the build identification (src/engine/version/engine_version.h).
target_link_libraries(SilentHillPC PRIVATE sh_engine_version)
# The window icon and the Linux menu entry (src/engine/desktop).
target_link_libraries(SilentHillPC PRIVATE sh_desktop)
# The file table from gamedata/tables/filetable.json (engine/loader/queue/disc_files.cpp).
target_link_libraries(SilentHillPC PRIVATE sh_convert_filetable)
target_link_libraries(SilentHillPC PRIVATE sh_input)
# The modding tools' UI toolkit (src/features/tools).
target_link_libraries(SilentHillPC PRIVATE sh_imgui)
# The Lua script host (gamedata/scripts/): linked when the vendored Lua built it.
if(TARGET sh_script)
    target_link_libraries(SilentHillPC PRIVATE sh_script sh_player_script)
endif()
# sh_rhi_gl and sh_rhi (SDL3 GPU) always: the game's splits go through them (RENDER.md section 2).
add_subdirectory(src/render/rhi)
target_link_libraries(SilentHillPC PRIVATE sh_rhi)
# The engine's 2D overlay (UI and debug drawers) on the RHI.
add_subdirectory(src/render/overlay)
target_link_libraries(SilentHillPC PRIVATE sh_overlay)
# The engine's scene renderer: sh_gpu draws it at the scene point of the frame.
add_subdirectory(src/render/scene)
target_link_libraries(sh_render INTERFACE sh_overlay)
add_subdirectory(src/render/feedback)
# The engine's 2D draw list: sh_gpu draws its commands at their place in the frame.
add_subdirectory(src/render/draw2d)
add_subdirectory(src/render/frame)
add_subdirectory(src/render/offscreen)
add_subdirectory(src/render/shaderstate)
target_link_libraries(sh_render INTERFACE sh_shaderstate)
target_link_libraries(sh_render INTERFACE sh_frame)
add_subdirectory(src/render/post)
target_link_libraries(sh_render INTERFACE sh_post)
# Present-path math (src/render/present): aspect, scaled size, present rect, point-sampled texture set.
add_subdirectory(src/render/present)
target_link_libraries(sh_render INTERFACE sh_draw2d)
# The engine's world-space effect list, drawn the same way.
add_subdirectory(src/render/draw3d)
add_subdirectory(src/render/override_tex)
target_link_libraries(sh_render INTERFACE sh_draw3d)
# The engine's primitive renderer on the RHI (RENDER.md section 3).
add_subdirectory(src/render/gpu)
target_link_libraries(SilentHillPC PRIVATE sh_gpu sh_loop)

enable_testing()
# The unit tests run on the build machine; an Android build has nothing to run them on.
if(EXISTS "${PROJECT_SOURCE_DIR}/tests/CMakeLists.txt" AND NOT ANDROID)
    add_subdirectory(tests)
endif()

# =============================================================================
# Install
# =============================================================================
install(TARGETS SilentHillPC RUNTIME DESTINATION bin)
