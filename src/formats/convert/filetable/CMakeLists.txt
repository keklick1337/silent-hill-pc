# sh_convert_filetable: the game's file table from SLUS_007.07 as gamedata/tables/filetable.json,
# and its loader.
add_library(sh_convert_filetable STATIC file_table.cpp)

target_compile_features(sh_convert_filetable PUBLIC cxx_std_20)
target_include_directories(sh_convert_filetable PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(sh_convert_filetable PUBLIC sh_common)
target_compile_options(sh_convert_filetable PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:-Wall -Wextra -Wpedantic>)
