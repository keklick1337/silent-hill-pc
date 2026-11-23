// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "features/dump/perf_bench.h"

#include "engine/debug/overlay/debug_overlay.h"
#include "engine/log/sh_log.h"
#include "ui/menu/options_binds.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>

namespace sh::dump {

namespace {

namespace dt = sh::devtools;

constexpr double kSettleSeconds = 3.0;
// Keys that rebuild or reload something when they change: their first seconds are not the steady cost.
constexpr double kSlowSettleSeconds = 6.0;

bool slow_key(const std::string& key) {
    return key == "texture_upscale" || key == "render_scale" || key == "model_upscaling" || key == "ray_tracing" ||
           key == "upscaler";
}

struct Step {
    std::string label;
    std::string key;  // empty: the settings as they are
    float value = 0.0f;
    float original = 0.0f;
    dt::PerfRecordSummary result;
    bool done = false;
};

enum class Phase { Idle, Settle, Record };

struct Bench {
    Phase phase = Phase::Idle;
    std::vector<Step> steps;
    std::size_t at = 0;
    double seconds = 10.0;
    double clock = 0.0;  // seconds in the current phase
    PassTiming timing = PassTiming::All;
    std::string folder;
};
Bench s_bench;

void say(const std::string& line) {
    SH_LOG_INFO(RENDER, "[PERFBENCH] %s", line.c_str());
    sh::debug::console_print(line.c_str());
}

std::string stamp() {
    const std::time_t now = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &now);
#else
    localtime_r(&now, &tm);
#endif
    char text[32];
    std::strftime(text, sizeof(text), "bench-%Y%m%d-%H%M%S", &tm);
    return text;
}

std::string value_text(float v) {
    char text[24];
    std::snprintf(text, sizeof(text), "%g", static_cast<double>(v));
    return text;
}

double gpu_ms(const dt::PerfRecordSummary& s) { return s.rows_timed > 0 ? s.gpu_ms_timed : s.gpu_ms_untimed; }

void apply(const Step& step, bool original) {
    if (!step.key.empty()) Pc_Options_ApplyGraphicsKeyLive(step.key.c_str(), original ? step.original : step.value);
}

void report() {
    if (s_bench.steps.empty() || !s_bench.steps.front().done) return;
    const dt::PerfRecordSummary& base = s_bench.steps.front().result;
    say("perfbench results (perfrec/" + s_bench.folder + "/), against the first baseline:");
    say("  variant                         fps     ms   p99 ms  p99.9  jitter  GPU ms  dGPU ms  dframe ms");
    for (const Step& step : s_bench.steps) {
        if (!step.done) continue;
        const dt::PerfRecordSummary& r = step.result;
        char text[220];
        std::snprintf(
            text, sizeof(text), "  %-28s %6.1f %6.2f %7.2f %6.2f %6.2f %7.2f %+8.3f %+9.3f", step.label.c_str(),
            r.fps_avg, r.fps_avg > 0.0 ? 1000.0 / r.fps_avg : 0.0, r.frame_ms_p99, r.frame_ms_p999, r.jitter_ms,
            gpu_ms(r), gpu_ms(r) - gpu_ms(base),
            (r.fps_avg > 0.0 ? 1000.0 / r.fps_avg : 0.0) - (base.fps_avg > 0.0 ? 1000.0 / base.fps_avg : 0.0));
        say(text);
    }
    say("  (a negative dGPU means the variant is cheaper; tools/scripts/perf_report.py bench <folder> sorts them)");
}

void finish() {
    if (perf_record_active()) (void)perf_record_stop();
    if (s_bench.at < s_bench.steps.size()) apply(s_bench.steps[s_bench.at], true);
    report();
    s_bench = Bench{};
}

void begin_step() {
    Step& step = s_bench.steps[s_bench.at];
    apply(step, false);
    s_bench.phase = Phase::Settle;
    s_bench.clock = 0.0;
    say("perfbench " + std::to_string(s_bench.at + 1) + "/" + std::to_string(s_bench.steps.size()) + ": " + step.label);
}

}  // namespace

const std::vector<PerfBenchFeature>& perf_bench_features() {
    static const std::vector<PerfBenchFeature> features = {
        {"rt", "ray_tracing", 1, 0},
        {"rt_shadows", "rt_shadows", 1, 0},
        {"rt_reflections", "rt_reflections", 1, 0},
        {"rt_gi", "rt_gi", 1, 0},
        {"rt_lights", "rt_lights", 1, 0},
        {"volfog", "volumetric_fog", 1, 0},
        {"godrays", "god_rays", 1, 0},
        {"ao", "ambient_occlusion", 1, 0},
        {"taa", "upscaler", 1, 0},
        {"taau", "upscaler", 2, 1},
        {"scale", "render_scale", 1.0f, 0.5f},
        {"model_upscaling", "model_upscaling", 1, 0},
        {"texture_upscale", "texture_upscale", 2, 0},
        {"extra_lights", "extra_lights", 1, 0},
        {"map_lights", "map_lights", 1, 0},
        {"sun_shadows", "sun_shadows", 1, 0},
        {"particle_shadows", "particle_shadows", 1, 0},
        {"flashlight_shadows", "shadows", 1, 0},
        {"hdr", "hdr_frame", 1, 0},
        {"bloom", "bloom", 0.3f, 0},
        {"water", "water_quality", 2, 0},
        {"post", "post_process", 5, 0},
        {"aniso", "texture_filter", 6, 2},
    };
    return features;
}

