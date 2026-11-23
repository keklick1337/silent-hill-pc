// The engine's sound driver (engine/audio/sound_driver.h),
// on a host that records the calls and answers the disc at once.

#include "engine/audio/sound_driver.h"

#include "framework/sh_test.h"

#include <string>
#include <vector>

namespace {

namespace au = sh::audio;

class FakeHost final : public au::DriverHost {
public:
    std::vector<std::string> calls;
    int next_voice = 0;
    int pitch = 0x1000;
    std::uint32_t released = 0;
    std::uint32_t vblank = 100;
    int status = 1;

    void log(const std::string& s) { calls.push_back(s); }
    bool has(const std::string& s) const {
        for (const std::string& c : calls) {
            if (c == s) {
                return true;
            }
        }
        return false;
    }

    void set_mono(bool m) override { log("mono " + std::to_string(m)); }
    void set_music_slot(int s) override { log("music_slot " + std::to_string(s)); }
    void set_reverb_depth(int d) override { log("reverb " + std::to_string(d)); }
    void set_line_mix(bool on) override { log("line_mix " + std::to_string(on)); }
    void set_main_volume(int v) override { log("main " + std::to_string(v)); }
    void set_song_volume(int l, int r) override { log("song_vol " + std::to_string(l) + " " + std::to_string(r)); }
    void set_line_volume(int l, int r) override { log("line_vol " + std::to_string(l) + " " + std::to_string(r)); }
    void song_play() override { log("song_play"); }
    void song_stop() override { log("song_stop"); }
    void song_close() override { log("song_close"); }
    void close_bank(int s) override { log("close_bank " + std::to_string(s)); }
    int key_on_program(int s, int p, int n, int, int l, int r) override {
        log("key_on " + std::to_string(s) + " " + std::to_string(p) + " " + std::to_string(n) + " " + std::to_string(l) +
            " " + std::to_string(r));
        return next_voice;
    }
    int key_on_tone(int s, int p, int t, int n, int f, int l, int r) override {
        log("key_tone " + std::to_string(s) + " " + std::to_string(p) + " " + std::to_string(t) + " " + std::to_string(n) +
            " " + std::to_string(f) + " " + std::to_string(l) + " " + std::to_string(r));
        return next_voice;
    }
    int key_on_voice(int v, int, int, int, int, int f, int, int) override {
        log("key_voice " + std::to_string(v) + " " + std::to_string(f));
        return v;
    }
    void key_off_program(int s, int p, int n) override {
        log("key_off " + std::to_string(s) + " " + std::to_string(p) + " " + std::to_string(n));
    }
    void key_off_voice(int v, bool c) override { log("off_voice " + std::to_string(v) + " " + std::to_string(c)); }
    int key_status(int) override { return status; }
    int voice_pitch(int) override { return pitch; }
    void set_voice_pitch(int v, int p) override { log("pitch " + std::to_string(v) + " " + std::to_string(p)); }
    void set_voice_volume(int v, int l, int r) override {
        log("voice_vol " + std::to_string(v) + " " + std::to_string(l) + " " + std::to_string(r));
    }
    std::uint32_t take_released_voices() override {
        const std::uint32_t r = released;
        released = 0;
        return r;
    }
    int channel_volume(int c) override { return 10 + c; }
    void set_channel_volume(int c, int v) override { log("channel " + std::to_string(c) + " " + std::to_string(v)); }
    void load_bank(int s, std::int32_t l) override { log("load_bank " + std::to_string(s) + " " + std::to_string(l)); }
    bool open_song(std::int32_t l) override {
        log("open_song " + std::to_string(l));
        return true;
    }
    void line_play(std::uint16_t i) override { log("line_play " + std::to_string(i)); }
    void line_stop() override { log("line_stop"); }
    void line_player_volume(int, int) override {}
    void line_seek_play(std::uint16_t i, int v) override { log("seek_play " + std::to_string(i) + " " + std::to_string(v)); }
    void line_seek_stop() override { log("seek_stop"); }
    std::uint32_t voice_file_sector(int f) override { return 1000u * static_cast<std::uint32_t>(f); }
    void disc_mix(bool) override {}
    bool disc_ready() override { return true; }
    bool disc_command(au::DiscCommand c, const std::uint8_t*) override {
        log("disc " + std::to_string(static_cast<int>(c)));
        return true;
    }
    void disc_read(int n, au::ReadTarget) override { log("read " + std::to_string(n)); }
    int disc_read_remaining() override { return 0; }
    void disc_reset(bool) override { log("disc_reset"); }
    void store_read(au::ReadTarget, int s, std::uint32_t b) override {
        log("store " + std::to_string(s) + " " + std::to_string(b));
    }
    std::uint32_t vblank_count() override { return vblank; }
    bool disc_paced() override { return false; }
    bool effect_is_file(std::uint16_t) override { return false; }
    void effect_file_play(std::uint16_t, int, int, int) override {}
    void effect_file_volume(std::uint16_t, int, int) override {}
    void effect_file_stop(std::uint16_t) override {}
    bool effect_swallowed(au::EffectRequest, std::uint16_t, int, int, int) override { return false; }
};

au::DriverTables tables() {
    au::DriverTables t;
    t.effect_base = 1280;
    t.effects = {{}, {0, 2, 0x85, 48, -30}, {3, 1, 4, 50, 0}, {1, 0, 2, 60, 10}};
    t.files.resize(130);
    t.files[0] = {0, 100, 4000, 500};          // command 160: base bank
    t.files[10] = {2, 100, 60000, 700};        // command 170: ambient bank, two pieces
    t.files[54 + 32] = {0, 0, 3000, 900};      // song command 32
    t.files[32 + 173 - 160] = {3, 100, 2000, 950};  // its music bank
    t.ambient_reverb = {60, 20, 20};
    t.layer_channels.resize(41);
    t.layer_channels[1] = {0, 1, 2, 1};
    t.lines.resize(10);
    t.lines[5] = {2, 30, 1, 200, 4};
    t.line_volumes = {{5, 84, true}};
    t.default_line_volume = 84;
    t.fixed_voices = {{1283, 22, 0}};
    t.first_bank_command = 160;
    t.first_ambient_command = 170;
    t.last_ambient_command = 172;
    t.song_file_offset = 54;
    t.song_bank_offset = 173;
    t.music_slot = 3;
    t.music_volume = 40;
    t.music_ducked_volume = 24;
    t.default_reverb = 20;
    return t;
}

}  // namespace

