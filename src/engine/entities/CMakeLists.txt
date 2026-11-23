# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 Vladislav Tislenko (keklick1337)
#
# sh_entities: characters of the engine - skeleton, animation, Character, EntityWorld.
add_library(sh_entities STATIC
    fixed_transform.cpp
    bind_pose.cpp
    animation_set.cpp
    anim_player.cpp
    character.cpp
    entity_world.cpp
    console_character.cpp
    npc_rules.cpp
    npc_director.cpp
    remains.cpp
    entity_script.cpp
    anim_bank.cpp)

target_compile_features(sh_entities PUBLIC cxx_std_20)
target_include_directories(sh_entities PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(sh_entities PUBLIC sh_convert_anim sh_chara sh_common)
target_compile_options(sh_entities PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:-Wall -Wextra -Wpedantic>)
