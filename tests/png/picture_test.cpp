// A mod's picture files as RGBA (formats/texture/picture.h, bc7_dds.h): PNG, BC7 DDS and TIM, told apart by their
// first bytes; what the readers refuse.

#include "framework/sh_test.h"
#include "formats/png/sh_png.h"
#include "formats/texture/bc7_dds.h"
#include "formats/texture/picture.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace {

namespace tex = sh::formats::texture;
using Bytes = std::vector<std::uint8_t>;

void put32(Bytes& out, std::uint32_t v) {
    for (int shift = 0; shift < 32; shift += 8) out.push_back(static_cast<std::uint8_t>(v >> shift));
}
void put16(Bytes& out, std::uint32_t v) {
    out.push_back(static_cast<std::uint8_t>(v));
    out.push_back(static_cast<std::uint8_t>(v >> 8));
}

struct Colour {
    std::uint8_t r, g, b, a;
    bool operator==(const Colour&) const = default;
};

Colour texel(const tex::RgbaImage& image, int x, int y) {
    const std::uint8_t* p = &image.pixels[(static_cast<std::size_t>(y) * image.width + x) * 4];
    return {p[0], p[1], p[2], p[3]};
}

// ---- BC7 --------------------------------------------------------------------------------------------------------

// One BC7 block of mode 6: two RGBA endpoints of 7 bits and a shared low bit each, then a 4-bit weight per texel
// (the first texel's has 3 bits). `second` marks the texels that take the second endpoint; the others take the
// first. All channels of an endpoint must have the same low bit.
std::array<std::uint8_t, 16> bc7_block(Colour first, Colour second, std::uint16_t second_mask) {
    std::array<std::uint8_t, 16> block{};
    int at = 0;
    const auto bits = [&](unsigned value, int count) {
        for (int i = 0; i < count; ++i, ++at) {
            if (value >> i & 1) block[static_cast<std::size_t>(at / 8)] |= static_cast<std::uint8_t>(1u << (at % 8));
        }
    };
    bits(1u << 6, 7);  // mode 6
    const std::uint8_t a[4] = {first.r, first.g, first.b, first.a};
    const std::uint8_t b[4] = {second.r, second.g, second.b, second.a};
    for (int c = 0; c < 4; ++c) {
        bits(a[c] >> 1, 7);
        bits(b[c] >> 1, 7);
    }
    bits(first.r & 1, 1);
    bits(second.r & 1, 1);
    bits(0, 3);  // texel 0 is the anchor: always the first endpoint
    for (int i = 1; i < 16; ++i) bits((second_mask >> i & 1) ? 15 : 0, 4);
    return block;
}

Bytes dds_header(std::uint32_t width, std::uint32_t height, std::uint32_t format, const char* code = "DX10") {
    Bytes out = {'D', 'D', 'S', ' '};
    std::array<std::uint32_t, 31> header{};
    header[0] = 124;
    header[2] = height;
    header[3] = width;
    header[6] = 1;     // one level
    header[18] = 32;   // size of the pixel format
    header[19] = 0x4;  // a four-character code follows
    header[20] = static_cast<std::uint32_t>(static_cast<unsigned char>(code[0])) |
                 static_cast<std::uint32_t>(static_cast<unsigned char>(code[1])) << 8 |
                 static_cast<std::uint32_t>(static_cast<unsigned char>(code[2])) << 16 |
                 static_cast<std::uint32_t>(static_cast<unsigned char>(code[3])) << 24;
    for (const std::uint32_t v : header) put32(out, v);
    for (const std::uint32_t v : {format, 3u, 0u, 1u, 0u}) put32(out, v);  // the DX10 extension
    return out;
}

constexpr Colour kRed{200, 20, 40, 254};
constexpr Colour kBlue{31, 41, 201, 255};
constexpr Colour kGreen{10, 240, 30, 128};

// A 10x6 picture: three blocks across, two down, the right and bottom ones cut. Block (bx, by) is kRed when
// bx + by is even, else kBlue; the first block has its lower half in kGreen.
Bytes bc7_file() {
    Bytes out = dds_header(10, 6, 98);
    for (int by = 0; by < 2; ++by) {
        for (int bx = 0; bx < 3; ++bx) {
            const Colour c = (bx + by) % 2 == 0 ? kRed : kBlue;
            const auto block = (bx == 0 && by == 0) ? bc7_block(kRed, kGreen, 0xFF00) : bc7_block(c, c, 0);
            out.insert(out.end(), block.begin(), block.end());
        }
    }
    return out;
}

