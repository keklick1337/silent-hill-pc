// Flashlight shadow pass of sh::gpu (render/gpu/shadow_pass.h): caster eligibility, run splitting, the caster stream
// of a batch, the gates and the fragment-block inputs with the light matrix.

#include "framework/sh_test.h"
#include "render/gpu/shadow_pass.h"

#include <cmath>
#include <limits>
#include <vector>

namespace {

namespace gpu = sh::gpu;
namespace shadow = sh::gpu::shadow;

constexpr float kNear = 16.f;

gpu::Vertex caster(float x, float y, float z) {
    gpu::Vertex v;
    v.nx = 0.f;  // not suppressed
    v.ny = 1.f;  // validated view position
    v.vsx = x;
    v.vsy = y;
    v.vsz = z;
    return v;
}

std::vector<gpu::Vertex> triangle(float z = 100.f) { return {caster(0, 0, z), caster(10, 0, z), caster(0, 10, z)}; }

bool can_cast(const std::vector<gpu::Vertex>& tri) { return shadow::triangle_can_cast(tri.data(), kNear); }

std::array<float, 4> transform(const std::array<float, 16>& m, float x, float y, float z) {
    std::array<float, 4> out{};
    for (int r = 0; r < 4; ++r)
        out[static_cast<std::size_t>(r)] = m[static_cast<std::size_t>(r)] * x + m[static_cast<std::size_t>(4 + r)] * y +
                                           m[static_cast<std::size_t>(8 + r)] * z + m[static_cast<std::size_t>(12 + r)];
    return out;
}

gpu::Split prims(std::uint32_t first, std::uint32_t count, gpu::Blend blend = gpu::Blend::None) {
    gpu::Split s;
    s.kind = gpu::Split::Kind::Prims;
    s.blend = blend;
    s.first = first;
    s.count = count;
    return s;
}

gpu::ShadowSettings all_on() {
    gpu::ShadowSettings s;
    s.use_shadows = s.config_shadows = s.per_pixel = s.flashlight_active = s.allowed = true;
    s.frozen = false;
    return s;
}

}  // namespace

SH_TEST(gpu_shadow_valid_triangle_casts) { SH_CHECK(can_cast(triangle())); }

SH_TEST(gpu_shadow_markers) {
    auto t = triangle();
    t[1].ny = 0.f;  // no validated view position
    SH_CHECK(!can_cast(t));
    t = triangle();
    t[2].nx = 1.f;  // suppressed caster
    SH_CHECK(!can_cast(t));
    // A NaN marker compares false both ways and passes.
    t = triangle();
    t[0].nx = std::numeric_limits<float>::quiet_NaN();
    t[0].ny = std::numeric_limits<float>::quiet_NaN();
    SH_CHECK(can_cast(t));
}

SH_TEST(gpu_shadow_non_finite_and_huge) {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    for (int axis = 0; axis < 3; ++axis)
        for (float bad : {nan, inf, -inf}) {
            auto t = triangle();
            float& f = axis == 0 ? t[1].vsx : (axis == 1 ? t[1].vsy : t[1].vsz);
            f = bad;
            SH_CHECK(!can_cast(t));
        }
    auto t = triangle();
    t[0].vsx = 2.0e6f;
    SH_CHECK(!can_cast(t));
    t = triangle();
    t[0].vsy = -2.0e6f;
    SH_CHECK(!can_cast(t));
    t = triangle(2.0e6f);
    SH_CHECK(!can_cast(t));
    t = triangle(1.0e6f);  // the limit itself is still sane
    SH_CHECK(can_cast(t));
}

SH_TEST(gpu_shadow_near_plane) {
    SH_CHECK(!can_cast(triangle(0.f)));
    SH_CHECK(!can_cast(triangle(-5.f)));
    SH_CHECK(!can_cast(triangle(kNear - 0.5f)));
    SH_CHECK(can_cast(triangle(kNear)));
    auto t = triangle();
    t[2].vsz = 1.f;  // one vertex inside the near plane drops the whole triangle
    SH_CHECK(!can_cast(t));
    SH_CHECK(shadow::triangle_can_cast(triangle(2.f).data(), 1.f));
}

