#include "formats/convert/tim/tim_file.h"

#include <string>
#include <utility>

namespace sh::convert::tim {
namespace {

constexpr std::uint32_t kMagic = 0x10;
constexpr std::uint32_t kFlagClut = 8;
constexpr std::size_t kBlockHeader = 12;

std::uint32_t load32(const std::uint8_t* p) {
    return std::uint32_t{p[0]} | (std::uint32_t{p[1]} << 8) | (std::uint32_t{p[2]} << 16) | (std::uint32_t{p[3]} << 24);
}

std::uint16_t load16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
}

void store32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
    }
}

void store16(std::vector<std::uint8_t>& out, std::uint16_t v) {
    out.push_back(static_cast<std::uint8_t>(v));
    out.push_back(static_cast<std::uint8_t>(v >> 8));
}

Result<TimFile> bad(std::string message) {
    return Result<TimFile>::fail("tim: " + std::move(message));
}

}  // namespace

Result<TimFile> parse_tim(std::span<const std::uint8_t> raw) {
    if (raw.size() < 8) {
        return bad("the file is shorter than its header");
    }
    if (load32(raw.data()) != kMagic) {
        return bad("the first word is not 0x10");
    }
    const std::uint32_t flags = load32(raw.data() + 4);
    if ((flags & ~std::uint32_t{0xF}) != 0 || (flags & 7) > 3) {
        return bad("unknown flags word " + std::to_string(flags));
    }
    TimFile tim;
    tim.depth = static_cast<Depth>(flags & 7);
    tim.has_clut = (flags & kFlagClut) != 0;

    std::size_t pos = 8;
    if (tim.has_clut) {
        if (raw.size() - pos < kBlockHeader) {
            return bad("the CLUT block is cut off");
        }
        const std::uint8_t* p = raw.data() + pos;
        tim.clut.x = load16(p + 4);
        tim.clut.y = load16(p + 6);
        tim.clut.width = load16(p + 8);
        tim.clut.height = load16(p + 10);
        const std::size_t words = std::size_t{tim.clut.width} * tim.clut.height;
        if (words == 0) {
            return bad("the CLUT is empty");
        }
        if (load32(p) != kBlockHeader + words * 2) {
            return bad("the CLUT block length does not match its size");
        }
        if (raw.size() - pos < kBlockHeader + words * 2) {
            return bad("the CLUT block is cut off");
        }
        tim.clut.words.resize(words);
        for (std::size_t i = 0; i < words; ++i) {
            tim.clut.words[i] = load16(p + kBlockHeader + i * 2);
        }
        pos += kBlockHeader + words * 2;
    }

    if (raw.size() - pos < kBlockHeader) {
        return bad("the pixel block is cut off");
    }
    const std::uint8_t* p = raw.data() + pos;
    tim.image_x = load16(p + 4);
    tim.image_y = load16(p + 6);
    tim.image_words = load16(p + 8);
    tim.image_height = load16(p + 10);
    const std::size_t bytes = std::size_t{tim.image_words} * tim.image_height * 2;
    if (bytes == 0) {
        return bad("the picture is empty");
    }
    if (load32(p) != kBlockHeader + bytes) {
        return bad("the pixel block length does not match its size");
    }
    if (raw.size() - pos < kBlockHeader + bytes) {
        return bad("the pixel block is cut off");
    }
    tim.data.assign(p + kBlockHeader, p + kBlockHeader + bytes);
    pos += kBlockHeader + bytes;
    tim.tail.assign(raw.begin() + static_cast<std::ptrdiff_t>(pos), raw.end());
    return Result<TimFile>::ok(std::move(tim));
}

std::vector<std::uint8_t> write_tim(const TimFile& tim) {
    std::vector<std::uint8_t> out;
    store32(out, kMagic);
    store32(out, static_cast<std::uint32_t>(tim.depth) | (tim.has_clut ? kFlagClut : 0));
    if (tim.has_clut) {
        store32(out, static_cast<std::uint32_t>(kBlockHeader + tim.clut.words.size() * 2));
        store16(out, tim.clut.x);
        store16(out, tim.clut.y);
        store16(out, tim.clut.width);
        store16(out, tim.clut.height);
        for (std::uint16_t w : tim.clut.words) {
            store16(out, w);
        }
    }
    store32(out, static_cast<std::uint32_t>(kBlockHeader + tim.data.size()));
    store16(out, tim.image_x);
    store16(out, tim.image_y);
    store16(out, tim.image_words);
    store16(out, tim.image_height);
    out.insert(out.end(), tim.data.begin(), tim.data.end());
    out.insert(out.end(), tim.tail.begin(), tim.tail.end());
    return out;
}

}  // namespace sh::convert::tim
