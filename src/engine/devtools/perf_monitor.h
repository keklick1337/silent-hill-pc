// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Always-on performance history (`dump`): a ring of the last frames with the time every named
// section of the frame took and a set of per-frame counters. It runs whether or not the profiler overlay is shown, so
// a dump taken while the game lags holds the seconds before the command. Header-only: any module records into it
// without a link dependency. Main thread only.

#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace sh::devtools {

// One section or counter over the history.
struct PerfSeries {
    std::string name;
    std::string inside;  // the section this one's time is a part of, empty: the frame itself
    double last = 0.0, average = 0.0, max = 0.0;
    double in_slowest_frame = 0.0;  // its value in the longest frame of the history
    double calls_per_frame = 0.0;   // sections only
};

struct PerfReport {
    std::vector<double> frame_ms;  // oldest first
    double min_ms = 0.0, average_ms = 0.0, max_ms = 0.0, p99_ms = 0.0;
    double low_1pct_ms = 0.0;      // mean of the worst 1% of the history
    int stutter_frames = 0;        // frames longer than twice the average
    // Frames the console or the quick options held the game for are not recorded: `before_report` since the newest
    // recorded frame, `total` for the run.
    std::uint64_t held_frames_before_report = 0, held_frames_total = 0;
    std::vector<PerfSeries> sections;  // milliseconds per frame
    std::vector<PerfSeries> counters;  // units per frame
};

class PerfMonitor {
public:
    static constexpr int kFrames = 300;
    static constexpr int kMaxSections = 80;
    static constexpr int kMaxCounters = 24;

    // The id of a named section, registered on the first ask; -1 when the table is full.
    int section(std::string_view name, std::string_view inside = {}) {
        for (int i = 0; i < section_count_; ++i) {
            if (sections_[static_cast<std::size_t>(i)].name == name) return i;
        }
        if (section_count_ == kMaxSections) return -1;
        sections_[static_cast<std::size_t>(section_count_)] = {std::string(name), std::string(inside)};
        return section_count_++;
    }
    int counter(std::string_view name) {
        for (int i = 0; i < counter_count_; ++i) {
            if (counters_[static_cast<std::size_t>(i)].name == name) return i;
        }
        if (counter_count_ == kMaxCounters) return -1;
        counters_[static_cast<std::size_t>(counter_count_)] = {std::string(name), {}};
        return counter_count_++;
    }

    void add(int section, double ms) {
        if (section < 0) return;
        Open& o = open_sections_[static_cast<std::size_t>(section)];
        o.value += ms;
        ++o.calls;
    }
    void count(int counter, double amount) {
        if (counter >= 0) open_counters_[static_cast<std::size_t>(counter)] += amount;
    }

    // A section entered inside itself is timed once, by the outermost entry.
    bool enter(int section) { return section >= 0 && depth_[static_cast<std::size_t>(section)]++ == 0; }
    void leave(int section) {
        if (section >= 0) --depth_[static_cast<std::size_t>(section)];
    }

    // The frame is over: what was recorded since the last call becomes its row. A held frame is dropped.
    void end_frame(double frame_ms, bool held) {
        if (held) {
            ++held_run_;
            ++held_total_;
        } else {
            held_run_ = 0;
            const std::size_t row = static_cast<std::size_t>(head_);
            frame_ms_[row] = static_cast<float>(frame_ms);
            for (int i = 0; i < section_count_; ++i) {
                const Open& o = open_sections_[static_cast<std::size_t>(i)];
                section_ms_[static_cast<std::size_t>(i)][row] = static_cast<float>(o.value);
                section_calls_[static_cast<std::size_t>(i)][row] = static_cast<std::uint16_t>(std::min(o.calls, 65535u));
            }
            for (int i = 0; i < counter_count_; ++i) {
                counter_values_[static_cast<std::size_t>(i)][row] = open_counters_[static_cast<std::size_t>(i)];
            }
            head_ = (head_ + 1) % kFrames;
            if (frames_ < kFrames) ++frames_;
        }
        open_sections_.fill({});
        open_counters_.fill(0.0);
    }

    int frames() const { return frames_; }

    // The section's time in the newest recorded frame; 0 before any frame or for an unknown id.
    double newest(int section) const {
        if (section < 0 || section >= section_count_ || frames_ == 0) return 0.0;
        return section_ms_[static_cast<std::size_t>(section)]
                          [static_cast<std::size_t>((head_ + kFrames - 1) % kFrames)];
    }

