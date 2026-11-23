# sh-audio-check: the engine's sound output without the game: opens
# a sink as the game does, plays silence or a tone and reports what the device took. Built from the same sink
# sources as the game.
add_executable(sh-audio-check
    main.cpp
    ${PROJECT_SOURCE_DIR}/src/engine/audio/output/device/platform_sinks.cpp
    ${PROJECT_SOURCE_DIR}/src/engine/audio/output/device/sdl_sink.cpp
    ${PROJECT_SOURCE_DIR}/src/engine/audio/output/device/wasapi_sink.cpp)
target_compile_features(sh-audio-check PRIVATE cxx_std_20)
target_include_directories(sh-audio-check PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(sh-audio-check PRIVATE sh_audio sh_log ${SH_SDL3_TARGET})
if(WIN32)
    target_link_libraries(sh-audio-check PRIVATE ole32 ksguid avrt propsys)
endif()
target_compile_options(sh-audio-check PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:-Wall -Wextra -Wpedantic>)
