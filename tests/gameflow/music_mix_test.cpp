// The music layer mix against its specification (the original's music update): fade steps, ducking, the
// 7-bit driver volumes and the restart handshake, with values worked out by hand from the integer
// formulas. The tests do not link the game layer.

#include "engine/gameflow/music_mix.h"

#include <vector>

#include "framework/sh_test.h"

namespace {

namespace gf = sh::gameflow;

class FakeDriver final : public gf::MusicDriver {
public:
    std::uint16_t status_word = 5;  // a song is playing
    std::uint8_t layers[8] = {};
    std::vector<int> calls;  // 100 + layer for set, 1 stop, 2 restart

    std::uint16_t status() const override { return status_word; }
    std::uint8_t layer_volume(std::uint8_t layer) const override { return layers[layer]; }
    void set_layer_volume(std::uint8_t layer, std::uint8_t volume) override {
        layers[layer] = volume;
        calls.push_back(100 + layer);
    }
    void stop_music() override { calls.push_back(1); }
    void restart_channels() override { calls.push_back(2); }
};

using Volumes = std::array<std::int16_t, gf::kMusicVolumes>;

gf::MusicFrame frame(std::uint32_t request, std::int32_t fade, std::int32_t delta) {
    gf::MusicFrame f;
    f.request = request;
    f.fade_speed = fade;
    f.delta = delta;
    return f;
}

}  // namespace

SH_TEST(music_mix_fades_requested_layer_in_at_double_speed) {
    gf::MusicMix mix;
    FakeDriver driver;
    Volumes v{};
    std::uint32_t status = 0;
    // Request layer 1 only (bit 0 clear: bit 0 flips on, so layer 0 fades in too).
    // delta 136 (1/30 s), fade 4096: in-step (136 * 4096) >> 11 = 272.
    mix.update(frame(1u << 1, 4096, 136), v, status, driver);
    SH_CHECK(v[0] == 272 && v[1] == 272 && v[2] == 0 && v[8] == 0);
    // Driver volume: 272 * 127 >> 12 = 8, limit 128: 8 * 128 >> 7 = 8.
    SH_CHECK(driver.layers[0] == 8 && driver.layers[1] == 8);
    SH_CHECK(driver.calls.size() == 8);
}

SH_TEST(music_mix_layer0_request_silences_everything) {
    gf::MusicMix mix;
    FakeDriver driver;
    Volumes v{};
    v[3] = 4096;
    std::uint32_t status = 0;
    // Out-step (136 * 4096) >> 12 = 136.
    mix.update(frame(gf::music_flags::kLayer0 | (1u << 3), 4096, 136), v, status, driver);
    SH_CHECK(v[3] == 4096 - 136 && v[0] == 0);
}

SH_TEST(music_mix_ducks_base_layer_only) {
    gf::MusicMix mix;
    FakeDriver driver;
    Volumes v{};
    v[0] = 4096;
    v[1] = 4096;
    v[8] = 2048;
    std::uint32_t status = gf::music_status::kDuck;
    mix.update(frame(1u << 1, 0, 0), v, status, driver);
    // Base: 4096 * (4096 - 2048) >> 12 = 2048 -> 2048 * 127 >> 12 = 63. Layer 1: 127.
    SH_CHECK(driver.layers[0] == 63 && driver.layers[1] == 127);
}

SH_TEST(music_mix_duck_channel_targets) {
    gf::MusicMix mix;
    FakeDriver driver;
    Volumes v{};
    std::uint32_t status = gf::music_status::kRequestMute;
    // Duck step: 4096 * 1024 >> 12 = 1024 per frame towards 4096 (mute).
    mix.update(frame(0, 0, 4096), v, status, driver);
    SH_CHECK((status & gf::music_status::kApplyMute) != 0);
    SH_CHECK(v[8] == 1024);
}

SH_TEST(music_mix_limits_and_dead_player) {
    gf::MusicMix mix;
    FakeDriver driver;
    Volumes v{};
    v[2] = 4096;
    const std::uint8_t limits[8] = {128, 128, 64, 128, 128, 128, 128, 128};
    gf::MusicFrame f = frame(1u << 2, 4096, 0);
    f.limits = limits;
    std::uint32_t status = 0;
    mix.update(f, v, status, driver);
    SH_CHECK(driver.layers[2] == 63);  // 127 * 64 >> 7
    f.player_dead_or_game_over = true;
    f.delta = 136;
    mix.update(f, v, status, driver);
    // Dead: everything fades out at 0.2/s: (136 * 819) >> 12 = 27.
    SH_CHECK(v[2] == 4096 - 27);
}

SH_TEST(music_mix_restart_handshake) {
    gf::MusicMix mix;
    FakeDriver driver;
    Volumes v{};
    std::uint32_t status = 0;
    // Nothing on and MuteAll: stop once, handshake 3.
    mix.update(frame(gf::music_flags::kLayer0 | gf::music_flags::kMuteAll, 0, 0), v, status, driver);
    SH_CHECK(mix.handshake() == 3 && driver.calls == std::vector<int>{1});
    mix.update(frame(gf::music_flags::kLayer0 | gf::music_flags::kMuteAll, 0, 0), v, status, driver);
    SH_CHECK(driver.calls.size() == 1);
    // A layer comes on while the driver is idle: restart, then 2 -> 1 -> 0 with read-back skipped.
    driver.status_word = 0;
    v[1] = 100;
    mix.update(frame(1u << 1, 0, 0), v, status, driver);
    SH_CHECK(mix.handshake() == 2 && driver.calls.back() == 2 && v[1] == 0);
    v[1] = 100;
    mix.update(frame(1u << 1, 0, 0), v, status, driver);
    SH_CHECK(mix.handshake() == 1 && v[1] == 0);
    v[1] = 100;
    driver.status_word = 5;
    driver.layers[1] = 3;
    mix.update(frame(1u << 1, 0, 0), v, status, driver);
    SH_CHECK(mix.handshake() == 0 && v[1] == 96 && v[8] == 0);  // read back: 3 << 5
}

SH_TEST(music_wobble_starts_at_centre) {
    gf::MusicWobble w;
    SH_CHECK(w.next(0) == 64);
    // One frame of 1/30 s: phase 136 * 262145 = 35651720. Slow: sin(136) = 848, * 8 = 6784;
    // fast: (phase & 0xFFFF) / 16 = 136 / 16 = 8, sin(8) = 50, * 32 = 1600.
    // (262144 + 6784 + 1600) >> 12 = 66.
    SH_CHECK(w.next(136) == 66);
}
