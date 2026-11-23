// Frame feedback (RENDER.md section 3): the PS1 effects that sample what was drawn earlier (dream blur,
// scene scratch, motion trails) read video memory the GPU drew into. The PC draws the scene into Targets, so the
// frame is packed back into the video-memory texture rect when the game would read it, and the previous frame is
// kept for screen-space sampling. The geometry is in src/render/feedback.
//
// Three layers: FeedbackLogic decides (pure, no device), FeedbackGpu records the passes on any rhi::Device, and
// FeedbackPass wires both to the Renderer (Targets latches, Vram hooks, the Textures registry).
//
// Orientation conventions (both backends): a capture texture holds the area with its BOTTOM row at storage row 0
// (the GL layout prim_batch.frag's feedback sampling expects); the pack writes video-memory
// rows top first. The video-memory texture is RG8 holding 16-bit words (low byte in R), so a pack renders
// 5:5:5:1 words into an RG8 scratch target and copies the rect into it.
#pragma once

#include "render/feedback/feedback_geometry.h"
#include "render/gpu/gpu_types.h"
#include "render/post/post.h"
#include "render/rhi/prim_batch.h"
#include "render/rhi/rhi.h"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace sh::gpu {

class Renderer;

// Config knobs and game state of the feedback logic. The bridge fills them.
struct FeedbackSettings {
    bool dream_feedback = true;     // render::graphics().dream_feedback (dream_blur)
    bool screen = true;             // render::feedback_control().screen (console FBSCREEN): render-resolution capture
    bool exact = true;              // render::feedback_control().exact (console FBEXACT): exact PS1 loop for opaque readers
    float damp = 1.f;               // render::feedback_control().damp: loop gain of an opaque reader
    float damp_blend = 1.f;         // render::feedback_control().damp_blend: loop gain of a blending reader
    bool freeze_capture_hold = false;  // render::feedback_control().freeze_capture_hold: keep the frozen frame (pause sub-screens)
};

// Inputs of the pack shader (std140 block FeedbackPackFs of shaders/feedback_pack.frag).
enum class PackMode : int { Scaled = 0, Exact = 1, Truncate = 2 };
struct PackParams {
    float gain = 1.f;
    bool mask = false;     // set the STP bit on every non-black word
    PackMode mode = PackMode::Scaled;
    bool decay = false;    // Exact: drop one 5-bit level (a 127/128 modulation frame)
    friend bool operator==(const PackParams&, const PackParams&) = default;
};

// One video-memory rect to pack and the capture it is packed from.
enum class CaptureSlot : std::uint8_t { Pack, Scratch };
struct PackStep {
    CaptureSlot source = CaptureSlot::Pack;
    Rect rect;
    PackParams params;
};

// One capture of the scene target into a capture slot.
struct CaptureStep {
    CaptureSlot slot = CaptureSlot::Pack;
    int w = 0, h = 0;
    render::feedback::CaptureBlit blit;  // source rect (GL bottom-left scene pixels) -> destination texels
    bool nearest = true;
};

// What one feedback operation does on the device, in order: captures, then blanks, then packs.
struct FeedbackPlan {
    std::vector<CaptureStep> captures;
    std::vector<Rect> clears;
    std::vector<PackStep> packs;
    bool tick_redirect = false;  // tick the scene-scratch redirect afterwards (once per present)
    bool empty() const { return captures.empty() && clears.empty() && packs.empty(); }
};

// The frame and view state a decision reads (Targets::latches(), the scene redirect, the per-frame flags).
struct FeedbackView {
    render::feedback::FRect world_area;
    bool world_area_valid = false;
    render::feedback::FRect ui_area;
    bool ui_area_valid = false;
    render::feedback::IRect present_viewport;
    int target_w = 0, target_h = 0;
    float wide_scale = 1.f;
    // World ortho wider than the display buffer (Hor+): a scene-scratch capture spans it.
    bool world_wide = false;
    std::optional<Rect> redirect;  // live scene-scratch redirect rect
};

// Result of a feedback sample query.
struct FeedbackSample {
    CaptureSlot slot = CaptureSlot::Pack;
    render::feedback::SampleResult result;  // kind None: sample video memory as before
};

// The decisions of the feedback store, with no device: unit-tested on their own.
class FeedbackLogic {
public:
    FeedbackSettings& settings() { return settings_; }
    const FeedbackSettings& settings() const { return settings_; }

