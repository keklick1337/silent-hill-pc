// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Console commands that tune the world's drawing live: draw distance, fog, culling, the whole-map exteriors,
// the precise-vertex (PGXP) options, and the GPU benchmark.

#include "engine/config/pc_config.h"
#include "engine/debug/command_services.h"
#include "engine/debug/commands/command_groups.h"
#include "engine/debug/commands/command_kit.h"
#include "engine/log/sh_log.h"
#include "engine/world/world_atmosphere.h"
#include "render/gpu/render_state.h"

#include <SDL3/SDL_timer.h>

#include <algorithm>
#include <cstdint>
#include <string>

namespace sh::debug {

namespace {

using namespace sh::debug::commands;

sh::render::PreciseVertices& precise() { return sh::render::precise(); }

// What each graphics setting costs on this machine, measured in place: one setting is changed at a time, held
// for a fixed number of frames and timed, then everything is put back. A log from another machine says how
// slow it is but not why; this ranking carries to a slower card of the same vendor.
class GpuBench {
public:
    bool running() const { return step_ >= 0; }

    void start() {
        saved_.texture_filter = sh::render::graphics().texture_filter;
        saved_.aniso_level = sh::render::graphics().aniso_level;
        saved_.hero_light = g_PcConfig.heroLight;
        saved_.precise = precise().enabled;
        saved_.flashlight_mode = g_PcConfig.flashlightMode;
        step_ = 0;
        frames_ = 0;
        SH_DBG_ECHO("[GPUBENCH] %d settings x %d frames: stand still, keep the view; results follow here and in the log",
                    kSteps, kSettleFrames + kMeasuredFrames);
    }

    void frame() {
        if (!running()) return;
        ++frames_;
        if (frames_ == kSettleFrames) {
            measure_from_ms_ = SDL_GetTicks();
            return;
        }
        if (frames_ < kSettleFrames + kMeasuredFrames) return;

        const double frame_ms = static_cast<double>(SDL_GetTicks() - measure_from_ms_) / kMeasuredFrames;
        SH_DBG_ECHO("[GPUBENCH] %-24s frame %6.2f ms", kStepNames[step_], frame_ms);
        frames_ = 0;
        if (++step_ >= kSteps) {
            restore();
            step_ = -1;
            SH_DBG_ECHO("[GPUBENCH] done; settings restored (MSAA needs a restart to compare)");
            return;
        }
        restore();
        change_one(step_);
    }

private:
    static constexpr int kSettleFrames = 20;
    static constexpr int kMeasuredFrames = 120;
    static constexpr int kSteps = 7;
    static constexpr const char* kStepNames[kSteps] = {
        "as configured", "texture filter off", "bilinear", "aniso 4x",
        "hero light off", "PGXP off",          "flashlight shadows off",
    };

    void restore() const {
        sh::render::graphics().texture_filter = saved_.texture_filter;
        sh::render::graphics().aniso_level = saved_.aniso_level;
        g_PcConfig.heroLight = saved_.hero_light;
        precise().enabled = saved_.precise;
        if (g_PcConfig.flashlightMode != saved_.flashlight_mode) Pc_FlashlightModeApply(saved_.flashlight_mode, 0);
    }

    void change_one(int step) const {
        sh::render::GraphicsSettings& graphics = sh::render::graphics();
        switch (step) {
            case 1: graphics.texture_filter = 0; break;
            case 2: graphics.texture_filter = 1; break;
            case 3:
                graphics.texture_filter = 3;
                graphics.aniso_level = 4;
                break;
            case 4: g_PcConfig.heroLight = 0; break;
            case 5: precise().enabled = 0; break;
            case 6:  // the shadowed flashlight modes are one above their plain ones
                if (saved_.flashlight_mode == 1 || saved_.flashlight_mode == 3) {
                    Pc_FlashlightModeApply(saved_.flashlight_mode - 1, 0);
                }
                break;
            default: break;
        }
    }

