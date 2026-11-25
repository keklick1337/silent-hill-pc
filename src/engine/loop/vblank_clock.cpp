#include "engine/loop/vblank_clock.h"

#include <chrono>
#include <cstdlib>
#include <cstring>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif
#else
#include <time.h>
#endif

namespace sh::loop {

namespace {

using Clock = std::chrono::steady_clock;

std::int64_t now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count();
}

bool env_set(const char* name) {
    const char* value = std::getenv(name);
    return value != nullptr && value[0] != '\0' && std::strcmp(value, "0") != 0;
}

// Sleeps about `seconds`. False where no sub-millisecond sleep exists (Windows before 10 1803): the caller spins
// instead, since a coarse 15 ms sleep would drop blanks.
bool precise_sleep(double seconds) {
#if defined(_WIN32)
    static HANDLE timer = nullptr;
    static bool tried = false;
    if (!tried) {
        tried = true;
        timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    }
    if (timer == nullptr) return false;
    LARGE_INTEGER due;
    due.QuadPart = -static_cast<LONGLONG>(seconds * 1e7);
    if (!SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE)) return false;
    WaitForSingleObject(timer, INFINITE);
    return true;
#else
    timespec ts{};
    ts.tv_sec = 0;
    ts.tv_nsec = static_cast<long>(seconds * 1e9);
    nanosleep(&ts, nullptr);
    return true;
#endif
}

#if !defined(__ANDROID__)
void relax() {
#if defined(_WIN32)
    YieldProcessor();
#endif
}
#endif

}  // namespace

VBlankClock::~VBlankClock() { stop(); }

VBlankClock::Mode VBlankClock::mode_from_environment() {
    if (!(env_set("SH_REPLAY_INPUT") || env_set("SH_REPLAY_RECORD")) || env_set("SH_REPLAY_REALTIME"))
        return Mode::RealTime;
    return (env_set("SH_REPLAY_RECORD") || env_set("SH_REPLAY_PACED")) ? Mode::VirtualPaced : Mode::Virtual;
}

VBlankClock::Mode VBlankClock::mode() {
    if (mode_ < 0) mode_ = static_cast<int>(mode_from_environment());
    return static_cast<Mode>(mode_);
}

void VBlankClock::set_mode(Mode mode) { mode_ = static_cast<int>(mode); }

VBlankClock::Callback VBlankClock::set_callback(Callback callback) { return callback_.exchange(callback); }

bool VBlankClock::start() {
    if (virtual_time() || thread_.joinable()) return true;
    stop_.store(false);
    try {
        thread_ = std::thread([this] { thread_main(); });
    } catch (...) {
        return false;
    }
    return true;
}

void VBlankClock::stop() {
    if (!thread_.joinable()) return;
    stop_.store(true);
    thread_.join();
}

void VBlankClock::thread_main() {
    const std::int64_t t0 = now_ns();
    auto seconds = [t0] { return static_cast<double>(now_ns() - t0) * 1e-9; };
    double next = 1.0 / rate();

    while (!stop_.load()) {
        const double step = 1.0 / rate();
        double now = seconds();
        if (now >= next) {
            {
                std::lock_guard<std::mutex> lock(interrupt_);
                if (Callback cb = callback_.load()) cb();
            }
            count_.fetch_add(1);
            next += step;
            if (next <= now) next = now + step;
        }
        // Wake at least every 2 ms; sleep short of the deadline and spin the last half millisecond, since the
        // high-resolution timer can overshoot that.
        double wake = next;
        if (now + 0.002 < wake) wake = now + 0.002;
        now = seconds();
#if defined(__ANDROID__)
        // A phone's battery and heat matter more than a blank a fraction of a millisecond late: no spinning.
        if (wake > now) precise_sleep(wake - now);
#else
        if (wake - now > 0.0007) {
            if (!precise_sleep(wake - now - 0.0005)) relax();
        } else {
            relax();
        }
#endif
    }
}

int VBlankClock::vsync(int mode, bool fast_forward) {
    if (mode == 0) {
        if (virtual_time())
            virtual_blank();
        else if (!fast_forward)
            wait_blanks(1);
    }
    return counted(fast_forward);
}

int VBlankClock::counted(bool fast_forward) {
    if (fast_forward) skip(1);
    return count();
}

void VBlankClock::wait_blanks(int blanks) {
    while (count() - last_wait_ < blanks) {
#if defined(_WIN32)
        relax();
#else
        // A 1 ms nap only delays noticing the blank, never the count itself.
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
#endif
    }
    last_wait_ = count();
}

void VBlankClock::pace() {
    // Sleeps up to the wall-clock moment of the next blank. A run that falls more than a few blanks behind
    // re-anchors instead of racing to catch up.
    const std::int64_t r = rate();
    std::int64_t now = now_ns();
    if (pace_base_ns_ == 0 || now > pace_base_ns_ + (pace_count_ + 4) * 1000000000 / r) {
        pace_base_ns_ = now;
        pace_count_ = 0;
    }
    pace_count_++;
    const std::int64_t deadline = pace_base_ns_ + pace_count_ * 1000000000 / r;
    while (now < deadline) {
        const std::int64_t left_ms = (deadline - now) / 1000000;
        if (left_ms > 1) std::this_thread::sleep_for(std::chrono::milliseconds(left_ms - 1));
        now = now_ns();
    }
}

void VBlankClock::virtual_blank() {
    if (mode() == Mode::VirtualPaced) pace();
    busy_polls_ = 0;
    if (Callback cb = callback_.load()) cb();
    count_.fetch_add(1);
    if (listener_ != nullptr) listener_(rate());
}

void VBlankClock::busy() {
    if (virtual_time() && ++busy_polls_ >= 64) virtual_blank();
}

std::uint32_t VBlankClock::virtual_ms() const {
    return static_cast<std::uint32_t>(static_cast<std::uint64_t>(static_cast<std::uint32_t>(count())) * 1000u /
                                      static_cast<std::uint64_t>(rate()));
}

VBlankClock& vblank_clock() {
    static VBlankClock clock;
    return clock;
}

}  // namespace sh::loop
