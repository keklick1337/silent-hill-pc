// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "features/dump/perf_log.h"

#include "engine/app/pc_paths.h"
#include "engine/config/pc_config.h"
#include "engine/log/sh_log.h"
#include "features/dump/perf_recorder.h"
#include "platform/process_stats.h"
#include "platform/sh_platform.h"
#include "render/gpu/renderer.h"
#include "render/gpu/split_renderer.h"
#include "render/gpu/targets.h"
#include "render/settings/render_preset.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <iterator>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace sh::dump {

namespace {

namespace fs = std::filesystem;

constexpr Uint64 kLogPeriodMs = 5000;
constexpr Uint64 kCaptureMs = 10000;
constexpr Uint64 kPollPeriodMs = 1000;

// The GPU's current clock in MHz, 0 where no known file shows it. Read at most every 50 ms: a sysfs read per frame
// is cheap but not free.
double gpu_clock_mhz() {
#if defined(__ANDROID__) || defined(__linux__)
    static Uint64 read_at = 0;
    static double mhz = 0.0;
    static int source = -1;  // index into kFiles, -2: none found
    static constexpr const char* kFiles[] = {
        "/sys/kernel/gpu/gpu_clock",          // Exynos (Mali): kHz
        "/sys/class/kgsl/kgsl-3d0/gpuclk",    // Snapdragon (Adreno): Hz
        "/sys/class/misc/mali0/device/cur_freq",  // other Mali kernels: Hz or kHz
    };
    if (source == -2) return 0.0;
    const Uint64 now = SDL_GetTicks();
    if (read_at != 0 && now - read_at < 50) return mhz;
    read_at = now;
    for (int i = source < 0 ? 0 : source; i < static_cast<int>(std::size(kFiles)); ++i) {
        std::FILE* f = std::fopen(kFiles[i], "r");
        if (f == nullptr) {
            if (source >= 0) break;
            continue;
        }
        double v = 0.0;
        const bool ok = std::fscanf(f, "%lf", &v) == 1;
        std::fclose(f);
        if (!ok) continue;
        source = i;
        mhz = v > 1e7 ? v / 1e6 : v / 1e3;  // Hz or kHz
        return mhz;
    }
    if (source < 0) source = -2;
    return 0.0;
#else
    return 0.0;
#endif
}

// What the frames of one log window added up to.
struct Window {
    int frames = 0;
    double ms = 0.0, worst_ms = 0.0;
    double gpu_ms = 0.0;
    int gpu_frames = 0;
    // The GPU clock where the system shows it (phones): a governor that lowers the clock stretches the GPU time, so
    // the work is compared in cycles (ms x MHz).
    double mhz_sum = 0.0, mcycles = 0.0;
    int mhz_frames = 0;
    std::uint64_t draws = 0, triangles = 0;
    sh::rhi::ProfileStats::Counts counts;
    std::vector<std::pair<std::string, double>> passes;  // name, ms summed

