# sh_convert_gamefiles: the game's files after the import without raw/ (game_files.h): rebuilt by the lossless
# converters, read from maps/areas/*.glb, or kept in convert/files/.
add_library(sh_convert_gamefiles STATIC game_files.cpp)

target_compile_features(sh_convert_gamefiles PUBLIC cxx_std_20)
target_include_directories(sh_convert_gamefiles PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(sh_convert_gamefiles PUBLIC sh_common
    PRIVATE sh_convert_tim sh_convert_text sh_convert_audio sh_convert_cutscene sh_convert_archive sh_convert_anim
            sh_convert_level sh_convert_filetable)
target_compile_options(sh_convert_gamefiles PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:-Wall -Wextra -Wpedantic>)