SH_TEST(gpu_shadow_run_splitting) {
    // Triangles: ok ok bad ok bad bad ok, then two stray vertices.
    std::vector<gpu::Vertex> stream;
    const bool good[] = {true, true, false, true, false, false, true};
    for (bool g : good) {
        auto t = triangle();
        if (!g) t[0].ny = 0.f;
        stream.insert(stream.end(), t.begin(), t.end());
    }
    stream.push_back(caster(0, 0, 50));
    stream.push_back(caster(1, 0, 50));

    std::vector<shadow::Run> runs;
    shadow::caster_runs(stream, 0, static_cast<std::uint32_t>(stream.size()), kNear, &runs);
    const std::vector<shadow::Run> want = {{0, 6}, {9, 3}, {18, 3}};
    SH_CHECK(runs == want);

    // A sub-range: starts mid-stream, count clamped to the stream.
    runs.clear();
    shadow::caster_runs(stream, 9, 1000, kNear, &runs);
    const std::vector<shadow::Run> want2 = {{9, 3}, {18, 3}};
    SH_CHECK(runs == want2);

    runs.clear();
    shadow::caster_runs(stream, 0, 2, kNear, &runs);  // not even one triangle
    SH_CHECK(runs.empty());
    shadow::caster_runs(stream, 500, 3, kNear, &runs);  // past the end
    SH_CHECK(runs.empty());
}

SH_TEST(gpu_shadow_split_filter_and_stream) {
    gpu::SplitBatch batch;
    for (int i = 0; i < 4; ++i) {
        auto t = triangle(100.f + static_cast<float>(i));
        batch.vertices.insert(batch.vertices.end(), t.begin(), t.end());
    }
    batch.splits.push_back(prims(0, 3));                        // casts
    batch.splits.push_back(prims(3, 3, gpu::Blend::Average));  // muzzle flash: skipped
    batch.splits.push_back(prims(6, 3, gpu::Blend::Add));      // skipped
    gpu::Split capture = prims(9, 3);
    capture.kind = gpu::Split::Kind::Capture;  // not a caster
    batch.splits.push_back(capture);
    SH_CHECK(shadow::split_casts(batch.splits[0]));
    SH_CHECK(!shadow::split_casts(batch.splits[1]));
    SH_CHECK(!shadow::split_casts(prims(0, 2)));

    std::vector<float> pos;
    std::vector<std::int32_t> scene;
    SH_CHECK(shadow::collect_casters(batch, kNear, &pos, &scene));
    SH_CHECK(pos.size() == 9);
    SH_CHECK(pos[0] == 0.f && pos[2] == 100.f && pos[3] == 10.f && pos[7] == 10.f);
    SH_CHECK(scene.empty());

    // Scene splits cast through the scene renderer.
    gpu::SplitBatch only_scene;
    gpu::Split sc;
    sc.kind = gpu::Split::Kind::Scene;
    sc.scene_item = -1;
    only_scene.splits.push_back(sc);
    sc.scene_item = 3;
    only_scene.splits.push_back(sc);
    SH_CHECK(shadow::collect_casters(only_scene, kNear, &pos, &scene));
    SH_CHECK(scene.size() == 2 && scene[0] == -1 && scene[1] == 3);

    // Nothing that casts: the pass stays closed and the old map is kept.
    gpu::SplitBatch ui;
    auto t = triangle();
    ui.vertices.assign(t.begin(), t.end());
    ui.splits.push_back(prims(0, 3, gpu::Blend::Subtract));
    SH_CHECK(!shadow::collect_casters(ui, kNear, &pos, &scene));
}

