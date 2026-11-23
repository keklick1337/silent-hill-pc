#pragma once

// Music layer mix: the background music is up to eight
// layers played together plus one ducking channel. Every frame the map's music logic asks for a
// set of layers; MusicMix fades each layer volume towards on or off, fades the ducking channel
// towards the strongest active duck (mute, radio, dialogue), and hands the sound driver eight
// 7-bit volumes. It also runs the little handshake that restarts the music channels after the
// driver stopped them.
//
// Bit-exactness contract: with the same inputs and driver answers, the volumes, the flags, the
// handshake state and the sequence of driver calls equal the original's to the last bit.

#include <array>
#include <cstdint>

namespace sh::gameflow {

// Layer request bits (the map music logic's argument).
namespace music_flags {
constexpr std::uint32_t kLayer0 = 1u << 0;  // on the request: silence every layer
constexpr std::uint32_t kKeepAlive = 1u << 8;
constexpr std::uint32_t kMuteAll = 1u << 9;
}  // namespace music_flags

// Music status bits shared with the rest of the game.
namespace music_status {
constexpr std::uint32_t kApplyMute = 1u << 1;
constexpr std::uint32_t kRadioActive = 1u << 2;
constexpr std::uint32_t kDuck = 1u << 3;
constexpr std::uint32_t kRequestMute = 1u << 7;
}  // namespace music_status

// The sound driver as the mix uses it.
class MusicDriver {
public:
    virtual ~MusicDriver() = default;
    // Driver status word: 0 = idle, 0xFFFF = busy; anything else = a song is loaded and playing.
    virtual std::uint16_t status() const = 0;
    virtual std::uint8_t layer_volume(std::uint8_t layer) const = 0;
    virtual void set_layer_volume(std::uint8_t layer, std::uint8_t volume) = 0;
    virtual void stop_music() = 0;
    // Re-arms the song channels after a stop.
    virtual void restart_channels() = 0;
};

constexpr int kMusicLayers = 8;
constexpr int kMusicVolumes = kMusicLayers + 1;  // the last one is the ducking channel

struct MusicFrame {
    std::uint32_t request = 0;     // music_flags
    std::int32_t fade_speed = 0;   // Q12 per second
    const std::uint8_t* limits = nullptr;  // eight per-layer caps out of 128; null = all 128
    std::int32_t delta = 0;        // raw frame time, Q12 seconds
    bool player_dead_or_game_over = false;
    bool radio_noise = false;      // radio crackling and switched on
};

class MusicMix {
public:
    // One frame. `volumes` are the nine Q12 layer volumes the game keeps (read and written);
    // `status` the shared music status bits (read and written).
    void update(const MusicFrame& frame, std::array<std::int16_t, kMusicVolumes>& volumes, std::uint32_t& status,
                MusicDriver& driver);

    // All nine volumes to zero.
    static void silence(std::array<std::int16_t, kMusicVolumes>& volumes);

    // The volumes back from what the driver plays (after a channel restart).
    static void read_back(std::array<std::int16_t, kMusicVolumes>& volumes, const MusicDriver& driver);

    int handshake() const { return handshake_; }
    const std::array<std::int8_t, kMusicLayers>& driver_volumes() const { return out_; }

private:
    // 0 = playing; 3 = stopped, waiting to restart; 2 and 1 = restarting.
    int handshake_ = 0;
    std::array<std::int8_t, kMusicLayers> out_{};
};

// Slow pitch wobble some maps put on their music: two sines of a phase that advances with the frame
// time (one turn of the slow sine every 2^30 phase units, the fast one every 2^16).
class MusicWobble {
public:
    // Advances the phase by `delta` (raw frame time, Q12 seconds) and returns the wobbled value
    // around 64.
    std::uint32_t next(std::int32_t delta);

private:
    std::uint32_t phase_ = 0;
};

}  // namespace sh::gameflow