Result<std::string> perf_bench_start(double seconds, const std::vector<std::string>& variants, PassTiming timing) {
    if (s_bench.phase != Phase::Idle) return Result<std::string>::fail("a bench is running ('perfbench stop')");
    if (perf_record_active()) return Result<std::string>::fail("a recording is running ('perfrec stop' first)");
    Bench bench;
    bench.seconds = std::max(seconds, 1.0);
    bench.timing = timing;
    bench.folder = stamp();
    bench.steps.push_back(Step{"baseline"});
    const auto add = [&](const std::string& label, const std::string& key, float value) -> bool {
        float now = 0.0f;
        if (!Pc_Options_GraphicsKeyValue(key.c_str(), &now)) return false;
        bench.steps.push_back(Step{label + " " + key + "=" + value_text(value), key, value, now});
        return true;
    };
    const auto add_feature = [&](const PerfBenchFeature& f) {
        float now = 0.0f;
        Pc_Options_GraphicsKeyValue(f.key, &now);
        return add(f.name, f.key, std::fabs(now - f.on) < 1e-4f ? f.off : f.on);
    };
    if (variants.empty()) {
        for (const PerfBenchFeature& f : perf_bench_features()) add_feature(f);
    }
    for (const std::string& v : variants) {
        if (const std::size_t eq = v.find('='); eq != std::string::npos) {
            const std::string key = v.substr(0, eq);
            if (!add(key, key, static_cast<float>(std::atof(v.c_str() + eq + 1)))) {
                return Result<std::string>::fail("perfbench: '" + key + "' is not a graphics key Options can change");
            }
            continue;
        }
        bool found = false;
        for (const PerfBenchFeature& f : perf_bench_features()) {
            if (v == f.name) found = add_feature(f);
        }
        if (!found) return Result<std::string>::fail("perfbench: unknown variant '" + v + "' (perfbench list)");
    }
    bench.steps.push_back(Step{"baseline again"});
    s_bench = std::move(bench);
    const double settle = kSettleSeconds;
    double total = 0.0;
    for (const Step& step : s_bench.steps)
        total += s_bench.seconds + (slow_key(step.key) ? kSlowSettleSeconds : settle);
    begin_step();
    char text[160];
    std::snprintf(text, sizeof(text), "perfbench: %zu steps of %.0f s, about %.0f s; stand still, no input",
                  s_bench.steps.size(), s_bench.seconds, total);
    return Result<std::string>::ok(text);
}

Result<std::string> perf_bench_stop() {
    if (s_bench.phase == Phase::Idle) return Result<std::string>::fail("no bench is running");
    finish();
    return Result<std::string>::ok("perfbench stopped; settings restored");
}

bool perf_bench_active() { return s_bench.phase != Phase::Idle; }

std::string perf_bench_status() {
    if (s_bench.phase == Phase::Idle) return "perfbench: not running";
    const Step& step = s_bench.steps[s_bench.at];
    char text[200];
    std::snprintf(text, sizeof(text), "perfbench: step %zu/%zu %s, %s %.1f s", s_bench.at + 1, s_bench.steps.size(),
                  step.label.c_str(), s_bench.phase == Phase::Settle ? "settling" : "recording", s_bench.clock);
    return text;
}

void perf_bench_frame(double frame_ms, bool held) {
    if (s_bench.phase == Phase::Idle || held) return;
    s_bench.clock += frame_ms / 1000.0;
    Step& step = s_bench.steps[s_bench.at];
    if (s_bench.phase == Phase::Settle) {
        if (s_bench.clock < (slow_key(step.key) ? kSlowSettleSeconds : kSettleSeconds)) return;
        PerfRecordRequest request;
        char name[16];
        std::snprintf(name, sizeof(name), "%02zu_", s_bench.at);
        std::string label = step.key.empty() ? step.label : step.key + "_" + value_text(step.value);
        for (char& c : label) {
            if (c == ' ') c = '_';
        }
        request.name = name + label;
        request.folder = s_bench.folder;
        request.timing = s_bench.timing;
        request.note = step.label;
        if (auto r = perf_record_start(request); !r) {
            say("perfbench: " + r.error());
            finish();
            return;
        }
        s_bench.phase = Phase::Record;
        s_bench.clock = 0.0;
        return;
    }
    if (s_bench.clock < s_bench.seconds) return;
    (void)perf_record_stop();
    step.result = perf_record_last_summary();
    step.done = true;
    apply(step, true);
    if (++s_bench.at >= s_bench.steps.size()) {
        finish();
        return;
    }
    begin_step();
}

}  // namespace sh::dump
