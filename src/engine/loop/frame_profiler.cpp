#include "engine/loop/frame_profiler.h"

namespace sh::loop {

ProfileSample FrameProfiler::sample(std::uint32_t now_ms, int vblanks) {
    ProfileSample out;
    if (last_ms_ != 0) {
        const std::uint32_t dt = now_ms - last_ms_;
        total_ms_ += dt;
        if (dt > worst_ms_) {
            worst_ms_ = dt;
        }

        const std::uint32_t top = at(FrameMark::Top);
        const std::uint32_t update_start = at(FrameMark::UpdateStart);
        const std::uint32_t update_end = at(FrameMark::UpdateEnd);
        const std::uint32_t files_end = at(FrameMark::FilesEnd);
        const std::uint32_t swap_end = at(FrameMark::SwapEnd);
        // The previous frame's swap, the rest of that frame, this frame's top, the update, the
        // file queue, then the sync and pacing up to now.
        const std::array<std::uint32_t, kPhaseCount> phases{swap_end - last_ms_,      top - swap_end,
                                                            update_start - top,       update_end - update_start,
                                                            files_end - update_end,   now_ms - files_end};
        if (dt >= spec_.slow_frame_ms && swap_end != 0 && slow_reports_ < spec_.slow_reports_max) {
            ++slow_reports_;
            out.slow = SlowFrame{dt, phases};
        }
        // A stamp that was not reached this frame would wrap; such frames are left out of the sums.
        if (swap_end != 0 && swap_end >= last_ms_ && top >= swap_end && update_start >= top && update_end >= update_start &&
            files_end >= update_end && now_ms >= files_end) {
            for (int i = 0; i < kPhaseCount; ++i) {
                phase_sum_[i] += phases[i];
            }
            ++phase_frames_;
        }
        vblanks_ += static_cast<std::uint32_t>(vblanks);
        ++frames_;
        if (total_ms_ >= spec_.window_ms) {
            ProfileWindow w;
            w.average_ms = static_cast<double>(total_ms_) / frames_;
            w.fps = 1000.0 * frames_ / static_cast<double>(total_ms_);
            w.worst_ms = worst_ms_;
            w.vblanks_per_frame = static_cast<double>(vblanks_) / frames_;
            w.frames = frames_;
            w.has_phases = phase_frames_ > 0;
            for (int i = 0; i < kPhaseCount && w.has_phases; ++i) {
                w.phase_ms[i] = phase_sum_[i] / static_cast<double>(phase_frames_);
            }
            out.window = w;
            phase_sum_ = {};
            phase_frames_ = 0;
            frames_ = 0;
            total_ms_ = 0;
            worst_ms_ = 0;
            vblanks_ = 0;
        }
    }
    last_ms_ = now_ms;
    return out;
}

}  // namespace sh::loop
