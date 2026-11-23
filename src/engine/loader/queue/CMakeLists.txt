# sh_loader: the file queue of the engine: the ordered requests for files of
# the imported file table, their read and after-load steps and the blocking waits. The game's queue
# and host steps are src/engine/app/loader/game_files.cpp.
add_library(sh_loader STATIC
    file_queue.cpp
    file_source.cpp
    disc_files.cpp
    image_step.cpp
    boot_audio.cpp)

target_compile_features(sh_loader PRIVATE cxx_std_20)
target_include_directories(sh_loader PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(sh_loader PRIVATE sh_common PUBLIC sh_convert_filetable)
target_compile_options(sh_loader PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:-Wall -Wextra -Wpedantic>)
