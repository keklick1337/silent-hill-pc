# The game manifest (gamedata/game.json): one reader shared by the game
# executable, the VFS and the journal. Depends only on sh_common's json so it
# can be linked into tools and tests.
add_library(sh_manifest STATIC map_manifest.cpp)
target_compile_features(sh_manifest PUBLIC cxx_std_20)
target_include_directories(sh_manifest PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(sh_manifest PUBLIC sh_common)
if(NOT MSVC)
    target_compile_options(sh_manifest PRIVATE -Wall -Wextra -Wpedantic)
endif()
