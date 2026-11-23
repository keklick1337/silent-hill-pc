# The engine's sound system: synthesizer, banks and songs from converted data, sound effects,
# sequencer, voice lines, the mixer and its C interface, offline music rendering and the "music" converter of the
# importer. No console types; links only the format libraries below it.
add_library(sh_audio STATIC
    synth.cpp
    synth_tables.cpp
    sound_bank.cpp
    song.cpp
    sequencer.cpp
    effects.cpp
    voice_stream.cpp
    file_player.cpp
    music_player.cpp
    engine.cpp
    sound_api.cpp
    sound_driver.cpp
    disc_drive.cpp
    positional_sound.cpp
    loop_sustain.cpp
    guest_banks.cpp
    music_change.cpp
    sound_rules.cpp
    music_layers.cpp
    music_render.cpp
    music_converter.cpp
    output/device_format.cpp
    output/mix_feed.cpp
    output/status_board.cpp
    output/output_device.cpp
    output/output_settings.cpp
    output/mix_capture.cpp
    output/vblank_sink.cpp
    voice_clip.cpp
    line_start_delay.cpp
    message_voice.cpp)

target_compile_features(sh_audio PUBLIC cxx_std_20)
target_include_directories(sh_audio PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(sh_audio PUBLIC sh_common sh_convert_audio sh_world)
target_compile_options(sh_audio PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:-Wall -Wextra -Wpedantic>)

# sh-music: one song to a WAV with adjustable render settings (modding, comparisons with the game's output).
add_executable(sh-music render_main.cpp)
target_link_libraries(sh-music PRIVATE sh_audio)
target_compile_options(sh-music PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:-Wall -Wextra -Wpedantic>)

# sh_sound_registry: sound effects by readable name (gamedata/sounds/sounds.json). Its own small library:
# the character data and the script host resolve names without linking the synthesizer.
add_library(sh_sound_registry STATIC sound_registry.cpp)
target_compile_features(sh_sound_registry PUBLIC cxx_std_20)
target_include_directories(sh_sound_registry PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(sh_sound_registry PRIVATE sh_common sh_mods)
target_compile_options(sh_sound_registry PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:-Wall -Wextra -Wpedantic>)