    // The two display buffers' rects in video memory.
    void set_display_buffers(int x0, int y0, int x1, int y1, int w, int h);
    const render::feedback::DisplayBuffers& display_buffers() const { return buffers_; }
    // A game upload: true when it landed in a display buffer (the store stands down).
    bool note_upload(const Rect& rect);
    // A primitive samples a display buffer this frame: `ui_pass` is the pass that
    // draws it (render::frame_state().ui_ortho_pass), `modulation` its red modulation (< 128 = a PS1 decay frame).
    void note_sampler(bool semi_trans, int modulation, bool ui_pass);
    // Per-frame flags of the game: this present takes no loop pass (render::feedback_control().hold_frame), the screen protects
    // its pages (render::feedback_control().skip_store; cleared by plan_store).
    void set_hold_frame() { hold_frame_ = true; }
    void set_skip_store(bool skip) { skip_store_ = skip; }
    // The game asks for `frames` stores without a reader this frame (render::feedback_control().store_allowed: a scene that
    // reads its previous frame starts before its first reader draws).
    void allow_store(int frames) { store_allowed_ = frames > store_allowed_ ? frames : store_allowed_; }
    // This frame's screen copy (mode 1) keeps a fraction of the stored frame in full precision instead of the PS1's
    // 5-bit levels; its sprite colour is the fraction times 128.
    void set_smooth_trail(bool on) { smooth_trail_ = on; }
    bool skip_store() const { return skip_store_; }

    // End of frame. Call captured() for each capture the device made.
    FeedbackPlan plan_store(const FeedbackView& view);
    // Mid-frame Capture split.
    FeedbackPlan plan_scratch_capture(const FeedbackView& view, const Rect& rect);
    // The video-memory texture got CPU words over `rect`.
    FeedbackPlan plan_repack(const FeedbackView& view, const Rect& uploaded);
    // A rect of the texture was written by something other than this logic: the blank state is stale there.
    void note_texture_write(const Rect& rect);

    // The device made a capture of w x h (origin: the GL bottom-left scene pixel of capture texel 0,0).
    void captured(CaptureSlot slot, int w, int h);
    void capture_failed(CaptureSlot slot);
    // The scene-scratch redirect lapsed (Targets::tick_scene_redirect returned true).
    void redirect_lapsed() { scratch_valid_ = false; }

    FeedbackSample sample(const FeedbackView& view, const render::feedback::SampleQuery& query) const;
    // The RGBA program's feedback terms for a split whose feedback code is `code`.
    void shader_inputs(rhi::PrimFsUniforms* fs, int code) const;

    // State, for tests and the console.
    int store_allowed() const { return store_allowed_; }
    bool screen_valid() const { return screen_valid_; }
    bool scratch_valid() const { return scratch_valid_; }
    bool pack_valid() const { return pack_valid_; }
    bool blank_valid() const { return blank_valid_; }
    std::array<float, 2> screen_origin() const { return screen_origin_; }
    int pack_w() const { return pack_w_; }
    int pack_h() const { return pack_h_; }

    // The area a capture reads for the current reader.
    render::feedback::Area area(const FeedbackView& view, bool ui_pass, bool world_wide) const;

private:
    struct Reader {
        bool ui_pass = false, semi = false, decay = false;
    };
    PackParams pack_params(const Reader& reader, float gain) const;
    void pack_all(FeedbackPlan& plan, const FeedbackView& view, const Reader& reader) const;
    void blank_all(FeedbackPlan& plan, const FeedbackView& view);
    CaptureStep capture_step(CaptureSlot slot, const render::feedback::Area& area, int w, int h, bool exact1to1,
                             bool nearest) const;
    std::vector<Rect> feedback_rects(const FeedbackView& view) const;

    FeedbackSettings settings_;
    render::feedback::DisplayBuffers buffers_;
    Reader reader_;      // the last display-buffer sampler of this frame
    Reader last_store_;  // what the last store packed with (a repack reproduces it)
    int store_allowed_ = 0;
    bool smooth_trail_ = false;
    bool hold_frame_ = false;
    bool skip_store_ = false;
    bool pack_valid_ = false;     // a capture exists this session
    bool screen_valid_ = false;   // the pack capture is a 1:1 render-resolution capture of this store
    bool scratch_valid_ = false;  // the scratch capture of this frame
    std::array<float, 2> screen_origin_{0.f, 0.f};
    int pack_w_ = 0, pack_h_ = 0;
    // Blanked rects stay blank until something writes over them: the clear is repeated only then.
    bool blank_valid_ = false;
    std::vector<Rect> blank_key_;
};

// The passes of a plan on a device. Owns the capture textures, the RG8 pack target and the freeze frame.
class FeedbackGpu {
public:
    explicit FeedbackGpu(rhi::Device& device);
    ~FeedbackGpu();
    FeedbackGpu(const FeedbackGpu&) = delete;
    FeedbackGpu& operator=(const FeedbackGpu&) = delete;

    // Capture of `scene` (scene_w x scene_h, an RHI colour target) into a slot. False when it could not be made.
    bool capture(rhi::CommandList& cmd, rhi::TextureH scene, int scene_w, int scene_h, const CaptureStep& step);
    // Packs `src_rect` (storage texels of `src`, src_w x src_h; `bottom_up`: its first row is the rect's bottom)
    // into `dst` of the RG8 video-memory texture `vram`.
    bool pack(rhi::CommandList& cmd, rhi::TextureH src, int src_w, int src_h, const rhi::Rect& src_rect,
              bool bottom_up, rhi::TextureH vram, const Rect& dst, const PackParams& params);
    bool pack_from(rhi::CommandList& cmd, CaptureSlot slot, rhi::TextureH vram, const Rect& dst,
                   const PackParams& params);
    // Word 0 (transparent) over a video-memory rect.
    void clear(rhi::CommandList& cmd, rhi::TextureH vram, const Rect& dst);

