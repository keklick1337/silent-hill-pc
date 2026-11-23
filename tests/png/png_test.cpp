#include "framework/sh_test.h"
#include "formats/png/sh_png.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Checks sh_png against things it does not share code with: a bitwise CRC-32 and an Adler-32 written here, a chunk
// walker written here, PNG files assembled by hand (stored deflate blocks, every filter, packed samples), and
// stb_image through decode_rgba8.

namespace {

namespace png = sh::png;
using Bytes = std::vector<std::uint8_t>;

std::uint32_t crc32_bitwise(const std::uint8_t* data, std::size_t size) {
    std::uint32_t crc = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (int k = 0; k < 8; ++k) {
            crc = (crc & 1) ? (crc >> 1) ^ 0xEDB88320u : crc >> 1;
        }
    }
    return ~crc;
}

std::uint32_t adler32(const Bytes& data) {
    std::uint32_t a = 1;
    std::uint32_t b = 0;
    for (std::uint8_t v : data) {
        a = (a + v) % 65521;
        b = (b + a) % 65521;
    }
    return (b << 16) | a;
}

void put32(Bytes& out, std::uint32_t v) {
    out.push_back(static_cast<std::uint8_t>(v >> 24));
    out.push_back(static_cast<std::uint8_t>(v >> 16));
    out.push_back(static_cast<std::uint8_t>(v >> 8));
    out.push_back(static_cast<std::uint8_t>(v));
}

std::uint32_t get32(const std::uint8_t* p) {
    return (std::uint32_t{p[0]} << 24) | (std::uint32_t{p[1]} << 16) | (std::uint32_t{p[2]} << 8) | p[3];
}

struct Chunk {
    std::string type;
    Bytes data;
};

// Walks a PNG: checks the signature and the CRC of every chunk with the CRC above. False on any violation.
bool walk_chunks(const Bytes& png, std::vector<Chunk>& chunks) {
    static constexpr std::array<std::uint8_t, 8> sig = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    if (png.size() < 8 || !std::equal(sig.begin(), sig.end(), png.begin())) {
        return false;
    }
    std::size_t pos = 8;
    while (pos < png.size()) {
        if (png.size() - pos < 12) {
            return false;
        }
        const std::uint32_t length = get32(&png[pos]);
        if (length > png.size() - pos - 12) {
            return false;
        }
        const std::uint32_t crc = get32(&png[pos + 8 + length]);
        if (crc != crc32_bitwise(&png[pos + 4], length + 4)) {
            return false;
        }
        chunks.push_back({std::string(reinterpret_cast<const char*>(&png[pos + 4]), 4),
                          Bytes(png.begin() + pos + 8, png.begin() + pos + 8 + length)});
        pos += length + 12;
    }
    return true;
}

Bytes stored_zlib(const Bytes& raw) {
    Bytes out = {0x78, 0x01};
    std::size_t pos = 0;
    do {
        const std::size_t n = std::min<std::size_t>(65535, raw.size() - pos);
        const bool last = pos + n == raw.size();
        out.push_back(last ? 1 : 0);
        out.push_back(static_cast<std::uint8_t>(n));
        out.push_back(static_cast<std::uint8_t>(n >> 8));
        out.push_back(static_cast<std::uint8_t>(~n));
        out.push_back(static_cast<std::uint8_t>((~n) >> 8));
        out.insert(out.end(), raw.begin() + pos, raw.begin() + pos + n);
        pos += n;
    } while (pos < raw.size());
    put32(out, adler32(raw));
    return out;
}

void add_chunk(Bytes& out, const char* type, const Bytes& data) {
    put32(out, static_cast<std::uint32_t>(data.size()));
    const std::size_t start = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), data.begin(), data.end());
    put32(out, crc32_bitwise(&out[start], out.size() - start));
}

struct Spec {
    std::uint32_t width;
    std::uint32_t height;
    std::uint8_t depth;
    std::uint8_t color;
    Bytes raw;  // scanlines with their filter bytes
    Bytes palette;
    Bytes trns;
    std::uint8_t interlace = 0;
};

Bytes build_png(const Spec& s) {
    Bytes out = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    Bytes ihdr;
    put32(ihdr, s.width);
    put32(ihdr, s.height);
    ihdr.insert(ihdr.end(), {s.depth, s.color, 0, 0, s.interlace});
    add_chunk(out, "IHDR", ihdr);
    if (!s.palette.empty()) {
        add_chunk(out, "PLTE", s.palette);
    }
    if (!s.trns.empty()) {
        add_chunk(out, "tRNS", s.trns);
    }
    add_chunk(out, "IDAT", stored_zlib(s.raw));
    add_chunk(out, "IEND", {});
    return out;
}

