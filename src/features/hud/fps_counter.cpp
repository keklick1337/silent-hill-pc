// SPDX-License-Identifier: GPL-3.0-or-later
#include "features/hud/fps_counter.h"

#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

#include "engine/config/pc_config.h"
#include "engine/log/sh_log.h"
#include "features/dump/perf_recorder.h"
#include "features/dump/perf_sampler.h"
#include "features/hud/frame_stats.h"
#include "features/hud/text_panel.h"
#include "platform/process_stats.h"
#include "render/overlay/overlay.h"
#include "render/rhi/rhi.h"

namespace sh::hud {

namespace {

std::string format(const char* fmt, ...) {
    char text[96];
    std::va_list args;
    va_start(args, fmt);
    std::vsnprintf(text, sizeof(text), fmt, args);
    va_end(args);
    return text;
}

long long megabytes(long long kilobytes) { return kilobytes / 1024; }

// The profiler's figures: collected every frame, turned into rows when a counting window closes.
class Profiler {
public:
    // The last row stays blank: room under the figures.
    static constexpr std::size_t kRows = 9;

    void set_active(bool on) {
        if (on == active_) {
            return;
        }
        *this = Profiler{};
        active_ = on;
        if (on) {
            process_before_ = sh::platform::read_process_stats();
        }
    }

    bool has_rows() const { return !rows_.empty(); }
    const std::vector<std::string>& rows() const { return rows_; }

    void frame(std::uint64_t now, std::uint64_t frequency) {
        if (seen_frame_) {
            times_.add(static_cast<float>(static_cast<double>(now - last_frame_) * 1000.0 /
                                          static_cast<double>(frequency)));
        }
        seen_frame_ = true;
        last_frame_ = now;
        // The device hands its counters out once a frame, to the always-on history: read them from there.
        gpu_ = sh::dump::perf_frame_stats();
        draws_ += gpu_.drawCalls;
        triangles_ += gpu_.triangles;
        if (gpu_.gpuMs >= 0.0) {
            gpu_ms_ += gpu_.gpuMs;
            gpu_timed_ = true;
        }
        for (std::uint32_t i = 0; i < gpu_.passCount && i < gpu_.passes.size(); ++i) {
            const std::string name(gpu_.passes[i].name.data());
            auto it = std::find_if(passes_.begin(), passes_.end(), [&](const auto& p) { return p.first == name; });
            if (it == passes_.end()) {
                passes_.emplace_back(name, gpu_.passes[i].ms);
            } else {
                it->second += gpu_.passes[i].ms;
            }
        }
    }

