#pragma once

#include "formats/common/status.h"
#include "formats/convert/converter.h"

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Cutscene converter "cutscene": ANIM/<name>.DMS -> cutscenes/ANIM/<name>.dms.json (sh-cutscene/1), lossless.
//
// DMS: a 0x2C-byte header with file-relative
// offsets, the playback segments, one camera entry and N character entries. An entry is a 4-character name, keyframes
// (character: position Q8 + rotation Q12 angles; camera: position target, look-at target Q8, an unused angle and the
// projection distance) and hold ranges, which map a range of playback frames to one keyframe. Playback runs at 30
// frames per second; keyframe i is played at frame i unless a hold range covers the frame (Dms_KeyframeIdxGet).
//
// The JSON keeps every field as the integer on disc, the offsets of every block, and the bytes no block covers
// ("fill"), so encode() rebuilds the file byte for byte. Positions are relative to header.origin (Q8, 256 = 1 m,
// PS1 axes: +Y down); angles are Q12 turns (4096 = 360 degrees).
namespace sh::convert::cutscene {

struct DmsHoldRange {
    std::int16_t start = 0, end = 0, keyframe = 0;
};

struct DmsEntry {
    std::uint32_t offset = 0;  // where the 16-byte entry itself lies
    std::int16_t keyframe_count = 0;
    std::uint8_t hold_count = 0;
    std::uint8_t pad3 = 0;
    std::array<std::uint8_t, 4> name{};
    std::uint32_t holds_offset = 0;
    std::uint32_t keyframes_offset = 0;
    std::vector<DmsHoldRange> holds;
    std::vector<std::array<std::int16_t, 8>> keyframes;  // character: 6 values used, camera: 8
};

struct Dms {
    std::uint32_t size = 0;  // file size
    std::uint8_t is_loaded = 0;
    std::uint8_t unused_3 = 0;
    std::uint32_t unused_4 = 0;
    std::uint32_t segments_offset = 0;
    std::array<std::int32_t, 3> origin{};
    std::uint32_t characters_offset = 0;
    std::vector<std::array<std::int16_t, 2>> segments;  // start frame, frame count
    DmsEntry camera;
    std::vector<DmsEntry> characters;
    struct Fill {
        std::uint32_t offset;
        std::vector<std::uint8_t> bytes;
    };
    std::vector<Fill> fill;
};

bool looks_like_dms(std::span<const std::uint8_t> raw);
Result<Dms> parse_dms(std::span<const std::uint8_t> raw);
Result<std::vector<std::uint8_t>> serialize_dms(const Dms& dms);
std::string dms_to_json(const Dms& dms, std::string_view game_path, std::span<const std::uint8_t> raw);
Result<Dms> dms_from_json(std::string_view json_text);

std::unique_ptr<sh::convert::Converter> make_converter();

}  // namespace sh::convert::cutscene
