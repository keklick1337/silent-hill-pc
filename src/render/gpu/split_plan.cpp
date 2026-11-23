// The device-free half of SplitRenderer (split_renderer.h): what a split is drawn with and in which order.

#include "render/gpu/split_renderer.h"

#include "render/override_tex/override_tex.h"

#include <algorithm>
#include <cmath>

namespace sh::gpu {

namespace {

using rhi::BlendMode;


// The depth request only takes effect with the depth buffer on; the item bracket
// forces it either way.
bool depth_on(const ShadeInputs& in, const SplitContext& ctx) { return in.pgxp_zbuffer || ctx.force_item_depth; }

void flashlight_block(const ShadeInputs& in, rhi::PrimFsUniforms& fs) {
    const float intensity = in.flashlight_fps_mode ? in.flashlight_intensity_fps : in.flashlight_intensity;
    const float size = in.flashlight_fps_mode ? in.flashlight_size_fps : in.flashlight_size;
    // The classic style keeps its own fixed outer cone.
    const float base_outer = in.flashlight_style ? in.flashlight_outer_cos : kModernOuterCos;
    const float inner = std::max(0.05f, 1.0f - size * (1.0f - in.flashlight_inner_cos));
    const float outer = std::max(0.05f, 1.0f - size * (1.0f - base_outer));
    fs.flMode = {(in.per_pixel_flashlight && in.flashlight_active) ? 1.f : 0.f, in.flashlight_style ? 1.f : 0.f,
                 in.soft_highlights ? 1.f : 0.f, in.flashlight_range};
    fs.flPos = {in.flashlight_pos[0], in.flashlight_pos[1], in.flashlight_pos[2], inner};
    fs.flDir = {in.flashlight_dir[0], in.flashlight_dir[1], in.flashlight_dir[2], outer};
    fs.flColor = {in.flashlight_color[0] * intensity, in.flashlight_color[1] * intensity,
                  in.flashlight_color[2] * intensity, 0.f};
}

}  // namespace

SplitDraw plan_split(const Split& split, std::span<const Vertex> vertices, const ShadeInputs& in,
                     const SplitContext& ctx) {
    SplitDraw d;
    d.first = split.first;
    d.count = split.count;
    d.damp = in.feedback_damp;
    if (split.kind != Split::Kind::Prims || split.count == 0) {
        d.skip = true;
        return d;
    }

    // Fixed-function state. The blend mode turns depth on for opaque and off for blended splits.
    rhi::PrimPipelineKey& key = d.key;
    key.blend = split.blend;
    key.stencil = split.mask_mode ? rhi::StencilMode::MaskWrite : rhi::StencilMode::MaskTest;
    key.depthTest = split.blend == Blend::None ? depth_on(in, ctx) : ctx.force_item_depth;
    key.depthWrite = true;
    // An isolated item's translucent shell must not occlude its contents.
    if (ctx.item_pass && split.blend != Blend::None) key.depthWrite = false;
    // With the engine's scene the world is drawn before every PS1 prim, so translucent 3D prims test its depth
    // instead of relying on the OT; 2D prims (no view depth) are left as they are.
    const bool scene_tested = in.pgxp && split.blend != Blend::None &&
                              split.first < vertices.size() && vertices[split.first].ppw > 0.0f;
    if (scene_tested) {
        key.depthTest = depth_on(in, ctx);
        key.depthWrite = false;
    }

    // Uniform blocks.
    d.rgba = split.format == TexFormat::Rgba32;
    d.vs.projection = ctx.projection;
    d.vs.pgxp = {in.pgxp ? 1.f : 0.f, in.sz_max, in.far_w_clamp, in.world_far_bias};
    d.vs.modes = {in.soft_fog ? 1.f : 0.f, 0.f, 0.f, 0.f};
    d.fog = in.fog;

    rhi::PrimFsUniforms& fs = d.fs;
    d.shadow = in.shadows && ctx.shadow_map;
    fs.prog[0] = d.rgba ? 1.f : 0.f;
    fs.prog[3] = d.shadow ? 1.f : 0.f;

    float taps = 1.f;
    if (in.texture_filter >= 3 && !d.rgba) taps = std::clamp(static_cast<float>(in.aniso_level), 1.f, 16.f);
    const int bilinear = split.glyph               ? 0
                         : in.dither_suppressed ? (in.menu_filter ? 2 : 0)
                                                : (in.texture_filter > 0 ? 1 : 0);
    // Sharp sampling where the picture is not in PSX pixels: 1 = 2D prims, 2 = the whole split (glyph quads on a
    // 3D frame carry the 3D marker, and text needs it most).
    fs.rgbaFb[2] = in.native_render ? 0.f : (split.glyph ? 2.f : 1.f);
    // foliage_edge: on opaque splits only; mode 2 needs samples.
    int foliage = split.blend == Blend::None ? in.foliage_edge : 0;
    if (foliage > 1 && !(in.multisampled && ctx.multisampled)) foliage = 1;
    fs.rgbaFb[3] = static_cast<float>(foliage);
    key.alphaToCoverage = foliage > 1;
    fs.fog = {in.fog_color[0], in.fog_color[1], in.fog_color[2], in.fog_strength};
    fs.modes = {rhi::fogsToBlack(split.blend) ? 1.f : 0.f, in.soft_fog ? 1.f : 0.f, static_cast<float>(bilinear),
                taps};
    fs.dither = {(in.dither && !in.dither_suppressed) ? 1.f : 0.f,
                 (in.window_width > 0 && in.window_width < 480) ? 4.f : 8.f,
                 (in.deband && !in.dither && !in.dither_suppressed) ? 1.f : 0.f, ctx.target_height};
    flashlight_block(in, fs);

    if (d.rgba) {
        const OverrideInfo& o = split.override_info;
        const render::OverrideTexTerms t =
            render::ComputeOverrideTexTerms(o.width, o.height, o.offset_x, o.offset_y, o.hires_w, o.hires_h);
        fs.rgbaTex = {t.texelSize[0], t.texelSize[1], t.offset[0], t.offset[1]};
        fs.rgbaHalf[0] = t.halfHires[0];
        fs.rgbaHalf[1] = t.halfHires[1];
    }
    return d;
}

std::vector<DrawStep> order_splits(const SplitBatch& batch) {
    std::vector<DrawStep> steps;
    steps.reserve(batch.splits.size());
    const auto& splits = batch.splits;
    const auto& verts = batch.vertices;
    const std::size_t n = splits.size();
    auto plain = [&](std::size_t i) {
        steps.push_back({static_cast<std::uint32_t>(i), splits[i].first, splits[i].count, false});
    };

    for (std::size_t i = 0; i < n; ++i) {
        if (!batch.force_item_depth) {
            plain(i);
            continue;
        }
        // The run from i that can be sorted: opaque or average-blended prims with finite depth. Anything else is
        // an ordering barrier.
        std::size_t end = i;
        bool translucent = false;
        for (; end < n; ++end) {
            const Split& s = splits[end];
            if (s.kind != Split::Kind::Prims || (s.blend != Blend::None && s.blend != Blend::Average)) break;
            bool finite = true;
            for (std::uint32_t v = s.first; v < s.first + s.count && v < verts.size(); ++v)
                finite = finite && std::isfinite(verts[v].z);
            if (!finite) break;
            translucent = translucent || s.blend == Blend::Average;
        }
        if (!translucent) {
            plain(i);
            continue;
        }
        struct Tri {
            std::uint32_t split, vertex;
            float depth;
        };
        std::vector<Tri> tris;
        for (std::size_t j = i; j < end; ++j) {
            const Split& s = splits[j];
            if (s.blend == Blend::None) {
                steps.push_back({static_cast<std::uint32_t>(j), s.first, s.count, true});
                continue;
            }
            for (std::uint32_t v = s.first; v + 2 < s.first + s.count && v + 2 < verts.size(); v += 3)
                tris.push_back({static_cast<std::uint32_t>(j), v, (verts[v].z + verts[v + 1].z + verts[v + 2].z) / 3.f});
        }
        // The projection negates z: a smaller stored z is farther away.
        std::stable_sort(tris.begin(), tris.end(), [](const Tri& a, const Tri& b) { return a.depth < b.depth; });
        for (const Tri& t : tris) steps.push_back({t.split, t.vertex, 3, true});
        i = end - 1;
    }
    return steps;
}

Result<void> issue_split(rhi::RenderPass& pass, rhi::PrimRenderer& prims, const SplitDraw& draw,
                         const SplitBindings& textures, BoundTextures& bound) {
    if (draw.skip) return Result<void>::ok();
    if (textures.vram && textures.vram != bound.vram) {
        pass.bindSampler(rhi::ShaderStage::Fragment, rhi::kPrimSamplerVram, textures.vram, textures.vram_sampler);
        bound.vram = textures.vram;
    }
    if (draw.rgba && textures.rgba && textures.rgba != bound.rgba) {
        pass.bindSampler(rhi::ShaderStage::Fragment, rhi::kPrimSamplerRgba, textures.rgba, textures.rgba_sampler);
        bound.rgba = textures.rgba;
    }
    if (draw.shadow && textures.shadow && textures.shadow != bound.shadow) {
        pass.bindSampler(rhi::ShaderStage::Fragment, rhi::kPrimSamplerShadow, textures.shadow, textures.shadow_sampler);
        bound.shadow = textures.shadow;
    }
    return prims.drawSplit(pass, draw.key, draw.vs, draw.fs, draw.fog, draw.first, draw.count, draw.damp);
}

}  // namespace sh::gpu