    void close_window(const FrameWindow::Closed& window) {
        const int frames = window.frames;
        const double seconds = window.seconds();
        const double rate = frames / seconds;
        rows_.assign(kRows, std::string());
        rows_[0] = format("FPS\t%.0f   %.2f ms", rate, 1000.0 / rate);

        if (times_.count() > 0) {
            const double slow = times_.slowest_percent();
            rows_[1] = format("1%% low\t%.0f   %.2f ms", slow > 0.0 ? 1000.0 / slow : 0.0, slow);
        } else {
            rows_[1] = "1% low\tn/a";
        }

        const sh::platform::ProcessStats process = sh::platform::read_process_stats();
        if (process.processCpuSec >= 0.0 && process_before_.processCpuSec >= 0.0) {
            // Percent of one core, as `top` shows it: several busy threads read above 100.
            const double all = (process.processCpuSec - process_before_.processCpuSec) / seconds * 100.0;
            if (process.threadCpuSec >= 0.0 && process_before_.threadCpuSec >= 0.0) {
                const double main = (process.threadCpuSec - process_before_.threadCpuSec) / seconds * 100.0;
                rows_[2] = format("CPU\t%.0f%%   main thread %.0f%%", all, main);
            } else {
                rows_[2] = format("CPU\t%.0f%%", all);
            }
        } else {
            rows_[2] = "CPU\tn/a";
        }
        process_before_ = process;

        const sh::platform::GpuUsage usage = sh::platform::read_gpu_usage();
        const std::string busy = usage.busyPercent >= 0 ? format("%d%%", usage.busyPercent) : std::string("n/a");
        const std::string time = gpu_timed_ ? format("%.2f ms", gpu_ms_ / frames) : std::string("n/a ms");
        rows_[3] = "GPU\t" + busy + "   " + time;

        rows_[4] = process.rssKb >= 0 ? format("RAM\t%lld MB", megabytes(process.rssKb)) : std::string("RAM\tn/a");

        // The game's own share first, then the whole card: the total includes what other programs take.
        // Where the figure comes from: the driver's count of the process, or only the engine's own allocations.
        if (gpu_.appVramKb >= 0)
            rows_[5] = format("VRAM game\t%lld MB   %s", megabytes(gpu_.appVramKb),
                              gpu_.appVramFromDriver ? "driver" : "allocations");
        else
            rows_[5] = "VRAM game\tn/a";
        long long used = gpu_.vramUsedKb;
        long long total = gpu_.vramTotalKb;
        if (used < 0 || total < 0) {
            used = usage.vramUsedKb;
            total = usage.vramTotalKb;
        }
        if (used >= 0 && total >= 0) {
            rows_[6] = format("VRAM total\t%lld / %lld MB", megabytes(used), megabytes(total));
        } else if (gpu_.vramFreeKb >= 0) {
            rows_[6] = format("VRAM total\t%lld MB free", megabytes(gpu_.vramFreeKb));
        } else {
            rows_[6] = "VRAM total\tn/a";
        }

        rows_[7] = format("Draws\t%llu   %llu tris", static_cast<unsigned long long>(draws_ / frames),
                          static_cast<unsigned long long>(triangles_ / frames));

        // GPU time per pass, where the backend measures it (native Vulkan), above the blank last row.
        std::sort(passes_.begin(), passes_.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
        std::string logged;
        for (const auto& [name, ms] : passes_) {
            rows_.insert(rows_.end() - 1, format("  %s\t%.3f ms", name.c_str(), ms / frames));
            logged += format("%s%s %.3f ms", logged.empty() ? "" : ", ", name.c_str(), ms / frames);
        }
        const std::uint64_t now = SDL_GetTicks();
        if (!logged.empty() && (logged_at_ms_ == 0 || now - logged_at_ms_ >= kLogPeriodMs)) {
            logged_at_ms_ = now;
            SH_LOG("[GPU] pass times per frame: %s", logged.c_str());
        }
        passes_.clear();
        if (const std::string recording = sh::dump::perf_record_brief(); !recording.empty()) {
            rows_.insert(rows_.end() - 1, "REC\t" + recording);
        }

        gpu_ms_ = 0.0;
        gpu_timed_ = false;
        draws_ = 0;
        triangles_ = 0;
    }

private:
    bool active_ = false;
    FrameTimes times_;
    bool seen_frame_ = false;
    std::uint64_t last_frame_ = 0;
    double gpu_ms_ = 0.0;
    bool gpu_timed_ = false;
    std::uint64_t draws_ = 0;
    std::uint64_t triangles_ = 0;
    sh::rhi::ProfileStats gpu_;
    sh::platform::ProcessStats process_before_;
    std::vector<std::string> rows_;
    std::vector<std::pair<std::string, double>> passes_;  // name, milliseconds summed over the window
    // The pass times go to the log too, but not every window.
    static constexpr std::uint64_t kLogPeriodMs = 10000;
    std::uint64_t logged_at_ms_ = 0;
};

// The newest frames' times as bars under the profiler's figures, against a line at the frame cap's time: a hitch is a
// bar far above its neighbours, uneven pacing a ragged top. Overlay quads only, no GPU readback.
class FrameGraph {
public:
    static constexpr int kFrames = 240;

    void add(float ms) {
        times_[static_cast<std::size_t>(head_)] = ms;
        head_ = (head_ + 1) % kFrames;
        count_ = std::min(count_ + 1, kFrames);
    }
    void reset() { head_ = count_ = 0; }