    PerfReport report() const {
        PerfReport out;
        out.held_frames_before_report = held_run_;
        out.held_frames_total = held_total_;
        const int n = frames_;
        if (n == 0) return out;
        const auto row_of = [&](int age_index) {  // 0: the oldest recorded frame
            return static_cast<std::size_t>((head_ - n + age_index + 2 * kFrames) % kFrames);
        };
        out.frame_ms.reserve(static_cast<std::size_t>(n));
        std::size_t slowest = row_of(0);
        double sum = 0.0;
        for (int i = 0; i < n; ++i) {
            const std::size_t row = row_of(i);
            out.frame_ms.push_back(frame_ms_[row]);
            sum += frame_ms_[row];
            if (frame_ms_[row] > frame_ms_[slowest]) slowest = row;
        }
        std::vector<double> sorted = out.frame_ms;
        std::sort(sorted.begin(), sorted.end());
        out.min_ms = sorted.front();
        out.max_ms = sorted.back();
        out.average_ms = sum / n;
        out.p99_ms = sorted[static_cast<std::size_t>(std::max(0, (n * 99 + 99) / 100 - 1))];
        const int worst = std::max(1, n / 100);
        double worst_sum = 0.0;
        for (int i = 0; i < worst; ++i) worst_sum += sorted[static_cast<std::size_t>(n - 1 - i)];
        out.low_1pct_ms = worst_sum / worst;
        for (const double ms : out.frame_ms) {
            if (ms > 2.0 * out.average_ms) ++out.stutter_frames;
        }

        const std::size_t newest = row_of(n - 1);
        const auto series = [&](const auto& values, PerfSeries& s) {
            double total = 0.0;
            for (int i = 0; i < n; ++i) {
                const double v = values[row_of(i)];
                total += v;
                s.max = std::max(s.max, v);
            }
            s.average = total / n;
            s.last = values[newest];
            s.in_slowest_frame = values[slowest];
        };
        for (int i = 0; i < section_count_; ++i) {
            const std::size_t at = static_cast<std::size_t>(i);
            PerfSeries s;
            s.name = sections_[at].name;
            s.inside = sections_[at].inside;
            series(section_ms_[at], s);
            double calls = 0.0;
            for (int f = 0; f < n; ++f) calls += section_calls_[at][row_of(f)];
            s.calls_per_frame = calls / n;
            out.sections.push_back(std::move(s));
        }
        for (int i = 0; i < counter_count_; ++i) {
            const std::size_t at = static_cast<std::size_t>(i);
            PerfSeries s;
            s.name = counters_[at].name;
            series(counter_values_[at], s);
            out.counters.push_back(std::move(s));
        }
        return out;
    }

private:
    struct Name {
        std::string name, inside;
    };
    struct Open {
        double value = 0.0;
        std::uint32_t calls = 0;
    };

    std::array<Name, kMaxSections> sections_;
    std::array<Name, kMaxCounters> counters_;
    int section_count_ = 0, counter_count_ = 0;
    std::array<Open, kMaxSections> open_sections_{};
    std::array<double, kMaxCounters> open_counters_{};
    std::array<int, kMaxSections> depth_{};

    std::array<float, kFrames> frame_ms_{};
    std::array<std::array<float, kFrames>, kMaxSections> section_ms_{};
    std::array<std::array<std::uint16_t, kFrames>, kMaxSections> section_calls_{};
    std::array<std::array<double, kFrames>, kMaxCounters> counter_values_{};
    int head_ = 0, frames_ = 0;
    std::uint64_t held_run_ = 0, held_total_ = 0;
};

// The game's one history.
inline PerfMonitor& perf_monitor() {
    static PerfMonitor monitor;
    return monitor;
}

// Times its own lifetime into a section of perf_monitor().
class PerfScope {
public:
    explicit PerfScope(int section) : section_(section), timed_(perf_monitor().enter(section)) {
        if (timed_) start_ = std::chrono::steady_clock::now();
    }
    ~PerfScope() {
        PerfMonitor& m = perf_monitor();
        if (timed_) {
            m.add(section_, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start_).count());
        }
        m.leave(section_);
    }
    PerfScope(const PerfScope&) = delete;
    PerfScope& operator=(const PerfScope&) = delete;

private:
    int section_;
    bool timed_;
    std::chrono::steady_clock::time_point start_;
};

// Times the rest of the enclosing block into the section `name` (a part of `inside`); the id is looked up once per
// call site.
#define SH_PERF_SCOPE(name, inside)                                                                  \
    static const int sh_perf_section_id_ = ::sh::devtools::perf_monitor().section((name), (inside)); \
    const ::sh::devtools::PerfScope sh_perf_scope_(sh_perf_section_id_)

}  // namespace sh::devtools