// Deterministic noise.
Bytes noise(std::size_t count, std::uint32_t seed) {
    Bytes out(count);
    for (auto& v : out) {
        seed = seed * 1664525u + 1013904223u;
        v = static_cast<std::uint8_t>(seed >> 24);
    }
    return out;
}

png::Image make_image(png::ColorType type, std::uint32_t w, std::uint32_t h, std::uint32_t seed) {
    png::Image image;
    image.width = w;
    image.height = h;
    image.color_type = type;
    image.pixels = noise(std::size_t{w} * h * png::channels(type), seed);
    if (type == png::ColorType::Indexed) {
        image.palette = noise(3 * 20, seed + 1);
        image.palette_alpha = {0, 128, 255, 7};
        for (auto& v : image.pixels) {
            v %= 20;
        }
    }
    return image;
}

// What an RGBA reader must show for the image.
Bytes expected_rgba(const png::Image& image) {
    Bytes out;
    const std::size_t n = std::size_t{image.width} * image.height;
    const std::size_t ch = png::channels(image.color_type);
    for (std::size_t i = 0; i < n; ++i) {
        const std::uint8_t* p = &image.pixels[i * ch];
        switch (image.color_type) {
            case png::ColorType::Gray: out.insert(out.end(), {p[0], p[0], p[0], 255}); break;
            case png::ColorType::GrayAlpha: out.insert(out.end(), {p[0], p[0], p[0], p[1]}); break;
            case png::ColorType::Rgb: out.insert(out.end(), {p[0], p[1], p[2], 255}); break;
            case png::ColorType::Rgba: out.insert(out.end(), {p[0], p[1], p[2], p[3]}); break;
            case png::ColorType::Indexed: {
                const std::uint8_t a = p[0] < image.palette_alpha.size() ? image.palette_alpha[p[0]] : 255;
                out.insert(out.end(), {image.palette[p[0] * 3], image.palette[p[0] * 3 + 1],
                                       image.palette[p[0] * 3 + 2], a});
                break;
            }
        }
    }
    return out;
}

constexpr std::array<png::ColorType, 5> kTypes = {png::ColorType::Gray, png::ColorType::GrayAlpha,
                                                  png::ColorType::Rgb, png::ColorType::Rgba, png::ColorType::Indexed};

}  // namespace

SH_TEST(PngCrcAndAdlerReferenceValues) {
    // The standard check values: CRC-32("123456789") = CBF43926, Adler-32("Wikipedia") = 11E60398.
    const std::string a = "123456789";
    SH_CHECK(crc32_bitwise(reinterpret_cast<const std::uint8_t*>(a.data()), a.size()) == 0xCBF43926u);
    const std::string b = "Wikipedia";
    SH_CHECK(adler32(Bytes(b.begin(), b.end())) == 0x11E60398u);
}

SH_TEST(PngEncodedStructureIsValid) {
    for (png::ColorType type : kTypes) {
        const png::Image image = make_image(type, 13, 9, 5);
        const auto encoded = png::encode(image);
        SH_CHECK(static_cast<bool>(encoded));
        if (!encoded) {
            continue;
        }
        std::vector<Chunk> chunks;
        SH_CHECK(walk_chunks(encoded.value(), chunks));  // signature and every CRC with the reference CRC
        if (chunks.size() < 3) {
            SH_CHECK(false);
            continue;
        }
        SH_CHECK(chunks.front().type == "IHDR");
        SH_CHECK(chunks.back().type == "IEND");
        SH_CHECK(chunks.back().data.empty());
        const Bytes& ihdr = chunks.front().data;
        SH_CHECK(ihdr.size() == 13);
        SH_CHECK(get32(&ihdr[0]) == 13 && get32(&ihdr[4]) == 9);
        SH_CHECK(ihdr[8] == 8 && ihdr[9] == static_cast<std::uint8_t>(type));
        SH_CHECK(ihdr[10] == 0 && ihdr[11] == 0 && ihdr[12] == 0);

        // Order: IHDR, [PLTE, tRNS], IDAT, IEND, with PLTE and tRNS only for Indexed.
        std::vector<std::string> order;
        for (const Chunk& c : chunks) {
            order.push_back(c.type);
        }
        const std::vector<std::string> want = type == png::ColorType::Indexed
            ? std::vector<std::string>{"IHDR", "PLTE", "tRNS", "IDAT", "IEND"}
            : std::vector<std::string>{"IHDR", "IDAT", "IEND"};
        SH_CHECK(order == want);

        // The zlib header of the IDAT: deflate, 32K window, check bits make CMF*256+FLG a multiple of 31.
        const Bytes& idat = chunks[chunks.size() - 2].data;
        SH_CHECK(idat.size() > 6);
        SH_CHECK((idat[0] & 0x0F) == 8 && (idat[0] >> 4) <= 7);
        SH_CHECK((idat[0] * 256 + idat[1]) % 31 == 0);
        if (type == png::ColorType::Indexed) {
            SH_CHECK(chunks[1].data == image.palette);
            SH_CHECK(chunks[2].data == image.palette_alpha);  // no trailing 255 to trim in this palette
        }
    }
}

