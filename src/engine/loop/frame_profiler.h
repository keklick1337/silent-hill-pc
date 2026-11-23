#pragma once

// Frame profiler of the game loop: wall-clock telemetry that says
// where a frame's time went. The loop stamps five points of every frame (its top, the start and
// end of the game state update, the end of the file queue pump, the end of the buffer swap) and
// samples once a frame at the pacing point. A frame over the slow limit is reported phase by
// phase (the phases tile the whole interval between two samples); every window (30 s) reports
// the average and worst frame, vertical blanks per frame and the average of every phase.
// Nothing here changes what the game does; it only measures.

#include <array>
#include <cstdint>
#include <optional>

namespace sh::loop {

enum class FrameMark : int { Top, UpdateStart, UpdateEnd, FilesEnd, SwapEnd, Count };

// The phases between two samples, in loop order.
enum FramePhase : int { kPresent, kTail, kTopOfFrame, kUpdate, kFiles, kSync, kPhaseCount };

struct FrameProfilerSpec {
    std::uint32_t slow_frame_ms = 150;
    int slow_reports_max = 200;    // per run, so a broken machine does not flood the journal
    std::uint32_t window_ms = 30000;
};

struct SlowFrame {
    std::uint32_t total_ms = 0;
    std::array<std::uint32_t, kPhaseCount> phase_ms{};
};

struct ProfileWindow {
    double average_ms = 0.0;
    double fps = 0.0;
    std::uint32_t worst_ms = 0;
    double vblanks_per_frame = 0.0;
    std::uint32_t frames = 0;
    bool has_phases = false;  // some frame of the window had every stamp in order
    std::array<double, kPhaseCount> phase_ms{};
};

struct ProfileSample {
    std::optional<SlowFrame> slow;
    std::optional<ProfileWindow> window;
};

class FrameProfiler {
public:
    explicit FrameProfiler(const FrameProfilerSpec& spec = {}) : spec_(spec) {}

    void mark(FrameMark m, std::uint32_t ms) { marks_[static_cast<int>(m)] = ms; }

    // Once a frame at the pacing point: `vblanks` the frame took, uncapped.
    ProfileSample sample(std::uint32_t now_ms, int vblanks);

private:
    std::uint32_t at(FrameMark m) const { return marks_[static_cast<int>(m)]; }

    FrameProfilerSpec spec_;
    std::array<std::uint32_t, static_cast<int>(FrameMark::Count)> marks_{};
    std::uint32_t last_ms_ = 0;
    std::uint32_t total_ms_ = 0;
    std::uint32_t worst_ms_ = 0;
    std::uint32_t frames_ = 0;
    std::uint32_t vblanks_ = 0;
    std::array<std::uint32_t, kPhaseCount> phase_sum_{};
    std::uint32_t phase_frames_ = 0;
    int slow_reports_ = 0;
};

}  // namespace sh::loop
