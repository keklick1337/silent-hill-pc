#pragma once

#include "formats/common/status.h"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sh::convert {

// One file produced by decode(). The path is relative to the converted tree, e.g. "textures/CHARA/HERO.png".
struct Output {
    std::string relative_path;
    std::vector<std::uint8_t> bytes;
};

// A converter turns one raw game file into a modern representation.
// When lossless() is true, encode(decode(raw)) must return raw byte for byte.
// Converters that are not lossless (glTF models, WAV audio) give a representation only:
// the game logic and the sound engine keep reading the raw files.
class Converter {
public:
    virtual ~Converter() = default;

    // Short identifier: "tim", "model", "vab", "xa", "text", "font", "level".
    virtual std::string_view name() const = 0;

    virtual bool lossless() const = 0;

    // True when this converter takes the file. Decided by the bytes and the game path, not by the extension alone.
    // The importer calls decode() only for files that were accepted.
    virtual bool accepts(std::span<const std::uint8_t> raw, std::string_view game_path) const = 0;

    // Decodes one raw file. `game_path` is the path inside the game, e.g. "CHARA/HERO.TIM".
    virtual Result<std::vector<Output>> decode(std::span<const std::uint8_t> raw, std::string_view game_path) const = 0;

    // Rebuilds the raw bytes from the outputs of decode(). Only meaningful when lossless() is true.
    virtual Result<std::vector<std::uint8_t>> encode(std::span<const Output> outputs) const = 0;
};

}  // namespace sh::convert
