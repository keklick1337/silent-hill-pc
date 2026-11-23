// sh::gpu::FeedbackLogic (feedback_pass.h): when the frame is stored into the feedback rects, when they are blanked
// or repacked, which capture a feedback sprite reads. No device here.

#include "render/gpu/feedback_pass.h"

namespace sh::gpu {

namespace fb = sh::render::feedback;

namespace {

bool overlaps(const Rect& a, const Rect& b) {
    return a.w > 0 && a.h > 0 && b.w > 0 && b.h > 0 && a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h &&
           b.y < a.y + a.h;
}

Rect to_rect(const fb::IRect& r) {
    return Rect{static_cast<std::int16_t>(r.x), static_cast<std::int16_t>(r.y), static_cast<std::int16_t>(r.w),
                static_cast<std::int16_t>(r.h)};
}

// The blit takes its source corners as (int)(v + 0.5f); the corners are never negative.
float round_corner(float v) { return static_cast<float>(static_cast<int>(v + 0.5f)); }

}  // namespace

void FeedbackLogic::set_display_buffers(int x0, int y0, int x1, int y1, int w, int h) {
    buffers_.set(x0, y0, x1, y1, w, h);
}

bool FeedbackLogic::note_upload(const Rect& rect) {
    if (!buffers_.noteUpload(rect.x, rect.y, rect.w, rect.h)) return false;
    // Readers go back to the video-memory rect, which holds the game's own data again.
    screen_valid_ = false;
    return true;
}

void FeedbackLogic::note_sampler(bool semi_trans, int modulation, bool ui_pass) {
    reader_.ui_pass = ui_pass;
    reader_.semi = semi_trans;
    reader_.decay = modulation < 128;
    // A short trailing window: one frame where the overlay happens not to draw must not blank a running effect.
    if (settings_.dream_feedback) store_allowed_ = 2;
}

fb::Area FeedbackLogic::area(const FeedbackView& view, bool ui_pass, bool world_wide) const {
    fb::AreaInputs in;
    in.worldArea = view.world_area;
    in.worldAreaValid = view.world_area_valid;
    in.uiArea = view.ui_area;
    in.uiAreaValid = view.ui_area_valid;
    in.presentViewport = view.present_viewport;
    in.windowW = view.target_w;
    in.windowH = view.target_h;
    in.readerIsUiPass = ui_pass;
    in.captureWorldWide = world_wide;
    in.wideScale = view.wide_scale;
    return fb::areaFor(in);
}

std::vector<Rect> FeedbackLogic::feedback_rects(const FeedbackView& view) const {
    std::vector<Rect> rects;
    if (!buffers_.valid()) return rects;
    rects.push_back(to_rect(buffers_.buffer(0)));
    rects.push_back(to_rect(buffers_.buffer(1)));
    if (view.redirect) rects.push_back(*view.redirect);
    return rects;
}

PackParams FeedbackLogic::pack_params(const Reader& reader, float gain) const {
    PackParams p;
    const bool exact = !reader.semi && settings_.exact;
    p.gain = gain;
    p.mask = reader.semi;  // a blending reader's texels must read as STP-set
    p.mode = exact ? PackMode::Exact : PackMode::Scaled;
    p.decay = exact && reader.decay;
    return p;
}

void FeedbackLogic::pack_all(FeedbackPlan& plan, const FeedbackView& view, const Reader& reader) const {
    // A blending reader's own 50/50 composite is the decay; only the opaque loop has a gain of its own.
    const PackParams params = pack_params(reader, reader.semi ? settings_.damp_blend : settings_.damp);
    for (const Rect& r : feedback_rects(view)) plan.packs.push_back({CaptureSlot::Pack, r, params});
}

void FeedbackLogic::blank_all(FeedbackPlan& plan, const FeedbackView& view) {
    if (!buffers_.valid()) return;
    std::vector<Rect> key = feedback_rects(view);
    if (key != blank_key_) {
        blank_key_ = key;
        blank_valid_ = false;
    }
    if (blank_valid_) return;
    plan.clears.insert(plan.clears.end(), key.begin(), key.end());
    blank_valid_ = true;
}

void FeedbackLogic::note_texture_write(const Rect& rect) {
    if (!blank_valid_) return;
    for (const Rect& r : blank_key_)
        if (overlaps(r, rect)) {
            blank_valid_ = false;
            return;
        }
}

CaptureStep FeedbackLogic::capture_step(CaptureSlot slot, const fb::Area& area, int w, int h, bool exact1to1,
                                        bool nearest) const {
    CaptureStep c;
    c.slot = slot;
    c.w = w;
    c.h = h;
    c.blit = fb::captureBlit(area, w, h, exact1to1);
    c.blit.src = {round_corner(c.blit.src.x0), round_corner(c.blit.src.y0), round_corner(c.blit.src.x1),
                  round_corner(c.blit.src.y1)};
    c.nearest = exact1to1 || nearest;
    return c;
}

FeedbackPlan FeedbackLogic::plan_store(const FeedbackView& view) {
    FeedbackPlan plan;
    // The redirect ticks on every present: it is armed by the game every frame the scene draws, so a tick only on
    // the store path would keep a rect that holds real textures in other rooms blanked.
    plan.tick_redirect = true;

    // Both are consumed before any early return, so a flag set on a frame the store stands down for cannot leak.
    const bool hold = hold_frame_;
    hold_frame_ = false;
    const bool skip = skip_store_;
    skip_store_ = false;
    // The scratch capture serves the strips of the frame it was taken in.
    scratch_valid_ = false;

    if (!buffers_.valid() || skip) {
        screen_valid_ = false;
        return plan;
    }
    if (buffers_.consumeSuppress()) {
        screen_valid_ = false;
        return plan;
    }
    // No reader this frame: blank the rects (word 0, transparent) so the per-map overlays read nothing instead of a
    // stale frame.
    if (store_allowed_ <= 0) {
        blank_all(plan, view);
        screen_valid_ = false;
        return plan;
    }
    --store_allowed_;
    // A scene stepping slower than the present rate: the rects keep the last store, which is what it redraws.
    if (hold) return plan;

    const fb::Area a = area(view, reader_.ui_pass, false);
    fb::IRect px;
    if (settings_.screen && fb::areaPixelRect(a, &px)) {
        plan.captures.push_back(capture_step(CaptureSlot::Pack, a, px.w, px.h, true, true));
        screen_origin_ = {static_cast<float>(px.x), static_cast<float>(px.y)};
        screen_valid_ = true;
    } else {
        const fb::IRect& b0 = buffers_.buffer(0);
        plan.captures.push_back(
            capture_step(CaptureSlot::Pack, a, b0.w, b0.h, false, !reader_.semi && settings_.exact));
        screen_valid_ = false;
    }
    pack_all(plan, view, reader_);
    last_store_ = reader_;
    blank_valid_ = false;
    return plan;
}

FeedbackPlan FeedbackLogic::plan_scratch_capture(const FeedbackView& view, const Rect& rect) {
    FeedbackPlan plan;
    // dream_blur off: the strips that would composite the capture are dropped at decode, the scene draws plain.
    if (rect.w <= 0 || rect.h <= 0 || !settings_.dream_feedback) return plan;

    // The game redraws this rect with world-pass prims at PS1 coordinates: the world mapping, whatever the last
    // display-buffer reader was. A one-shot copy, not a loop: unity gain. The scene rendered the rect under
    // DR_STP(1) on the PS1, so every texel carries STP and the average strips blend; the mask bit is set.
    PackParams params;
    params.gain = 1.f;
    params.mask = true;
    params.mode = PackMode::Scaled;

    const fb::Area a = area(view, false, view.world_wide);
    fb::IRect px;
    if (settings_.screen && fb::areaPixelRect(a, &px)) {
        plan.captures.push_back(capture_step(CaptureSlot::Scratch, a, px.w, px.h, true, true));
        scratch_valid_ = true;
        plan.packs.push_back({CaptureSlot::Scratch, rect, params});
    } else {
        plan.captures.push_back(capture_step(CaptureSlot::Pack, a, rect.w, rect.h, false, false));
        screen_valid_ = false;
        scratch_valid_ = false;
        plan.packs.push_back({CaptureSlot::Pack, rect, params});
    }
    note_texture_write(rect);
    return plan;
}

FeedbackPlan FeedbackLogic::plan_repack(const FeedbackView& view, const Rect& uploaded) {
    FeedbackPlan plan;
    if (skip_store_) return plan;
    bool hit = false;
    for (const Rect& r : feedback_rects(view)) hit = hit || overlaps(r, uploaded);
    if (!hit) return plan;
    note_texture_write(uploaded);

    // Re-blank (not re-pack) while no reader is drawing: CPU bytes just went over the rects.
    if (store_allowed_ <= 0) {
        blank_all(plan, view);
        return plan;
    }
    if (!pack_valid_) return plan;
    // Exactly what the last store wrote: this runs on every upload a loading screen makes, and this frame's decay
    // flag would take the trail down an extra step each time.
    pack_all(plan, view, last_store_);
    return plan;
}

void FeedbackLogic::captured(CaptureSlot slot, int w, int h) {
    pack_valid_ = true;
    if (slot == CaptureSlot::Pack) {
        pack_w_ = w;
        pack_h_ = h;
    }
}

void FeedbackLogic::capture_failed(CaptureSlot slot) {
    if (slot == CaptureSlot::Pack)
        screen_valid_ = false;
    else
        scratch_valid_ = false;
}

FeedbackSample FeedbackLogic::sample(const FeedbackView& view, const fb::SampleQuery& query) const {
    FeedbackSample s;
    fb::IRect scratch;
    if (view.redirect) scratch = {view.redirect->x, view.redirect->y, view.redirect->w, view.redirect->h};
    s.result = fb::selectSample(query, settings_.screen, scratch_valid_ && view.redirect.has_value(), scratch,
                                screen_valid_ && pack_valid_, buffers_);
    s.slot = s.result.kind == fb::SampleKind::Scratch ? CaptureSlot::Scratch : CaptureSlot::Pack;
    return s;
}

void FeedbackLogic::shader_inputs(rhi::PrimFsUniforms* fs, int code) const {
    if (!fs) return;
    // Every RGBA split passes here, so the feedback state never leaks into a hi-res override drawn after it.
    // 4 + mode: the smooth screen copy (set_smooth_trail).
    fs->prog[1] = static_cast<float>((code & 3) + (smooth_trail_ && (code & 3) == 1 ? 4 : 0));
    fs->prog[2] = (code & 4) ? 1.f : 0.f;
    fs->rgbaHalf[2] = screen_origin_[0];
    fs->rgbaHalf[3] = screen_origin_[1];
    fs->rgbaFb[0] = pack_w_ > 0 ? 1.f / static_cast<float>(pack_w_) : 0.f;
    fs->rgbaFb[1] = pack_h_ > 0 ? 1.f / static_cast<float>(pack_h_) : 0.f;
}

}  // namespace sh::gpu
