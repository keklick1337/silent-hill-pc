// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// perfrec and perfbench: the performance recorder and its A/B bench.

#include "engine/debug/commands/command_groups.h"
#include "engine/debug/commands/command_kit.h"
#include "features/dump/perf_bench.h"
#include "features/dump/perf_recorder.h"

#include <cctype>
#include <sstream>
#include <string>
#include <vector>

namespace sh::debug {

namespace {

using namespace sh::debug::commands;

void say_lines(Context& ctx, const std::string& text) {
    std::istringstream lines(text);
    for (std::string line; std::getline(lines, line);) say_text(ctx, line);
}

Result<void> run_perfrec(Context& ctx, const Args& args) {
    const std::vector<std::string> words = spaced_words(args);
    const std::string verb = words.empty() ? std::string() : lower(words[0]);
    if (verb.empty() || verb == "status") {
        say_text(ctx, sh::dump::perf_record_status());
        if (const std::string brief = sh::dump::perf_record_brief(); !brief.empty()) say_text(ctx, "  " + brief);
        return done();
    }
    if (verb == "stop" || (verb == "toggle" && sh::dump::perf_record_active())) {
        auto r = sh::dump::perf_record_stop();
        if (!r) return Result<void>::fail(r.error());
        say_lines(ctx, r.value());
        return done();
    }
    if (verb != "start" && verb != "toggle") {
        return Result<void>::fail("perfrec start [name] [all|sampled|off] | stop | toggle | status");
    }
    sh::dump::PerfRecordRequest request;
    for (std::size_t i = 1; i < words.size(); ++i) {
        const std::string w = lower(words[i]);
        if (w == "all" || w == "sampled" || w == "off") {
            request.timing = sh::dump::pass_timing_from_name(w, nullptr);
        } else if (request.name.empty()) {
            request.name = words[i];
        }
    }
    auto r = sh::dump::perf_record_start(request);
    if (!r) return Result<void>::fail(r.error());
    say(ctx, "perfrec: recording to %s (pass timing %s); 'perfrec stop' ends it", r.value().c_str(),
        sh::dump::pass_timing_name(request.timing));
    return done();
}

Result<void> run_perfbench(Context& ctx, const Args& args) {
    std::vector<std::string> words = spaced_words(args);
    const std::string verb = words.empty() ? std::string() : lower(words[0]);
    if (verb == "stop") {
        auto r = sh::dump::perf_bench_stop();
        if (!r) return Result<void>::fail(r.error());
        say_text(ctx, r.value());
        return done();
    }
    if (verb == "status") {
        say_text(ctx, sh::dump::perf_bench_status());
        return done();
    }
    if (verb == "list") {
        std::string line = "perfbench variants:";
        for (const sh::dump::PerfBenchFeature& f : sh::dump::perf_bench_features()) {
            line += std::string(" ") + f.name + "(" + f.key + ")";
            if (line.size() > 180) {
                say_text(ctx, line);
                line = " ";
            }
        }
        say_text(ctx, line);
        say_text(ctx, "or any Options graphics key as key=value, e.g. render_scale=1 upscaler=0");
        return done();
    }
    double seconds = 10.0;
    std::size_t first = 0;
    if (!words.empty() && !words[0].empty() && (std::isdigit(static_cast<unsigned char>(words[0][0])) != 0)) {
        seconds = real(words[0]);
        first = 1;
    }
    sh::dump::PassTiming timing = sh::dump::PassTiming::All;
    std::vector<std::string> variants;
    for (std::size_t i = first; i < words.size(); ++i) {
        const std::string w = lower(words[i]);
        if (w == "timing=off" || w == "timing=all" || w == "timing=sampled") {
            timing = sh::dump::pass_timing_from_name(w.substr(7), nullptr);
            continue;
        }
        variants.push_back(words[i]);
    }
    auto r = sh::dump::perf_bench_start(seconds, variants, timing);
    if (!r) return Result<void>::fail(r.error());
    say_text(ctx, r.value());
    return done();
}

}  // namespace

namespace commands {

void register_perf_commands(sh::console::Registry& registry) {
    add(registry, "perfrec", "debug", "record frame times, hitches and GPU pass times to perfrec/<name>.csv",
        {arg("what", ArgType::Rest, true, "start [name] [all|sampled|off] | stop | toggle | status")}, run_perfrec, {},
        "perfrec start [name] [timing]   timing: sampled (default: per-pass GPU times 3 windows in 4, the rest\n"
        "                                measures what the timestamps cost), all, off (one GPU total per frame)\n"
        "perfrec stop                    writes the summary; tools/scripts/perf_report.py reads the files\n"
        "config.cfg perf_record = 1 records from the start until the game quits");
    add(registry, "perfbench", "debug", "A/B bench: record N seconds here per graphics variant",
        {arg("args", ArgType::Rest, true, "[seconds] [variant|key=value ...] [timing=all|sampled|off] | list | stop")},
        run_perfbench, {},
        "perfbench [seconds] [variants]  baseline, one recording per variant (one key changed in the running game\n"
        "                                only), baseline again; perfrec/bench-<time>/. No variants: all of them.\n"
        "perfbench list | status | stop");
}

}  // namespace commands
}  // namespace sh::debug
