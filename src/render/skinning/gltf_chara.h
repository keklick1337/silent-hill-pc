/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PC_GLTF_CHARA_H
#define PC_GLTF_CHARA_H

/* Characters drawn by the engine's scene renderer (RENDER.md section 6) from their skinned
 * glb: the imported convert/models/CHARA/<NAME>.glb, or a mod's copy -- an HD model with the importer's joint names
 * (boneNN). The game poses its bones as always; every vertex follows them through the file's skin weights
 * (engine/assets/skin_model.h), is lit with the engine's per-bone light (engine/world/bone_light.h) and handed to the
 * scene as one view-space mesh per material. A mesh node named like a part of the disc model is drawn only while the
 * skeleton shows that part (Harry's hand variants); other nodes always. This is the only character drawer: a
 * character without a usable file is not drawn.
 *
 * Plain C types only: the game's character draw calls this inside its C ABI region. The frame handles and records
 * are opaque here; the renderer reads them through render/game_view.h. */

#ifdef __cplusplus
extern "C" {
#endif

/* Draws the character whose skeleton is `skeleton` (bone frames from `boneFrames`) into ordering table `ot` at the
 * depth its parts would have had (`shift`: the table's depth shift). `hideHead` leaves out the head (first-person
 * camera). */
void Pc_GltfChara_Draw(const void* skeleton, void* boneFrames, void* ot, int shift, int hideHead);

/* A model with its own proportions (its glb's asset extras {"proportions": "model"}) keeps the lengths of its own
 * bones: the game's clips turn them, the rest offset of every bone the clips do not move comes from the file's bind
 * pose. `out` gets how far the model's joint of `bone` is from the game's (world axes, geometry units), for whatever
 * is drawn on that bone besides the model (the item in the hand). Returns 0 with zeros for a model on the game's
 * skeleton as it is. */
int Pc_GltfChara_BoneShift(const void* skeleton, void* boneFrames, int bone, int out[3]);

#ifdef __cplusplus
}

#include <functional>
#include <string>

namespace sh::gltf_chara {
// A better picture of loaded character textures arrived (texture_upscale): the textures whose file `match` accepts
// (its generic path) are read again and handed to the scene again, texture by texture.
void refresh_textures(const std::function<bool(const std::string& path)>& match);
}  // namespace sh::gltf_chara
#endif

#endif
