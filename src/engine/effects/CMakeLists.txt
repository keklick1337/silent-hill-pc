# SPDX-License-Identifier: GPL-3.0-or-later
#
# sh_effects: particle and weapon effects written on the engine.
# The module decides what an effect draws and reports draw records (effect_prims.h) through
# a host; the game's effect bridges turn the records into primitives. Data:
# gamedata/effects/weapon_beams.json.
add_library(sh_effects STATIC
    acid.cpp
    combat_effects.cpp
    combat_effects_params.cpp
    effects_file.cpp
    effect_textures.cpp
    glass.cpp
    ground_morph.cpp
    lake_surface.cpp
    map_effect_frame.cpp
    map_lighting_config.cpp
    map_lights.cpp
    water.cpp
    drain_splash.cpp
    effect_view.cpp
    named_effects_host.cpp
    named_glow.cpp
    named_heat.cpp
    named_ground.cpp
    ending_beams.cpp
    heat_field.cpp
    glow_field.cpp
    hand_flame.cpp
    ground_fx.cpp
    lens_flare.cpp
    fire_pool.cpp
    bolt_pool.cpp
    sfx_mix.cpp
    slot_effects.cpp
    slot_smoke_emitter.cpp
    slot_split_head.cpp
    water_stream.cpp
    water_params.cpp
    insects.cpp
    insects_config.cpp
    weapon_beams.cpp
    weather.cpp
    weather_params.cpp)

target_compile_features(sh_effects PRIVATE cxx_std_20)
target_include_directories(sh_effects PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(sh_effects PRIVATE sh_common sh_mods sh_camera sh_world sh_loop sh_ids)
target_link_libraries(sh_effects PUBLIC sh_draw3d)
target_compile_options(sh_effects PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:-Wall -Wextra -Wpedantic>)
