# sh-import: command line front end of the importer.
# sh_import_tool holds the logic (options, converter list, report); main.cpp is only the entry point.
add_library(sh_import_tool STATIC
    options.cpp
    converters.cpp
    game_files.cpp
    progress.cpp
    summary.cpp
    tables.cpp
    run.cpp)

target_compile_features(sh_import_tool PUBLIC cxx_std_20)
# Headers are included as "sh-import/options.h"; the module headers as "import/import.h".
target_include_directories(sh_import_tool PUBLIC ${PROJECT_SOURCE_DIR}/tools ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(sh_import_tool PUBLIC
    sh_common sh_import
    sh_convert_tim sh_convert_model sh_convert_audio sh_convert_text sh_convert_font sh_convert_level
    sh_convert_anim sh_convert_cutscene sh_convert_archive
    sh_tables sh_convert_mapdata sh_convert_filetable sh_convert_gamefiles sh_convert_materials sh_audio)
target_compile_options(sh_import_tool PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:-Wall -Wextra -Wpedantic>)

add_executable(sh-import main.cpp)
target_link_libraries(sh-import PRIVATE sh_import_tool)
# The game's names of flags and items (src/engine/ids/game_numbers.h): the
# map data it writes names them as the game's own import does.
if(TARGET sh_ids)
    target_compile_definitions(sh-import PRIVATE SH_IMPORT_GAME_NUMBERS=1)
    target_link_libraries(sh-import PRIVATE sh_ids)
endif()
target_compile_options(sh-import PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:-Wall -Wextra -Wpedantic>)
