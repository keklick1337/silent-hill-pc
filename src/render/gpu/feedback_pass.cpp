// sh::gpu::FeedbackPass (feedback_pass.h): the feedback store wired to the renderer's targets, video memory and
// texture registry. The decisions are FeedbackLogic's (feedback_logic.cpp), the passes FeedbackGpu's (feedback_gpu.cpp).

#include "render/gpu/feedback_pass.h"

#include "render/gpu/gpu_types.h"

#include "engine/log/sh_log.h"
#include "render/gpu/device_host.h"
#include "render/gpu/renderer.h"
#include "render/gpu/targets.h"
#include "render/gpu/temporal_pass.h"
#include "render/gpu/textures.h"
#include "render/gpu/vram.h"

#include <span>
#include <vector>

namespace sh::gpu {

namespace {

using namespace sh::rhi;

bool overlaps(const Rect& a, const Rect& b) {
    return a.w > 0 && a.h > 0 && b.w > 0 && b.h > 0 && a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h &&
           b.y < a.y + a.h;
}

std::size_t slot_index(CaptureSlot slot) { return slot == CaptureSlot::Pack ? 0 : 1; }

// One RGBA8 texel (R first) as the PS1 stores a drawn pixel: 5 bits per channel, STP on every non-black word.
std::uint16_t pack_word(const std::byte* px) {
    const auto c = [px](int i) { return static_cast<std::uint16_t>(std::to_integer<unsigned>(px[i]) >> 3); };
    const std::uint16_t w = static_cast<std::uint16_t>(c(0) | (c(1) << 5) | (c(2) << 10));
    return w != 0 ? static_cast<std::uint16_t>(w | 0x8000u) : w;
}

}  // namespace

FeedbackPass::FeedbackPass(Renderer& owner) : owner_(owner), gpu_(owner.device()) {
    gpu_.set_deferred_capture(phone_gpu());
    owner_.vram().on_upload([this](const Rect& rect) { vram_uploaded(rect); });
}

FeedbackPass::~FeedbackPass() {
    owner_.vram().on_upload({});
    Textures& tx = owner_.textures();
    for (std::uint32_t id : registry_)
        if (id) tx.destroy(id);
    for (std::uint32_t id : retired_) tx.destroy(id);
    if (gpu_only_copy_) owner_.device().destroy(gpu_only_copy_);
}

FeedbackView FeedbackPass::view() const {
    const Targets& t = owner_.targets();
    const ViewLatches& l = t.latches();
    FeedbackView v;
    v.world_area = {l.area_vp[0], l.area_vp[1], l.area_vp[2], l.area_vp[3]};
    v.world_area_valid = l.area_vp_valid;
    v.ui_area = {l.ui_area_vp[0], l.ui_area_vp[1], l.ui_area_vp[2], l.ui_area_vp[3]};
    v.ui_area_valid = l.ui_area_vp_valid;
    v.present_viewport = {l.present_vp[0], l.present_vp[1], l.present_vp[2], l.present_vp[3]};
    v.target_w = t.width();
    v.target_h = t.height();
    v.wide_scale = l.feedback_wide_scale;
    v.world_wide = l.world_ortho_valid && l.world_disp[0] > 0.f &&
                   (l.world_ortho[1] - l.world_ortho[0]) / l.world_disp[0] > 1.001f;
    Rect r;
    if (t.scene_redirect(&r)) v.redirect = r;
    return v;
}

std::uint32_t FeedbackPass::publish(CommandList& cmd, CaptureSlot slot) {
    const std::size_t i = slot_index(slot);
    int w = 0, h = 0;
    gpu_.capture_size(slot, &w, &h);
    if (w <= 0 || h <= 0) return 0;
    Textures& tx = owner_.textures();
    if (!registry_[i] || registry_w_[i] != w || registry_h_[i] != h) {
        if (registry_[i]) retired_.push_back(registry_[i]);
        registry_[i] = tx.create_rgba(w, h, {}, slot == CaptureSlot::Pack ? "feedback.screen" : "feedback.scratch");
        registry_w_[i] = registry_h_[i] = 0;
        if (!registry_[i]) return 0;
        int cw = 0, ch = 0;
        // A capture larger than the device limit comes back halved: unusable for a 1:1 copy.
        if (!tx.size(registry_[i], &cw, &ch) || cw != w || ch != h) {
            tx.destroy(registry_[i]);
            registry_[i] = 0;
            return 0;
        }
        // Read through the override sampler, whose preset filter must not turn a 1:1 copy bilinear.
        tx.set_nearest(registry_[i], true);
        // The new texture's initial clear is queued: it must not land after the copy.
        tx.flush(cmd);
        registry_w_[i] = w;
        registry_h_[i] = h;
    }
    cmd.copyTexture(gpu_.capture_texture(slot), {0, 0, w, h}, tx.handle(registry_[i]), 0, 0);
    return registry_[i];
}

void FeedbackPass::run(CommandList& cmd, const FeedbackPlan& plan) {
    if (plan.empty()) return;
    Targets& targets = owner_.targets();
    const TextureH vram = owner_.vram().texture();
    bool failed[2] = {false, false};
    for (const CaptureStep& c : plan.captures) {
        if (gpu_.capture(cmd, targets.resolved(cmd), targets.width(), targets.height(), c)) {
            logic_.captured(c.slot, c.w, c.h);
            publish(cmd, c.slot);
        } else {
            logic_.capture_failed(c.slot);
            failed[slot_index(c.slot)] = true;
        }
    }
    for (const Rect& r : plan.clears) gpu_.clear(cmd, vram, r);
    for (const PackStep& p : plan.packs)
        if (!failed[slot_index(p.source)]) gpu_.pack_from(cmd, p.source, vram, p.rect, p.params);
}

void FeedbackPass::set_display_buffers(int x0, int y0, int x1, int y1, int w, int h) {
    logic_.set_display_buffers(x0, y0, x1, y1, w, h);
}

void FeedbackPass::note_vram_upload(const Rect& rect) {
    if (own_write_) return;
    if (!logic_.note_upload(rect)) return;
    static bool s_logged = false;
    if (!s_logged) {
        s_logged = true;
        SH_LOG("[FBFEEDBACK] VRAM upload (%d,%d %dx%d) lands in a display buffer - feedback store standing down",
               rect.x, rect.y, rect.w, rect.h);
    }
}

void FeedbackPass::vram_uploaded(const Rect& rect) {
    CommandList& cmd = owner_.host().frame();
    // The CPU words just restamped an offscreen writeback they never held: put the packed picture back.
    if (gpu_only_copy_ && overlaps(rect, gpu_only_)) {
        PackParams p;
        p.mask = true;
        p.mode = PackMode::Truncate;
        gpu_.pack(cmd, gpu_only_copy_, gpu_only_w_, gpu_only_h_, {0, 0, gpu_only_.w, gpu_only_.h},
                  owner_.device().backend() == Backend::Gl33, owner_.vram().texture(), gpu_only_, p);
    }
    run(cmd, logic_.plan_repack(view(), rect));
}

void FeedbackPass::note_sampler_prim(bool semi_trans, int modulation) {
    logic_.note_sampler(semi_trans, modulation, owner_.targets().view().ui_pass);
    owner_.temporal().note_feedback();
}

void FeedbackPass::capture_to_vram(CommandList& cmd, const Rect& rect) {
    owner_.temporal().note_feedback();
    run(cmd, logic_.plan_scratch_capture(view(), rect));
}

void FeedbackPass::store_frame(CommandList& cmd) {
    Textures& tx = owner_.textures();
    for (std::uint32_t id : retired_) tx.destroy(id);
    retired_.clear();

    const FeedbackPlan plan = logic_.plan_store(view());
    run(cmd, plan);
    if (!plan.tick_redirect) return;
    Rect r;
    const bool live = owner_.targets().scene_redirect(&r);
    if (owner_.targets().tick_scene_redirect()) {
        logic_.redirect_lapsed();
        static int s_logs = 0;
        if (live && s_logs++ < 32)
            SH_LOG("[FBSCRATCH] redirect LAPSED (%d,%d %dx%d) - rect no longer refreshed", r.x, r.y, r.w, r.h);
    }
}

void FeedbackPass::capture_last_frame(CommandList& cmd) {
    Targets& t = owner_.targets();
    gpu_.capture_last_frame(cmd, t.resolved(cmd), t.scene_format(), t.width(), t.height(),
                            logic_.settings().freeze_capture_hold);
}

void FeedbackPass::settle_capture(CommandList& cmd, bool presents) {
    if (!presents) {
        gpu_.drop_capture();
        return;
    }
    Targets& t = owner_.targets();
    gpu_.flush_capture(cmd, t.resolved(cmd), t.width(), t.height());
}

void FeedbackPass::present_last_frame(CommandList& cmd) {
    owner_.temporal().note_feedback();
    Targets& t = owner_.targets();
    if (gpu_.present_last_frame(cmd, t.resolved(cmd), t.width(), t.height())) t.wrote_resolved();
}

void FeedbackPass::write_back_offscreen(CommandList& cmd, const Rect& rect) {
    // A screen that protects its pages (the paper-map pickup) keeps the rect it just loaded.
    if (logic_.skip_store() || rect.w <= 0 || rect.h <= 0) return;
    Targets& targets = owner_.targets();
    const TextureH src = targets.offscreen_color();
    // Targets recreates the redirect target when the next area has another size, so the picture of `rect` is
    // only still there when the sizes match.
    const Rect& now = targets.offscreen_area();
    if (!src || now.w != rect.w || now.h != rect.h) {
        static int s_logs = 0;
        if (s_logs++ < 4)
            SH_WARN("[OFFSCREEN] writeback of (%d,%d %dx%d) skipped: the redirect target was resized", rect.x, rect.y,
                    rect.w, rect.h);
        return;
    }
    Vram& vram = owner_.vram();
    const bool gl = owner_.device().backend() == Backend::Gl33;
    PackParams p;
    p.mask = true;
    p.mode = PackMode::Truncate;
    // On GL a rendered target's storage runs bottom up; SDL3 GPU keeps the top row first.
    const bool packed = gpu_.pack(cmd, src, rect.w, rect.h, {0, 0, rect.w, rect.h}, gl, vram.texture(), rect, p);
    logic_.note_texture_write(rect);

    // Small areas (the sewer water caustic) are read back so the CPU words stay authoritative: a later upload over
    // them then restamps the same picture, and StoreImage/MoveImage read it. Bigger ones and devices that cannot
    // read back mid-frame keep a GPU copy that is repacked after such an upload.
    constexpr int kReadbackMax = 64;
    bool synced = false;
    if (rect.w <= kReadbackMax && rect.h <= kReadbackMax) {
        std::vector<std::byte> px(std::size_t(rect.w) * std::size_t(rect.h) * 4);
        if (owner_.device().readback(src, {0, 0, rect.w, rect.h}, px)) {
            std::span<std::uint16_t> words = vram.words();
            for (int y = 0; y < rect.h; ++y)
                for (int x = 0; x < rect.w; ++x) {
                    const std::size_t at = std::size_t((rect.y + y) & (Vram::kHeight - 1)) * Vram::kWidth +
                                           std::size_t((rect.x + x) & (Vram::kWidth - 1));
                    words[at] = pack_word(&px[(std::size_t(y) * rect.w + x) * 4]);
                }
            synced = true;
            if (!packed) {
                own_write_ = true;
                vram.mark_dirty(rect);
                own_write_ = false;
                vram.upload(cmd);
            }
        }
    }
    if (synced || !packed) {
        if (overlaps(gpu_only_, rect)) gpu_only_ = {};
        return;
    }
    if (!gpu_only_copy_ || gpu_only_w_ != rect.w || gpu_only_h_ != rect.h) {
        if (gpu_only_copy_) owner_.device().destroy(gpu_only_copy_);
        gpu_only_copy_ = {};
        gpu_only_w_ = gpu_only_h_ = 0;
        auto t = owner_.device().createTexture({TextureFormat::RGBA8, std::uint32_t(rect.w), std::uint32_t(rect.h),
                                                kUsageSampled | kUsageColorTarget, "offscreen writeback copy"});
        if (!t) return;
        gpu_only_copy_ = t.value();
        gpu_only_w_ = rect.w;
        gpu_only_h_ = rect.h;
    }
    cmd.copyTexture(src, {0, 0, rect.w, rect.h}, gpu_only_copy_, 0, 0);
    gpu_only_ = rect;
}

std::uint32_t FeedbackPass::sample_texture(bool scratch, int page_x, int page_y, int u0, int v0, int dst_x,
                                           int dst_y, int w, int h, int* mode, int* off_x, int* off_y, int* nat_w,
                                           int* nat_h) {
    render::feedback::SampleQuery q;
    q.scratch = scratch;
    q.pageX = page_x;
    q.pageY = page_y;
    q.u0 = u0;
    q.v0 = v0;
    q.dstX = dst_x;
    q.dstY = dst_y;
    q.w = w;
    q.h = h;
    const FeedbackSample s = logic_.sample(view(), q);
    if (s.result.kind == render::feedback::SampleKind::None) return 0;
    const std::uint32_t id = registry_[slot_index(s.slot)];
    if (!id) return 0;
    if (mode) *mode = s.result.mode;
    if (off_x) *off_x = s.result.offX;
    if (off_y) *off_y = s.result.offY;
    if (nat_w) *nat_w = s.result.natW;
    if (nat_h) *nat_h = s.result.natH;
    return id;
}

void FeedbackPass::shader_inputs(PrimFsUniforms* fs, int code) const { logic_.shader_inputs(fs, code); }

}  // namespace sh::gpu
