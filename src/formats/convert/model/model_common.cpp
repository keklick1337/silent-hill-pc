#include "formats/convert/model/model_build.h"

#include <algorithm>
#include <cctype>

namespace sh::convert::model {

std::string hex_bytes(std::span<const std::uint8_t> bytes) {
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (const std::uint8_t b : bytes) {
        out += kDigits[b >> 4];
        out += kDigits[b & 15];
    }
    return out;
}

std::string upper_copy(std::string_view text) {
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return out;
}

namespace {

std::string normalized(std::string_view game_path) {
    std::string p(game_path);
    std::replace(p.begin(), p.end(), '\\', '/');
    return p;
}

}  // namespace

std::string path_without_extension(std::string_view game_path) {
    const std::string p = normalized(game_path);
    const std::size_t slash = p.rfind('/');
    const std::size_t dot = p.rfind('.');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) {
        return p;
    }
    return p.substr(0, dot);
}

std::string path_directory(std::string_view game_path) {
    const std::string p = normalized(game_path);
    const std::size_t slash = p.rfind('/');
    return slash == std::string::npos ? std::string() : p.substr(0, slash);
}

std::string path_stem(std::string_view game_path) {
    const std::string p = path_without_extension(game_path);
    const std::size_t slash = p.rfind('/');
    return slash == std::string::npos ? p : p.substr(slash + 1);
}

}  // namespace sh::convert::model
