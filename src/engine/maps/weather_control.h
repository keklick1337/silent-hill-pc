/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

/* The weather requests of a map: the map's environment (clear,
 * snow or rain and how heavy), the light mode that picks which weather is lit, and a density change
 * that waits for the running transition to settle. They only write the weather words; the particles
 * themselves are sh::effects::WeatherSystem's, which reads the same words on every update. One copy
 * for every map, owned by the executable; the words start at zero on each map load, as the overlay's
 * zero-filled data did. */

#include <cstdint>

#include "engine/effects/weather.h"

namespace sh::maps {

/* What the requests read and write in the rest of the game. */
class WeatherControlHost {
public:
    virtual ~WeatherControlHost() = default;
    virtual int light_mode() = 0;                // the stored mode (a narrow game word: may read back truncated)
    virtual void set_light_mode(int mode) = 0;
    virtual bool transition_ready() = 0;         // the previous density change has finished
    virtual void set_transition_ready(bool ready) = 0;
    virtual void set_density_mode(int level) = 0;
};

/* What the loaded map has: gamedata/maps/<map>/particles.json. */
struct WeatherFeatures {
    bool snow = false;
    bool rain = false;
    bool particles() const { return snow || rain; }
};

class WeatherControl {
public:
    /* The light request that keeps the current light mode. */
    static constexpr std::uint32_t kKeepLightMode = 127;
    /* A rain environment starts already dense (the ramp the rain otherwise climbs). */
    static constexpr std::int32_t kRainRampFull = 135000;
    /* Q12(0.0005): the small lift the light snow and light windy snow start with. */
    static constexpr std::int32_t kLightSnowLift = 2;

    void reset() { *this = WeatherControl{}; }

    sh::effects::WeatherWords& words() { return words_; }
    const sh::effects::WeatherWords& words() const { return words_; }
    std::int8_t environment() const { return environment_; }

    /* The words the weather update gets on a map without rain or without any particles: such a map
     * has no rain ramp and no previous height, whatever the last update left there. */
    void prepare_update(const WeatherFeatures& features) {
        if (!features.rain) {
            words_.rain_ramp = 0;
        }
        if (!features.particles()) {
            words_.prev_height = 0;
        }
    }

    /* The map's environment: 0 clear, 1 light snow, 2 heavy snow, 5 light rain, 6 heavy rain (any
     * other value is clear). `light_request` 127 keeps the light mode, 2 stands for mode 3. */
    void environment_set(WeatherControlHost& host, const WeatherFeatures& features, int environment,
                         std::uint32_t light_request) {
        if (light_request != kKeepLightMode) {
            host.set_light_mode(light_request == 2 ? 3 : static_cast<int>(light_request));
        }
        environment_ = static_cast<std::int8_t>(environment);
        if (features.rain) {
            words_.rain_ramp = 0;
        }

        struct Setting {
            std::int32_t kind;
            std::int32_t variant;
            bool rain_dense;
        };
        Setting s{0, 0, false};
        switch (environment) {
            case 1: s = {1, 0, false}; break;
            case 2: s = {1, 1, false}; break;
            case 5: s = {2, 0, true}; break;
            case 6: s = {2, 1, true}; break;
            default: break;
        }
        words_.snow_flag = s.kind == 1 ? 1 : 0;
        words_.rain_flag = s.kind == 2 ? 1 : 0;
        if (s.rain_dense && features.rain) {
            words_.rain_ramp = kRainRampFull;
        }

        /* The requested weather: kind and variant in bits 12..15, and the 6-bit code of the kind
         * (two copies of variant + 4, three bits each) in its slot -- snow in bits 6..11, rain in 0..5. */
        std::int32_t requested = s.variant << 12;
        if (s.kind != 0) {
            const std::int32_t code = s.variant + 4;
            const std::int32_t slot = (2 - s.kind) * 6;
            requested = (s.kind << 14) | (s.variant << 12) | (code << slot) | (code << (slot + 3));
        }
        words_.requested = static_cast<std::uint32_t>(requested);

        light_mode_set(host, host.light_mode());
        words_.kind = s.kind;
        words_.variant = s.variant;
    }

    /* The light mode: which of snow (bit 16), rain (bit 20) and the match light (bit 24) the
     * request's upper half lights; the same three bits go to the running weather's bits 4, 8, 12. */
    void light_mode_set(WeatherControlHost& host, int mode) {
        host.set_light_mode(mode);
        std::uint32_t lit = 0;
        switch (mode) {
            case 0: case 2: case 3: case 7: case 9: case 11: lit = 0x001; break;
            case 4: lit = 0x010; break;
            case 5: case 10: lit = 0x011; break;
            case 6: case 8: lit = 0x101; break;
            default: break;
        }
        words_.requested = (words_.requested & 0xFFFFu) | (lit << 16);
        words_.kind_bits = static_cast<std::uint16_t>((words_.kind_bits & 0xFu) | ((words_.requested >> 12) & 0x1110u));
    }

    /* A new density level 0..8 (0 clear, 1..4 snow, 5..8 rain; the variant cycles 0..3), taken only
     * while no transition runs. Returns whether it was taken. A clear level keeps the old variant in
     * the target code, as the game does. */
    bool density_set(WeatherControlHost& host, const WeatherFeatures& features, int level) {
        if (!host.transition_ready()) {
            return false;
        }
        host.set_density_mode(level);

        words_.kind = level == 0 ? 0 : (level < 5 ? 1 : 2);
        if (words_.kind != 0) {
            words_.variant = static_cast<std::int32_t>(static_cast<std::uint32_t>(level - 1) % 4u);
        }
        if (words_.kind == 1) {
            words_.snow_flag = 1;
        } else if (words_.kind == 2) {
            words_.rain_flag = 1;
            if (features.rain) {
                words_.rain_ramp = 0;
            }
        }

        /* The target: kind and variant in the high nibble, the lower 12 bits of the old target
         * shifted up by three bits and masked to the two code slots, plus variant + 4 in the kind's
         * slot. All of it wraps at 16 bits. */
        const std::int32_t high = static_cast<std::uint16_t>((words_.kind * 4 + words_.variant) << 12);
        const std::int32_t merged = (words_.target & 0x0FFF) | high;
        std::int32_t target = (merged & 0xF000) + ((merged * 8) & 0x0E38);
        if (words_.kind != 0) {
            target += ((words_.variant + 4) << 12) >> (words_.kind * 6);
        }
        words_.target = static_cast<std::uint16_t>(target);

        if (words_.target != words_.current) {
            host.set_transition_ready(false);
        }
        if (features.particles()) {
            words_.prev_height = (words_.variant == 0 || words_.variant == 2) ? kLightSnowLift : 0;
        }
        words_.kind_bits = static_cast<std::uint16_t>((words_.kind_bits & ~0xF) + (words_.target >> 12));
        return true;
    }

private:
    sh::effects::WeatherWords words_{};
    std::int8_t environment_ = 0;
};

}  // namespace sh::maps