// ---- TIM --------------------------------------------------------------------------------------------------------

std::uint16_t word555(int r, int g, int b) { return static_cast<std::uint16_t>(r | g << 5 | b << 10); }

// A 4-bit TIM of 8x2 texels: two palette rows of 16 words, the indices 0..15 over the two lines.
Bytes tim4() {
    Bytes out;
    put32(out, 0x10);
    put32(out, 0x8);  // 4 bits with a palette
    put32(out, 12 + 2 * 16 * 2);
    put16(out, 0);
    put16(out, 480);
    put16(out, 16);
    put16(out, 2);
    for (int i = 0; i < 16; ++i) put16(out, i == 0 ? 0 : word555(i, 31 - i, 2 * i));  // row 0; entry 0 is transparent
    for (int i = 0; i < 16; ++i) put16(out, word555(31, 31, 31));                     // row 1 is not read
    put32(out, 12 + 2 * 2 * 2);
    put16(out, 704);
    put16(out, 0);
    put16(out, 2);
    put16(out, 2);
    for (int i = 0; i < 8; ++i) out.push_back(static_cast<std::uint8_t>((2 * i) | (2 * i + 1) << 4));
    return out;
}

}  // namespace

SH_TEST(picture_bc7_blocks_land_where_they_belong) {
    const Bytes file = bc7_file();
    SH_CHECK(tex::is_dds(file));
    const auto header = tex::read_bc7(file);
    SH_CHECK(static_cast<bool>(header));
    if (header) {
        SH_CHECK(header.value().width == 10 && header.value().height == 6);
        SH_CHECK(header.value().levels == 1 && !header.value().srgb);
        SH_CHECK(header.value().blocks.size() == 6 * 16);
    }
    const auto image = tex::decode_bc7(file);
    SH_CHECK(static_cast<bool>(image));
    if (!image) return;
    const tex::RgbaImage& p = image.value();
    SH_CHECK(p.width == 10 && p.height == 6 && p.pixels.size() == 10 * 6 * 4);
    for (int y = 0; y < 6; ++y) {
        for (int x = 0; x < 10; ++x) {
            const int bx = x / 4, by = y / 4;
            Colour want = (bx + by) % 2 == 0 ? kRed : kBlue;
            if (bx == 0 && by == 0 && y >= 2) want = kGreen;
            SH_CHECK(texel(p, x, y) == want);
        }
    }
    // The same through the reader of any picture.
    const auto any = tex::decode_picture(file);
    SH_CHECK(static_cast<bool>(any) && any.value().pixels == p.pixels);
}

SH_TEST(picture_bc7_refusals_name_the_format) {
    Bytes srgb = dds_header(4, 4, 99);
    const auto block = bc7_block(kBlue, kBlue, 0);
    srgb.insert(srgb.end(), block.begin(), block.end());
    SH_CHECK(static_cast<bool>(tex::read_bc7(srgb)) && tex::read_bc7(srgb).value().srgb);
    SH_CHECK(static_cast<bool>(tex::decode_bc7(srgb)));

    Bytes bc1 = dds_header(4, 4, 71);
    bc1.resize(bc1.size() + 16);
    SH_CHECK(!tex::read_bc7(bc1) && tex::read_bc7(bc1).error().find("BC1") != std::string::npos);
    SH_CHECK(!tex::decode_picture(bc1));

    Bytes dxt5 = dds_header(4, 4, 0, "DXT5");
    dxt5.resize(dxt5.size() + 16);
    SH_CHECK(!tex::read_bc7(dxt5) && tex::read_bc7(dxt5).error().find("DXT5") != std::string::npos);

    // Blocks for less than the picture.
    Bytes cut = bc7_file();
    cut.resize(cut.size() - 1);
    SH_CHECK(static_cast<bool>(tex::read_bc7(cut)) && !tex::decode_bc7(cut));
    // No blocks, no header, no file.
    SH_CHECK(!tex::read_bc7(dds_header(4, 4, 98)));
    SH_CHECK(!tex::read_bc7(Bytes{'D', 'D', 'S', ' ', 124, 0, 0, 0}));
    SH_CHECK(!tex::is_dds(Bytes{'D', 'D', 'S'}) && !tex::read_bc7({}));
}

