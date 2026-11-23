# ANM <-> sh-anim JSON (lossless), the game's bone arithmetic, and the named clips of anim_clips.json.
# anim_clips.json is edited by hand (tools/scripts/check_anim_clips.py) and compiled in as a byte array.
set(SH_ANIM_CLIPS_JSON ${CMAKE_CURRENT_SOURCE_DIR}/anim_clips.json)
set(SH_ANIM_CLIPS_CPP ${CMAKE_CURRENT_BINARY_DIR}/anim_clips_data.cpp)
add_custom_command(
    OUTPUT ${SH_ANIM_CLIPS_CPP}
    COMMAND ${CMAKE_COMMAND} -DIN=${SH_ANIM_CLIPS_JSON} -DOUT=${SH_ANIM_CLIPS_CPP}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/embed_clips.cmake
    DEPENDS ${SH_ANIM_CLIPS_JSON} ${CMAKE_CURRENT_SOURCE_DIR}/embed_clips.cmake
    VERBATIM)

add_library(sh_convert_anim STATIC
    anm_file.cpp
    anim_json.cpp
    anim_clips.cpp
    anim_converter.cpp
    ${SH_ANIM_CLIPS_CPP})

target_compile_features(sh_convert_anim PUBLIC cxx_std_20)
target_include_directories(sh_convert_anim PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(sh_convert_anim PUBLIC sh_common)
target_compile_options(sh_convert_anim PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:-Wall -Wextra -Wpedantic>)