SH_TEST(gpu_shadow_gates) {
    gpu::ShadowSettings s = all_on();
    SH_CHECK(shadow::pass_active(s));
    SH_CHECK(shadow::sampling_on(s, true));
    SH_CHECK(!shadow::sampling_on(s, false));
    s.allowed = false;  // paused: the map is not rebuilt but still sampled
    SH_CHECK(!shadow::pass_active(s));
    SH_CHECK(shadow::sampling_on(s, true));
    s = all_on();
    s.frozen = true;
    SH_CHECK(!shadow::pass_active(s) && !shadow::sampling_on(s, true));
    s = all_on();
    s.config_shadows = false;
    SH_CHECK(!shadow::pass_active(s) && !shadow::sampling_on(s, true));
    s = all_on();
    s.per_pixel = false;
    SH_CHECK(!shadow::pass_active(s));
}

SH_TEST(gpu_shadow_map_light_takes_the_map) {
    gpu::ShadowSettings off = all_on();
    off.use_shadows = false;
    off.per_pixel = false;
    off.flashlight_active = false;
    gpu::LightShadow l;
    l.position = {10.f, -300.f, 600.f};
    l.axis = {0.f, 1.f, 0.f};
    l.cos_half = 0.342f;
    l.range = 1152.f;
    l.near_clip = 51.f;
    const gpu::ShadowSettings s = shadow::for_map_light(off, l);
    SH_CHECK(s.map_light && shadow::pass_active(s) && shadow::sampling_on(s, true));
    SH_CHECK(s.light.position == l.position && s.light.direction == l.axis && s.light.zNear == 51.f);
    // The flashlight keeps its own map; the shadows setting off leaves it off.
    const gpu::ShadowSettings lit = all_on();
    SH_CHECK(!shadow::for_map_light(lit, l).map_light);
    off.config_shadows = false;
    SH_CHECK(!shadow::for_map_light(off, l).map_light && !shadow::pass_active(shadow::for_map_light(off, l)));
    // The near plane leaves the hand out of the map.
    SH_CHECK(sh::post::buildShadowLight(s.light).zNear == 51.f);
}

SH_TEST(gpu_shadow_matrix_and_inputs) {
    gpu::ShadowSettings s = all_on();
    s.light.sizeScale = 1.f;
    s.light.outerCos = 0.9f;
    s.light.range = 4000.f;
    s.light.position = {0.f, 0.f, 0.f};
    s.light.direction = {0.f, 0.f, 1.f};
    const sh::post::ShadowLight light = sh::post::buildShadowLight(s.light);

    // On the light axis between near and far: frustum centre, depth inside [-1, 1].
    auto c = transform(light.matrix, 0.f, 0.f, 500.f);
    SH_CHECK(c[3] > 0.f);
    SH_CHECK(std::fabs(c[0] / c[3]) < 1e-4f && std::fabs(c[1] / c[3]) < 1e-4f);
    SH_CHECK(c[2] / c[3] > -1.f && c[2] / c[3] < 1.f);
    // Farther is deeper.
    auto d = transform(light.matrix, 0.f, 0.f, 1500.f);
    SH_CHECK(d[2] / d[3] > c[2] / c[3]);

    // Top-left targets: the receiver's Y is mirrored, everything else unchanged.
    const auto gl = shadow::receiver_matrix(light.matrix, false);
    SH_CHECK(gl == light.matrix);
    const auto tl = shadow::receiver_matrix(light.matrix, true);
    auto a = transform(light.matrix, 30.f, 40.f, 600.f);
    auto b = transform(tl, 30.f, 40.f, 600.f);
    SH_CHECK(std::fabs(a[1]) > 1e-3f);
    SH_CHECK(b[0] == a[0] && b[1] == -a[1] && b[2] == a[2] && b[3] == a[3]);

    sh::rhi::PrimFsUniforms fs;
    s.bias = 0.002f;
    s.normal_offset = 0.1f;
    s.strength = 0.7f;
    s.fade_distance = 50.f;
    shadow::fill_inputs(s, true, gl, 1024, light.zNear, light.zFar, &fs);
    SH_CHECK(fs.prog[3] == 2.f);  // filter 1 (3x3) by default; the shaders read the kernel as w - 1
    for (const int filter : {0, 1, 2}) {
        gpu::ShadowSettings k = s;
        k.filter = filter;
        sh::rhi::PrimFsUniforms f;
        shadow::fill_inputs(k, true, gl, 4096, light.zNear, light.zFar, &f);
        SH_CHECK(f.prog[3] == 1.f + static_cast<float>(filter));
        SH_CHECK(f.shadowQ[0] == 1.f / 4096.f);
    }
    {
        gpu::ShadowSettings k = s;
        k.filter = 9;  // out of range clamps to the widest kernel the shaders take
        sh::rhi::PrimFsUniforms f;
        shadow::fill_inputs(k, true, gl, 1024, light.zNear, light.zFar, &f);
        SH_CHECK(f.prog[3] == 3.f);
    }
    SH_CHECK(fs.shadowMatrix == light.matrix);
    SH_CHECK(fs.shadowP[0] == 0.002f && fs.shadowP[1] == 0.1f && fs.shadowP[2] == 0.7f && fs.shadowP[3] == 50.f);
    SH_CHECK(fs.shadowQ[0] == 1.f / 1024.f && fs.shadowQ[1] == 1.f / 1024.f);
    SH_CHECK(fs.shadowQ[2] == 20.f && fs.shadowQ[3] == 4000.f * 1.3f);

    sh::rhi::PrimFsUniforms off;
    off.prog[3] = 1.f;
    shadow::fill_inputs(s, false, gl, 1024, 20.f, 5200.f, &off);
    SH_CHECK(off.prog[3] == 0.f);
    SH_CHECK(off.shadowMatrix == sh::rhi::PrimFsUniforms{}.shadowMatrix);
}