SH_TEST(PngTrnsDropsTrailingOpaqueEntries) {
    png::Image image;
    image.width = 2;
    image.height = 1;
    image.color_type = png::ColorType::Indexed;
    image.pixels = {0, 3};
    image.palette = {0, 0, 0, 1, 1, 1, 2, 2, 2, 3, 3, 3};
    image.palette_alpha = {0, 255, 255, 255};
    auto encoded = png::encode(image);
    SH_CHECK(static_cast<bool>(encoded));
    std::vector<Chunk> chunks;
    SH_CHECK(encoded && walk_chunks(encoded.value(), chunks));
    SH_CHECK(chunks.size() == 5 && chunks[2].type == "tRNS" && chunks[2].data == Bytes{0});

    image.palette_alpha = {255, 255};
    encoded = png::encode(image);
    chunks.clear();
    SH_CHECK(encoded && walk_chunks(encoded.value(), chunks));
    SH_CHECK(chunks.size() == 4);  // all opaque: no tRNS at all
}

SH_TEST(PngRoundTripAndStbAgree) {
    const std::array<std::pair<std::uint32_t, std::uint32_t>, 5> sizes = {{{1, 1}, {7, 5}, {64, 33}, {1, 40}, {40, 1}}};
    for (png::ColorType type : kTypes) {
        for (const auto& [w, h] : sizes) {
            const png::Image image = make_image(type, w, h, w * 31 + h);
            const auto encoded = png::encode(image);
            SH_CHECK(static_cast<bool>(encoded));
            if (!encoded) {
                continue;
            }
            const auto back = png::decode(encoded.value());
            SH_CHECK(static_cast<bool>(back));
            if (back) {
                SH_CHECK(back.value().width == w && back.value().height == h);
                SH_CHECK(back.value().color_type == type);
                SH_CHECK(back.value().pixels == image.pixels);
                SH_CHECK(back.value().palette == image.palette);
                // The trailing opaque entries are trimmed from tRNS; the visible alpha is the same.
                SH_CHECK(back.value().palette_alpha.size() <= image.palette_alpha.size());
            }
            const auto rgba = png::decode_rgba8(encoded.value());
            SH_CHECK(static_cast<bool>(rgba));
            if (rgba) {
                SH_CHECK(rgba.value().width == w && rgba.value().height == h);
                SH_CHECK(rgba.value().pixels == expected_rgba(image));
            }
        }
    }
}

SH_TEST(PngEncodingIsDeterministicAndCompresses) {
    png::Image flat;
    flat.width = 256;
    flat.height = 256;
    flat.color_type = png::ColorType::Rgba;
    flat.pixels.assign(256 * 256 * 4, 0x55);
    const auto a = png::encode(flat);
    const auto b = png::encode(flat);
    SH_CHECK(a && b && a.value() == b.value());
    SH_CHECK(a && a.value().size() < 1024);
}

