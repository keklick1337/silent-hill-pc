// SPDX-License-Identifier: GPL-3.0-or-later
// The fog is the draw distance (docs/RENDER.md section 7.7): the view depth past which the world's fog, as the scene
// and prim shaders apply it, hides everything, so nothing beyond it needs drawing, lighting or casting a shadow.
#pragma once

#include "render/rhi/prim_batch.h"

namespace sh::scene {

// The draw cutoff lies this share past the depth where the fog is complete, so rounding, the ramp's interpolation and
// a box that reaches just in front of it never cut what still shows a trace.
inline constexpr float kFogCutoffMargin = 0.15f;

// No cutoff: the fog never hides everything (it is off, its strength or intensity leaves light through, the map is
// dark without fog), so what is drawn is the game's own draw distance.
inline constexpr float kNoFogCutoff = 3.0e38f;

// The view depth (PS1 view units) from which every point is in full fog as scene.frag and prim_batch.frag shade it:
// the ramp (`fog.params` y: entries per view unit, z: intensity) times `strength` (the fog's alpha) reaching 1 for
// good, the soft fog's knee (full at 1 as well) and, with `soft_fog`, its fade to full at the cull distance
// (params w, P4); the classic fog takes the per-vertex byte (full at 127). kNoFogCutoff when the fog never is full.
float fog_full_depth(const rhi::FogRampUniforms& fog, float strength, bool soft_fog);

// fog_full_depth with kFogCutoffMargin: the depth past which a whole surface, model or light reach is not drawn.
float fog_draw_cutoff(const rhi::FogRampUniforms& fog, float strength, bool soft_fog);

}  // namespace sh::scene
