// Present-path math (docs/RENDER.md): what size the scene target gets, where it lands in the window, how wide
// a 2D screen may spread, which pixel aspect makes the picture 4:3, and which textures must stay point-sampled.
#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace sh::present {

struct Size {
    int w = 0, h = 0;
};
struct PlacedRect {
    int x = 0, y = 0, w = 0, h = 0;
};

// Scene target size for a window/render size: scaled by `scale` (non-positive means 1), rounded, at least 1x1.
Size scaled_render_size(int w, int h, float scale);

// The scene target's MSAA sample count for the msaa key: snapped down to 8 / 4 / 2, then halved until the device
// takes it (device_max = Caps::maxSamples); 1 = off.
int msaa_sample_count(int requested, int device_max);

// A supersampled target (`s` larger than the window w x h) shrunk, keeping its aspect, to at most max_pixels pixels
// and max_side on a side (0 = no limit); never below the window size. A target not larger than the window passes.
Size cap_supersampled_size(Size s, int w, int h, long long max_pixels, int max_side);

// How many 4:3 frames wide the window is; 1 when it is 4:3 or narrower, or in native render.
float ui2d_wide_factor(bool native_render, int window_w, int window_h);

struct PresentRectInput {
    int present_w = 0, present_h = 0;   // window pixels the picture may use
    int internal_w = 0, internal_h = 0; // scene target size
    bool native_render = false;
    bool hor_plus = false;              // 3D gameplay frame
    int widescreen_mode = 1;            // 0 pillarbox, 1 Hor+, 2 stretch
    bool menu_pillarbox = true;
};
// Stretch to the window by default; native render is always a centred 4:3 picture, and pillarbox modes keep the
// scene target's aspect.
PlacedRect present_rect(const PresentRectInput& in);

// Where a pointer position lands in the pixels the 2D overlay is laid out in. The pointer arrives in window
// units; the overlay is drawn either straight into the back buffer or into the scene target, which the present
// rect then stretches over the back buffer (a supersampled target is larger than the window).
struct PointerInput {
    int window_w = 0, window_h = 0;       // window size in pointer units
    int drawable_w = 0, drawable_h = 0;   // back buffer size in pixels
    bool overlay_to_window = false;       // the overlay is drawn in back-buffer pixels
    PlacedRect present;                   // the scene target as placed in the back buffer
    int target_w = 0, target_h = 0;       // scene target size
};
struct PointF {
    float x = 0, y = 0;
};
PointF pointer_to_overlay(const PointerInput& in, float x, float y);

struct PixelAspectInput {
    bool raw = false;
    float configured_par = 1.0f;
    float disp_w = 0, disp_h = 0;       // display environment size
    float crt_trim = 1.0f;
    bool hor_plus = false, ui_pass = false, item_take = false, cutscene = false;
    float world_hscale = 1.0f, world_vscale = 1.0f, cutscene_vscale = 0.0f;
};
// Pixel aspect that lands the final on-screen picture on 4:3 (times the CRT trim), dividing out exactly the
// world scale knobs the ortho setup applies for this pass.
float display_pixel_aspect(const PixelAspectInput& in);

// Rect of the picture inside a render target of target_w x target_h. A target wider than 4:3 keeps the full width
// (Hor+, stretch) or pillarboxes (pillarbox mode, 2D screens with menu_pillarbox, unless a 2D screen spreads wide);
// a narrower one always letterboxes, because nothing in the game is framed for a taller view. Native render fills
// the target (its rect is chosen at present). Bottom-left origin: y is the gap below.
struct PictureRectInput {
    int target_w = 0, target_h = 0;
    bool native_render = false;
    bool hor_plus = false;
    int widescreen_mode = 1;
    bool menu_pillarbox = true;
    bool ui2d_wide = false;             // a 2D screen asked for the whole window this frame
};
PlacedRect picture_rect(const PictureRectInput& in);

// Ortho window over the display buffer of an on-screen pass, in display coordinates.
struct OrthoRect {
    float left = 0, right = 0, top = 0, bottom = 0;
};
struct ScreenOrthoInput {
    float disp_w = 0, disp_h = 0;       // display environment size
    int target_w = 0, target_h = 0;     // render target size
    bool hor_plus = false;              // 3D gameplay frame
    bool ui_pass = false, item_take = false, cutscene = false;
    bool aspect_raw = false;
    bool ui2d_wide = false;
    int widescreen_mode = 1;
    float ui2d_wide_factor = 1.0f;
    float world_hscale = 1.0f, world_vscale = 1.0f, cutscene_vscale = 0.0f, vcrop_anchor = 0.0f;
    float pixel_aspect = 1.0f;          // display_pixel_aspect for this pass
};
// The world pass is cropped vertically by the vfov knob and widened by Hor+ (with hfov) in a wide target; the UI
// pass and 2D screens keep the authored frame, except a 2D screen that spreads over the whole window.
OrthoRect screen_ortho(const ScreenOrthoInput& in);

// Column-major orthographic projection with the GL clip space (NDC +Y up, Z in [-1, 1]).
std::array<float, 16> ortho_matrix(float left, float right, float bottom, float top, float znear, float zfar);

// Half-widths of the Hor+ world ortho from its centre: the authored 4:3 frame and the one installed. False (both
// untouched) when no widening is in effect.
struct HalfWidths {
    float authored = 0, installed = 0;
};
bool hor_plus_half_widths(const ScreenOrthoInput& in, HalfWidths* out);

// Where the display buffer [0, disp_w] x [0, disp_h] lands under `ortho` drawn into viewport `vp` (bottom-left
// origin): x0, y0 bottom-left corner, x1, y1 top-right.
struct AreaRect {
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
};
bool display_area(const OrthoRect& ortho, float disp_w, float disp_h, const PlacedRect& vp, AreaRect* out);

// Scissor of a draw area on screen, in target pixels with bottom-left origin; false when the area covers the whole
// display (no scissor). An interlaced display always clips. The area maps onto the whole target: the 2D ortho's
// pixel space, with no widescreen remap, except on a wide 2D screen (ClipInput::wide).
struct ClipInput {
    int clip_x = 0, clip_y = 0, clip_w = 0, clip_h = 0;
    int disp_x = 0, disp_y = 0, disp_w = 0, disp_h = 0;
    bool interlaced = false;
    int target_w = 0, target_h = 0;
    // A 2D screen spread over the whole window: x maps through its ortho, and a clip covering the whole display
    // does not narrow the screen back to 4:3.
    bool wide = false;
    float ortho_left = 0, ortho_right = 0;
};
bool clip_scissor(const ClipInput& in, PlacedRect* out);

// Where the authored display frame [0, disp_w] lands across a target of target_w x target_h drawn with `ortho`
// (full height), in target pixels.
PlacedRect authored_frame_rect(const OrthoRect& ortho, float disp_w, int target_w, int target_h);

// The overlap of two rects (empty when they do not meet).
PlacedRect intersect(const PlacedRect& a, const PlacedRect& b);

// A rect of a bottom-left-origin space of height `space_h` in top-left origin (and back: the flip is its own inverse).
constexpr PlacedRect flip_y(const PlacedRect& r, int space_h) { return {r.x, space_h - r.y - r.h, r.w, r.h}; }

// Textures whose loader chose point sampling (font atlases without gutters); a per-bind filter must not override it.
class NearestTextureSet {
public:
    void mark(std::uint32_t tex);
    bool contains(std::uint32_t tex) const;
private:
    static constexpr std::size_t kCapacity = 256;
    std::vector<std::uint32_t> tex_;
};

} // namespace sh::present