    rhi::TextureH capture_texture(CaptureSlot slot) const;
    void capture_size(CaptureSlot slot, int* w, int* h) const;

    // Freeze frame: a copy of the composed scene target.
    void capture_last_frame(rhi::CommandList& cmd, rhi::TextureH scene, rhi::TextureFormat format, int w, int h,
                            bool hold);
    bool present_last_frame(rhi::CommandList& cmd, rhi::TextureH scene, int w, int h);
    const post::FreezeFrame& freeze() const { return freeze_; }

    bool pipelines_ready();  // creates them on first use; false (said once) when they cannot be built

    struct Stats {
        std::uint64_t captures = 0, packs = 0, clears = 0, failures = 0;
    };
    const Stats& stats() const { return stats_; }

private:
    struct Target {
        rhi::TextureH texture;
        int w = 0, h = 0;
        rhi::TextureFormat format = rhi::TextureFormat::RGBA8;
    };
    bool ensure_target(Target& t, int w, int h, rhi::TextureFormat format, const char* name);
    void release();

    rhi::Device& device_;
    rhi::ShaderH vs_, capture_fs_, pack_fs_;
    rhi::PipelineH capture_pipe_, pack_pipe_;
    rhi::SamplerH nearest_, linear_;
    rhi::BufferH triangle_;
    bool failed_ = false;
    Target captures_[2];  // CaptureSlot::Pack, CaptureSlot::Scratch
    Target pack_target_;  // RG8, the size of video memory
    Target freeze_target_;
    post::FreezeFrame freeze_;
    std::vector<std::byte> zeros_;
    Stats stats_;
};

class FeedbackPass {
public:
    explicit FeedbackPass(Renderer& owner);
    ~FeedbackPass();

    FeedbackLogic& logic() { return logic_; }
    FeedbackSettings& settings() { return logic_.settings(); }

    void set_display_buffers(int x0, int y0, int x1, int y1, int w, int h);
    // Vram::on_change: a game upload.
    void note_vram_upload(const Rect& rect);
    // Vram::on_upload: CPU words went to the texture over `rect`.
    void vram_uploaded(const Rect& rect);
    // A primitive samples a display buffer (the page resolution of a display-band reader calls it).
    void note_sampler_prim(bool semi_trans, int modulation);

    // A Capture split: pack the scene drawn so far into `rect` of the video-memory texture.
    void capture_to_vram(rhi::CommandList& cmd, const Rect& rect);
    // End of frame: store the frame for the next frame's feedback reads, then tick the
    // scene-scratch redirect.
    void store_frame(rhi::CommandList& cmd);
    void capture_last_frame(rhi::CommandList& cmd);
    void present_last_frame(rhi::CommandList& cmd);
    // DrawTarget::offscreen_end: pack the offscreen redirect target (the area `rect` just left) into video memory,
    // and keep the CPU words in step when the device can read it back mid-frame.
    void write_back_offscreen(rhi::CommandList& cmd, const Rect& rect);

    // Registry id (Textures) of the capture a feedback sample reads (0 = sample video memory) with the sampling
    // mode and override footprint (the signature of WalkHooks::feedback_sample).
    std::uint32_t sample_texture(bool scratch, int page_x, int page_y, int u0, int v0, int dst_x, int dst_y, int w,
                                 int h, int* mode, int* off_x, int* off_y, int* nat_w, int* nat_h);
    void shader_inputs(rhi::PrimFsUniforms* fs, int code) const;

private:
    FeedbackView view() const;
    void run(rhi::CommandList& cmd, const FeedbackPlan& plan);
    std::uint32_t publish(rhi::CommandList& cmd, CaptureSlot slot);

    Renderer& owner_;
    FeedbackLogic logic_;
    FeedbackGpu gpu_;
    // Registry copies of the captures (Textures ids), the textures split TextureRefs name.
    std::uint32_t registry_[2] = {0, 0};
    int registry_w_[2] = {0, 0}, registry_h_[2] = {0, 0};
    // Ids replaced mid-frame: splits decoded earlier in the frame may still name them, so they go at the store.
    std::vector<std::uint32_t> retired_;
    bool own_write_ = false;  // writing CPU words ourselves: not a game upload
    // Offscreen writeback the CPU words do not hold (no mid-frame readback): repacked after an upload over it.
    Rect gpu_only_;
    rhi::TextureH gpu_only_copy_;
    int gpu_only_w_ = 0, gpu_only_h_ = 0;
};

}  // namespace sh::gpu