SH_TEST(PngDecodesEveryFilter) {
    // 3x5 RGB image; row y uses filter y. The forward filter is written out here, apart from the library's.
    constexpr std::uint32_t w = 3;
    constexpr std::uint32_t h = 5;
    constexpr std::size_t stride = w * 3;
    const Bytes pixels = noise(stride * h, 99);
    auto paeth = [](int a, int b, int c) {
        const int p = a + b - c;
        const int pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
        return pa <= pb && pa <= pc ? a : (pb <= pc ? b : c);
    };
    Bytes raw;
    for (std::uint32_t y = 0; y < h; ++y) {
        raw.push_back(static_cast<std::uint8_t>(y));
        for (std::size_t i = 0; i < stride; ++i) {
            const int x = pixels[y * stride + i];
            const int a = i >= 3 ? pixels[y * stride + i - 3] : 0;
            const int b = y > 0 ? pixels[(y - 1) * stride + i] : 0;
            const int c = (y > 0 && i >= 3) ? pixels[(y - 1) * stride + i - 3] : 0;
            int pred = 0;
            switch (y) {
                case 1: pred = a; break;
                case 2: pred = b; break;
                case 3: pred = (a + b) / 2; break;
                case 4: pred = paeth(a, b, c); break;
                default: break;
            }
            raw.push_back(static_cast<std::uint8_t>(x - pred));
        }
    }
    const Bytes file = build_png({w, h, 8, 2, raw, {}, {}});
    const auto image = png::decode(file);
    SH_CHECK(static_cast<bool>(image));
    if (image) {
        SH_CHECK(image.value().pixels == pixels);
    }
    const auto rgba = png::decode_rgba8(file);  // stb agrees that the file is what we think it is
    SH_CHECK(rgba && image && rgba.value().pixels == expected_rgba(image.value()));
}

SH_TEST(PngUnpacksLowBitDepths) {
    // 4-bit indexed, 5x2: two samples per byte, high nibble first, rows padded to a byte.
    const Bytes indices = {1, 15, 0, 7, 9, 3, 3, 12, 2, 8};
    Bytes raw = {0, 0x1F, 0x07, 0x90, 0, 0x33, 0xC2, 0x80};
    Bytes palette;
    for (int i = 0; i < 16; ++i) {
        palette.insert(palette.end(), {static_cast<std::uint8_t>(i * 16), static_cast<std::uint8_t>(255 - i), 7});
    }
    Bytes file = build_png({5, 2, 4, 3, raw, palette, {}});
    auto image = png::decode(file);
    SH_CHECK(static_cast<bool>(image));
    if (image) {
        SH_CHECK(image.value().color_type == png::ColorType::Indexed);
        SH_CHECK(image.value().pixels == indices);
        SH_CHECK(image.value().palette == palette);
        const auto rgba = png::decode_rgba8(file);
        SH_CHECK(rgba && rgba.value().pixels == expected_rgba(image.value()));
    }

    // 1-bit gray, 10x1: 10 bits across two bytes.
    file = build_png({10, 1, 1, 0, {0, 0xA5, 0x40}, {}, {}});
    image = png::decode(file);
    SH_CHECK(static_cast<bool>(image));
    if (image) {
        SH_CHECK((image.value().pixels == Bytes{1, 0, 1, 0, 0, 1, 0, 1, 0, 1}));
    }
}

