// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/world/mesh_draw.h"

#include <string>

#include "formats/common/json.h"

namespace sh::world {

std::int32_t mesh_far_base(std::int32_t base, std::int32_t draw_distance_pct) {
    const std::int32_t pct = draw_distance_pct <= 0 ? 100 : draw_distance_pct;
    return static_cast<std::int32_t>(base * pct / 100);
}

std::int32_t mesh_far_limit(std::int32_t draw_distance_pct, std::int32_t shift, bool fog_enabled,
                            std::int32_t fog_far, bool whole_map) {
    const std::int32_t drop = mesh_far_base(kMeshFarBase, draw_distance_pct) << (shift + 2);
    std::int32_t limit = drop;
    if (fog_enabled && fog_far < drop) {
        limit = fog_far;
    }
    return whole_map ? 0x7FFFFFFF : limit;
}

std::int32_t mesh_visible_half_width(std::int32_t screen_width, std::int32_t screen_height, std::int32_t win_width,
                                     std::int32_t win_height, float pixel_aspect, float hscale) {
    const std::int32_t screen_half = screen_width >> 1;
    const float visible =
        ((win_height > 0 ? (static_cast<float>(screen_height) * static_cast<float>(win_width) /
                            (2.0f * static_cast<float>(win_height))) *
                               pixel_aspect
                         : static_cast<float>(screen_half)) /
         (hscale > 0.01f ? hscale : 1.0f));
    std::int32_t half = static_cast<std::int32_t>(visible + 64.5f);
    return half < screen_half ? screen_half : half;
}

std::int32_t ot_last_depth(std::int32_t ot_size, std::int32_t tag_base, std::int32_t shift) {
    return (ot_size - 1 - tag_base) << (shift + 2);
}

std::int32_t ot_clamp_depth(std::int32_t depth, std::int32_t ot_size, std::int32_t tag_base, std::int32_t shift) {
    if (((depth >> shift) >> 2) >= ot_size - tag_base) {
        return ot_last_depth(ot_size, tag_base, shift);
    }
    return depth;
}

std::int32_t whole_map_depth_rescue(std::int32_t depth, std::int32_t ot_size, std::int32_t tag_base,
                                    std::int32_t shift, bool whole_map) {
    return (depth < 0 && whole_map) ? ot_last_depth(ot_size, tag_base, shift) : depth;
}

Result<MeshDrawOptions> MeshDrawOptions::parse(std::string_view text) {
    using json::Value;
    Result<Value> doc = json::parse(text);
    if (!doc) {
        return Result<MeshDrawOptions>::fail(doc.error());
    }
    const Value& root = doc.value();
    const Value* fmt = root.find("format");
    if (!root.is_object() || !fmt || !fmt->is_string() || fmt->string() != "sh-mesh-draw") {
        return Result<MeshDrawOptions>::fail("not a sh-mesh-draw file");
    }
    MeshDrawOptions o;
    auto integer = [&](const char* key, std::int32_t& out, std::int64_t lo, std::int64_t hi) -> std::string {
        const Value* v = root.find(key);
        if (!v) {
            return {};
        }
        if (!v->is_integer() || v->integer() < lo || v->integer() > hi) {
            return std::string("\"") + key + "\" is an integer in range";
        }
        out = static_cast<std::int32_t>(v->integer());
        return {};
    };
    auto flag = [&](const char* key, bool& out) -> std::string {
        const Value* v = root.find(key);
        if (!v) {
            return {};
        }
        if (!v->is_bool()) {
            return std::string("\"") + key + "\" is a boolean";
        }
        out = v->boolean();
        return {};
    };
    std::string e;
    if (!(e = integer("texture_filter", o.texture_filter, 0, 2)).empty() ||
        !(e = integer("msaa_samples", o.msaa_samples, 0, 16)).empty() ||
        !(e = integer("edge_smoothing", o.edge_smoothing, 0, 1)).empty() ||
        !(e = integer("draw_distance_pct", o.draw_distance_pct, 0, 1000)).empty() ||
        !(e = flag("vertex_snap", o.vertex_snap)).empty() || !(e = flag("affine_warp", o.affine_warp)).empty() ||
        !(e = flag("smooth_shading", o.smooth_shading)).empty() || !(e = flag("dither", o.dither)).empty()) {
        return Result<MeshDrawOptions>::fail(e);
    }
    const std::int32_t m = o.msaa_samples;
    if (m != 0 && m != 2 && m != 4 && m != 8 && m != 16) {
        return Result<MeshDrawOptions>::fail("\"msaa_samples\" is 0, 2, 4, 8 or 16");
    }
    return Result<MeshDrawOptions>::ok(o);
}

}  // namespace sh::world
