#pragma once

// A song of the game as the sequencer plays it: tracks of steps (delta times and events) in file order. The importer
// writes every song as convert/audio/seq/SND/<NAME>.kdt.json (format sh-kdt/1, lossless); the engine reads that.
//
// Steps keep what the sequencer needs and nothing more: a delta time, a key (with its velocity), "release the last
// key", a channel, tempo, pitch bend, program, end of track, or a control change. Value bytes keep bit 7, which says
// that no delta time follows the event.

#include "formats/common/status.h"

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace sh::audio {

struct SongStep {
    enum class Kind : std::uint8_t {
        Wait,             // ticks
        Note,             // a = key, b = velocity (0: release), bit 7 of b: no delta follows
        ReleaseLast,      // a delta follows
        ReleaseLastNoDelta,
        Channel,          // a = channel
        Tempo,            // a: tempo (a & 0x7F) * 2 + 2
        Bend,             // a = pitch bend MSB
        Program,          // a = program
        End,              // a = value byte
        Control,          // a = controller, b = value
    };
    Kind kind = Kind::Wait;
    std::uint8_t a = 0, b = 0;
    std::uint32_t ticks = 0;
};

struct Song {
    std::uint32_t ticks_per_quarter = 480;
    std::vector<std::vector<SongStep>> tracks;
};

// sh-kdt/1 JSON. The song's size field cuts the tracks the way the sequencer stops reading past it.
Result<Song> load_song(std::string_view json_text);

// A KDT1 file from the disc, through the importer's converter. For the importer and the tools.
Result<Song> parse_kdt(std::span<const std::uint8_t> raw);

}  // namespace sh::audio
