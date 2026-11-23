add_library(sh_mods STATIC semver.cpp manifest.cpp load_order.cpp registry.cpp mods.cpp mods_c.cpp asset_names.cpp
    asset_files_c.cpp mods_admin.cpp json_merge.cpp data_merge.cpp data_reload.cpp)
target_compile_features(sh_mods PUBLIC cxx_std_20)
target_include_directories(sh_mods PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(sh_mods PUBLIC sh_common sh_engine_version)
target_compile_options(sh_mods PRIVATE $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall;-Wextra;-Wpedantic>)

# The `mods` console command (mods_console.h), apart so sh_mods does not need the console.
add_library(sh_mods_console STATIC mods_console.cpp)
target_compile_features(sh_mods_console PUBLIC cxx_std_20)
target_link_libraries(sh_mods_console PUBLIC sh_mods sh_console)
target_compile_options(sh_mods_console PRIVATE $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall;-Wextra;-Wpedantic>)

# The config keys the game's parser does not know, kept by name for the modding tools (extra_config.h).
add_library(sh_extra_config STATIC extra_config.cpp)
target_compile_features(sh_extra_config PUBLIC cxx_std_20)
target_include_directories(sh_extra_config PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_compile_options(sh_extra_config PRIVATE $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall;-Wextra;-Wpedantic>)