SH_TEST(PngRejectsDamagedFiles) {
    const png::Image image = make_image(png::ColorType::Rgba, 16, 16, 3);
    const Bytes good = png::encode(image).value();
    SH_CHECK(static_cast<bool>(png::decode(good)));

    Bytes bad = good;
    bad[0] = 0x88;
    SH_CHECK(!png::decode(bad));

    for (std::size_t cut : {std::size_t{4}, std::size_t{20}, good.size() / 2, good.size() - 12, good.size() - 1}) {
        bad.assign(good.begin(), good.begin() + cut);
        SH_CHECK(!png::decode(bad));
    }

    // One bit flipped in each chunk body, not only in the pixel data: the CRC catches IHDR and IDAT alike.
    std::vector<Chunk> chunks;
    SH_CHECK(walk_chunks(good, chunks));
    std::size_t pos = 8;
    for (const Chunk& c : chunks) {
        if (!c.data.empty()) {
            bad = good;
            bad[pos + 8] ^= 0x10;
            SH_CHECK(!png::decode(bad));
        }
        pos += c.data.size() + 12;
    }

    // A stream that is valid but not for this image: the Adler-32 is wrong, the CRC of the chunk is made right.
    Spec spec{2, 2, 8, 0, {0, 1, 2, 0, 3, 4}, {}, {}};
    SH_CHECK(static_cast<bool>(png::decode(build_png(spec))));
    Bytes file = build_png(spec);
    // Layout: signature (8), IHDR (25), then IDAT: length, type, data, CRC. The Adler-32 ends the data.
    const std::size_t idat_at = 8 + 25;
    const std::uint32_t idat_len = get32(&file[idat_at]);
    file[idat_at + 8 + idat_len - 1] ^= 1;
    const std::uint32_t crc = crc32_bitwise(&file[idat_at + 4], idat_len + 4);
    for (int i = 0; i < 4; ++i) {
        file[idat_at + 8 + idat_len + i] = static_cast<std::uint8_t>(crc >> (24 - 8 * i));
    }
    SH_CHECK(!png::decode(file));

    // Filter type 5 does not exist.
    SH_CHECK(!png::decode(build_png({2, 2, 8, 0, {5, 1, 2, 0, 3, 4}, {}, {}})));
    // Raw data too short for the image.
    SH_CHECK(!png::decode(build_png({2, 2, 8, 0, {0, 1, 2, 0, 3}, {}, {}})));
    // Indexed without PLTE, and an index past the palette.
    SH_CHECK(!png::decode(build_png({1, 1, 8, 3, {0, 0}, {}, {}})));
    SH_CHECK(!png::decode(build_png({1, 1, 8, 3, {0, 2}, {1, 2, 3, 4, 5, 6}, {}})));
    // 16-bit and interlaced files are for decode_rgba8.
    const Bytes sixteen = build_png({1, 1, 16, 0, {0, 0x12, 0x34}, {}, {}});
    SH_CHECK(!png::decode(sixteen));
    SH_CHECK(static_cast<bool>(png::decode_rgba8(sixteen)));
    SH_CHECK(!png::decode(build_png({1, 1, 8, 0, {0, 9}, {}, {}, 1})));
    // Bad bit depth for the colour type.
    SH_CHECK(!png::decode(build_png({1, 1, 4, 2, {0, 0, 0, 0}, {}, {}})));
}

SH_TEST(PngEncodeRejectsBadInput) {
    png::Image image = make_image(png::ColorType::Rgb, 4, 4, 1);
    image.pixels.pop_back();
    SH_CHECK(!png::encode(image));

    image = make_image(png::ColorType::Rgb, 0, 4, 1);
    SH_CHECK(!png::encode(image));

    image = make_image(png::ColorType::Indexed, 4, 4, 1);
    image.pixels[3] = 20;  // the palette has 20 entries
    SH_CHECK(!png::encode(image));

    image = make_image(png::ColorType::Indexed, 4, 4, 1);
    image.palette.clear();
    SH_CHECK(!png::encode(image));

    image = make_image(png::ColorType::Indexed, 4, 4, 1);
    image.palette_alpha.assign(21, 0);
    SH_CHECK(!png::encode(image));

    image = make_image(png::ColorType::Rgb, 4, 4, 1);
    image.palette = {1, 2, 3};
    SH_CHECK(!png::encode(image));
}

SH_TEST(PngEncodePalettesSharesThePixelsAndKeepsEachPalette) {
    png::Image image = make_image(png::ColorType::Indexed, 9, 7, 17);
    std::vector<png::Palette> palettes(3);
    for (std::size_t k = 0; k < palettes.size(); ++k) {
        palettes[k].rgb = noise(3 * 20, 100 + static_cast<std::uint32_t>(k));
        palettes[k].alpha.assign(k + 1, 0);
    }
    const auto files = png::encode_palettes(image, palettes);
    SH_CHECK(static_cast<bool>(files));
    if (!files) {
        return;
    }
    SH_CHECK(files.value().size() == 3);
    for (std::size_t k = 0; k < 3; ++k) {
        const auto back = png::decode(files.value()[k]);
        SH_CHECK(static_cast<bool>(back));
        if (!back) {
            continue;
        }
        SH_CHECK(back.value().pixels == image.pixels);
        SH_CHECK(back.value().palette == palettes[k].rgb);
        SH_CHECK(back.value().palette_alpha == palettes[k].alpha);
        // One file equals what encode() writes for the same image and palette.
        png::Image single = image;
        single.palette = palettes[k].rgb;
        single.palette_alpha = palettes[k].alpha;
        SH_CHECK(png::encode(single).value() == files.value()[k]);
    }
    // An index past the end of any palette is an error, and so is a non-indexed image or no palette.
    palettes[1].rgb.resize(3 * 5);
    SH_CHECK(!png::encode_palettes(image, palettes));
    SH_CHECK(!png::encode_palettes(image, std::span<const png::Palette>{}));
    SH_CHECK(!png::encode_palettes(make_image(png::ColorType::Rgb, 2, 2, 1), palettes));
}
