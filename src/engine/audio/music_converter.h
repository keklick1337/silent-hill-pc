#pragma once

#include "engine/audio/music_render.h"
#include "formats/convert/converter.h"

#include <functional>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

// Converter "music": SND/<name>.KDT with the bank of the same name, SND/<name>.VAB (read through the sibling reader),
// rendered by the engine's sequencer and SPU model into audio/music/<name>.wav (stereo, 44.1 kHz, smpl loop), or
// with MusicFile::Opus into audio/music/<name>.opus (Ogg Opus, stereo, 48 kHz, 128 kbit/s, loop in the tags
// LOOPSTART and LOOPLENGTH), and audio/music/<name>.music.json (loop points, spill, render settings, in the frames of
// the written file). Not lossless: a representation for players,
// mods and the native audio engine; the lossless form of the song is the kdt converter's .kdt.json.
//
// It lives in the engine rather than in formats/convert/audio because it runs the engine's synthesizer.
namespace sh::audio {

using SiblingReader = std::function<std::optional<std::vector<std::uint8_t>>(std::string_view game_path)>;

enum class MusicFile { Wav, Opus };

std::unique_ptr<sh::convert::Converter> make_music_converter(SiblingReader sibling, MusicOptions options = {},
                                                             MusicFile file = MusicFile::Wav);

}  // namespace sh::audio
