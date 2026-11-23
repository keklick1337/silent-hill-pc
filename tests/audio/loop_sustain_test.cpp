// Sustained falloff loops (engine/audio/loop_sustain.h) and the event music change (engine/audio/music_change.h).

#include "engine/audio/loop_sustain.h"
#include "engine/audio/music_change.h"

#include "framework/sh_test.h"

#include <set>
#include <string>
#include <vector>

namespace {

namespace au = sh::audio;

au::SustainedLoop loop(std::uint16_t id, std::int32_t volume) {
    au::SustainedLoop l;
    l.id = id;
    l.position = {1, 2, 3};
    l.volume = volume;
    l.falloff = 4096 * 8;
    return l;
}

}  // namespace

SH_TEST(loop_sustain_replays_skipped_frames) {
    au::LoopSustain s;
    std::set<std::uint16_t> playing = {1400};
    std::vector<au::SustainedLoop> replayed;
    auto is_playing = [&](std::uint16_t id) { return playing.count(id) != 0; };
    auto replay = [&](const au::SustainedLoop& l) {
        replayed.push_back(l);
        s.record(l);  // the replay goes through the caller's path: must not count as an update
    };

    s.record(loop(1400, 200));
    s.sweep(is_playing, replay);
    SH_CHECK(replayed.empty());  // the caller updated it this frame

    s.sweep(is_playing, replay);
    SH_CHECK(replayed.size() == 1 && replayed[0].volume == 200);
    s.sweep(is_playing, replay);
    SH_CHECK(replayed.size() == 2);  // the replay did not mark it fresh

    s.record(loop(1400, 100));  // the same loop again: one place, new values
    SH_CHECK(s.tracked() == 1);
    s.sweep(is_playing, replay);
    SH_CHECK(replayed.size() == 2);
    s.sweep(is_playing, replay);
    SH_CHECK(replayed.size() == 3 && replayed[2].volume == 100);

    playing.clear();  // the game stopped the sound: forgotten
    s.sweep(is_playing, replay);
    SH_CHECK(s.tracked() == 0 && replayed.size() == 3);
}

SH_TEST(loop_sustain_capacity_and_reset) {
    au::LoopSustain s;
    for (int i = 0; i < au::LoopSustain::kCapacity + 4; ++i) {
        s.record(loop(static_cast<std::uint16_t>(1300 + i), 10));
    }
    SH_CHECK(s.tracked() == au::LoopSustain::kCapacity);
    // A freed place is taken by the next new loop.
    s.sweep([](std::uint16_t id) { return id != 1300; }, [](const au::SustainedLoop&) {});
    SH_CHECK(s.tracked() == au::LoopSustain::kCapacity - 1);
    s.record(loop(1500, 10));
    SH_CHECK(s.tracked() == au::LoopSustain::kCapacity);
    s.reset();
    SH_CHECK(s.tracked() == 0);
}

namespace {

class ChangeLog final : public au::MusicChangeHost {
public:
    bool is_busy = false;
    bool differs = true;
    bool silent = false;
    int current = 0;
    std::vector<std::string> calls;

    bool busy() override { return is_busy; }
    int step() override { return current; }
    void set_step(int s) override {
        current = s;
        calls.push_back("step " + std::to_string(s));
    }
    void finish() override { calls.push_back("finish"); }
    bool track_differs(int) override { return differs; }
    void request_mute() override { calls.push_back("mute"); }
    void fade_out_fast() override { calls.push_back("fade"); }
    bool music_silent() override { return silent; }
    void load_track(int track) override { calls.push_back("load " + std::to_string(track)); }
};

}  // namespace

SH_TEST(music_change_steps) {
    ChangeLog h;
    h.is_busy = true;
    au::music_crossfade_step(7, h);
    SH_CHECK(h.calls.empty());
    h.is_busy = false;

    au::music_crossfade_step(7, h);
    au::music_crossfade_step(7, h);
    au::music_crossfade_step(7, h);  // still sounding: waits, keeps the mix muted
    h.silent = true;
    au::music_crossfade_step(7, h);
    au::music_crossfade_step(7, h);
    const std::vector<std::string> want = {"mute", "step 1", "mute", "fade", "step 2", "mute",
                                           "mute", "load 7", "step 3", "finish"};
    SH_CHECK(h.calls == want);
}

SH_TEST(music_change_same_track) {
    ChangeLog h;
    h.differs = false;
    au::music_crossfade_step(3, h);
    au::music_crossfade_step(3, h);
    const std::vector<std::string> want = {"step 3", "finish"};
    SH_CHECK(h.calls == want);
}
