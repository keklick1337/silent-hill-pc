add_library(sh_vfs STATIC
    vfs.cpp
    disc_sectors.cpp
    game_files_layer.cpp
)

target_compile_features(sh_vfs PUBLIC cxx_std_20)
target_include_directories(sh_vfs PUBLIC ${PROJECT_SOURCE_DIR}/src)

if(NOT MSVC)
    target_compile_options(sh_vfs PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Only the disc modules that are part of the build are linked.
foreach(sh_dep IN ITEMS sh_common sh_disc sh_iso9660 sh_archive sh_manifest)
    if(TARGET ${sh_dep})
        target_link_libraries(sh_vfs PUBLIC ${sh_dep})
    endif()
endforeach()
# The file table (tables/filetable.json) and the game files of an import without raw/; declared after this folder.
target_link_libraries(sh_vfs PUBLIC sh_convert_filetable sh_convert_gamefiles)

# C facade for the game exe (vfs_c.h). Its C++20 requirement stays private and sh_vfs is linked privately,
# so the exe keeps its own language standard. The disc sector source it installs is declared in
# engine/platform/disc.h and defined in the exe.
add_library(sh_vfs_c STATIC vfs_c.cpp)
target_compile_features(sh_vfs_c PRIVATE cxx_std_20)
target_include_directories(sh_vfs_c PRIVATE ${PROJECT_SOURCE_DIR}/src)
if(NOT MSVC)
    target_compile_options(sh_vfs_c PRIVATE -Wall -Wextra -Wpedantic)
endif()
# sh_mods is declared after this folder; the name is resolved when the build is generated.
# sh_maps_unified (src/engine/maps): the map_format = unified layer.
# sh_assets (src/engine/assets): the anim_source = json layer.
target_link_libraries(sh_vfs_c PRIVATE sh_vfs sh_mods sh_maps_unified sh_assets)
