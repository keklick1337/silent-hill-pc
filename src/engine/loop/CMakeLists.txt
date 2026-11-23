# sh_loop: the game loop of the engine: the frame clock that turns
# what a frame took into the simulation step, the frame pacer that waits out the frame, the pad
# tracker that turns the pad reports into the controller state, the soft reset watch, the frame
# profiler, the input gate of the overlays, the vertical blank clock (VSync) and the game's random
# number generator.
# The game's services over these classes are src/engine/app/loop/frame_loop.cpp (on the session).
add_library(sh_loop STATIC
    frame_clock.cpp
    pad_tracker.cpp
    reset_watch.cpp
    frame_pacer.cpp
    frame_profiler.cpp
    input_gate.cpp
    vblank_clock.cpp
    game_random.cpp)

target_compile_features(sh_loop PRIVATE cxx_std_20)
target_include_directories(sh_loop PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_compile_options(sh_loop PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:-Wall -Wextra -Wpedantic>)
