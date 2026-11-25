// SPDX-License-Identifier: GPL-3.0-or-later
// TemporalPass: TAA, TAAU and the spatial upscale at the end of the world layer (temporal_pass.h).

#include "render/gpu/temporal_pass.h"

#include "engine/log/sh_log.h"
#include "render/gpu/gpu_types.h"
#include "render/gpu/device_host.h"
#include "render/gpu/renderer.h"
#include "render/gpu/targets.h"
#include "render/scene/area_cells.h"

#include <algorithm>
#include <cstddef>

namespace sh::gpu {

namespace {

// More motion triangles than this in one frame are not drawn; what is left falls back to the camera's motion.
constexpr std::size_t kMaxMotionVertices = 1500000;
// Frames after a feedback effect (the game reading or holding its own frame) that are left alone.
constexpr int kFeedbackFrames = 30;

bool same_shape(const scene::PrelitMesh& a, const scene::PrelitMesh& b) {
    return a.vertices.size() == b.vertices.size() && a.indices.size() == b.indices.size() &&
           a.texture_key == b.texture_key && a.isolated == b.isolated;
}

post::CameraPose pose_of(const scene::Frame& f) {
    post::CameraPose p;
    p.rot = f.camera.rot;
    p.trans = f.camera.trans;
    p.h = f.h;
    return p;
}

std::array<double, 3> to_view(const post::CameraPose& c, const std::array<double, 3>& w) {
    const auto& m = c.rot;
    return {m[0] * w[0] + m[1] * w[1] + m[2] * w[2] + c.trans[0], m[3] * w[0] + m[4] * w[1] + m[5] * w[2] + c.trans[1],
            m[6] * w[0] + m[7] * w[1] + m[8] * w[2] + c.trans[2]};
}

std::array<double, 3> to_world(const scene::ObjectRef& o, const scene::CellVertex& v) {
    const auto& m = o.rot;
    return {m[0] * v.x + m[1] * v.y + m[2] * v.z + o.trans[0], m[3] * v.x + m[4] * v.y + m[5] * v.z + o.trans[1],
            m[6] * v.x + m[7] * v.y + m[8] * v.z + o.trans[2]};
}

}  // namespace

TemporalPass::TemporalPass(Renderer& owner) : owner_(owner) {}

TemporalPass::~TemporalPass() = default;

bool TemporalPass::available() {
    if (!tried_) {
        tried_ = true;
        impl_ = rhi::createTemporal(owner_.device(), &reason_, phone_gpu());
        if (impl_)
            SH_LOG("[GPU] TAA, TAAU and the spatial upscale available");
        else if (owner_.host().backend() == BackendKind::Vulkan)
            SH_LOG("[GPU] TAA, TAAU and the spatial upscale unavailable: %s", reason_.c_str());
    }
    return impl_ != nullptr;
}

const std::string& TemporalPass::unavailable() {
    available();
    return reason_;
}

bool TemporalPass::wanted() { return post::upscalerOf(post::temporal()) != post::Upscaler::Off && available(); }

bool TemporalPass::engaged() const {
    return impl_ != nullptr && post::upscalerOf(post::temporal()) != post::Upscaler::Off &&
           owner_.targets().samples() <= 1 && feedback_frames_ == 0;
}

void TemporalPass::begin_frame() {
    ++frame_;
    if (feedback_frames_ > 0) --feedback_frames_;
    jittering_ = false;
    scale_ = 1.0f;
}

void TemporalPass::begin_world() {
    scale_ = 1.0f;
    jittering_ = false;
    if (!wanted() || !engaged()) {
        owner_.targets().set_world_scale(1.0f);
        return;
    }
    const post::TemporalSettings& s = post::temporal();
    const post::Upscaler mode = post::upscalerOf(s);
    const float scale = post::renderScale(
        mode, static_cast<post::UpscaleQuality>(std::clamp(s.quality, 0, post::kUpscaleQualityCount - 1)));
    // A frame after one without the world (a menu, a movie) is drawn whole: it may be a 2D screen.
    if (scale < 1.0f && now_.valid && now_.frame == frame_ - 1) scale_ = scale;
    owner_.targets().set_world_scale(scale_);
    if (mode == post::Upscaler::Taa || mode == post::Upscaler::Taau) {
        jittering_ = true;
        jitter_px_ = post::jitterOffset(jitter_index_++, post::jitterPhases(scale));
    }
}

void TemporalPass::jitter(scene::View& view, float viewport_w, float viewport_h) const {
    if (!jittering_ || viewport_w <= 0.0f || viewport_h <= 0.0f) return;
    // NDC +Y is up, the jitter's y down.
    view.ortho[12] += 2.0f * jitter_px_[0] / viewport_w;
    view.ortho[13] -= 2.0f * jitter_px_[1] / viewport_h;
}

void TemporalPass::note_world(const scene::Frame& frame, const scene::View& view, rhi::TextureH color,
                              rhi::TextureH depth, const rhi::Viewport& viewport) {
    if (!engaged()) return;
    if (!(now_.valid && now_.frame == frame_)) before_ = std::move(now_);
    Snapshot& s = now_;
    s.valid = true;
    s.frame = frame_;
    s.pose = pose_of(frame);
    s.o0 = view.ortho[0];
    s.o5 = view.ortho[5];
    s.o12 = view.ortho[12] - (jittering_ ? 2.0f * jitter_px_[0] / viewport.w : 0.0f);
    s.o13 = view.ortho[13] + (jittering_ ? 2.0f * jitter_px_[1] / viewport.h : 0.0f);
    s.draw_ofs_x = view.draw_ofs_x;
    s.draw_ofs_y = view.draw_ofs_y;
    s.geom_ofs_x = frame.geom_ofs_x;
    s.geom_ofs_y = frame.geom_ofs_y;
    s.sz_max = view.sz_max;
    s.first_cell = frame.cells.empty() ? std::string() : frame.cells.front().stem;
    s.prelit = frame.prelit;
    s.objects = frame.objects;
    color_ = color;
    depth_ = depth;
    src_ = viewport;
    have_world_ = true;
}

void TemporalPass::note_feedback() {
    if (post::upscalerOf(post::temporal()) != post::Upscaler::Off) feedback_frames_ = kFeedbackFrames;
}

std::array<float, 4> TemporalPass::proj_of(const Snapshot& s, float h, float gx, float gy) const {
    return {s.o0 * h, s.o5 * h, s.o0 * (gx + s.draw_ofs_x) + s.o12, s.o5 * (gy + s.draw_ofs_y) + s.o13};
}

void TemporalPass::build_motion(const Snapshot& now, const Snapshot& before) {
    motion_.clear();
    const float jx = jittering_ ? 2.0f * jitter_px_[0] / src_.w : 0.0f;
    const float jy = jittering_ ? -2.0f * jitter_px_[1] / src_.h : 0.0f;
    const float depth_scale = 1.0f / std::max(now.sz_max, 1.0f);
    auto emit = [&](const std::array<float, 4>& pc, float x, float y, float z, const std::array<float, 4>& pp, float px,
                    float py, float pz, float kind) {
        rhi::TemporalVertex v{};
        const float cx = pc[0] * x + pc[2] * z, cy = pc[1] * y + pc[3] * z;
        v.clip[0] = cx + jx * z;
        v.clip[1] = cy + jy * z;
        v.clip[2] = (z - 1.0f) * depth_scale * z;
        v.clip[3] = z;
        v.cur[0] = cx;
        v.cur[1] = cy;
        v.cur[2] = z;
        v.prev[0] = pp[0] * px + pp[2] * pz;
        v.prev[1] = pp[1] * py + pp[3] * pz;
        v.prev[2] = pz;
        v.kind = kind;
        motion_.push_back(v);
    };

    std::vector<bool> used(before.prelit.size(), false);
    for (std::size_t i = 0; i < now.prelit.size(); ++i) {
        const scene::PrelitMesh& m = now.prelit[i];
        if (m.indices.size() < 3 || m.depth_reset) continue;
        const scene::PrelitMesh* p = nullptr;
        if (i < before.prelit.size() && !used[i] && same_shape(before.prelit[i], m)) {
            p = &before.prelit[i];
            used[i] = true;
        } else {
            for (std::size_t j = 0; j < before.prelit.size() && !p; ++j)
                if (!used[j] && same_shape(before.prelit[j], m)) {
                    p = &before.prelit[j];
                    used[j] = true;
                }
        }
        const float kind = m.blend != scene::Blend::Opaque ? 3.0f : (p ? 1.0f : 2.0f);
        const auto pc = proj_of(now, m.isolated ? m.h : now.pose.h, m.isolated ? m.geom_ofs_x : now.geom_ofs_x,
                                m.isolated ? m.geom_ofs_y : now.geom_ofs_y);
        const scene::PrelitMesh& q = p ? *p : m;
        const auto pp =
            p ? proj_of(before, q.isolated ? q.h : before.pose.h, q.isolated ? q.geom_ofs_x : before.geom_ofs_x,
                        q.isolated ? q.geom_ofs_y : before.geom_ofs_y)
              : pc;
        const std::size_t n = m.indices.size() / 3 * 3;
        if (motion_.size() + n > kMaxMotionVertices) break;
        for (std::size_t t = 0; t < n; t += 3) {
            const std::uint32_t* tri = &m.indices[t];
            if (tri[0] >= m.vertices.size() || tri[1] >= m.vertices.size() || tri[2] >= m.vertices.size()) continue;
            for (int c = 0; c < 3; ++c) {
                const scene::PrelitVertex& a = m.vertices[tri[c]];
                const scene::PrelitVertex& b = q.vertices[tri[c]];
                emit(pc, a.x, a.y, a.z, pp, b.x, b.y, b.z, kind);
            }
        }
    }

    for (std::size_t i = 0; i < now.objects.size(); ++i) {
        const scene::ObjectRef& o = now.objects[i];
        if (!o.mesh) continue;
        const scene::ObjectRef* p = nullptr;
        if (i < before.objects.size() && before.objects[i].mesh == o.mesh) {
            p = &before.objects[i];
        } else {
            for (const scene::ObjectRef& b : before.objects)
                if (b.mesh == o.mesh) {
                    p = &b;
                    break;
                }
        }
        // Still in the world: the camera's reprojection already moves it right.
        if (p && p->rot == o.rot && p->trans == o.trans) continue;
        const auto pc = proj_of(now, now.pose.h, now.geom_ofs_x, now.geom_ofs_y);
        const auto pp = proj_of(before, before.pose.h, before.geom_ofs_x, before.geom_ofs_y);
        const scene::CellMesh& mesh = *o.mesh;
        for (const scene::CellRange& r : mesh.ranges) {
            if (r.material < 0 || static_cast<std::size_t>(r.material) >= mesh.materials.size()) continue;
            const float kind = mesh.materials[static_cast<std::size_t>(r.material)].semi_transparent ? 3.0f
                               : p                                                                   ? 1.0f
                                                                                                     : 2.0f;
            const std::uint32_t n = r.count / 3 * 3;
            if (motion_.size() + n > kMaxMotionVertices || r.first + n > mesh.indices.size()) break;
            for (std::uint32_t t = 0; t < n; t += 3) {
                const std::uint32_t* tri = &mesh.indices[r.first + t];
                if (tri[0] >= mesh.vertices.size() || tri[1] >= mesh.vertices.size() || tri[2] >= mesh.vertices.size())
                    continue;
                for (int c = 0; c < 3; ++c) {
                    const scene::CellVertex& v = mesh.vertices[tri[c]];
                    const auto a = to_view(now.pose, to_world(o, v));
                    const auto b = p ? to_view(before.pose, to_world(*p, v)) : a;
                    emit(pc, static_cast<float>(a[0]), static_cast<float>(a[1]), static_cast<float>(a[2]), pp,
                         static_cast<float>(b[0]), static_cast<float>(b[1]), static_cast<float>(b[2]), kind);
                }
            }
        }
    }
}

void TemporalPass::apply() {
    Targets& targets = owner_.targets();
    rhi::Viewport full{}, small{};
    const bool scaled = targets.world_scaled(&full, &small);
    targets.set_world_scale(1.0f);
    const bool world = have_world_;
    have_world_ = false;
    if (!impl_) return;
    const post::TemporalSettings& s = post::temporal();
    const post::Upscaler upscaler = post::upscalerOf(s);
    rhi::TemporalInputs in;
    in.sharpness = s.sharpness;
    if (!world || !engaged()) {
        if (!scaled) return;
        // The world layer was drawn small but had no world scene: it is scaled up alone, without history.
        in.color = targets.scene_color();
        in.depth = targets.scene_depth();
        in.src = {small.x, small.y, small.w, small.h};
        in.dst = {full.x, full.y, full.w, full.h};
        in.mode = rhi::TemporalMode::Spatial;
        impl_->reset();
        if (auto r = impl_->apply(in); !r && failures_++ < 4) SH_WARN("[GPU] %s", r.error().c_str());
        return;
    }
    in.color = color_;
    in.depth = depth_;
    in.src = {src_.x, src_.y, src_.w, src_.h};
    in.dst = scaled ? std::array<float, 4>{full.x, full.y, full.w, full.h} : in.src;
    in.proj = proj_of(now_, now_.pose.h, now_.geom_ofs_x, now_.geom_ofs_y);
    in.sz_max = now_.sz_max;
    in.jitter = jittering_ ? jitter_px_ : std::array<float, 2>{0.0f, 0.0f};
    in.mode = s.debug != 0                          ? rhi::TemporalMode::Motion
              : upscaler == post::Upscaler::Spatial ? rhi::TemporalMode::Spatial
                                                    : rhi::TemporalMode::Taa;
    const int mode = static_cast<int>(upscaler) * 4 + static_cast<int>(in.mode);
    const bool continuous = before_.valid && before_.frame == now_.frame - 1 && before_.first_cell == now_.first_cell &&
                            !post::cameraCut(before_.pose, now_.pose) && mode == last_mode_;
    last_mode_ = mode;
    in.reset = !continuous;
    motion_.clear();
    if (continuous) {
        in.prev_proj = proj_of(before_, before_.pose.h, before_.geom_ofs_x, before_.geom_ofs_y);
        in.reproject = post::reprojection(now_.pose, before_.pose);
        if (in.mode != rhi::TemporalMode::Spatial) build_motion(now_, before_);
    } else {
        // No motion across a cut: the debug view shows none, and nothing reads the history.
        in.prev_proj = in.proj;
        in.reproject = post::reprojection(now_.pose, now_.pose);
    }
    in.dynamic = motion_;
    // A phone's result over the whole target is left to the HDR resolve, which reads it from the history instead of
    // a pass that loads and stores the scene target (Targets::world_layer).
    in.fold = phone_gpu() && in.mode == rhi::TemporalMode::Taa &&
              targets.scene_format() == rhi::TextureFormat::RGBA16F && in.dst[0] == 0.f && in.dst[1] == 0.f &&
              in.dst[2] == float(targets.width()) && in.dst[3] == float(targets.height());
    if (auto r = impl_->apply(in); !r) {
        if (failures_++ < 4) SH_WARN("[GPU] %s", r.error().c_str());
        return;
    }
    if (const rhi::TextureH result = in.fold ? impl_->result() : rhi::TextureH{})
        targets.set_world_result(result, in.sharpness, [this]() {
            if (auto r = impl_->applyResult(); !r && failures_++ < 4) SH_WARN("[GPU] %s", r.error().c_str());
        });
}

}  // namespace sh::gpu
