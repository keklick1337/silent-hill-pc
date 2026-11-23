#pragma once

// Names in the cache of upscaled textures (docs/RENDER.md section 12), shared by the tool that writes it
// (tools/sh-upscale) and the engine that reads it (texture_upscale.h). Header only: the tool links no engine library.
//
//   <gamedata>/cache/upscale/<model>-x<scale>-v<version>/<Folder>/<NAME>[.<label>].<source hash>.png
//
// The source hash is of the PNG bytes the import wrote, so a re-import or a changed picture misses the cache by
// itself; a change of the model or of the processing changes kModelVersion.

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace sh::upscale {

// The model: Real-ESRGAN x4plus, chosen over realesr-animevideov3 (soft, smeared skin and walls) and x4plus-anime
// (paints outlines) on the game's own textures. It runs at x4 only: x2 is its result averaged down. Nothing goes
// above x4: a second pass over the x4 picture (the former x8 and x16) smeared more than it added.
inline constexpr std::string_view kModel = "realesrgan-x4plus";

// The scales texture_upscale takes, 0 being off.
inline constexpr int kScales[] = {0, 2, 4};

inline bool valid_scale(int scale) { return scale == 2 || scale == 4; }

// No side of an upscaled picture passes this: a larger picture gets the largest power of two scale that fits.
inline constexpr int kMaxSide = 2048;
inline constexpr std::string_view kModelVersion = "2";

// The scale the network works at, and the scale a picture of w x h gets when `scale` is asked: the largest power of
// two up to it within kMaxSide (1 when even x2 does not fit).
inline constexpr int kNetScale = 4;
inline int fitted_scale(int width, int height, int scale) {
    const int side = width > height ? width : height;
    while (scale > 1 && side * scale > kMaxSide) scale /= 2;
    return scale;
}
inline constexpr std::string_view kTexturesDir = "convert/textures/";

// FNV-1a, 64 bits.
inline std::uint64_t source_hash(std::span<const std::uint8_t> bytes) {
    std::uint64_t h = 0xcbf29ce484222325ull;
    for (const std::uint8_t b : bytes) {
        h ^= b;
        h *= 0x100000001b3ull;
    }
    return h;
}

// "<Folder>/<NAME>[.<label>]" of a path under convert/textures/ (any prefix before it, '\' or '/'), or nullopt for
// a path elsewhere or a picture that is not upscaled: fonts (their cells are sampled exactly), the blend layers of a
// texture (`.subtract`, `.add`: masks, not pictures) and the importer's TEST folder.
inline std::optional<std::string> texture_key(std::string_view path) {
    std::string p(path);
    for (char& c : p)
        if (c == '\\') c = '/';
    const std::size_t at = p.rfind(kTexturesDir);
    if (at == std::string::npos) return std::nullopt;
    std::string key = p.substr(at + kTexturesDir.size());
    if (key.size() < 5 || key.compare(key.size() - 4, 4, ".png") != 0) return std::nullopt;
    key.resize(key.size() - 4);
    const std::size_t slash = key.find('/');
    if (slash == std::string::npos || key.find('/', slash + 1) != std::string::npos) return std::nullopt;
    const std::string_view folder = std::string_view(key).substr(0, slash);
    const std::string_view name = std::string_view(key).substr(slash + 1);
    if (folder == "TEST" || name.substr(0, 4) == "FONT") return std::nullopt;
    const std::size_t dot = name.find('.');
    if (dot != std::string::npos) {
        const std::string_view label = name.substr(dot + 1);
        if (label == "subtract" || label == "add") return std::nullopt;
    }
    return key;
}

inline std::filesystem::path model_dir(const std::filesystem::path& cache_root, std::string_view model, int scale) {
    return cache_root / (std::string(model) + "-x" + std::to_string(scale) + "-v" + std::string(kModelVersion));
}

inline std::filesystem::path cache_file(const std::filesystem::path& cache_root, std::string_view model,
                                        std::string_view key, int scale, std::uint64_t hash) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string name(key);
    name += '.';
    for (int i = 15; i >= 0; --i) name += kHex[(hash >> (i * 4)) & 0xF];
    name += ".png";
    return model_dir(cache_root, model, scale) / std::filesystem::path(name);
}

// In a model directory: written when every picture of a pass is there, and "<done> <total>" while a pass runs. The
// engine draws each picture as soon as its file is there; these only tell the player how far the pass is.
inline constexpr std::string_view kCompleteFile = "complete";
inline constexpr std::string_view kProgressFile = "progress";

// In a model directory: "<key>\t<lettering>" of each picture made with lettering (sh-upscale's Lettering), so a
// changed entry of gamedata/config/texture_upscale.json makes that picture again.
inline constexpr std::string_view kLetteringFile = "lettering";

// In the cache root: the keys the game draws now without their upscaled picture, one per line, most wanted first.
// The tool reads it again whenever it changes and does those pictures before the rest.
inline constexpr std::string_view kWantedFile = "wanted";

// In the cache root, written by the game for the tool: config/texture_upscale.json with the active mods' patches
// (--lettering), and the keys of the pictures active mods replace (--skip), one per line.
inline constexpr std::string_view kLetteringInput = "texture_upscale.json";
inline constexpr std::string_view kSkipInput = "replaced_by_mods";

// The cache under a gamedata folder.
inline std::filesystem::path cache_root(const std::filesystem::path& gamedata) {
    return gamedata / "cache" / "upscale";
}

}  // namespace sh::upscale
