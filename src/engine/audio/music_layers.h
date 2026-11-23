#pragma once

// Music layers of the game's songs. Most songs start every channel at volume 0; the game opens groups of channels
// ("layers" 1..7) as the situation changes (Bgm_LayersUpdate -> Sd_MidiChannelsVolumeSet, which sets control 7 of
// every channel of the layer). Layer 0 is the song volume itself; a channel of layer 0 is never opened.
//
// A row is the song's place in the game's list of song files + 1.

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace sh::audio {

// Layer of each MIDI channel 0..15 of the song SND/<name>.KDT (name without directory and extension, upper case),
// or nothing for a name the game does not play as music.
std::optional<std::array<std::uint8_t, 16>> song_layers(std::string_view name);

}  // namespace sh::audio
