// Render configuration and per-frame state the game, the console and the options screens write and the renderer
// reads (RENDER.md section 3), grouped by meaning, one accessor per group. The frame driver (frame_driver.h) copies
// it into the renderer's own settings once per frame and publishes the renderer's latches back (PublishedView).
// Field types stay int/float on purpose: the config parser and the console variables bind to them by address.
#pragma once

// C++ linkage even when a header pulls this in from inside an extern "C" block (pc_config.h).
extern "C++" {

namespace sh::render {

// Graphics keys of config.cfg (the options screens and the console change them live).
struct GraphicsSettings {
    int aniso_level = 8;
    int bilinear_filtering = 0;
    int texture_filter = 0;
    int menu_filter = 0;
    int dither = 1;        // psx_dither: the console's ordered dither on 2D and gouraud prims
    int deband = 0;
    int foliage_aa = 0;
    int load_screen_trail = 2;  // 0 off, 1 the PS1 5-bit fade, 2 a short smooth afterimage (load_screen_trail)
    int soft_highlights = 0;
    int water_quality = 1;  // 0 low, 1 medium, 2 high: the level of water.json "quality" (water_quality)
    int model_upscaling = 0;  // 0 off, 1 tessellation, 2 mesh shaders (model_upscaling; native Vulkan only)
    int msaa_samples = 0;
    int depth_buffer = 1;  // pgxp_zbuffer: 3D prims test and write depth
    int flashlight_shadows = 1;
    int dream_feedback = 1;
    int post_process = 0;
    float post_process_intensity = 1.0f;
    int tonemap = 0;
    float tonemap_intensity = 1.0f;
    float brightness = 1.0f;
    float contrast = 1.0f;
    float saturation = 1.0f;
    int calibration_bar = 0;
    int fxaa = 0;
    int image_adjust_scope = 1;
    int scene_frame = 1;
    int render_width = 0, render_height = 0;
    int fullscreen_mode = 0;
    int backend = 0;  // sh::gpu::BackendKind of the `renderer` key (gpu/backend_name.h)
    int vulkan_validation = 0;  // the native Vulkan backend loads the validation layer when it is installed
    int shader_prewarm = 1;     // the native Vulkan backend builds the pipelines of earlier runs at start
    int software_gl = 0;        // sh::gpu::SoftwareGl of the `software_gl` key (gpu/software_gl.h)
    int ray_tracing = 0;        // native Vulkan: the scene's acceleration structures each frame (ray_tracing)
    int rt_shadows = 0;         // with ray_tracing: the flashlight's shadows by rays (rt_shadows)
    int rt_reflections = 0;     // with ray_tracing: reflections on the surfaces gamedata/materials/ make reflect
    int rt_gi = 0;              // with ray_tracing: one bounce of the flashlight (rt_gi)
    int rt_lights = 0;          // with ray_tracing: the map's lights and sun, PBR-lite specular (rt_lights)
    int extra_lights = 0;       // the lamps lights.json places by hand ("extra"), on every renderer (extra_lights)
};

// Picture shape: aspect, widescreen and the world's projection scales.
struct PictureShape {
    int hor_plus = 1;
    int menu_pillarbox = 1;
    int native_render = 0;
    int present_scale = 2;
    float render_scale = 1.0f;
    int ui2d_wide_frames = 0;  // frames left that draw scaled 2D at the wide width
    int widescreen_mode = 1;
    int aspect_raw = 0;
    float crt_aspect_trim = 1.0f;
    float pixel_aspect = 1.0f;
    float world_h_scale = 0.76f;  // 0.872^2: the validated world shape
    float world_v_scale = 1.0f;
    float world_v_shift = 0.0f;
    float world_v_crop_anchor = 0.0f;
    float cutscene_v_scale = 0.0f;
    float cutscene_v_shift = 0.0f;
    int message_v_shift = 0;  // message box lift in 320x240 units (compensates the vertical crop)
    int letterbox_outer = 112;
    int letterbox_inner = 96;
};

// What the game's current frame is; the game sets these while it builds the frame.
struct FrameState {
    int cutscene = 0;
    int item_take = 0;
    int ui_ortho_pass = 0;
    int dither_suppressed = 0;
    int world_3d = 0;          // this frame draws the 3D world in perspective
    int fixed_camera = 0;      // a fixed camera shot is live (the camera host sets it)
    int ui2d_bucket = -1;      // overlay slot holding scaled 2D text, -1 none
    int force_item_depth = 0;
};

// Published by the renderer after each frame (the last on-screen split's view and the target sizes).
struct PublishedView {
    float world_ortho_rect[4] = {-160.0f, 160.0f, -120.0f, 120.0f};
    float world_ortho[4] = {0.0f, 320.0f, 0.0f, 224.0f};
    float world_disp[2] = {320.0f, 224.0f};
    int world_ortho_valid = 0;
    float feedback_wide_scale = 1.0f;
    float feedback_wide_center = 160.0f;
    int scene_width = 0, scene_height = 0;    // scene target size
    int window_width = 0, window_height = 0;  // window size in pixels
};

// Frame feedback and the freeze frame. The one-shot fields are consumed at the frame end.
struct FeedbackControl {
    float damp = 1.0f;
    float damp_blend = 1.0f;
    int exact = 1;
    int screen = 1;
    int hold_frame = 0;      // this present takes no loop pass (one-shot)
    int store_allowed = 0;   // frames the store runs without a reader (one-shot)
    int skip_store = 0;      // this frame protects its pages from the store (one-shot)
    int freeze_capture_hold = 0;
    int present_last_frame = 0;  // frozen: the captured frame is drawn again under this frame's UI
    int smooth_trail = 0;        // this frame's screen copy fades as a smooth afterimage (one-shot)
};

// The flashlight cone, its shadow map and the per-pixel scene light.
struct Flashlight {
    int per_pixel = 0;
    int shadows = 0;
    int shadows_allowed = 0;
    int shadow_map_size = 1024;
    int shadow_filter = 1;  // PCF kernel: 0 = 2x2, 1 = 3x3, 2 = 5x5 shadow texels (shadow_filter)
    float shadow_bias = 0.0018f;
    float shadow_normal_offset = 0.0f;
    float shadow_strength = 1.0f;
    float shadow_fade_distance = 0.0f;
    float shadow_fps_drop = 0.0f;
    int shadow_player = 1;  // the player and what he holds cast into the map (console shadowplayer)
    int active = 0;
    int fps_mode = 0;
    int style = 0;
    float intensity = 1.20f;
    float intensity_fps = 2.10f;
    float size = 3.0f;
    float size_fps = 1.30f;
    float pos[3] = {0.0f, 0.0f, 0.0f};
    float shadow_pos[3] = {0.0f, 0.0f, 0.0f};
    float dir[3] = {0.0f, 0.0f, 1.0f};
    float color[3] = {1.0f, 1.0f, 1.0f};
    float inner_cos = 0.94f;
    float outer_cos = 0.76f;
    float range = 4000.0f;
    float hero_light[4] = {0.0f, 0.0f, 0.0f, 0.0f};
};

struct Fog {
    float color[3] = {0.0f, 0.0f, 0.0f};
    float strength = 1.1f;
    int soft = 0;
};

// Precise vertices: sub-pixel screen positions and a real view depth per vertex instead of the console's integer
// screen grid, plus the depth rules the 3D prims draw with.
struct PreciseVertices {
    int enabled = 0;
    float edge_max = 8192.0f;
    float far_w_clamp = 12288.0f;  // about 48 world units
    float near_z = 16.0f;
    // Projection of the newest vertex: screen offset and projection plane distance.
    float screen_offset_x = 0.0f, screen_offset_y = 0.0f, projection_distance = 1.0f;
    int unquantized_depth = 1;
    float weld_px = 0.0f;
    float weld_w_ratio = 1.04f;
    int debug = 0;  // bits: 16 = skip the exact camera, 32 = unclamped view depth
    int depth_stats = 0;
    int near_clip = 1;
    int world_depth = 1;
    int world_far_bias = 64;
    int actor_depth = 1;
    int poly_size_cull = 1;
    float poly_size_wide_scale = 1.0f;
    // smooth_models: double-precision bone, model and camera transforms for drawing (needs `enabled`).
    int smooth_models = 1;
};

// Whole-town view (the map overview that draws the town past the fog).
struct WholeTownView {
    int reproject_far = 0;  // re-project from unclamped values
    int last_depth = 0;  // unclamped depth of the newest projection
    int probe = 0;
};

struct RenderDebug {
    int mixed_program = 1;  // one program for every texture format (fewer draw calls)
};

namespace detail {
extern GraphicsSettings g_graphics;
extern PictureShape g_picture;
extern FrameState g_frame_state;
extern PublishedView g_published;
extern FeedbackControl g_feedback_control;
extern Flashlight g_flashlight;
extern Fog g_fog;
extern PreciseVertices g_precise;
extern WholeTownView g_whole_town;
extern RenderDebug g_debug;
}  // namespace detail

inline GraphicsSettings& graphics() { return detail::g_graphics; }
inline PictureShape& picture() { return detail::g_picture; }
inline FrameState& frame_state() { return detail::g_frame_state; }
inline PublishedView& published() { return detail::g_published; }
inline FeedbackControl& feedback_control() { return detail::g_feedback_control; }
inline Flashlight& flashlight() { return detail::g_flashlight; }
inline Fog& fog() { return detail::g_fog; }
inline PreciseVertices& precise() { return detail::g_precise; }
inline WholeTownView& whole_town() { return detail::g_whole_town; }
// Drawing uses the double-precision transforms and the unquantised camera (smooth_models with precise vertices).
inline bool smooth_view() { return detail::g_precise.enabled != 0 && detail::g_precise.smooth_models != 0; }
inline RenderDebug& debug_state() { return detail::g_debug; }

// The captures want view-space corner data this frame (per-pixel flashlight, precise vertices or texture filtering); the
// capture then stamps the "world geometry" marker the filtering and the flashlight rely on.
inline bool view_space_wanted() {
    return flashlight().per_pixel || precise().enabled || graphics().texture_filter > 0;
}

}  // namespace sh::render

}  // extern "C++"