    void draw(float x, float y, float w, float h, float cap_ms) const {
        if (count_ == 0) return;
        sh::overlay::Overlay& overlay = sh::overlay::overlay();
        overlay.quad(sh::overlay::Space::Picture, {x, y, w, h}, {0.0f, 0.0f, 0.0f, 0.45f});
        float sum = 0.0f, most = 0.0f;
        for (int i = 0; i < count_; ++i) {
            sum += times_[static_cast<std::size_t>(i)];
            most = std::max(most, times_[static_cast<std::size_t>(i)]);
        }
        const float average = sum / static_cast<float>(count_);
        const float target = cap_ms > 0.0f ? cap_ms : average;
        // Twice the target fits, so steady frames sit mid-height; taller spikes widen the scale up to 100 ms.
        const float top = std::clamp(std::max(target * 2.0f, most * 1.1f), 4.0f, 100.0f);
        const float bar = w / static_cast<float>(kFrames);
        for (int i = 0; i < count_; ++i) {
            const int at = (head_ - count_ + i + kFrames) % kFrames;
            const float ms = times_[static_cast<std::size_t>(at)];
            const float bh = std::min(ms / top, 1.0f) * h;
            const sh::overlay::Color color = ms >= 2.0f * average && ms >= 10.0f
                                                 ? sh::overlay::Color{1.0f, 0.25f, 0.2f, 0.95f}
                                             : ms > target * 1.25f ? sh::overlay::Color{1.0f, 0.8f, 0.2f, 0.9f}
                                                                   : sh::overlay::Color{0.4f, 0.9f, 0.5f, 0.8f};
            overlay.quad(sh::overlay::Space::Picture,
                         {x + w - static_cast<float>(count_ - i) * bar, y + h - bh, std::max(bar, 1.0f), bh}, color);
        }
        const float line = std::max(1.0f, h * 0.012f);
        overlay.quad(sh::overlay::Space::Picture, {x, y + h - std::min(target / top, 1.0f) * h - line * 0.5f, w, line},
                     {1.0f, 1.0f, 1.0f, 0.7f});
    }

private:
    std::array<float, kFrames> times_{};
    int head_ = 0;
    int count_ = 0;
};

// fps_display: 1 top left, 2 top right, 3 bottom left, 4 bottom right.
bool on_right(int corner) { return corner == 2 || corner == 4; }
bool at_bottom(int corner) { return corner == 3 || corner == 4; }

}  // namespace

void draw_fps_counter() {
    static Profiler profiler;
    static FrameWindow window;
    static int rate = 0;
    static TextPanel panel;
    static FrameGraph graph;
    static std::uint64_t graph_last = 0;

    const std::uint64_t now = SDL_GetPerformanceCounter();
    const std::uint64_t frequency = SDL_GetPerformanceFrequency();
    const bool profiling = g_PcConfig.fpsProfiler != 0;
    // The profiler alone still needs a corner.
    const int corner = profiling && g_PcConfig.fpsDisplay == 0 ? 1 : g_PcConfig.fpsDisplay;

    sh::dump::perf_frame();
    profiler.set_active(profiling);
    if (corner < 1 || corner > 4) {
        window.reset();
        return;
    }
    if (profiling) {
        profiler.frame(now, frequency);
        if (graph_last != 0) {
            graph.add(
                static_cast<float>(static_cast<double>(now - graph_last) * 1000.0 / static_cast<double>(frequency)));
        }
        graph_last = now;
    } else {
        graph.reset();
        graph_last = 0;
    }
    if (FrameWindow::Closed closed; window.frame(now, frequency, closed)) {
        rate = closed.rate();
        if (profiling) {
            profiler.close_window(closed);
        }
    }

    const sh::overlay::Overlay& overlay = sh::overlay::overlay();
    if (!shared_font().ready() || !overlay.frame().valid()) {
        return;
    }
    const float picture_width = overlay.width();
    const float picture_height = overlay.height();

    const bool figures = profiling && profiler.has_rows();
    const std::vector<std::string> one_line{format("%d FPS", rate)};
    const int text_height = std::max(static_cast<int>((figures ? 0.022f : 0.026f) * picture_height), 12);
    if (!panel.set(figures ? profiler.rows() : one_line, text_height)) {
        return;
    }
    const float margin = 0.02f * picture_height;
    const float padding = 0.008f * picture_height;
    const float panel_x = on_right(corner) ? picture_width - margin - panel.box_width(padding) : margin;
    const float panel_y = at_bottom(corner) ? picture_height - margin - panel.box_height(padding) : margin;
    panel.draw({panel_x, panel_y}, padding);
    if (figures) {
        const float graph_w = std::max(panel.box_width(padding), 0.22f * picture_width);
        const float graph_h = 0.09f * picture_height;
        const float graph_x = on_right(corner) ? picture_width - margin - graph_w : margin;
        const float graph_y =
            at_bottom(corner) ? panel_y - padding - graph_h : panel_y + panel.box_height(padding) + padding;
        graph.draw(graph_x, graph_y, graph_w, graph_h, g_PcConfig.fpsCap > 0 ? 1000.0f / g_PcConfig.fpsCap : 0.0f);
    }
}

}  // namespace sh::hud
