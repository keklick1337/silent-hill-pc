// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Named binds of the music mix and the sound driver (the maps' music hooks, hooks.lua bgm(init)):
//   music.update(flags, fade_speed[, l0..l7]): one step of the music mixer: the requested layer flags, the fade
//       speed (Q12 per second) and, optionally, the eight layer volume caps (0..255, missing ones 0). Without caps
//       the layers are not capped.
//   music.wobble(): the next value of the slowly wobbling music pitch (around 64); advances it.
//   music.layer_volume(i) / music.set_layer_volume(i, v): the mixer's volume of layer i (0..8, Q12).
//   music.channel_set(): restarts the loaded track's channels.
//   sound.music_channels(): the driver's count of busy music channels (0 = the track stopped).
//   sound.music_layer_volume_set(layer, volume): the driver's volume of one layer (0..255), immediately.
//   sound.sfx_mix_reset() / sound.sfx_mix_update(sfx, x, y, z, volume, range, mode): the positional music mix (the
//       park's music box): pan and volume from the player's distance to (x, y, z) and the stereo balance, eased
//       per frame; `range` is the distance it is heard within (Q12), `mode` the mix mode.

#include "engine/ids/game_numbers.h"
#include "engine/scripting/binds/binds.h"

#include <array>
#include <cstdint>

#include "engine/game_state/game_state.h"
#include "engine/game_state/session_audio.h"
#include "engine/scripting/binds/game_links.h"
#include "engine/world/fixed_math.h"

namespace sh::script::binds {

namespace {

using named::Call;
namespace audio = sh::game_state::audio;

int music_update(Call& c) {
    std::array<std::uint8_t, 8> caps{};
    std::uint8_t* limits = nullptr;
    if (c.argc() > 2) {
        for (int i = 0; i < 8; ++i) {
            caps[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(c.integer(2 + i));
        }
        limits = caps.data();
    }
    Bgm_Update(static_cast<std::int32_t>(c.integer(0)), static_cast<std::int32_t>(c.integer(1)), limits);
    return 0;
}

int no_args(Call& c) { return c.argc() == 0 ? 0 : c.fail("%s: no arguments expected", c.name()); }

int wobble(Call& c) { return no_args(c) < 0 ? -1 : c.reply({static_cast<long long>(Bgm_CurrentBgmIdxGet())}); }

int layer_volume(Call& c) {
    long long v[1];
    if (!c.ints(1, v) || !c.in_range(v[0], 0, audio::kMusicVolumes - 1)) {
        return -1;
    }
    return c.reply({audio::music_layer_volume(static_cast<int>(v[0]))});
}

int set_layer_volume(Call& c) {
    long long v[2];
    if (!c.ints(2, v) || !c.in_range(v[0], 0, audio::kMusicVolumes - 1)) {
        return -1;
    }
    audio::set_music_layer_volume(static_cast<int>(v[0]), static_cast<std::int16_t>(v[1]));
    return 0;
}

int channel_set(Call& c) {
    if (no_args(c) < 0) {
        return -1;
    }
    Bgm_ChannelSet();
    return 0;
}

int music_channels(Call& c) { return no_args(c) < 0 ? -1 : c.reply({Sd_UnkU16Get()}); }

int driver_layer_volume_set(Call& c) {
    long long v[2];
    if (!c.ints(2, v) || !c.in_range(v[0], 0, 255) || !c.in_range(v[1], 0, 255)) {
        return -1;
    }
    Sd_BgmLayerVolumeSet(static_cast<std::uint8_t>(v[0]), static_cast<std::uint8_t>(v[1]));
    return 0;
}

int sfx_mix_reset(Call&) {
    Pc_CarouselMix_Reset();
    return 0;
}

// The length of a Q12 vector with each component cut to Q6 first, so the squares do not overflow.
std::int32_t length_q6(std::int32_t x, std::int32_t y, std::int32_t z) {
    const std::int32_t a = x >> 6;
    const std::int32_t b = y >> 6;
    const std::int32_t d = z >> 6;
    using sh::world::wrap_add;
    using sh::world::wrap_mul;
    return sh::world::game_sqrt(wrap_add(wrap_add(wrap_mul(a, a), wrap_mul(b, b)), wrap_mul(d, d))) << 6;
}

int sfx_mix_update(Call& c) {
    if (c.argc() != 7 || !c.ints_at(1, 6)) {
        return c.fail("%s takes a sound, x, y, z, volume, range, mode", c.name());
    }
    const int sfx = c.is_text(0) ? sh::ids::sound_number(c.text(0)) : (c.is_int(0) ? static_cast<int>(c.integer(0)) : -1);
    if (sfx < 0) {
        return c.fail("%s: unknown sound", c.name());
    }
    const audio::Point at{static_cast<std::int32_t>(c.integer(1)), static_cast<std::int32_t>(c.integer(2)),
                          static_cast<std::int32_t>(c.integer(3))};
    const audio::Point player = audio::player_position();
    const std::int32_t distance = length_q6(player.x - at.x, player.y - at.y, player.z - at.z);
    int pan = 0;
    int volume = 0;
    Pc_CarouselMix_Update(static_cast<std::int8_t>(audio::stereo_balance(at)), static_cast<int>(c.integer(4)), distance,
                          static_cast<int>(c.integer(5)), static_cast<int>(c.integer(6)), sh::game_state::delta_time(),
                          &pan, &volume);
    Sd_SfxAttributesUpdate(static_cast<std::uint16_t>(sfx), static_cast<std::int8_t>(pan),
                           static_cast<std::uint8_t>(volume), 0);
    return 0;
}

const named::Bind kBinds[] = {
    {"music.update", music_update},
    {"music.wobble", wobble},
    {"music.layer_volume", layer_volume},
    {"music.set_layer_volume", set_layer_volume},
    {"music.channel_set", channel_set},
    {"sound.music_channels", music_channels},
    {"sound.music_layer_volume_set", driver_layer_volume_set},
    {"sound.sfx_mix_reset", sfx_mix_reset},
    {"sound.sfx_mix_update", sfx_mix_update},
};

}  // namespace

std::span<const named::Bind> music_binds() { return kBinds; }

}  // namespace sh::script::binds
