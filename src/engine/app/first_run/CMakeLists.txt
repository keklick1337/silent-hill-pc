# First run of the game: imports the disc image before the game starts.
# A library of its own: the executable sees only the C header first_run.h, and none of the importer's usage
# requirements reaches it (every dependency is PRIVATE).
add_library(sh_first_run STATIC first_run.cpp picked_document.cpp progress_window.cpp)

target_compile_features(sh_first_run PRIVATE cxx_std_20)
target_include_directories(sh_first_run PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(sh_first_run PRIVATE sh_import_tool sh_import_ui sh_import sh_common sh_convert_mapdata sh_convert_filetable sh_convert_gamefiles sh_convert_level sh_convert_materials sh_desktop)
target_compile_options(sh_first_run PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:-Wall -Wextra -Wpedantic>)

# SDL3 the same way the executable gets it (root CMakeLists.txt).
target_link_libraries(sh_first_run PRIVATE ${SH_SDL3_TARGET} sh_screen_kit sh_i18n)
