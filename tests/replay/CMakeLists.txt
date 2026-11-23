# Replay harness tests.
#   replay_tools  the dump comparison on synthetic dumps; no game, a second at most.
#   replay_check  each route in routes/ twice: the dumps must be byte-identical and match
#                 reference/<route>.dump.gz. Needs the disc image (offscreen video; Xvfb only as a
#                 fallback); skipped (77) without it.
find_package(Python3 COMPONENTS Interpreter QUIET)
if(NOT Python3_Interpreter_FOUND)
    message(STATUS "replay: no python3, replay_check and replay_tools are not registered")
    return()
endif()

set(SH_REPLAY_TOOL "${PROJECT_SOURCE_DIR}/tools/replay/sh_replay.py")

add_test(NAME replay_tools COMMAND ${Python3_EXECUTABLE} -I ${SH_REPLAY_TOOL} selftest)
set_tests_properties(replay_tools PROPERTIES LABELS replay TIMEOUT 60)

if(TARGET SilentHillPC)
    add_test(NAME replay_check
        COMMAND ${Python3_EXECUTABLE} -I ${SH_REPLAY_TOOL} check
                --build $<TARGET_FILE_DIR:SilentHillPC>)
    set_tests_properties(replay_check PROPERTIES
        LABELS replay SKIP_RETURN_CODE 77 TIMEOUT 3600 RUN_SERIAL TRUE)
endif()
