// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "features/dump/perf_sampler.h"

#include "features/dump/perf_bench.h"
#include "features/dump/perf_recorder.h"

#include "engine/config/pc_config.h"
#include "engine/debug/console_host.h"
#include "engine/devtools/perf_monitor.h"
#include "platform/process_stats.h"
#include "render/gpu/renderer.h"
#include "render/gpu/split_renderer.h"
#include "render/gpu/textures.h"
#include "render/scene/scene.h"
#include "ui/menu/pc_debug_menu.h"

#include <SDL3/SDL.h>

extern int g_DebugUnlockFps;

namespace sh::dump {

namespace {

namespace dt = sh::devtools;

struct Sampler {
    Uint64 last_frame = 0;
    bool profiling_asked = false;
    sh::rhi::ProfileStats stats;

    // Running totals of the renderer, to take a frame's share from.
    std::uint64_t splits = 0, split_draws = 0, scene_passes = 0;
    std::uint64_t cell_draws = 0, prelit_draws = 0, object_draws = 0;

    // The process's load over the last half-second window.
    Uint64 window_start = 0;
    sh::platform::ProcessStats window_proc;
    double cpu_percent = -1.0, main_thread_percent = -1.0;
    std::int64_t rss_kb = -1;
};
Sampler s_sampler;

// The share of a running total this frame added.
double taken(std::uint64_t& kept, std::uint64_t now) {
    const std::uint64_t delta = now >= kept ? now - kept : 0;
    kept = now;
    return static_cast<double>(delta);
}

void sample_process(Uint64 now, Uint64 freq) {
    Sampler& s = s_sampler;
    if (s.window_start != 0 && now - s.window_start < freq / 2) {
        return;
    }
    const sh::platform::ProcessStats proc = sh::platform::read_process_stats();
    if (s.window_start != 0) {
        const double seconds = static_cast<double>(now - s.window_start) / static_cast<double>(freq);
        if (proc.processCpuSec >= 0.0 && s.window_proc.processCpuSec >= 0.0) {
            s.cpu_percent = (proc.processCpuSec - s.window_proc.processCpuSec) / seconds * 100.0;
        }
        if (proc.threadCpuSec >= 0.0 && s.window_proc.threadCpuSec >= 0.0) {
            s.main_thread_percent = (proc.threadCpuSec - s.window_proc.threadCpuSec) / seconds * 100.0;
        }
    }
    s.rss_kb = proc.rssKb;
    s.window_proc = proc;
    s.window_start = now;
}

}  // namespace

void perf_frame() {
    Sampler& s = s_sampler;
    dt::PerfMonitor& monitor = dt::perf_monitor();
    const Uint64 now = SDL_GetPerformanceCounter();
    const Uint64 freq = SDL_GetPerformanceFrequency();

    if (sh::gpu::Renderer* renderer = sh::gpu::renderer(); renderer != nullptr) {
        sh::rhi::Device& device = renderer->device();
        if (!s.profiling_asked) {
            // GPU time is a timer query per command list: cheap enough to keep for the history.
            device.setProfiling(true);
            s.profiling_asked = true;
        }
        s.stats = device.takeProfileStats();
        static const int draw_calls = monitor.counter("draw_calls");
        static const int triangles = monitor.counter("triangles");
        monitor.count(draw_calls, s.stats.drawCalls);
        monitor.count(triangles, static_cast<double>(s.stats.triangles));
        if (s.stats.gpuMs >= 0.0) {
            static const int gpu_ms = monitor.counter("gpu_ms");
            monitor.count(gpu_ms, s.stats.gpuMs);
        }

        const sh::gpu::SplitRenderer::Stats& splits = renderer->splits().stats();
        static const int batch_splits = monitor.counter("primitive_batch_splits");
        static const int batch_draws = monitor.counter("primitive_batch_draws");
        static const int scene_passes = monitor.counter("scene_passes");
        monitor.count(batch_splits, taken(s.splits, splits.splits));
        monitor.count(batch_draws, taken(s.split_draws, splits.draws));
        monitor.count(scene_passes, taken(s.scene_passes, splits.scenes));
        // Asked only once a scene pass ran: the first ask creates the scene renderer.
        if (sh::scene::SceneRenderer* scene = splits.scenes > 0 ? renderer->splits().scene_renderer() : nullptr;
            scene != nullptr) {
            const sh::scene::SceneRenderer::Stats& st = scene->stats();
            static const int cell_draws = monitor.counter("scene_cell_draws");
            static const int prelit_draws = monitor.counter("scene_prelit_draws");
            static const int object_draws = monitor.counter("scene_object_draws");
            monitor.count(cell_draws, taken(s.cell_draws, st.cell_draws));
            monitor.count(prelit_draws, taken(s.prelit_draws, st.prelit_draws));
            monitor.count(object_draws, taken(s.object_draws, st.object_draws));
        }
    }

    sample_process(now, freq);

    if (s.last_frame != 0) {
        const double ms = static_cast<double>(now - s.last_frame) * 1000.0 / static_cast<double>(freq);
        // The console and the quick options hold the game: their frames would push the frames before them out.
        const bool held = g_PcConsoleInputActive != 0 || g_PcDebugMenuActive != 0;
        monitor.end_frame(ms, held);
        perf_record_frame(ms, held);
        perf_bench_frame(ms, held);
    }
    s.last_frame = now;
}

const sh::rhi::ProfileStats& perf_frame_stats() { return s_sampler.stats; }

dt::DumpPerformance perf_snapshot() {
    const Sampler& s = s_sampler;
    dt::DumpPerformance p;
    p.history = dt::perf_monitor().report();
    p.cpu_percent = s.cpu_percent;
    p.main_thread_percent = s.main_thread_percent;
    if (s.rss_kb >= 0) {
        p.ram_mb = static_cast<double>(s.rss_kb) / 1024.0;
    }
    const sh::platform::GpuUsage usage = sh::platform::read_gpu_usage();
    p.gpu_percent = usage.busyPercent;
    if (s.stats.appVramKb >= 0) {
        p.vram_game_mb = static_cast<double>(s.stats.appVramKb) / 1024.0;
    }
    std::int64_t used = s.stats.vramUsedKb, total = s.stats.vramTotalKb;
    if (used < 0 || total < 0) {
        used = usage.vramUsedKb;
        total = usage.vramTotalKb;
    }
    if (used >= 0 && total >= 0) {
        p.vram_used_mb = static_cast<double>(used) / 1024.0;
        p.vram_total_mb = static_cast<double>(total) / 1024.0;
    }
    p.vsync = g_PcConfig.vsync;
    p.fps_cap = g_PcConfig.fpsCap;
    p.refresh_rate = g_PcConfig.refreshRate;
    p.fps_unlocked = g_DebugUnlockFps != 0;
    if (sh::gpu::Renderer* renderer = sh::gpu::renderer(); renderer != nullptr) {
        p.textures_loaded = static_cast<int>(renderer->textures().live_count());
        p.texture_uploads_pending = static_cast<int>(renderer->textures().pending_uploads());
    }
    return p;
}

}  // namespace sh::dump