SH_TEST(gpu_shadow_frustum_fits_the_cone) {
    // A point at `cosine` off the light axis, 1000 units out: its largest light-clip coordinate over w.
    const auto edge = [](const sh::post::ShadowLight& light, float cosine) {
        const float sine = std::sqrt(1.f - cosine * cosine);
        const auto c = transform(light.matrix, 1000.f * sine, 0.f, 1000.f * cosine);
        return c[3] > 0.f ? std::fabs(c[0] / c[3]) : 1e9f;
    };
    sh::post::ShadowLightInput in;
    in.position = {0.f, 0.f, 0.f};
    in.direction = {0.f, 0.f, 1.f};
    in.range = 4000.f;

    // The calibrated cone at size 3: outer cosine 1 - 3 * (1 - 0.76) = 0.28. Its edge is inside the map with a
    // margin, and the map is not much wider than the cone.
    in.sizeScale = 3.f;
    in.outerCos = 0.76f;
    const sh::post::ShadowLight calibrated = sh::post::buildShadowLight(in);
    SH_CHECK(edge(calibrated, 0.28f) < 0.9f);
    SH_CHECK(edge(calibrated, 0.28f) > 0.6f);

    // The Modern cone at size 2.4 (outer cosine 0.568) spills 0.2 further: the spill's end is inside the map, and
    // the map is narrower than the calibrated one, so the same texels cover the beam more finely.
    in.sizeScale = 2.4f;
    in.outerCos = gpu::kModernOuterCos;
    in.spillCos = gpu::kModernSpillCos;
    const sh::post::ShadowLight modern = sh::post::buildShadowLight(in);
    SH_CHECK(edge(modern, 0.568f - 0.2f) < 0.9f);
    SH_CHECK(edge(modern, 0.568f - 0.2f) > 0.6f);
    SH_CHECK(edge(modern, 0.9f) > edge(calibrated, 0.9f));

    // A cone past the widest map (the field of view is capped) still gives a usable matrix.
    in.sizeScale = 6.f;
    const sh::post::ShadowLight wide = sh::post::buildShadowLight(in);
    SH_CHECK(edge(wide, 0.9f) > 0.f && edge(wide, 0.9f) < 1.f);
}