SH_TEST(sound_driver_effect_volume_and_voices) {
    FakeHost h;
    au::SoundDriver d(tables(), h);
    d.init();
    h.calls.clear();
    h.next_voice = 5;
    // 127 - 30 = 97, attenuated by 64/255 -> 97 - 24 = 73; balance 64 takes half of the left.
    SH_CHECK(d.play_effect(1281, 64, 64) == 5);
    SH_CHECK(h.has("key_on 2 5 48 36 72"));  // program 0x85 & 0x7F, scaled by 127/128
    SH_CHECK(d.state().voice_effect[5] == 1281);
    SH_CHECK(d.effect_has_voice(1281));
    h.released = 1u << 5;
    SH_CHECK(!d.effect_has_voice(1281));
    // The base id and ids past the table play nothing.
    SH_CHECK(d.play_effect(1280, 0, 0) == 0xFF);
    SH_CHECK(d.play_effect(1300, 0, 0) == 0xFF);
    // A fixed-voice effect keys its own voice.
    SH_CHECK(d.play_effect(1283, 0, 0) == 22);
    SH_CHECK(h.has("key_voice 22 0"));
}

SH_TEST(sound_driver_update_and_step) {
    FakeHost h;
    au::SoundDriver d(tables(), h);
    d.init();
    h.next_voice = 3;
    d.play_effect(1282, 0, 0);
    h.calls.clear();
    d.update_effect(1282, 0, 0, 2);
    SH_CHECK(h.has("pitch 3 4100"));  // recorded pitch + 2 * 2
    // The volume shift wraps as a 16-bit value: 127 << 7 = 16256.
    SH_CHECK(h.has("voice_vol 3 16129 16129"));
    h.calls.clear();
    d.play_step(1282, 0, 0, 0);
    SH_CHECK(h.has("key_tone 1 4 3 50 127 126 126"));  // pitch 0 keys fine 127
    h.calls.clear();
    d.stop_effect(1282);
    SH_CHECK(h.has("key_off 1 4 50"));
}

