# sh_assets: the engine on the converted formats of gamedata/convert/.
#   anim_layer   ANIM/*.ANM served from convert/anim/**/*.anim.json (anim_source = json)
#   glb_anims    ANM files packed into the character glbs (SH_anim_files)
#   skin_model   skinned glTF characters (convert/models/CHARA/*.glb, HD models of mods) for model_source = gltf
#   texture_upscale  the upscaled pictures of the imported textures (texture_upscale, tools/sh-upscale)
add_library(sh_assets STATIC
    anim_layer.cpp
    glb_anims.cpp
    skin_model.cpp
    texture_upscale.cpp)

target_compile_features(sh_assets PUBLIC cxx_std_20)
target_include_directories(sh_assets PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(sh_assets PUBLIC sh_vfs sh_convert_anim sh_gltf sh_png sh_common)
target_compile_options(sh_assets PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:-Wall -Wextra -Wpedantic>)