    void add(const sh::rhi::ProfileStats& s, double frame_ms) {
        ++frames;
        ms += frame_ms;
        worst_ms = std::max(worst_ms, frame_ms);
        if (s.gpuMs >= 0.0) {
            gpu_ms += s.gpuMs;
            ++gpu_frames;
            if (const double mhz = gpu_clock_mhz(); mhz > 0.0) {
                mhz_sum += mhz;
                mcycles += s.gpuMs * mhz / 1000.0;
                ++mhz_frames;
            }
        }
        draws += s.drawCalls;
        triangles += s.triangles;
        const sh::rhi::ProfileStats::Counts& c = s.counts;
        counts.frames += c.frames;
        counts.passes += c.passes;
        counts.merged += c.merged;
        counts.inPassClears += c.inPassClears;
        counts.loads += c.loads;
        counts.stores += c.stores;
        counts.transfers += c.transfers;
        counts.copies += c.copies;
        counts.blits += c.blits;
        counts.textureUploads += c.textureUploads;
        counts.bufferUploads += c.bufferUploads;
        counts.hoisted += c.hoisted;
        counts.pipelineBinds += c.pipelineBinds;
        counts.descriptorSets += c.descriptorSets;
        counts.barriers += c.barriers;
        counts.uploadBytes += c.uploadBytes;
        counts.attachmentMB += c.attachmentMB;
        for (std::uint32_t i = 0; i < s.passCount && i < s.passes.size(); ++i) {
            const std::string name(s.passes[i].name.data());
            auto it = std::find_if(passes.begin(), passes.end(), [&](const auto& p) { return p.first == name; });
            if (it == passes.end())
                passes.emplace_back(name, s.passes[i].ms);
            else
                it->second += s.passes[i].ms;
        }
    }
};

struct State {
    Window window;
    Uint64 window_start = 0;
    sh::platform::ProcessStats process;
    Uint64 polled_at = 0;
    // The capture.
    Window capture;
    Uint64 capture_start = 0;
    bool capturing = false;
    bool traced_late = false;
    fs::file_time_type seen_flag{};  // a flag the game could not delete is not taken twice
    bool pass_timing = true;          // what this module last asked the device for
};
State s_state;

std::string flag_path() { return std::string(PcPaths_ExeDir()) + "perf_capture"; }

double per_frame(double total, int frames) { return frames > 0 ? total / frames : 0.0; }

// One line of what a window's frames did, per frame.
std::string describe(const Window& w) {
    const int n = std::max(w.counts.frames, 1u);
    const sh::rhi::ProfileStats::Counts& c = w.counts;
    char line[720];
    const double fps = w.ms > 0.0 ? w.frames * 1000.0 / w.ms : 0.0;
    std::string gpu = w.gpu_frames > 0 ? std::to_string(w.gpu_ms / w.gpu_frames) : std::string("n/a");
    if (gpu.size() > 5) gpu.resize(5);
    gpu += " ms";
    if (w.mhz_frames > 0) {
        char clock[64];
        std::snprintf(clock, sizeof clock, " at %.0f MHz (%.2f Mcycles)", w.mhz_sum / w.mhz_frames,
                      w.mcycles / w.mhz_frames);
        gpu += clock;
    }
    std::snprintf(line, sizeof line,
                  "%.1f fps (frame %.2f ms, worst %.1f), GPU %s; per frame: %.1f passes (+%.1f merged, %.1f clears "
                  "inside), %.1f loads, %.1f stores, %.1f MB attachments, %.1f transfers (%.1f copies, %.1f blits, "
                  "%.1f texture + %.1f buffer uploads, %.1f KB, %.1f hoisted), %.0f draws, %.0f tris, %.1f pipeline "
                  "binds, %.1f descriptor sets, %.1f barriers",
                  fps, per_frame(w.ms, w.frames), w.worst_ms, gpu.c_str(), double(c.passes) / n,
                  double(c.merged) / n, double(c.inPassClears) / n, double(c.loads) / n, double(c.stores) / n,
                  c.attachmentMB / n, double(c.transfers) / n, double(c.copies) / n, double(c.blits) / n,
                  double(c.textureUploads) / n, double(c.bufferUploads) / n, double(c.uploadBytes) / 1024.0 / n,
                  double(c.hoisted) / n, double(w.draws) / std::max(w.frames, 1),
                  double(w.triangles) / std::max(w.frames, 1), double(c.pipelineBinds) / n,
                  double(c.descriptorSets) / n, double(c.barriers) / n);
    return line;
}

std::string where() {
    char line[160];
    int rw = 0, rh = 0, pw = 0, ph = 0;
    if (sh::gpu::Renderer* r = sh::gpu::renderer(); r != nullptr) {
        rw = r->targets().width();
        rh = r->targets().height();
        pw = r->targets().present_width();
        ph = r->targets().present_height();
    }
    std::snprintf(line, sizeof line, "preset %s, scene %dx%d (render scale %.2f), window %dx%d",
                  RenderPreset_Name(g_PcConfig.graphicsPreset), rw, rh, g_PcConfig.renderScale, pw, ph);
    return line;
}

void set_pass_timing(bool on) {
    sh::gpu::Renderer* r = sh::gpu::renderer();
    if (r == nullptr || on == s_state.pass_timing) return;
    s_state.pass_timing = on;
    r->device().setPassTiming(on);
}

void start_capture(Uint64 now) {
    State& st = s_state;
    st.capturing = true;
    st.capture = Window{};
    st.capture_start = now;
    st.traced_late = false;
    sh::gpu::set_timed_sections(true);
    if (sh::gpu::Renderer* r = sh::gpu::renderer(); r != nullptr) r->device().traceFrames(1);
    SH_LOG("[PERF] capture: %llu s with per-pass GPU times; %s", static_cast<unsigned long long>(kCaptureMs / 1000),
           where().c_str());
}

void end_capture() {
    State& st = s_state;
    st.capturing = false;
    sh::gpu::set_timed_sections(false);
    const Window& w = st.capture;
    SH_LOG("[PERF] capture done, %d frames: %s", w.frames, describe(w).c_str());
    std::vector<std::pair<std::string, double>> passes = w.passes;
    std::sort(passes.begin(), passes.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    std::string list;
    for (const auto& [name, ms] : passes) {
        char item[64];
        std::snprintf(item, sizeof item, "%s%s %.3f", list.empty() ? "" : ", ", name.c_str(), per_frame(ms, w.frames));
        list += item;
    }
    // Sections ("~...") lie inside the pass they were timed in.
    SH_LOG("[PERF] capture GPU ms per frame by pass: %s", list.empty() ? "none measured" : list.c_str());
    SH_LOG("[PERF] capture %s", where().c_str());
}

// The flag file: polled once a second, deleted once taken.
void poll_flag(Uint64 now) {
    State& st = s_state;
    if (st.polled_at != 0 && now - st.polled_at < kPollPeriodMs) return;
    st.polled_at = now;
    const fs::path flag = flag_path();
    std::error_code ec;
    if (!fs::exists(flag, ec)) {
        st.seen_flag = {};
        return;
    }
    const fs::file_time_type stamp = fs::last_write_time(flag, ec);
    if (ec || stamp == st.seen_flag) return;
    st.seen_flag = stamp;
    fs::remove(flag, ec);
    if (!st.capturing) start_capture(now);
}

}  // namespace

void perf_log_frame(const sh::rhi::ProfileStats& stats, double frame_ms) {
    State& st = s_state;
    const Uint64 now = SDL_GetTicks();
    poll_flag(now);
    const bool mobile = Sh_IsMobile() != 0;
    const bool profiler = g_PcConfig.fpsProfiler != 0;
    // On a tile-based GPU a timestamp between passes keeps the next pass from overlapping the one before: a phone
    // times its passes only while someone looks at the times. The recorder sets its own.
    if (mobile && perf_record_brief().empty()) set_pass_timing(profiler || st.capturing);

    if (st.capturing) {
        st.capture.add(stats, frame_ms);
        if (!st.traced_late && now - st.capture_start >= kCaptureMs / 2) {
            st.traced_late = true;
            if (sh::gpu::Renderer* r = sh::gpu::renderer(); r != nullptr) r->device().traceFrames(1);
        }
        if (now - st.capture_start >= kCaptureMs) end_capture();
    }

    if (!mobile && !profiler) {
        st.window = Window{};
        st.window_start = 0;
        return;
    }
    if (st.window_start == 0) {
        st.window_start = now;
        st.process = sh::platform::read_process_stats();
        return;
    }
    st.window.add(stats, frame_ms);
    if (now - st.window_start < kLogPeriodMs) return;
    const double seconds = static_cast<double>(now - st.window_start) / 1000.0;
    const sh::platform::ProcessStats process = sh::platform::read_process_stats();
    char cpu[96] = "CPU n/a";
    if (process.processCpuSec >= 0.0 && st.process.processCpuSec >= 0.0) {
        const double all = (process.processCpuSec - st.process.processCpuSec) / seconds * 100.0;
        const double main = process.threadCpuSec >= 0.0 && st.process.threadCpuSec >= 0.0
                                ? (process.threadCpuSec - st.process.threadCpuSec) / seconds * 100.0
                                : -1.0;
        std::snprintf(cpu, sizeof cpu, "CPU %.0f%%, main thread %.0f%%", all, main);
    }
    SH_LOG("[PERF] %s; %s; %s", describe(st.window).c_str(), cpu, where().c_str());
    st.window = Window{};
    st.window_start = now;
    st.process = process;
}

bool perf_capture_active() { return s_state.capturing; }

std::string perf_capture_brief() {
    if (!s_state.capturing) return {};
    const Uint64 left = kCaptureMs - std::min(kCaptureMs, SDL_GetTicks() - s_state.capture_start);
    return "capture " + std::to_string((left + 999) / 1000) + " s";
}

}  // namespace sh::dump
