#include "engine/loop/frame_pacer.h"

namespace sh::loop {

int FramePacer::demo_frame(PacerHost& host, int interval, int min_interval) {
    const int now = host.vblank_count();
    int vblanks = now - last_count_;
    last_count_ = now;
    if (interval < min_interval) {
        interval = min_interval;
    }
    do {
        host.wait_vblank();
        ++vblanks;
        ++last_count_;
    } while (vblanks < interval);
    return vblanks;
}

int FramePacer::fixed_frame(PacerHost& host, bool fast_forward) {
    const int vblanks = host.vblank_count() - last_count_;
    last_count_ = host.vblank_count();
    host.wait_vblank();
    // The wait above can return at once when another wait in the frame already took this vertical
    // blank; wait on the loop's own count, which nothing else consumes. Never hang on a stalled one.
    if (!fast_forward) {
        const int target = last_count_ + 1;
        const std::uint64_t deadline = host.ticks_ms() + spec_.fixed_backstop_ms;
        while (host.vblank_count() < target && host.ticks_ms() < deadline) {
            host.wait_vblank();
        }
    }
    return vblanks;
}

int FramePacer::target_fps(const FpsChoice& fps) const {
    int target = fps.locked_refresh > 0 ? fps.locked_refresh : (fps.uncapped ? 0 : fps.cap);
    if (fps.menu && target > 0 && target < spec_.vblank_rate) {
        target = spec_.vblank_rate;
    }
    return target;
}

void FramePacer::wait_on_timer(PacerHost& host, int fps) {
    const std::uint64_t freq = host.perf_frequency();
    const std::uint64_t frame = freq / static_cast<std::uint64_t>(fps);
    if (timer_start_ == 0) {
        timer_start_ = host.perf_counter();
    }
    const std::uint64_t elapsed = host.perf_counter() - timer_start_;
    if (elapsed < frame) {
        // Sleep most of the rest, spin the last millisecond for precision (a phone sleeps it all: no spinning on
        // a battery).
        const std::uint64_t remain_ms = ((frame - elapsed) * 1000) / freq;
#if defined(__ANDROID__)
        if (remain_ms > 0) {
            host.sleep_ms(static_cast<std::uint32_t>(remain_ms));
        }
#else
        if (remain_ms > 2) {
            host.sleep_ms(static_cast<std::uint32_t>(remain_ms - 1));
        }
        while (host.perf_counter() - timer_start_ < frame) {
        }
#endif
    }
    timer_start_ = host.perf_counter();
}

int FramePacer::capped_frame(PacerHost& host, bool fade_wait, bool apply_cap, int game_min, const FpsChoice& fps) {
    if (fade_wait) {
        host.wait_vblank();
    }
    int vblanks = host.vblank_count() - last_count_;
    last_count_ = host.vblank_count();

    int min_vblanks = game_min;
    if (apply_cap) {
        const int target = target_fps(fps);
        if (target <= 0 || (fps.display_paced && target > spec_.vblank_rate)) {
            min_vblanks = 0;
        } else if (target > spec_.vblank_rate || (spec_.vblank_rate % target) != 0) {
            // Whole vertical blanks cannot express this rate.
            wait_on_timer(host, target);
            min_vblanks = 0;
        } else {
            min_vblanks = spec_.vblank_rate / target;
        }
    }
    // The timer start only survives frames that the timer paced or that waited nothing.
    if (min_vblanks > 0 || timer_start_ == 0) {
        timer_start_ = 0;
    }

    // Only blanks the counter shows are counted: after a frame longer than a blank the wait returns at once (the
    // blank it would wait for has passed), and counting that as a blank left the next frame a zero step. A stalled
    // counter is given up on, the wait then taken as one blank.
    const std::uint64_t deadline = host.ticks_ms() + spec_.fixed_backstop_ms;
    while (vblanks < min_vblanks) {
        host.wait_vblank();
        const int passed = host.vblank_count() - last_count_;
        if (passed > 0) {
            vblanks += passed;
            last_count_ += passed;
        } else if (host.ticks_ms() >= deadline) {
            ++vblanks;
            ++last_count_;
        }
    }
    return vblanks;
}

}  // namespace sh::loop
