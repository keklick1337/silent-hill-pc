# sh_tables: behaviour tables in gamedata/tables/. The game uses it through the C face sh_tables.h.
# cxx_std_20 is private so that linking it does not raise the language level of the game executable.
add_library(sh_tables STATIC
    anim_table.cpp
    anim_table_registry.cpp
    table_store.cpp
    sh_tables.cpp)

target_compile_features(sh_tables PRIVATE cxx_std_20)
target_include_directories(sh_tables PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(sh_tables PRIVATE sh_common sh_mods)
target_compile_options(sh_tables PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:-Wall -Wextra -Wpedantic>)
