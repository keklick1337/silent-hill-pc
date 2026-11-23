#include "engine/gameflow/music_mix.h"

#include "engine/world/fixed_trig.h"

namespace sh::gameflow {

namespace {

constexpr std::int32_t kOne = 4096;              // 1.0 in Q12
constexpr std::int32_t kDeathFade = 819;         // 0.2 per second, Q12 truncated
constexpr std::int32_t kDriverScale = 127;       // 0.0312 in Q12, truncated: Q12 1.0 -> 7-bit volume
constexpr std::int32_t kDuckMute = kOne;
constexpr std::int32_t kDuckRadio = 3072;        // 0.75
constexpr std::int32_t kDuckDialogue = 2048;     // 0.5
constexpr std::uint8_t kFullLimit = 128;

constexpr std::int32_t wrap_mul(std::int32_t a, std::int32_t b) {
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) * static_cast<std::uint32_t>(b));
}

constexpr std::int32_t mul_q12_wide(std::int32_t a, std::int32_t b) {
    return static_cast<std::int32_t>((static_cast<std::int64_t>(a) * static_cast<std::int64_t>(b)) >> 12);
}

// Moves `value` towards `target` by at most `step`.
std::int32_t approach(std::int32_t value, std::int32_t target, std::int32_t step) {
    const std::int32_t gap = target - value;
    if (value == target) {
        return value;
    }
    if (step < gap) {
        return value + step;
    }
    if (gap >= -step) {
        return target;
    }
    return value - step;
}

}  // namespace

void MusicMix::silence(std::array<std::int16_t, kMusicVolumes>& volumes) {
    volumes.fill(0);
}

void MusicMix::read_back(std::array<std::int16_t, kMusicVolumes>& volumes, const MusicDriver& driver) {
    for (int i = 1; i < kMusicLayers; ++i) {
        volumes[i] = static_cast<std::int16_t>(driver.layer_volume(static_cast<std::uint8_t>(i)) << 5);
    }
    if (driver.status() == 0) {
        volumes[0] = kOne;
    }
    volumes[kMusicLayers] = 0;
}

void MusicMix::update(const MusicFrame& frame, std::array<std::int16_t, kMusicVolumes>& volumes, std::uint32_t& status,
                      MusicDriver& driver) {
    static constexpr std::array<std::uint8_t, kMusicLayers> kNoLimits{kFullLimit, kFullLimit, kFullLimit, kFullLimit,
                                                                      kFullLimit, kFullLimit, kFullLimit, kFullLimit};
    const std::uint8_t* limits = frame.limits != nullptr ? frame.limits : kNoLimits.data();
    std::uint32_t request = frame.request;
    std::int32_t fade = frame.fade_speed;

    if (frame.player_dead_or_game_over) {
        request &= music_flags::kKeepAlive;
        request |= music_flags::kLayer0;
        fade = kDeathFade;
    }
    if (!(request & music_flags::kKeepAlive) && frame.radio_noise) {
        status |= music_status::kRadioActive;
    }
    if (status & music_status::kRequestMute) {
        request = music_flags::kLayer0 | music_flags::kMuteAll;
        status |= music_status::kApplyMute;
    }
    // From here bit i set = layer i fades in. Bit 0 on the request means "everything off".
    if (request & music_flags::kLayer0) {
        request &= music_flags::kKeepAlive | music_flags::kMuteAll;
    } else {
        request ^= music_flags::kLayer0;
    }

    for (int i = 0; i < kMusicVolumes; ++i) {
        std::int32_t step;
        std::int32_t target;
        if (i == kMusicLayers) {
            step = mul_q12_wide(frame.delta, kOne / 4);
            if (status & music_status::kApplyMute) {
                target = kDuckMute;
            } else if (status & music_status::kRadioActive) {
                target = kDuckRadio;
            } else {
                target = (status & music_status::kDuck) ? kDuckDialogue : 0;
            }
        } else if ((request >> i) & 1u) {
            // Fading in runs at twice the requested speed.
            step = wrap_mul(frame.delta, fade) >> 11;
            target = kOne;
        } else {
            step = wrap_mul(frame.delta, fade) >> 12;
            target = 0;
        }
        volumes[i] = static_cast<std::int16_t>(approach(volumes[i], target, step));
    }

    bool any_layer = false;
    const std::int32_t unducked = static_cast<std::int16_t>(kOne - volumes[kMusicLayers]);
    for (int i = 0; i < kMusicLayers; ++i) {
        std::int32_t v = volumes[i];
        any_layer |= v != 0;
        if (i == 0) {
            v = mul_q12_wide(v, unducked);  // only the base layer is ducked
        }
        v = mul_q12_wide(v, kDriverScale);
        if (v > kDriverScale) {
            v = kDriverScale;
        }
        v = (v * limits[i]) >> 7;
        if (v > kDriverScale) {
            v = kDriverScale;
        }
        out_[i] = static_cast<std::int8_t>(v);
    }

    const std::uint16_t driver_status = driver.status();
    const bool song_playing = driver_status != 0 && driver_status != 0xFFFF;
    bool send = false;

    if (any_layer) {
        switch (handshake_) {
            case 3:
                silence(volumes);
                if (song_playing) {
                    handshake_ = 0;
                } else {
                    driver.restart_channels();
                    handshake_ = 2;
                }
                break;
            case 2:
                silence(volumes);
                handshake_ = 1;
                break;
            case 1:
                if (song_playing) {
                    read_back(volumes, driver);
                } else {
                    silence(volumes);
                }
                handshake_ = 0;
                break;
            case 0:
                send = true;
                break;
            default:
                break;
        }
    } else if (request & music_flags::kMuteAll) {
        if (handshake_ != 3) {
            handshake_ = 3;
            driver.stop_music();
        }
    } else if (handshake_ == 0) {
        send = true;
    }

    if (send) {
        if (song_playing) {
            for (int i = 0; i < kMusicLayers; ++i) {
                driver.set_layer_volume(static_cast<std::uint8_t>(i), static_cast<std::uint8_t>(out_[i]));
            }
        } else {
            silence(volumes);
            handshake_ = 3;
        }
    }
}

std::uint32_t MusicWobble::next(std::int32_t delta) {
    constexpr std::int32_t kRate = 64 * kOne + 1;
    constexpr std::uint32_t kCentre = 64 * kOne;
    phase_ += static_cast<std::uint32_t>(wrap_mul(delta, kRate));
    const std::uint32_t slow =
        kCentre + static_cast<std::uint32_t>(sh::world::fixed_sin(static_cast<std::int32_t>(phase_ >> 18)) * 8);
    const std::uint32_t fast =
        static_cast<std::uint32_t>(sh::world::fixed_sin(static_cast<std::int32_t>((phase_ & 0xFFFF) / 16)) * 32);
    return (slow + fast) >> 12;
}

}  // namespace sh::gameflow
