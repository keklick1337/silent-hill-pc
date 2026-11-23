// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/devtools/perf_record.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>

namespace sh::devtools {

namespace {

// Frames before the running average is trusted for hitches: the first frames of a recording are often long.
constexpr int kWarmupFrames = 10;
constexpr double kAverageWeight = 0.05;

std::string number(double v) {
    if (!std::isfinite(v) || v < 0.0) return {};
    char text[40];
    std::snprintf(text, sizeof(text), "%.3f", v);
    std::string s(text);
    while (!s.empty() && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
    return s;
}

// Coordinates and angles may be negative.
std::string signed_number(double v) {
    if (!std::isfinite(v)) return {};
    return v < 0.0 ? "-" + number(-v) : number(v);
}

std::string whole(long long v) { return v < 0 ? std::string() : std::to_string(v); }

std::string quoted(std::string_view text) {
    if (text.find_first_of(",\"\n\r") == std::string_view::npos) return std::string(text);
    std::string out = "\"";
    for (const char c : text) {
        if (c == '"') out += '"';
        out += c;
    }
    return out + '"';
}

void join(std::string& line, const std::string& cell) {
    if (!line.empty()) line += ',';
    line += cell;
}

}  // namespace

PerfRecordCore::PerfRecordCore() : PerfRecordCore(Options{}) {}

PerfRecordCore::PerfRecordCore(const Options& options) : options_(options), histogram_(kBins + 1, 0) {
    options_.hitch_gpu_frames = std::max(options_.hitch_gpu_frames, 1);
}

int PerfRecordCore::pass_id(std::string_view name) {
    for (std::size_t i = 0; i < pass_names_.size(); ++i) {
        if (pass_names_[i] == name) return static_cast<int>(i);
    }
    pass_names_.emplace_back(name);
    return static_cast<int>(pass_names_.size() - 1);
}

void PerfRecordCore::accumulate(Window& w, const PerfRecordFrame& f, double previous_ms) {
    const double ms = f.frame_ms;
    ++w.frames;
    w.seconds += ms / 1000.0;
    w.sum_sq += ms * ms;
    w.max_ms = std::max(w.max_ms, ms);
    if (previous_ms >= 0.0) {
        w.jitter_sum += std::fabs(ms - previous_ms);
        ++w.jitter_count;
    }
    w.times.push_back(ms);
    if (f.gpu_ms >= 0.0) {
        w.gpu_sum += f.gpu_ms;
        ++w.gpu_frames;
        if (!f.passes.empty()) ++w.timed_frames;
    }
    for (const auto& [id, pass_ms] : f.passes) {
        if (id < 0) continue;
        const std::size_t i = static_cast<std::size_t>(id);
        if (w.pass_sum.size() <= i) {
            w.pass_sum.resize(i + 1, 0.0);
            w.pass_frames.resize(i + 1, 0);
        }
        w.pass_sum[i] += pass_ms;
        ++w.pass_frames[i];
    }
    w.cpu_update += f.cpu_update_ms;
    w.cpu_render += f.cpu_render_ms;
    w.wait += f.wait_ms;
    w.draws += f.draws;
    w.triangles += f.triangles;
    if (f.patch_triangles >= 0.0) {
        w.patches += f.patch_triangles;
        ++w.patch_frames;
    }
    w.pipelines += f.pipelines_built;
    w.pipeline_ms += f.pipeline_build_ms;
    w.upload_kb += f.texture_upload_kb;
    w.swaps += f.upscale_swaps;
    w.cells += f.cells_loaded;
}

PerfRecordStats PerfRecordCore::close(const Window& w) const {
    PerfRecordStats s;
    s.frames = w.frames;
    s.seconds = w.seconds;
    if (w.frames == 0) return s;
    const double n = static_cast<double>(w.frames);
    s.frame_ms_avg = w.seconds * 1000.0 / n;
    s.fps_avg = w.seconds > 0.0 ? n / w.seconds : 0.0;
    std::vector<double> slowest = w.times;
    std::sort(slowest.begin(), slowest.end(), std::greater<>());
    const std::size_t k = std::max<std::size_t>(1, slowest.size() / 100);
    double worst = 0.0;
    for (std::size_t i = 0; i < k; ++i) worst += slowest[i];
    worst /= static_cast<double>(k);
    s.fps_low_1pct = worst > 0.0 ? 1000.0 / worst : 0.0;
    s.frame_ms_max = w.max_ms;
    s.frame_ms_stdev = std::sqrt(std::max(0.0, w.sum_sq / n - s.frame_ms_avg * s.frame_ms_avg));
    s.jitter_ms = w.jitter_count > 0 ? w.jitter_sum / w.jitter_count : 0.0;
    s.hitches = w.hitches;
    s.gpu_ms = w.gpu_frames > 0 ? w.gpu_sum / w.gpu_frames : -1.0;
    s.pass_timing = w.gpu_frames > 0 ? static_cast<double>(w.timed_frames) / w.gpu_frames : 0.0;
    s.pass_ms.assign(pass_names_.size(), -1.0);
    for (std::size_t i = 0; i < w.pass_sum.size() && i < s.pass_ms.size(); ++i) {
        if (w.pass_frames[i] > 0) s.pass_ms[i] = w.pass_sum[i] / w.pass_frames[i];
    }
    s.cpu_update_ms = w.cpu_update / n;
    s.cpu_render_ms = w.cpu_render / n;
    s.wait_ms = w.wait / n;
    s.draws = w.draws / n;
    s.triangles = w.triangles / n;
    s.patch_triangles = w.patch_frames > 0 ? w.patches / w.patch_frames : -1.0;
    s.pipelines_built = w.pipelines;
    s.pipeline_build_ms = w.pipeline_ms;
    s.texture_upload_kb = w.upload_kb;
    s.upscale_swaps = w.swaps;
    s.cells_loaded = w.cells;
    return s;
}

void PerfRecordCore::finish_hitch() {
    if (!hitch_) return;
    hitch_ = false;
    hitch_row_.stats.pass_ms.resize(pass_names_.size(), -1.0);
    rows_.push_back(std::move(hitch_row_));
    hitch_row_ = {};
}

PerfRecordCore::Added PerfRecordCore::add(const PerfRecordFrame& f) {
    Added out;
    const double ms = std::max(f.frame_ms, 0.0);
    time_s_ += ms / 1000.0;

    ++frames_;
    sum_ += ms;
    sum_sq_ += ms * ms;
    max_ms_ = std::max(max_ms_, ms);
    ++histogram_[static_cast<std::size_t>(std::min(static_cast<int>(ms * 10.0), kBins))];
    if (previous_ms_ >= 0.0) {
        jitter_sum_ += std::fabs(ms - previous_ms_);
        ++jitter_count_;
    }

    const bool hitch =
        seen_ >= kWarmupFrames && ms >= options_.hitch_floor_ms && ms >= options_.hitch_factor * average_ms_;
    if (!hitch) {
        average_ms_ = seen_ == 0 ? ms : average_ms_ + (ms - average_ms_) * kAverageWeight;
        ++seen_;
    }

    // A hitch's GPU work shows up in the frames after it: keep the longest GPU time of the frames waited for.
    if (hitch_) {
        PerfRecordStats& s = hitch_row_.stats;
        if (f.gpu_ms > s.gpu_ms) {
            s.gpu_ms = f.gpu_ms;
            s.pass_timing = f.passes.empty() ? 0.0 : 1.0;
            s.pass_ms.assign(pass_names_.size(), -1.0);
            for (const auto& [id, pass_ms] : f.passes) {
                if (id >= 0 && static_cast<std::size_t>(id) < s.pass_ms.size())
                    s.pass_ms[static_cast<std::size_t>(id)] = pass_ms;
            }
        }
        if (--hitch_wait_ <= 0) finish_hitch();
    }
    if (hitch) {
        finish_hitch();
        ++hitches_;
        hitch_ = true;
        hitch_wait_ = options_.hitch_gpu_frames;
        hitch_row_ = {};
        hitch_row_.time_s = time_s_;
        hitch_row_.hitch = true;
        Window single;
        accumulate(single, f, previous_ms_);
        single.hitches = 1;
        hitch_row_.stats = close(single);
        out.hitch_began = true;
    }

    accumulate(window_, f, previous_ms_);
    if (hitch) ++window_.hitches;
    previous_ms_ = ms;

    if (window_.seconds >= options_.window_seconds - 1e-9) {
        PerfRecordRow row;
        row.time_s = time_s_;
        row.stats = close(window_);
        const PerfRecordStats& s = row.stats;
        // The cost of the per-pass timestamps: windows fully with them against windows fully without, hitches left
        // out so one stall does not decide it.
        if (s.hitches == 0 && s.gpu_ms >= 0.0) {
            if (s.pass_timing >= 0.999) {
                ++rows_timed_;
                gpu_timed_ += s.gpu_ms;
                frame_timed_ += s.frame_ms_avg;
            } else if (s.pass_timing <= 0.001) {
                ++rows_untimed_;
                gpu_untimed_ += s.gpu_ms;
                frame_untimed_ += s.frame_ms_avg;
            }
        }
        rows_.push_back(std::move(row));
        window_ = {};
        ++windows_closed_;
        out.window_closed = true;
    }
    return out;
}

void PerfRecordCore::flush() {
    finish_hitch();
    if (window_.frames > 0) {
        PerfRecordRow row;
        row.time_s = time_s_;
        row.stats = close(window_);
        rows_.push_back(std::move(row));
        window_ = {};
        ++windows_closed_;
    }
}

PerfRecordSummary PerfRecordCore::summary() const {
    PerfRecordSummary s;
    s.frames = frames_;
    s.seconds = sum_ / 1000.0;
    s.hitches = hitches_;
    s.rows_timed = rows_timed_;
    s.rows_untimed = rows_untimed_;
    if (rows_timed_ > 0) {
        s.gpu_ms_timed = gpu_timed_ / rows_timed_;
        s.frame_ms_timed = frame_timed_ / rows_timed_;
    }
    if (rows_untimed_ > 0) {
        s.gpu_ms_untimed = gpu_untimed_ / rows_untimed_;
        s.frame_ms_untimed = frame_untimed_ / rows_untimed_;
    }
    if (frames_ == 0) return s;
    const double n = static_cast<double>(frames_);
    const double average = sum_ / n;
    s.fps_avg = sum_ > 0.0 ? n * 1000.0 / sum_ : 0.0;
    s.frame_ms_max = max_ms_;
    s.frame_ms_stdev = std::sqrt(std::max(0.0, sum_sq_ / n - average * average));
    s.jitter_ms = jitter_count_ > 0 ? jitter_sum_ / static_cast<double>(jitter_count_) : 0.0;
    const auto percentile = [&](double q) {
        const double wanted = std::ceil(q * n);
        double seen = 0.0;
        for (int bin = 0; bin <= kBins; ++bin) {
            seen += histogram_[static_cast<std::size_t>(bin)];
            if (seen >= wanted) return bin == kBins ? max_ms_ : std::min((bin + 0.5) / 10.0, max_ms_);
        }
        return max_ms_;
    };
    s.frame_ms_p50 = percentile(0.5);
    s.frame_ms_p99 = percentile(0.99);
    s.frame_ms_p999 = percentile(0.999);
    return s;
}

std::string perf_csv_header(const std::vector<std::string>& pass_names) {
    std::string line =
        "time_s,kind,frames,map,room,player_x,player_y,player_z,player_yaw,camera_x,camera_y,camera_z,camera_yaw,"
        "cutscene,fps_avg,fps_1pct_low,frame_ms_avg,frame_ms_max,frame_ms_stdev,jitter_ms,hitches,cpu_main_ms,"
        "cpu_update_ms,cpu_render_ms,wait_ms,gpu_ms,gpu_busy_pct,pass_timing,draws,triangles,patch_triangles,lights,"
        "rt,vram_mb,uploads_pending,pipelines_built,pipeline_build_ms,texture_upload_kb,upscale_swaps,cells_loaded";
    for (const std::string& name : pass_names) join(line, quoted("gpu:" + name));
    return line;
}

std::string perf_csv_line(const PerfRecordRow& row) {
    const PerfRecordStats& s = row.stats;
    const PerfRecordContext& c = row.context;
    std::string line;
    for (const std::string& cell : {number(row.time_s),
                                    std::string(row.hitch ? "hitch" : "window"),
                                    whole(s.frames),
                                    quoted(c.map),
                                    whole(c.room),
                                    signed_number(c.player[0]),
                                    signed_number(c.player[1]),
                                    signed_number(c.player[2]),
                                    signed_number(c.player_yaw),
                                    signed_number(c.camera[0]),
                                    signed_number(c.camera[1]),
                                    signed_number(c.camera[2]),
                                    signed_number(c.camera_yaw),
                                    std::string(c.cutscene ? "1" : "0"),
                                    number(s.fps_avg),
                                    number(s.fps_low_1pct),
                                    number(s.frame_ms_avg),
                                    number(s.frame_ms_max),
                                    number(s.frame_ms_stdev),
                                    number(s.jitter_ms),
                                    whole(s.hitches),
                                    number(c.cpu_main_ms),
                                    number(s.cpu_update_ms),
                                    number(s.cpu_render_ms),
                                    number(s.wait_ms),
                                    number(s.gpu_ms),
                                    number(c.gpu_busy_pct),
                                    number(s.pass_timing),
                                    number(s.draws),
                                    number(s.triangles),
                                    number(s.patch_triangles),
                                    whole(c.lights),
                                    std::string(c.rt ? "1" : "0"),
                                    number(c.vram_mb),
                                    whole(c.uploads_pending),
                                    number(s.pipelines_built),
                                    number(s.pipeline_build_ms),
                                    number(s.texture_upload_kb),
                                    number(s.upscale_swaps),
                                    number(s.cells_loaded)}) {
        line += cell;
        line += ',';
    }
    line.pop_back();
    for (const double ms : s.pass_ms) {
        line += ',';
        line += number(ms);
    }
    return line;
}

std::string perf_frames_header() {
    return "time_s,frame_ms,gpu_ms,cpu_update_ms,cpu_render_ms,wait_ms,pipelines_built,texture_upload_kb,"
           "upscale_swaps,cells_loaded";
}

std::string perf_frames_line(double time_s, const PerfRecordFrame& f) {
    std::string line;
    for (const std::string& cell : {number(time_s), number(f.frame_ms), number(f.gpu_ms), number(f.cpu_update_ms),
                                    number(f.cpu_render_ms), number(f.wait_ms), number(f.pipelines_built),
                                    number(f.texture_upload_kb), number(f.upscale_swaps), number(f.cells_loaded)}) {
        line += cell;
        line += ',';
    }
    line.pop_back();
    return line;
}

std::string perf_record_file_name(std::string_view name) {
    std::string out;
    for (const char c : name) {
        const bool kept = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' ||
                          c == '_' || c == '.';
        if (kept) out += c;
    }
    while (!out.empty() && out.front() == '.') out.erase(out.begin());
    return out;
}

}  // namespace sh::devtools
