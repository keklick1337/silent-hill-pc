// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Decision core of the static-world mesh drawer (docs/RENDER.md section 5.3): the far
// drop distance, the per-polygon visibility half-width, the ordering-table depth clamps, and the render options the
// drawer hands to the renderer (smoothing, anti-aliasing, ...). Plain numbers, no game records, no GTE, no packets;
// the game's mesh bridge builds the packets and asks this module what to do.

#pragma once

#include <cstdint>
#include <string_view>

#include "formats/common/status.h"

namespace sh::world {

// Render options of the mesh drawer, from gamedata/config/mesh_draw.json (a mod replaces the file whole). The
// decision core only validates and carries them; the renderer reads them through the bridge. Defaults are the
// original look (everything off).
struct MeshDrawOptions {
    // Texture filtering of world polygons: 0 = nearest (original), 1 = bilinear, 2 = smoothed (bilinear + mip).
    std::int32_t texture_filter = 0;
    // Vertex position jitter of the PS1 look: 1 = keep the original snap, 0 = use the precise projection.
    bool vertex_snap = true;
    // Affine texture warp of the PS1: true = keep, false = perspective-correct.
    bool affine_warp = true;
    // Smooth (Gouraud) lighting of polygons that the original flat-shaded: false = keep original.
    bool smooth_shading = false;
    // Multisample anti-aliasing samples for the scene: 0 = off, else 2/4/8/16.
    std::int32_t msaa_samples = 0;
    // Post-process edge smoothing: 0 = off, 1 = FXAA-like.
    std::int32_t edge_smoothing = 0;
    // Dithering of the 15-bit colour: true = keep the PS1 dither.
    bool dither = true;
    // Draw distance scale in percent (0 or less is 100).
    std::int32_t draw_distance_pct = 100;

    static Result<MeshDrawOptions> parse(std::string_view json);
};

// The base of the per-polygon far drop (view depth units at OT shift 0) and the scale of the draw distance.
inline constexpr std::int32_t kMeshFarBase = 0x79C;

// draw_distance_pct scaling of a base distance; a percentage of 0 or less is 100 (the original's guard).
std::int32_t mesh_far_base(std::int32_t base, std::int32_t draw_distance_pct);

// The per-polygon far drop for ordering-table shift `shift` (arg3 of the drawer: 0 or 1): the scaled base shifted
// by shift + 2; with fog on, the smaller of that and the fog far distance; the whole-map mode lifts it entirely.
std::int32_t mesh_far_limit(std::int32_t draw_distance_pct, std::int32_t shift, bool fog_enabled,
                            std::int32_t fog_far, bool whole_map);

// Half-width of the screen box a polygon must touch to be kept. Without a window the PS1 half-width; with one, the
// Hor+ width (screen_height * win_w / (2 * win_h) * pixel_aspect / hscale) plus a 64.5 margin, never below the PS1
// half-width. Float arithmetic in the order of the original.
std::int32_t mesh_visible_half_width(std::int32_t screen_width, std::int32_t screen_height, std::int32_t win_width,
                                     std::int32_t win_height, float pixel_aspect, float hscale);

// The deepest depth that still lands in the ordering table for shift `shift` when the tag starts `tag_base` buckets in.
std::int32_t ot_last_depth(std::int32_t ot_size, std::int32_t tag_base, std::int32_t shift);

// Clamps a polygon depth so (depth >> shift) >> 2 stays inside the table.
std::int32_t ot_clamp_depth(std::int32_t depth, std::int32_t ot_size, std::int32_t tag_base, std::int32_t shift);

// Whole-map mode: a strictly negative (wrapped) depth is rescued into the last bucket; zero (behind the camera) is
// left for the "<= 0" drop.
std::int32_t whole_map_depth_rescue(std::int32_t depth, std::int32_t ot_size, std::int32_t tag_base,
                                    std::int32_t shift, bool whole_map);

}  // namespace sh::world