SH_TEST(sound_driver_bank_load_in_pieces) {
    FakeHost h;
    au::SoundDriver d(tables(), h);
    d.init();
    h.calls.clear();
    d.call(170);
    SH_CHECK(d.streaming_state() == 2);
    d.drain_tasks();
    // The queue is empty, but the current task is only refreshed by the next frame.
    SH_CHECK(d.streaming_state() == 5);
    d.run_tasks();
    SH_CHECK(d.streaming_state() == 0);
    SH_CHECK(h.has("reverb 60"));
    SH_CHECK(h.has("close_bank 2"));
    SH_CHECK(h.has("read 25"));
    SH_CHECK(h.has("read 5"));  // 60000 - 51200 = 8800 bytes
    SH_CHECK(h.has("store 2 100"));
    SH_CHECK(h.has("load_bank 2 700"));
    // The same bank again is not reloaded.
    h.calls.clear();
    d.call(170);
    d.drain_tasks();
    SH_CHECK(!h.has("close_bank 2"));
}

SH_TEST(sound_driver_song_and_music_bus) {
    FakeHost h;
    au::SoundDriver d(tables(), h);
    d.init();
    h.calls.clear();
    d.call(32);
    d.drain_tasks();
    SH_CHECK(h.has("load_bank 3 950"));
    SH_CHECK(h.has("open_song 900"));
    d.call(0x301);
    d.run_tasks();
    SH_CHECK(h.has("song_play"));
    SH_CHECK(d.music_channels() == 0x301);
    SH_CHECK(d.layer_volume(2) == 12);  // channel 2
    h.calls.clear();
    d.set_layer_volume(1, 50);
    SH_CHECK(h.has("channel 1 50"));
    SH_CHECK(h.has("channel 3 50"));
    // Fade out: one step a frame until the song stops.
    d.call(23);
    for (int i = 0; i < 30; ++i) {
        d.run_tasks();
    }
    SH_CHECK(h.has("song_stop"));
    SH_CHECK(d.state().music_volume == 0);
}

SH_TEST(sound_driver_voice_line) {
    FakeHost h;
    au::SoundDriver d(tables(), h);
    d.init();
    h.calls.clear();
    d.call(4096 + 7);  // no such line
    SH_CHECK(d.state().tasks[0] == 0);
    d.call(4096 + 5);
    SH_CHECK(d.streaming_state() == 1);
    d.run_tasks();  // stop task
    d.run_tasks();  // play task
    SH_CHECK(h.has("seek_play 5 83"));  // 84 scaled by the effects option
    SH_CHECK(h.has("line_play 5"));
    SH_CHECK(d.state().music_target == 24);
    d.line_finished();
    d.run_tasks();
    SH_CHECK(d.streaming_state() == 0);
    // Preload: mode, filter, seek, pause.
    h.calls.clear();
    d.preload_line(5);
    for (int i = 0; i < 8; ++i) {
        d.run_tasks();
    }
    SH_CHECK(h.has("disc 4"));
    SH_CHECK(d.state().line_limit == 232);
    SH_CHECK(d.streaming_state() == 0);
}
