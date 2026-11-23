#pragma once

// The vertical blank of the console as an engine clock: the counter the game
// reads, the interrupt callback it installs, and the waits of its frame loop (VSync).
//
// Two ways to advance it:
// - real time: a thread raises one blank every 1/60 s on absolute deadlines, calling the
//   callback first; a deadline that fell more than a period behind re-arms from now, so a stall yields one blank,
//   not a burst;
// - virtual time (replays): nothing runs by itself; every wait for a blank raises exactly one, so a run is the
//   same at any speed. Paced virtual time also sleeps to the wall-clock moment of that blank (recording at real
//   speed). Each virtual blank also steps whatever listens (the audio engine's virtual clock).

#include <atomic>
#include <cstdint>
#include <mutex>
#include <thread>

namespace sh::loop {

class VBlankClock {
public:
    enum class Mode : std::uint8_t { RealTime, Virtual, VirtualPaced };
    using Callback = void (*)();
    using Listener = void (*)(int rate);

    VBlankClock() = default;
    ~VBlankClock();
    VBlankClock(const VBlankClock&) = delete;
    VBlankClock& operator=(const VBlankClock&) = delete;

    // SH_REPLAY_INPUT or SH_REPLAY_RECORD select virtual time unless SH_REPLAY_REALTIME is set; recording or
    // SH_REPLAY_PACED pace it.
    static Mode mode_from_environment();

    // The mode is read once, on first use, unless set before.
    Mode mode();
    void set_mode(Mode mode);
    bool virtual_time() { return mode() != Mode::RealTime; }

    void set_rate(int hz) { rate_.store(hz > 0 ? hz : 60); }
    int rate() const { return rate_.load(); }

    // The game's interrupt handler; returns the previous one.
    Callback set_callback(Callback callback);
    void set_virtual_listener(Listener listener) { listener_ = listener; }

    // Real time: starts the interrupt thread (a no-op in virtual time). False when it cannot start.
    bool start();
    void stop();

    int count() const { return count_.load(); }
    // Blanks that pass without the interrupt (fast forward).
    void skip(int blanks) { count_.fetch_add(blanks); }

    // The SDK's VSync: mode < 0 only counts, 0 waits for a blank (real time: until one passed since the last
    // wait; virtual time: raises one), > 0 does nothing (the game never waits for several). Returns the count.
    // While fast-forwarding every count adds a blank and real-time waits return at once.
    int vsync(int mode, bool fast_forward);
    // The count as VSync reports it.
    int counted(bool fast_forward);
    // Real time: waits until `blanks` blanks passed since the previous return.
    void wait_blanks(int blanks);
    // Virtual time: one blank now.
    void virtual_blank();
    // Virtual time: a device was polled while not ready; 64 such polls without a blank raise one, so a wait
    // loop on a device cannot spin forever when nothing advances time by itself.
    void busy();
    // Virtual time: the blanks so far as milliseconds.
    std::uint32_t virtual_ms() const;

private:
    void thread_main();
    void pace();

    std::atomic<int> count_{0};
    std::atomic<int> rate_{60};
    std::atomic<bool> stop_{false};
    std::atomic<Callback> callback_{nullptr};
    Listener listener_ = nullptr;
    std::mutex interrupt_;
    std::thread thread_;
    int mode_ = -1;
    int busy_polls_ = 0;
    int last_wait_ = 0;
    std::int64_t pace_base_ns_ = 0;
    std::int64_t pace_count_ = 0;
};

// The process's clock.
VBlankClock& vblank_clock();

}  // namespace sh::loop
