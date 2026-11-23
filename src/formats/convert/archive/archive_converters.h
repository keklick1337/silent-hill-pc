#pragma once

#include "formats/common/status.h"
#include "formats/convert/converter.h"

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

// Converters for the game's packed and recorded data. Both are lossless.
//
// "cmp": TEST/<name>.CMP -> unpacked/TEST/<name>.tim (the unpacked bytes; every CMP on the disc holds a TIM, the
// same bytes as the head of TEST/<name>.TIM) and unpacked/TEST/<name>.cmp.json (sizes, the compressed stream's
// length and the sector padding behind it). The format is LZSS (as the game's unpacker reads it:
// a u32 unpacked size, then flag bytes, LSB first, 1 = literal byte, 0 = 12-bit window offset + 4-bit length - 3; a
// 4096-byte window of zeros, first write at 4078). The encoder below is Haruhiko Okumura's LZSS.C (1989, binary-tree
// match search, F = 18, THRESHOLD = 2) with a zero window, which rebuilds the five files of the disc byte for byte.
//
// "demo": MISC/DEMO<nnnn>.DAT and MISC/PLAY<nnnn>.DAT -> demo/MISC/<name>.dat.json.
// DEMO is the game state a demo starts from (options, savegame, frame count, random seed: s_DemoState, 2048 bytes),
// PLAY the recorded input, 16 bytes a frame (pad state, the game state the frame expects, the present interval, the
// random seed: s_DemoPlaybackFrame). Every field is kept as its integer; the savegame and unnamed bytes as hex.
namespace sh::convert::archive {

// LZSS of the CMP files. lzss_decode stops at the unpacked size of the header or at the end of the input;
// `consumed` is the number of input bytes the stream used (header included).
Result<std::vector<std::uint8_t>> lzss_decode(std::span<const std::uint8_t> packed, std::size_t* consumed = nullptr);
std::vector<std::uint8_t> lzss_encode(std::span<const std::uint8_t> data);

std::unique_ptr<sh::convert::Converter> make_cmp_converter();
std::unique_ptr<sh::convert::Converter> make_demo_converter();

}  // namespace sh::convert::archive
