# POST_BUILD mirror of assets/ into the build folder (cmake -DSRC_DIR=... -DDST_DIR=... -P).
# assets/runtime/ goes to <exe>/assets/: the engine's own files (icons). Fonts and text are game data (gamedata/).
# <exe>/gamedata/ is left alone: cmake/copy_gamedata.cmake and the first run's import fill it.
# Every other top-level entry of assets/ is copied next to the exe, except:
#   config.cfg       copied once at configure time, so user edits survive rebuilds;
#   res/             Windows resources, compiled into the exe;
#   icon/, linux/    the application icon and the Linux desktop entry: the root CMakeLists.txt deploys their
#                    run-time files itself (the sources and scripts stay in the repository).
# Globbed at build time, so a new entry added to assets/ is deployed without a reconfigure.

# A -P script starts without the policies of the project; IN_LIST needs CMP0057 (CMake before 4.0 leaves it OLD).
cmake_policy(VERSION 3.16)

file(GLOB ASSET_ENTRIES RELATIVE "${SRC_DIR}" "${SRC_DIR}/*")
set(ASSET_SKIP config.cfg res icon linux)

foreach(entry IN LISTS ASSET_ENTRIES)
    if(entry IN_LIST ASSET_SKIP)
        continue()
    endif()
    set(_dst "${DST_DIR}/${entry}")
    if(entry STREQUAL "runtime")
        set(_dst "${DST_DIR}/assets")
    endif()
    if(IS_DIRECTORY "${SRC_DIR}/${entry}")
        execute_process(COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${SRC_DIR}/${entry}" "${_dst}"
                RESULT_VARIABLE _rc)
    else()
        execute_process(COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${SRC_DIR}/${entry}" "${_dst}"
                RESULT_VARIABLE _rc)
    endif()
    if(NOT _rc EQUAL 0)
        message(FATAL_ERROR "copy_assets: failed to copy ${entry} (rc=${_rc})")
    endif()
endforeach()