    struct Saved {
        int texture_filter = 0, aniso_level = 0, hero_light = 0, precise = 0, flashlight_mode = 0;
    };
    Saved saved_;
    int step_ = -1;
    int frames_ = 0;
    std::uint64_t measure_from_ms_ = 0;
};

GpuBench& gpu_bench() {
    static GpuBench bench;
    return bench;
}

Result<void> run_wholemap(Context& ctx, const Args& args) {
    const std::string& mode = args.str(0);
    if (mode == "probe") {
        sh::render::whole_town().probe = 2;
        say(ctx, "whole map: one-frame [WMPROBE] queued (see the log)");
        return done();
    }
    // Live, not saved. A sub-setting word or no word flips it.
    const int on = mode == "auto" ? 2 : mode == "on" ? 1 : mode == "off" ? 0 : !g_PcConfig.wholeMapExteriors;
    g_PcConfig.wholeMapExteriors = on;
    const bool data_ready = g_PcConfig.preloadChunks && g_PcConfig.residentTextures;
    say(ctx, "whole map: %s (engaged here: %s)%s", on == 2 ? "AUTO (on below fogstr 1.0)" : on ? "ON" : "OFF",
        Pc_WholeMapDrawActive() ? "yes" : "no",
        on && !data_ready ? " -- needs preload_chunks=1 and resident_textures=1" : "");
    if (on && !precise().enabled) say(ctx, "distant geometry needs PGXP: pgxp 1");
    return done();
}

}  // namespace

void gpu_bench_frame() { gpu_bench().frame(); }

namespace commands {

void register_render_commands(Registry& registry) {
    // ---- draw distance, fog, culling ----
    add(registry, "drawdist", "graphics", "Draw distance", {opt_integer("percent", "25..200; config key draw_distance_pct")},
        [](Context& ctx, const Args& args) {
            // Past about 210 the view depth of the far polygons wraps.
            if (args.has(0)) g_PcConfig.drawDistancePct = std::clamp(whole(args.str(0)), 25, 200);
            say(ctx, "draw distance: %d%% (25..200; config key draw_distance_pct)", g_PcConfig.drawDistancePct);
            return done();
        });
    // Indoors the fog is black: its far plane is the wall of darkness, so this is also "see further".
    add(registry, "fogdist", "graphics", "Fog distance", {opt_integer("percent", "50..400; 100 = stock")},
        [](Context& ctx, const Args& args) {
            sh::world::WorldAtmosphere& atmosphere = sh::world::atmosphere();
            if (args.has(0)) {
                atmosphere.set_fog_distance_scale_pct(std::clamp(whole(args.str(0)), 50, 400));
                atmosphere.reapply_fog_distance();
            }
            say(ctx, "fog distance: %d%% (50..400; 100=stock)", atmosphere.fog_distance_scale_pct());
            return done();
        });
    add(registry, "fogstr", "graphics", "World fog strength",
        {opt_number("strength", "1.0 = native PC fog; >1 deepens toward the PS1 look")},
        number_setting([]() -> float& { return sh::render::fog().strength; },
                       "world fog strength: %.2f (1.0=native PC fog; >1 deepens toward PSX)"));
    add(registry, "fogsoft", "graphics", "Soft fog", {opt_integer("mode", "0 = PS1 stepped, 1 = smooth; config key soft_fog")},
        [](Context& ctx, const Args& args) {
            if (args.has(0)) {
                sh::render::fog().soft = whole(args.str(0)) != 0 ? 1 : 0;
                g_PcConfig.softFog = sh::render::fog().soft;
            }
            say(ctx, "soft fog: %d (0=PSX stepped, 1=smooth; config key soft_fog)", sh::render::fog().soft);
            return done();
        });
    // Only drawing: what the game's logic sees (AI, events) is the original test either way.
    add(registry, "cull", "render", "World draw culling",
        {arg("mode", ArgType::Enum, true, "no argument toggles", {"modern", "original"})},
        [](Context& ctx, const Args& args) {
            const std::string& mode = args.str(0);
            g_PcConfig.culling = mode == "modern" ? 1 : mode == "original" ? 0 : !g_PcConfig.culling;
            say(ctx, "world draw culling: %s",
                g_PcConfig.culling ? "modern (window FOV + fog distance)" : "original (retail PVS + frustum)");
            return done();
        },
        {}, "modern: window FOV + fog distance. original: retail PVS + frustum.");
    // The console's GPU drops triangles whose screen box passes 1023x511; drawing them is a wedge across the view.
    add(registry, "polysizecull", "render", "Reject oversize polygons (bbox > 1023x511) like the PS1",
        {opt_switch("on|off; no argument toggles")}, [](Context& ctx, const Args& args) {
            const bool on = switched(args, 0, precise().poly_size_cull != 0);
            precise().poly_size_cull = on ? 1 : 0;
            g_PcConfig.polySizeCull = precise().poly_size_cull;
            PcConfig_SaveKeyValue("psx_poly_size_cull", on ? "1" : "0");
            say(ctx, "PSX oversize-poly cull (bbox >1023x511 rejected): %s", on ? "ON" : "OFF");
            return done();
        });
    add(registry, "wholemap", "render", "Whole-map exterior drawing",
        {arg("mode", ArgType::Enum, true, "on|off|auto, or a sub-setting", {"on", "off", "auto", "probe", "fast", "gpu"}),
         arg("value", ArgType::String, true, "fast/gpu: on|off (toggles if absent)")},
        run_wholemap, {},
        "probe queues a one-frame [WMPROBE] log dump. Needs preload_chunks=1, resident_textures=1 and PGXP on for "
        "distant geometry.");
    add(registry, "mixshader", "render", "Mixed-format shader (fewer draw calls)",
        {opt_switch("on|off; no argument toggles")},
        switch_setting([]() -> int& { return sh::render::debug_state().mixed_program; }, "mixed-format shader: %s",
                       "ON (fewer draw calls)", "OFF (one program per format)"));
    add(registry, "gpubench", "debug", "GPU benchmark", {}, [](Context& ctx, const Args&) {
        if (gpu_bench().running()) {
            say(ctx, "gpubench is already running");
        } else {
            gpu_bench().start();
        }
        return done();
    });

    // ---- precise vertices (PGXP) ----
    add(registry, "pgxp", "render", "Perspective-correct geometry (PGXP)", {opt_switch("on|off; no argument toggles")},
        [](Context& ctx, const Args& args) {
            const bool on = switched(args, 0, precise().enabled != 0);
            precise().enabled = on ? 1 : 0;
            g_PcConfig.usePgxp = on ? 1 : 0;
            PcConfig_SaveKeyValue("use_pgxp", on ? "1" : "0");
            say(ctx, "PGXP %s (perspective-correct, WIP)", on ? "ON" : "OFF");
            return done();
        },
        {}, "Saved to the config (use_pgxp).");
    add(registry, "pgxpedge", "render", "PGXP off-screen position clamp",
        {opt_number("units", "PS1 units; higher = less edge warp")},
        number_setting([]() -> float& { return precise().edge_max; },
                       "PGXP off-screen position clamp: %.0f psx-units (higher = less edge warp)"));
    add(registry, "pgxpdepth", "render", "PGXP unquantized-depth W (distance-seam fix)",
        {opt_switch("on|off; no argument toggles")},
        switch_setting([]() -> int& { return precise().unquantized_depth; },
                       "PGXP unquantized-depth W (distance-seam fix): %s"));
    // Off drops the world's depth back to the painter's order at once.
    add(registry, "pgxpworlddepth", "render", "PGXP world depth channel", {opt_switch("on|off; no argument toggles")},
        switch_setting([]() -> int& { return precise().world_depth; }, "PGXP world depth channel: %s"));
    // Off gives each character polygon one flat depth again.
    add(registry, "pgxpactordepth", "render", "PGXP character depth", {opt_switch("on|off; no argument toggles")},
        switch_setting([]() -> int& { return precise().actor_depth; }, "PGXP character depth: %s"));
    add(registry, "pgxpwallbias", "render", "PGXP world far-bias", {opt_integer("units", "SZ units")},
        whole_setting([]() -> int& { return precise().world_far_bias; }, "PGXP world far-bias M: %d SZ units"));
    add(registry, "pgxpnearclip", "render", "PGXP near-plane clipping (close-camera warp fix)",
        {opt_switch("on|off; no argument toggles")},
        switch_setting([]() -> int& { return precise().near_clip; },
                       "PGXP near-plane clipping (close-camera warp fix): %s"));
    add(registry, "pgxpnearz", "render", "PGXP near-clip plane depth", {opt_number("units", "GTE units, minimum 1")},
        [](Context& ctx, const Args& args) {
            // The clipper divides by it, so it never goes below one.
            if (args.has(0)) precise().near_z = std::max(static_cast<float>(real(args.str(0))), 1.0f);
            say(ctx, "PGXP near-clip plane depth: %.1f gte-units", static_cast<double>(precise().near_z));
            return done();
        });
    // Past this view depth (256 = one world unit) texturing fades to affine, which stops the far shimmer.
    add(registry, "pgxpfarw", "render", "PGXP far-W clamp", {opt_number("units", "SZ units, 0 = off")},
        [](Context& ctx, const Args& args) {
            if (args.has(0)) precise().far_w_clamp = std::max(static_cast<float>(real(args.str(0))), 0.0f);
            say(ctx, "PGXP far-W clamp (affine beyond ~%.0fu): %.0f sz (0=off)",
                static_cast<double>(precise().far_w_clamp / 256.0f), static_cast<double>(precise().far_w_clamp));
            return done();
        });
    add(registry, "weld", "render", "PGXP seam weld radius", {opt_number("pixels", "0 = off")},
        number_setting([]() -> float& { return precise().weld_px; }, "PGXP seam weld radius: %.2f px (0=off)"));
    add(registry, "weldw", "render", "PGXP weld depth ratio", {opt_number("ratio", "")},
        number_setting([]() -> float& { return precise().weld_w_ratio; }, "PGXP weld depth ratio: %.3f"));
    add(registry, "smoothmodels", "render", "Smooth models (needs PGXP on)",
        {opt_integer("mode", "0|1; config key smooth_models")}, [](Context& ctx, const Args& args) {
            if (args.has(0)) {
                precise().smooth_models = whole(args.str(0)) != 0 ? 1 : 0;
                g_PcConfig.smoothModels = precise().smooth_models;
            }
            say(ctx, "smooth models: %d (config key smooth_models)%s", precise().smooth_models,
                precise().enabled ? "" : " -- inactive while PGXP is off");
            return done();
        });
    add(registry, "modelupscale", "render", "Model upscaling (renderer = vulkan only)",
        {opt_integer("mode", "0 off | 1 tessellation | 2 mesh shaders; config key model_upscaling")},
        [](Context& ctx, const Args& args) {
            if (args.has(0)) {
#ifdef __ANDROID__
                // Never on a phone or tablet (RenderPreset_Apply).
                sh::render::graphics().model_upscaling = 0;
#else
                sh::render::graphics().model_upscaling = std::clamp(whole(args.str(0)), 0, 2);
#endif
                g_PcConfig.modelUpscaling = sh::render::graphics().model_upscaling;
            }
            say(ctx, "model upscaling: %d (config key model_upscaling; 0 off, 1 tessellation, 2 mesh shaders)",
                sh::render::graphics().model_upscaling);
            return done();
        });
}

}  // namespace commands
}  // namespace sh::debug
