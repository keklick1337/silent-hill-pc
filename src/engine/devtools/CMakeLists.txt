# sh_devtools: developer tools of the engine: the free camera,
# the developer key table and the replay harness.
# The game is connected through the free-camera and developer-key bridges; sh_world
# supplies the fixed-point sine and cosine of the view.
add_library(sh_devtools STATIC
    free_camera.cpp
    dev_keys.cpp
    replay.cpp
    replay_format.cpp
    state_dump.cpp
    perf_record.cpp
    position_snippets.cpp
    map_snippets.cpp
    lua_edit.cpp
    lua_assist.cpp
    lua_ide.cpp
    mod_authoring.cpp)

target_compile_features(sh_devtools PRIVATE cxx_std_20)
target_include_directories(sh_devtools PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(sh_devtools PRIVATE sh_world sh_camera sh_loop sh_common)
target_compile_options(sh_devtools PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:-Wall -Wextra -Wpedantic>)
