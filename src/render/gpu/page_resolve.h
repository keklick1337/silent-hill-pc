// Texture page resolution of a textured primitive (RENDER.md section 3), shared by every producer (draw2d and
// draw3d commands): the text-glyph test, feedback / scene-scratch detection (16-bit pages over
// the display-buffer band or the live scratch rect), the hi-res override lookup into state().override_texture /
// override_info, and the CLUT drop (a palette row past 511 that no override claims).
#pragma once

#include "render/gpu/split_builder.h"

#include <cstdint>

namespace sh::gpu {

// Host hooks (set by the game side; null = none).
struct PageHooks {
    // Hi-res override for a tpage/CLUT: Textures registry id or 0, with the footprint (any out pointer may be null).
    std::uint32_t (*lookup_override)(int tpage, int clut, int* w, int* h, int* off_x, int* off_y, int* hires_w,
                                     int* hires_h) = nullptr;
    int (*glyph_test)(int tpage, int clut) = nullptr;
    // A primitive samples the display-buffer band this frame: the frame store learns it has a reader, whether that
    // reader blends and its red modulation.
    void (*note_feedback_reader)(bool semi, int r0) = nullptr;
    // Whether the live scene-scratch rect covers the page origin. Null: asked of sh::gpu::renderer()'s Targets.
    bool (*scratch_covers)(int x, int y) = nullptr;
};
PageHooks& page_hooks();

// False: the primitive must not be drawn (`type` names it in the rate-limited CLUTDROP log line).
bool resolve_page(SplitBuilder& out, std::uint16_t tpage, std::uint16_t clut, const char* type);
// The CLUT test alone (a flat quad tests it before latching its page).
bool clut_drops(std::uint16_t tpage, std::uint16_t clut, const char* type);

}  // namespace sh::gpu
