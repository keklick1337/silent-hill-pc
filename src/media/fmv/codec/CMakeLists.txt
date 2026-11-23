# sh_fmv: the movie formats without the game: the disc movies' stream, picture
# and sound decoders, the AVI reader and the uncompressed AVI pictures, the movie list and the choice of a
# replacement file. The player itself (src/media/fmv/*.cpp) is part of the exe.
add_library(sh_fmv STATIC
    mdec_decoder.cpp
    str_stream.cpp
    avi_file.cpp
    raw_video.cpp
    pcm.cpp
    movie_list.cpp
)
target_compile_features(sh_fmv PUBLIC cxx_std_20)
target_include_directories(sh_fmv PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(sh_fmv PUBLIC sh_common sh_convert_audio)
target_compile_options(sh_fmv PRIVATE -Wall -Wextra -Wpedantic)
