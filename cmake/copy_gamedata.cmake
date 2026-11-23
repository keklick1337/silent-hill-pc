# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 Vladislav Tislenko (keklick1337)
#
# POST_BUILD deploy of the repository's gamedata/ (the base game data) into the build
# folder's gamedata/ (cmake -DSRC_DIR=... -DDST_DIR=... -P). The first-run import writes raw/,
# convert/, tables/, maps/ and version.cfg into the same directory; the base data (items/,
# scripts/, ...) comes from the repository and never conflicts with it.
#
# Runs on every build, copy_if_different per file: a deployed base file that differs from the
# repository's is replaced (edit gamedata/ in the repository or ship a mod, not the build copy),
# and files the import wrote are never touched. Nothing is deleted: a file removed from the repository
# stays in the build folder until it is deleted there by hand.

file(GLOB_RECURSE GD_FILES RELATIVE "${SRC_DIR}" "${SRC_DIR}/*")
foreach(gd_file IN LISTS GD_FILES)
    get_filename_component(gd_dir "${gd_file}" DIRECTORY)
    if(gd_dir)
        file(MAKE_DIRECTORY "${DST_DIR}/${gd_dir}")
    endif()
    # In-process copy where CMake has it: this runs on every build, and a process per file made a
    # build with nothing to do take seconds.
    if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.21)
        file(COPY_FILE "${SRC_DIR}/${gd_file}" "${DST_DIR}/${gd_file}" ONLY_IF_DIFFERENT)
    else()
        execute_process(COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${SRC_DIR}/${gd_file}" "${DST_DIR}/${gd_file}"
                RESULT_VARIABLE _rc)
        if(NOT _rc EQUAL 0)
            message(FATAL_ERROR "copy_gamedata: failed to copy ${gd_file} (rc=${_rc})")
        endif()
    endif()
endforeach()