SH_TEST(picture_tim_first_palette_row) {
    const auto image = tex::decode_picture(tim4());
    SH_CHECK(static_cast<bool>(image));
    if (!image) return;
    const tex::RgbaImage& p = image.value();
    SH_CHECK(p.width == 8 && p.height == 2);
    const auto widen = [](int five) { return static_cast<std::uint8_t>(five << 3 | five >> 2); };
    for (int i = 0; i < 16; ++i) {
        const Colour got = texel(p, i % 8, i / 8);
        if (i == 0) {
            SH_CHECK((got == Colour{0, 0, 0, 0}));  // a palette word of 0 is a hole
        } else {
            SH_CHECK((got == Colour{widen(i), widen(31 - i), widen((2 * i) & 31), 255}));
        }
    }
}

SH_TEST(picture_tim_direct_colour_and_damage) {
    // 16 bits: 2x1 texels, the first a zero word.
    Bytes tim16;
    put32(tim16, 0x10);
    put32(tim16, 0x2);
    put32(tim16, 12 + 4);
    put16(tim16, 640);
    put16(tim16, 0);
    put16(tim16, 2);
    put16(tim16, 1);
    put16(tim16, 0);
    put16(tim16, word555(31, 0, 16) | 0x8000);
    const auto direct = tex::decode_tim(tim16);
    SH_CHECK(static_cast<bool>(direct));
    if (direct) {
        SH_CHECK(direct.value().width == 2 && direct.value().height == 1);
        SH_CHECK((texel(direct.value(), 0, 0) == Colour{0, 0, 0, 0}));
        SH_CHECK((texel(direct.value(), 1, 0) == Colour{255, 0, 132, 255}));
    }

    // 24 bits: 3 words hold 2 texels of three bytes.
    Bytes tim24;
    put32(tim24, 0x10);
    put32(tim24, 0x3);
    put32(tim24, 12 + 6);
    put16(tim24, 0);
    put16(tim24, 0);
    put16(tim24, 3);
    put16(tim24, 1);
    for (const std::uint8_t v : {1, 2, 3, 4, 5, 6}) tim24.push_back(v);
    const auto full = tex::decode_tim(tim24);
    SH_CHECK(static_cast<bool>(full));
    if (full) {
        SH_CHECK(full.value().width == 2);
        SH_CHECK((texel(full.value(), 0, 0) == Colour{1, 2, 3, 255}));
        SH_CHECK((texel(full.value(), 1, 0) == Colour{4, 5, 6, 255}));
    }

    // A picture that runs past the file, a palette that does, no palette for indices, another magic.
    Bytes cut = tim4();
    cut.pop_back();
    SH_CHECK(!tex::decode_tim(cut));
    Bytes short_palette = tim4();
    short_palette.resize(8 + 12 + 10);
    SH_CHECK(!tex::decode_tim(short_palette));
    Bytes bare = tim16;
    bare[4] = 0;  // 4 bits, no palette block
    SH_CHECK(!tex::decode_tim(bare));
    Bytes other = tim4();
    other[0] = 0x11;
    SH_CHECK(!tex::decode_picture(other));
    SH_CHECK(!tex::decode_picture({}));
}

SH_TEST(picture_png_keeps_alpha) {
    sh::png::Image source;
    source.width = 2;
    source.height = 2;
    source.color_type = sh::png::ColorType::Rgba;
    source.pixels = {10, 20, 30, 255, 40, 50, 60, 128, 70, 80, 90, 0, 1, 2, 3, 4};
    const auto file = sh::png::encode(source);
    SH_CHECK(static_cast<bool>(file));
    const auto image = tex::decode_picture(file.value());
    SH_CHECK(static_cast<bool>(image));
    if (image) {
        SH_CHECK(image.value().width == 2 && image.value().height == 2);
        SH_CHECK(image.value().pixels == source.pixels);
    }
    // A damaged PNG is an error with the reader's reason, not a TIM.
    Bytes broken = file.value();
    broken.resize(20);
    const auto refused = tex::decode_picture(broken);
    SH_CHECK(!refused && refused.error().rfind("PNG", 0) == 0);
}
