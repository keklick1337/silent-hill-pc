// The engine's primitive renderer (docs/RENDER.md section 3): shared types of the seam between the front
// end (what to draw: Frame entries) and the back end (sh::gpu::Renderer on the RHI). No GL type crosses this
// header.
#pragma once

#include "render/rhi/prim_batch.h"

#include <cstdint>
#include <span>
#include <vector>

namespace sh::gpu {

// The Modern flashlight's outer cone cosine at size 1 (the Classic one is calibrated: render::Flashlight::outer_cos),
// and how far past it, as a cosine step, its faint spill reaches (scene.frag ModernCone).
inline constexpr float kModernOuterCos = 0.82f;
inline constexpr float kModernSpillCos = 0.2f;

using Vertex = rhi::PrimVertex;  // 68 bytes, the layout prim_batch.vert reads
using Blend = rhi::BlendMode;

struct Rect {
    std::int16_t x = 0, y = 0, w = 0, h = 0;
    friend bool operator==(const Rect&, const Rect&) = default;
};

// Texture page format of the PS1 GPU, plus the PC hi-res override (an RGBA texture of its own).
enum class TexFormat : std::uint8_t { Clut4, Clut8, Direct16, Rgba32 };

// What the drawing environment of the game says (the PS1 DRAWENV in engine terms).
struct DrawEnv {
    Rect clip;                     // drawing area in video memory
    std::int16_t ofs_x = 0, ofs_y = 0;  // drawing offset
    Rect tex_window;               // texture window
    std::uint16_t tpage = 0;       // texture page word as the game packs it
    bool dither = false;
    bool draw_on_display = false;  // dfe: true = the area is the shown buffer (draw on screen), false = offscreen
    bool clear_bg = false;         // isbg
    std::uint8_t bg_r = 0, bg_g = 0, bg_b = 0;
};

// The display environment (PS1 DISPENV): which video-memory rect is shown.
struct DispEnv {
    Rect disp;
    Rect screen;
    bool interlaced = false;
    bool rgb24 = false;
};

// Which texture a split samples. Registry ids are handed out by sh::gpu::Textures.
struct TextureRef {
    enum class Kind : std::uint8_t { White, Vram, Override, Feedback } kind = Kind::White;
    std::uint32_t id = 0;         // Override / Feedback: registry id
    std::uint32_t normal_id = 0;  // Override: normal map registry id, 0 = none
    friend bool operator==(const TextureRef&, const TextureRef&) = default;
};

// Hi-res override sampling inputs of a split.
struct OverrideInfo {
    std::int16_t width = 0, height = 0;     // native footprint in texels
    std::int16_t offset_x = 0, offset_y = 0;  // tpage origin of the override
    std::int32_t hires_w = 0, hires_h = 0;  // texture size (0 = unknown)
    std::int32_t feedback_screen = 0;       // previous-frame capture sampled by fragment position (code)
    friend bool operator==(const OverrideInfo&, const OverrideInfo&) = default;
};

// World: static world drawn in painter order (depth func ALWAYS under PGXP); WorldFar:
// far-band world of the whole-town view, drawn LEQUAL on its true depth (painter order cannot sort one shared bucket).
enum class DepthMode : std::uint8_t { Disabled, World, Actor, WorldFar };

// One contiguous run of the frame's vertex stream with one state, or a non-vertex step of the frame.
struct Split {
    enum class Kind : std::uint8_t { Prims, Capture, Scene } kind = Kind::Prims;

    // Prims
    Blend blend = Blend::None;
    TexFormat format = TexFormat::Direct16;
    TextureRef texture;
    OverrideInfo override_info;
    std::uint8_t mask_mode = 0;   // drawPrimMode: 0 mask test, 1 mask write (stencil)
    DepthMode depth = DepthMode::Disabled;
    bool mixed = false;           // all formats in one program (the default)
    bool glyph = false;           // text glyph (page_resolve.h)
    const char* label = nullptr;  // debug group label
    std::uint32_t first = 0, count = 0;

    // State of the drawing environment the split was recorded under.
    DrawEnv env;
    DispEnv disp;

    // Scene: scene item (-1 map cells, >= 0 posed mesh), with the offset.
    std::int32_t scene_item = -1;
    float scene_ofs_x = 0.f, scene_ofs_y = 0.f;
    // Capture: the video-memory rect the frame drawn so far is packed into.
    Rect capture;
};

// What one flush draws: the vertex stream and the splits over it, in order.
struct SplitBatch {
    std::vector<Vertex> vertices;
    std::vector<Split> splits;
    bool force_item_depth = false;  // isolated item bracket: opaque first, translucent sorted back to front

    void clear() {
        vertices.clear();
        splits.clear();
    }
};

}  // namespace sh::gpu
