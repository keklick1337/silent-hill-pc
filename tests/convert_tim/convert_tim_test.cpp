#include "framework/sh_test.h"
#include "formats/archive/sh_archive.h"
#include "formats/convert/tim/tim_converter.h"
#include "formats/disc/disc_image.h"
#include "formats/iso9660/iso9660.h"
#include "formats/png/palette_variant.h"
#include "formats/png/sh_png.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifndef SH_PROJECT_ROOT
#define SH_PROJECT_ROOT "../.."
#endif

// TIM <-> PNG. The synthetic files are assembled byte by byte here (not with the module's writer), and the colours
// are checked against the colour rules written out again below. The last test runs every TIM of the image.

namespace {

namespace conv = sh::convert;
namespace png = sh::png;
using Bytes = std::vector<std::uint8_t>;

void le16(Bytes& out, unsigned v) {
    out.push_back(static_cast<std::uint8_t>(v));
    out.push_back(static_cast<std::uint8_t>(v >> 8));
}

void le32(Bytes& out, std::uint32_t v) {
    le16(out, v & 0xFFFF);
    le16(out, v >> 16);
}

struct Rect {
    unsigned x, y, w, h;
};

// header + optional CLUT block + pixel block + tail, laid out from the format description.
Bytes build_tim(unsigned flags, const std::optional<std::pair<Rect, std::vector<std::uint16_t>>>& clut, Rect image,
                const Bytes& pixels, const Bytes& tail = {}) {
    Bytes out;
    le32(out, 0x10);
    le32(out, flags);
    if (clut) {
        le32(out, 12 + static_cast<std::uint32_t>(clut->second.size()) * 2);
        le16(out, clut->first.x);
        le16(out, clut->first.y);
        le16(out, clut->first.w);
        le16(out, clut->first.h);
        for (std::uint16_t w : clut->second) {
            le16(out, w);
        }
    }
    le32(out, 12 + static_cast<std::uint32_t>(pixels.size()));
    le16(out, image.x);
    le16(out, image.y);
    le16(out, image.w);
    le16(out, image.h);
    out.insert(out.end(), pixels.begin(), pixels.end());
    out.insert(out.end(), tail.begin(), tail.end());
    return out;
}

Bytes noise(std::size_t count, std::uint32_t seed) {
    Bytes out(count);
    for (auto& v : out) {
        seed = seed * 1664525u + 1013904223u;
        v = static_cast<std::uint8_t>(seed >> 24);
    }
    return out;
}

std::vector<std::uint16_t> word_noise(std::size_t count, std::uint32_t seed) {
    const Bytes b = noise(count * 2, seed);
    std::vector<std::uint16_t> out(count);
    for (std::size_t i = 0; i < count; ++i) {
        out[i] = static_cast<std::uint16_t>(b[i * 2] | (b[i * 2 + 1] << 8));
    }
    return out;
}

// BGR555: expansion to 8 bits, alpha 0 only for the word 0x0000.
std::array<std::uint8_t, 4> rgba_of(std::uint16_t w) {
    auto e = [](unsigned v) { return static_cast<std::uint8_t>((v << 3) | (v >> 2)); };
    return {e(w & 31), e((w >> 5) & 31), e((w >> 10) & 31), static_cast<std::uint8_t>(w == 0 ? 0 : 255)};
}

const conv::Output* find(const std::vector<conv::Output>& outputs, const std::string& path) {
    for (const auto& o : outputs) {
        if (o.relative_path == path) {
            return &o;
        }
    }
    return nullptr;
}

conv::Output* find(std::vector<conv::Output>& outputs, const std::string& path) {
    for (auto& o : outputs) {
        if (o.relative_path == path) {
            return &o;
        }
    }
    return nullptr;
}

png::ByteReader reader_of(const std::vector<conv::Output>& outputs) {
    return [&outputs](const std::string& path) -> std::optional<Bytes> {
        const conv::Output* o = find(outputs, path);
        return o ? std::optional<Bytes>(o->bytes) : std::nullopt;
    };
}

// A palette variant ("textures/X/NAME.p03.png") is not written by the importer; it is made from indexed/X/NAME.png
// and the CLUT row 3 of NAME.tim.json (png/palette_variant.h), the way the engine's readers make it.
conv::Output variant(const std::vector<conv::Output>& outputs, const std::string& path) {
    SH_CHECK(find(outputs, path) == nullptr);
    auto made = png::read_texture_or_variant(path, reader_of(outputs));
    SH_CHECK(made.has_value());
    return conv::Output{path, made ? std::move(*made) : Bytes{}};
}

std::vector<std::string> names(const std::vector<conv::Output>& outputs) {
    std::vector<std::string> out;
    for (const auto& o : outputs) {
        out.push_back(o.relative_path);
    }
    return out;
}

const conv::Converter& converter() {
    static const auto instance = sh::convert::tim::make_converter();
    return *instance;
}

// decode + encode; checks the bytes come back and returns the outputs.
std::vector<conv::Output> round_trip(const Bytes& tim, const char* path) {
    SH_CHECK(converter().accepts(tim, path));
    auto decoded = converter().decode(tim, path);
    SH_CHECK(static_cast<bool>(decoded));
    if (!decoded) {
        std::fprintf(stderr, "decode(%s): %s\n", path, decoded.error().c_str());
        return {};
    }
    auto encoded = converter().encode(decoded.value());
    SH_CHECK(static_cast<bool>(encoded));
    if (encoded) {
        SH_CHECK(encoded.value() == tim);
    }
    return std::move(decoded.value());
}

Bytes json_of(const std::vector<conv::Output>& outputs, const std::string& path) {
    const conv::Output* o = find(outputs, path);
    return o ? o->bytes : Bytes{};
}

bool contains(const Bytes& text, std::string_view needle) {
    return std::search(text.begin(), text.end(), needle.begin(), needle.end()) != text.end();
}

png::Rgba8 rgba_of_png(const conv::Output* o) {
    SH_CHECK(o != nullptr);
    if (!o) {
        return {};
    }
    auto image = png::decode_rgba8(o->bytes);
    SH_CHECK(static_cast<bool>(image));
    return image ? image.value() : png::Rgba8{};
}

}  // namespace

SH_TEST(TimConverterIdentity) {
    SH_CHECK(converter().name() == "tim");
    SH_CHECK(converter().lossless());
}

SH_TEST(Tim4bppTwoPalettesRoundTripAndColours) {
    // 8x3 picture, 2 palettes. Entry 0: transparent; 1: opaque black (STP); 2: STP with a colour; 3: red; 15: white.
    std::vector<std::uint16_t> clut(32, 0);
    clut[1] = 0x8000;
    clut[2] = 0x8000 | (7 << 10) | (9 << 5) | 11;
    clut[3] = 0x001F;
    clut[15] = 0x7FFF;
    clut[16 + 0] = 0x0000;
    clut[16 + 3] = 0x03E0;  // palette 1: green at index 3
    clut[16 + 4] = 0x8421;
    const Bytes pixels = noise(2 * 2 * 3, 7);  // 2 words per row * 2 bytes * 3 rows
    const Bytes tail = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32};
    const Bytes tim = build_tim(8, {{{0, 480, 16, 2}, clut}}, {320, 0, 2, 3}, pixels, tail);

    const auto outputs = round_trip(tim, "CHARA/TEST.TIM");
    SH_CHECK((names(outputs) == std::vector<std::string>{"textures/CHARA/TEST.png", "indexed/CHARA/TEST.png",
                                                         "textures/CHARA/TEST.tim.json"}));
    // The picture under textures/ is true colour; only indexed/ keeps the indices.
    auto baked = png::decode(find(outputs, "textures/CHARA/TEST.png")->bytes);
    SH_CHECK(baked && baked.value().color_type == png::ColorType::Rgba);

    for (std::size_t k = 0; k < 3; ++k) {
        const conv::Output file = k == 0   ? *find(outputs, "textures/CHARA/TEST.png")
                                  : k == 1 ? *find(outputs, "indexed/CHARA/TEST.png")
                                           : variant(outputs, "textures/CHARA/TEST.p01.png");
        const std::size_t row = k == 2 ? 1 : 0;
        const png::Rgba8 image = rgba_of_png(&file);
        SH_CHECK(image.width == 8 && image.height == 3);
        for (std::size_t y = 0; y < 3; ++y) {
            for (std::size_t x = 0; x < 8; ++x) {
                const std::uint8_t byte = pixels[y * 4 + x / 2];
                const unsigned index = (x & 1) ? byte >> 4 : byte & 15;  // low nibble is the left pixel
                const auto want = rgba_of(clut[row * 16 + index]);
                const std::uint8_t* got = &image.pixels[(y * 8 + x) * 4];
                SH_CHECK(std::equal(want.begin(), want.end(), got));
            }
        }
    }
    // Palette 0 entry 1 is opaque black although its word is 0x8000; entry 0 is transparent.
    auto base = png::decode(find(outputs, "indexed/CHARA/TEST.png")->bytes);
    SH_CHECK(base && base.value().palette.size() == 48 && base.value().color_type == png::ColorType::Indexed);
    if (base) {
        SH_CHECK(base.value().palette_alpha.size() >= 2 && base.value().palette_alpha[0] == 0 &&
                 base.value().palette_alpha[1] == 255);
        SH_CHECK(base.value().palette[15 * 3] == 255);
    }

    const Bytes json = json_of(outputs, "textures/CHARA/TEST.tim.json");
    SH_CHECK(contains(json, "\"bpp\": 4"));
    SH_CHECK(contains(json, "\"image_vram\": {\"x\": 320, \"y\": 0, \"words\": 2}"));
    SH_CHECK(contains(json, "\"clut_vram\": {\"x\": 0, \"y\": 480, \"words\": 16, \"rows\": 2}"));
    SH_CHECK(contains(json, "\"tail_size\": 32"));
    SH_CHECK(contains(json, "\"tail_hex\": \"0102030405"));
    SH_CHECK(contains(json, "\"8000\""));
}

SH_TEST(Tim8bppRoundTrip) {
    const auto clut = word_noise(256 * 2, 3);
    const Bytes pixels = noise(8 * 2 * 5, 4);  // 8 words = 16 pixels per row, 5 rows
    const Bytes tim = build_tim(9, {{{0, 500, 256, 2}, clut}}, {512, 256, 8, 5}, pixels);
    const auto outputs = round_trip(tim, "BG/X8.TIM");
    SH_CHECK(names(outputs).size() == 3);
    const conv::Output p1 = variant(outputs, "textures/BG/X8.p01.png");
    const png::Rgba8 image = rgba_of_png(&p1);
    SH_CHECK(image.width == 16 && image.height == 5);
    for (std::size_t i = 0; i < 16 * 5; ++i) {
        const auto want = rgba_of(clut[256 + pixels[i]]);
        SH_CHECK(std::equal(want.begin(), want.end(), &image.pixels[i * 4]));
    }
}

SH_TEST(Tim4bppWideClutIsSplitIntoPalettesOf16) {
    // CLUT 32 words wide, 3 rows: 6 palettes of 16 side by side, as in four TIMs of the game.
    const auto clut = word_noise(32 * 3, 11);
    const Bytes tim = build_tim(8, {{{16, 481, 32, 3}, clut}}, {0, 0, 4, 4}, noise(4 * 2 * 4, 12));
    const auto outputs = round_trip(tim, "ITEM/WIDE.TIM");
    SH_CHECK((names(outputs) == std::vector<std::string>{"textures/ITEM/WIDE.png", "indexed/ITEM/WIDE.png",
                                                         "textures/ITEM/WIDE.tim.json"}));
    auto p3 = png::decode(variant(outputs, "textures/ITEM/WIDE.p03.png").bytes);
    SH_CHECK(p3 && p3.value().palette.size() == 48);
    if (p3) {
        // Palette 3 is words 48..63 of the flat CLUT: second row, second half.
        for (std::size_t i = 0; i < 16; ++i) {
            const auto want = rgba_of(clut[48 + i]);
            SH_CHECK(p3.value().palette[i * 3] == want[0] && p3.value().palette[i * 3 + 2] == want[2]);
        }
    }
}

SH_TEST(Tim4bppWithoutClutRoundTrip) {
    const Bytes pixels = noise(2 * 2 * 4, 21);
    const Bytes tim = build_tim(0, std::nullopt, {960, 0, 2, 4}, pixels, Bytes(224, 0xAB));
    const auto outputs = round_trip(tim, "1ST/NOCLUT.TIM");
    SH_CHECK((names(outputs) == std::vector<std::string>{"textures/1ST/NOCLUT.png", "indexed/1ST/NOCLUT.png",
                                                         "textures/1ST/NOCLUT.tim.json"}));
    const Bytes json = json_of(outputs, "textures/1ST/NOCLUT.tim.json");
    SH_CHECK(!contains(json, "palettes") && !contains(json, "clut_vram"));
    auto image = png::decode(find(outputs, "indexed/1ST/NOCLUT.png")->bytes);
    SH_CHECK(image && image.value().width == 8 && image.value().height == 4 &&
             image.value().color_type == png::ColorType::Indexed);
    // The true-colour picture is the gray ramp: index i of 16 is level i * 31 / 15.
    const png::Rgba8 picture = rgba_of_png(find(outputs, "textures/1ST/NOCLUT.png"));
    SH_CHECK(picture.width == 8 && picture.height == 4 && picture.pixels.size() == 8 * 4 * 4);
    for (std::size_t i = 0; i < 32 && picture.pixels.size() == 128; ++i) {
        const std::uint8_t byte = pixels[i / 2];
        const unsigned index = (i & 1) ? byte >> 4 : byte & 15;
        const unsigned level = index * 31 / 15;
        const auto want = rgba_of(static_cast<std::uint16_t>(0x8000 | level | (level << 5) | (level << 10)));
        SH_CHECK(std::equal(want.begin(), want.end(), &picture.pixels[i * 4]));
    }
    // The same through make_row_rgba: row 0 of a TIM without CLUT is the PNG's own ramp.
    auto row = png::make_row_rgba(find(outputs, "indexed/1ST/NOCLUT.png")->bytes, json, 0);
    SH_CHECK(row && row.value().pixels == picture.pixels);
    SH_CHECK(!png::make_row_rgba(find(outputs, "indexed/1ST/NOCLUT.png")->bytes, json, 1));
}

SH_TEST(Tim16bppAllWordsRoundTrip) {
    // Every 16-bit word once: transparent, STP with and without colour, every colour level.
    Bytes pixels;
    for (unsigned w = 0; w < 65536; ++w) {
        le16(pixels, w);
    }
    const Bytes tim = build_tim(2, std::nullopt, {512, 0, 256, 256}, pixels);
    const auto outputs = round_trip(tim, "BG/DIRECT.TIM");
    SH_CHECK((names(outputs) == std::vector<std::string>{"textures/BG/DIRECT.png", "textures/BG/DIRECT.tim.json"}));
    const png::Rgba8 image = rgba_of_png(find(outputs, "textures/BG/DIRECT.png"));
    SH_CHECK(image.width == 256 && image.height == 256);
    if (image.pixels.size() == 256 * 256 * 4) {
        bool all = true;
        for (unsigned w = 0; w < 65536; ++w) {
            const auto want = rgba_of(static_cast<std::uint16_t>(w));  // STP does not change the picture
            all = all && std::equal(want.begin(), want.end(), &image.pixels[w * 4]);
        }
        SH_CHECK(all);
    }
}

SH_TEST(Tim24bppRoundTrip) {
    // 6 words = 12 bytes = 4 pixels per row.
    const Bytes pixels = noise(6 * 2 * 3, 31);
    const Bytes tim = build_tim(3, std::nullopt, {0, 0, 6, 3}, pixels, Bytes(32, 0));
    const auto outputs = round_trip(tim, "MISC/TRUE.TIM");
    const png::Rgba8 image = rgba_of_png(find(outputs, "textures/MISC/TRUE.png"));
    SH_CHECK(image.width == 4 && image.height == 3);
    for (std::size_t i = 0; i < 12; ++i) {
        SH_CHECK(image.pixels[i * 4] == pixels[i * 3] && image.pixels[i * 4 + 1] == pixels[i * 3 + 1] &&
                 image.pixels[i * 4 + 2] == pixels[i * 3 + 2] && image.pixels[i * 4 + 3] == 255);
    }
    // All-zero tail: no hex in the JSON.
    SH_CHECK(!contains(json_of(outputs, "textures/MISC/TRUE.tim.json"), "tail_hex"));
}

SH_TEST(TimDirectTimWithClutKeepsItInTheJson) {
    const auto clut = word_noise(8, 41);
    const Bytes tim = build_tim(10, {{{0, 0, 8, 1}, clut}}, {0, 0, 2, 2}, noise(8, 42));
    const auto outputs = round_trip(tim, "MISC/ODD.TIM");
    SH_CHECK(outputs.size() == 2);  // one picture, one JSON; the CLUT has no PNG
}

SH_TEST(TimAcceptsOnlyWhatItCanRebuild) {
    const Bytes good = build_tim(8, {{{0, 0, 16, 1}, std::vector<std::uint16_t>(16, 1)}}, {0, 0, 1, 1}, Bytes(2, 0));
    SH_CHECK(converter().accepts(good, "A/B.TIM"));
    SH_CHECK(converter().accepts(good, "a/b.tim"));
    SH_CHECK(!converter().accepts(good, "A/B.BIN"));
    SH_CHECK(!converter().accepts(good, "A/B"));
    SH_CHECK(!converter().accepts(Bytes{}, "A/B.TIM"));
    SH_CHECK(!converter().decode(Bytes(7, 0x10), "A/B.TIM"));

    Bytes bad = good;
    bad[0] = 0x11;  // magic
    SH_CHECK(!converter().accepts(bad, "A/B.TIM"));
    bad = good;
    bad[4] = 0x18;  // reserved flag bit
    SH_CHECK(!converter().accepts(bad, "A/B.TIM"));
    bad = good;
    bad[4] = 0x0C;  // depth code 4
    SH_CHECK(!converter().accepts(bad, "A/B.TIM"));
    bad = good;
    bad[8] += 4;  // CLUT block length
    SH_CHECK(!converter().accepts(bad, "A/B.TIM"));
    bad = good;
    bad.resize(bad.size() - 1);  // cut in the pixel block
    SH_CHECK(!converter().accepts(bad, "A/B.TIM"));
    bad.resize(30);  // cut in the CLUT block
    SH_CHECK(!converter().accepts(bad, "A/B.TIM"));
    // CLUT of 17 words is not a whole number of 16-colour palettes.
    SH_CHECK(!converter().accepts(build_tim(8, {{{0, 0, 17, 1}, std::vector<std::uint16_t>(17, 1)}}, {0, 0, 1, 1}, Bytes(2, 0)), "A/B.TIM"));
    // Empty picture.
    SH_CHECK(!converter().accepts(build_tim(2, std::nullopt, {0, 0, 0, 4}, Bytes{}), "A/B.TIM"));
    // 24-bit row of 2 words = 4 bytes = 1.33 pixels.
    SH_CHECK(!converter().accepts(build_tim(3, std::nullopt, {0, 0, 2, 1}, Bytes(4, 0)), "A/B.TIM"));
}

SH_TEST(TimPaletteEditsComeFromThePng) {
    std::vector<std::uint16_t> clut(32, 0x7FFF);
    clut[2] = 0x8000 | 0x0421;  // STP, colour (1,1,1)
    clut[5] = 0x8000;           // opaque black
    const Bytes tim = build_tim(8, {{{0, 480, 16, 2}, clut}}, {0, 0, 1, 1}, Bytes{0x20, 0x00});
    auto outputs = round_trip(tim, "X/ED.TIM");

    auto base = png::decode(find(outputs, "indexed/X/ED.png")->bytes);
    SH_CHECK(static_cast<bool>(base));
    if (!base) {
        return;
    }
    png::Image image = base.value();
    // Entry 2 (STP): a new colour. The STP bit of the JSON word must stay.
    image.palette[2 * 3] = 255;
    image.palette[2 * 3 + 1] = 0;
    image.palette[2 * 3 + 2] = 128;
    // Entry 3: transparent. Entry 4: opaque black from a painted (0,0,0). Entry 5 (black, STP): a dark colour.
    image.palette_alpha.resize(16, 255);
    image.palette_alpha[3] = 0;
    image.palette[4 * 3] = image.palette[4 * 3 + 1] = image.palette[4 * 3 + 2] = 0;
    image.palette[5 * 3] = 3;  // rounds to level 0
    image.palette[5 * 3 + 1] = 5;  // rounds to level 1
    image.palette[5 * 3 + 2] = 0;
    // Entry 6: a value between levels.
    image.palette[6 * 3] = 100;  // (100*31+127)/255 = 12
    image.palette[6 * 3 + 1] = 254;
    image.palette[6 * 3 + 2] = 1;
    auto png_bytes = png::encode(image);
    SH_CHECK(static_cast<bool>(png_bytes));
    find(outputs, "indexed/X/ED.png")->bytes = png_bytes.value();

    // A recoloured p01 next to the pictures is a baked variant, not a palette: it must not reach the TIM.
    outputs.push_back(variant(outputs, "textures/X/ED.p01.png"));
    auto second = png::decode(find(outputs, "textures/X/ED.p01.png")->bytes);
    SH_CHECK(static_cast<bool>(second));
    if (!second) {
        return;
    }
    png::Image image1 = second.value();
    image1.palette_alpha.assign(1, 0);
    image1.palette[3] = 255;
    image1.palette[4] = 0;
    image1.palette[5] = 0;
    std::fill(image1.pixels.begin(), image1.pixels.end(), 15);
    find(outputs, "textures/X/ED.p01.png")->bytes = png::encode(image1).value();
    // Nor does the true-colour picture, which the bake rewrites: replace it with one of another size.
    png::Image other;
    other.width = 3;
    other.height = 3;
    other.color_type = png::ColorType::Rgba;
    other.pixels.assign(36, 77);
    find(outputs, "textures/X/ED.png")->bytes = png::encode(other).value();

    const auto rebuilt = converter().encode(outputs);
    SH_CHECK(static_cast<bool>(rebuilt));
    if (!rebuilt) {
        return;
    }
    const Bytes& out = rebuilt.value();
    auto word = [&](std::size_t i) { return static_cast<unsigned>(out[20 + i * 2] | (out[21 + i * 2] << 8)); };
    SH_CHECK(word(0) == 0x7FFF);
    SH_CHECK(word(2) == (0x8000u | (16u << 10) | (0u << 5) | 31u));  // 255 -> 31, 0 -> 0, 128 -> 16, STP kept
    SH_CHECK(word(3) == 0x0000);                                 // alpha 0
    SH_CHECK(word(4) == 0x8000);                                 // painted black must not become transparent
    SH_CHECK(word(5) == (0x8000u | (0u << 10) | (1u << 5) | 0u));  // the STP bit of the old word stays
    SH_CHECK(word(6) == ((31u << 5) | 12u));  // r 100 -> 12, g 254 -> 31, b 1 -> 0; the JSON word had no STP
    for (std::size_t i = 16; i < 32; ++i) {
        SH_CHECK(word(i) == clut[i]);  // palette 1: the JSON words
    }
    // The pixel block did not change.
    SH_CHECK(std::equal(out.end() - 2, out.end(), tim.end() - 2));
}

SH_TEST(TimIndexedTimRebuildsWithoutTheTrueColourPicture) {
    const auto clut = word_noise(48, 51);
    const Bytes tim = build_tim(8, {{{0, 480, 16, 3}, clut}}, {0, 0, 2, 2}, noise(8, 52));
    auto outputs = round_trip(tim, "X/MP.TIM");
    outputs.erase(std::remove_if(outputs.begin(), outputs.end(),
                                 [](const conv::Output& o) { return o.relative_path == "textures/X/MP.png"; }),
                  outputs.end());
    SH_CHECK(outputs.size() == 2);
    const auto rebuilt = converter().encode(outputs);
    SH_CHECK(rebuilt && rebuilt.value() == tim);

    // The outputs may sit under a common prefix: the indexed file is found next to the JSON's textures/ directory.
    for (auto& o : outputs) {
        o.relative_path = "mods/m/convert/" + o.relative_path;
    }
    const auto prefixed = converter().encode(outputs);
    SH_CHECK(prefixed && prefixed.value() == tim);
}

SH_TEST(TimShortPlteKeepsTheJsonColours) {
    const auto clut = word_noise(16, 61);
    const Bytes tim = build_tim(8, {{{0, 480, 16, 1}, clut}}, {0, 0, 1, 1}, Bytes{0x01, 0x00});
    auto outputs = round_trip(tim, "X/SP.TIM");
    // An editor that saves only the colours in use: 2 entries in PLTE.
    png::Image image;
    image.width = 4;
    image.height = 1;
    image.color_type = png::ColorType::Indexed;
    image.pixels = {1, 0, 0, 0};
    image.palette = {10, 20, 30, 40, 50, 60};
    find(outputs, "indexed/X/SP.png")->bytes = png::encode(image).value();
    const auto rebuilt = converter().encode(outputs);
    SH_CHECK(static_cast<bool>(rebuilt));
    if (rebuilt) {
        const Bytes& out = rebuilt.value();
        auto word = [&](std::size_t i) { return static_cast<unsigned>(out[20 + i * 2] | (out[21 + i * 2] << 8)); };
        auto level = [](unsigned v) { return (v * 31 + 127) / 255; };
        SH_CHECK(word(0) == ((clut[0] & 0x8000u) | level(10) | (level(20) << 5) | (level(30) << 10)));
        SH_CHECK(word(2) == clut[2] && word(15) == clut[15]);  // past the PLTE
        SH_CHECK(out[out.size() - 2] == 0x01);                 // the index of the first pixel
    }
}

SH_TEST(TimQuantisationInvertsTheExpansion) {
    // 8bpp, one palette of 256 gray levels 0..255. Opaque. The words must follow the rounding rule, and
    // the 32 expanded levels must map back to themselves.
    std::vector<std::uint16_t> clut(256, 0x0001);
    const Bytes tim = build_tim(9, {{{0, 0, 256, 1}, clut}}, {0, 0, 1, 1}, Bytes{0, 0});
    auto outputs = round_trip(tim, "X/Q.TIM");
    png::Image image;
    image.width = 2;
    image.height = 1;
    image.color_type = png::ColorType::Indexed;
    image.pixels = {0, 255};
    for (unsigned v = 0; v < 256; ++v) {
        image.palette.insert(image.palette.end(), {static_cast<std::uint8_t>(v), static_cast<std::uint8_t>(v), static_cast<std::uint8_t>(255 - v)});
    }
    find(outputs, "indexed/X/Q.png")->bytes = png::encode(image).value();
    const auto rebuilt = converter().encode(outputs);
    SH_CHECK(static_cast<bool>(rebuilt));
    if (!rebuilt) {
        return;
    }
    const Bytes& out = rebuilt.value();
    unsigned previous = 0;
    for (unsigned v = 0; v < 256; ++v) {
        const unsigned word = out[20 + v * 2] | (out[21 + v * 2] << 8);
        const unsigned r = word & 31;
        const unsigned b = (word >> 10) & 31;
        SH_CHECK(r == (v * 31 + 127) / 255);
        SH_CHECK(b == ((255 - v) * 31 + 127) / 255);
        SH_CHECK(r >= previous);  // monotone
        previous = r;
        const unsigned back = (r << 3) | (r >> 2);
        SH_CHECK((back > v ? back - v : v - back) <= 5);  // never further than half a step
    }
    for (unsigned level = 0; level < 32; ++level) {
        const unsigned v = (level << 3) | (level >> 2);
        SH_CHECK((v * 31 + 127) / 255 == level);
    }
}

SH_TEST(TimEncodeRejectsBrokenOutputs) {
    const auto clut = word_noise(16, 71);
    const Bytes tim = build_tim(8, {{{0, 480, 16, 1}, clut}}, {0, 0, 2, 2}, noise(8, 72));
    const auto good = round_trip(tim, "X/R.TIM");
    SH_CHECK(good.size() == 3);

    // No JSON, no indexed PNG, a JSON outside textures/, a PNG of the wrong size, a PNG that is not indexed, an
    // index above 15.
    SH_CHECK(!converter().encode(std::vector<conv::Output>{}));
    auto outputs = good;
    outputs.erase(outputs.begin() + 1);
    SH_CHECK(!converter().encode(outputs));
    outputs = good;
    outputs.pop_back();
    SH_CHECK(!converter().encode(outputs));
    outputs = good;
    outputs.back().relative_path = "pictures/X/R.tim.json";
    SH_CHECK(!converter().encode(outputs));

    outputs = good;
    png::Image image;
    image.width = 8;
    image.height = 3;  // the TIM is 8x2
    image.color_type = png::ColorType::Indexed;
    image.pixels.assign(24, 0);
    image.palette = {0, 0, 0};
    find(outputs, "indexed/X/R.png")->bytes = png::encode(image).value();
    SH_CHECK(!converter().encode(outputs));

    outputs = good;
    png::Image rgb;
    rgb.width = 8;
    rgb.height = 2;
    rgb.color_type = png::ColorType::Rgb;
    rgb.pixels.assign(48, 9);
    find(outputs, "indexed/X/R.png")->bytes = png::encode(rgb).value();
    const auto not_indexed = converter().encode(outputs);
    SH_CHECK(!not_indexed && not_indexed.error().find("indexed") != std::string::npos);

    outputs = good;
    image.height = 2;
    image.pixels.assign(16, 0);
    image.pixels[5] = 16;
    image.palette.assign(17 * 3, 0);
    find(outputs, "indexed/X/R.png")->bytes = png::encode(image).value();
    SH_CHECK(!converter().encode(outputs));  // index 16 in a 4-bit TIM (and 17 PLTE entries)

    // A broken JSON.
    outputs = good;
    find(outputs, "textures/X/R.tim.json")->bytes = Bytes{'{', '"', 'a'};
    SH_CHECK(!converter().encode(outputs));
    outputs = good;
    Bytes& json = find(outputs, "textures/X/R.tim.json")->bytes;
    const std::string text(json.begin(), json.end());
    const std::size_t at = text.find("\"width\": 8");
    SH_CHECK(at != std::string::npos);
    if (at != std::string::npos) {
        std::string changed = text;
        changed.replace(at, 10, "\"width\": 9");
        json.assign(changed.begin(), changed.end());
        SH_CHECK(!converter().encode(outputs));
    }
}

// ---- the real image ----------------------------------------------------------------------------------------------

namespace {

namespace fs = std::filesystem;

struct Disc {
    std::shared_ptr<const sh::disc::Image> image;
    std::shared_ptr<const sh::ByteSource> silent;
    std::vector<sh::archive::Entry> table;
};

std::optional<Disc> open_disc() {
    const fs::path path = fs::path(SH_PROJECT_ROOT) / "data" / "Silent Hill (v1.1).bin";
    if (!fs::exists(path)) {
        SH_SKIP("data/Silent Hill (v1.1).bin is not present");
    }
    const std::shared_ptr<const sh::disc::Image> image(sh::disc::Image::open(path));
    if (!image) {
        return std::nullopt;
    }
    const auto volume = sh::iso9660::Volume::open(image);
    if (!volume) {
        return std::nullopt;
    }
    const auto slus = volume->find("SLUS_007.07");
    const auto silent = volume->find("SILENT.");
    const auto hill = volume->find("HILL.");
    if (!slus || !silent || !hill) {
        return std::nullopt;
    }
    auto sectors = [](std::uint32_t size) { return (size + 2047) / 2048; };
    const std::shared_ptr<const sh::ByteSource> exe(volume->open_file(*slus));
    Disc disc;
    disc.image = image;
    disc.silent = std::shared_ptr<const sh::ByteSource>(volume->open_file(*silent));
    if (!exe || !disc.silent) {
        return std::nullopt;
    }
    auto table = sh::archive::read_file_table(*exe, {silent->lba, sectors(silent->size)}, {hill->lba, sectors(hill->size)});
    if (!table) {
        return std::nullopt;
    }
    disc.table = std::move(*table);
    return disc;
}

struct RefIndexed {
    unsigned width = 0;
    unsigned height = 0;
    Bytes indices;
    std::vector<std::uint16_t> words;  // palette k; the importer's gray ramp (STP set) without a CLUT
};

// The decode of a 4/8-bit TIM, with the CLUT row and column offset of palette k.
RefIndexed reference_indexed(const Bytes& d, std::size_t palette_index) {
    auto u16 = [&](std::size_t o) { return static_cast<unsigned>(d[o] | (d[o + 1] << 8)); };
    auto u32 = [&](std::size_t o) { return u16(o) | (u16(o + 2) << 16); };
    const unsigned bpp = u32(4) & 7;
    std::size_t p = 8;
    std::size_t clut_off = 0;
    unsigned clut_w = 0;
    const bool has_clut = (u32(4) & 8) != 0;
    if (has_clut) {
        clut_w = u16(p + 8);
        clut_off = p + 12;
        p += u32(p);
    }
    const unsigned cell_w = u16(p + 8);
    RefIndexed ref;
    ref.height = u16(p + 10);
    ref.width = bpp == 0 ? cell_w * 4 : cell_w * 2;
    const unsigned pal_size = bpp == 0 ? 16 : 256;
    for (unsigned i = 0; i < pal_size; ++i) {
        if (has_clut) {
            const unsigned per_row = clut_w / pal_size;
            const std::size_t row_base =
                clut_off + ((palette_index / per_row) * clut_w + (palette_index % per_row) * pal_size) * 2;
            ref.words.push_back(static_cast<std::uint16_t>(u16(row_base + i * 2)));
        } else {
            const unsigned level = i * 31 / (pal_size - 1);
            ref.words.push_back(static_cast<std::uint16_t>(0x8000 | level | (level << 5) | (level << 10)));
        }
    }
    for (unsigned y = 0; y < ref.height; ++y) {
        const std::size_t row = p + 12 + std::size_t{y} * cell_w * 2;
        for (unsigned x = 0; x < ref.width; ++x) {
            if (bpp == 0) {
                const unsigned b = d[row + x / 2];
                ref.indices.push_back(static_cast<std::uint8_t>((x & 1) ? b >> 4 : b & 15));
            } else {
                ref.indices.push_back(d[row + x]);
            }
        }
    }
    return ref;
}

std::vector<std::uint8_t> reference_rgba(const Bytes& d, std::size_t palette_index) {
    const RefIndexed ref = reference_indexed(d, palette_index);
    std::vector<std::uint8_t> out;
    out.reserve(ref.indices.size() * 4);
    for (const std::uint8_t idx : ref.indices) {
        const auto px = rgba_of(ref.words[idx]);
        out.insert(out.end(), px.begin(), px.end());
    }
    return out;
}

// What importer 2 wrote as textures/X/NAME.png: the indices with PLTE/tRNS of row 0, through the PNG writer.
Bytes importer2_png(const Bytes& d) {
    const RefIndexed ref = reference_indexed(d, 0);
    png::Image image;
    image.width = ref.width;
    image.height = ref.height;
    image.color_type = png::ColorType::Indexed;
    image.pixels = ref.indices;
    png::Palette palette;
    for (const std::uint16_t w : ref.words) {
        const auto c = rgba_of(w);
        palette.rgb.insert(palette.rgb.end(), c.begin(), c.begin() + 3);
        palette.alpha.push_back(c[3]);
    }
    auto files = png::encode_palettes(image, std::span<const png::Palette>(&palette, 1));
    SH_CHECK(files && files.value().size() == 1);
    return files ? files.value()[0] : Bytes{};
}

}  // namespace

SH_TEST(TimEveryTimOfTheImageRoundTripsByteForByte) {
    const auto disc = open_disc();
    SH_CHECK(disc.has_value());
    if (!disc) {
        return;
    }
    const auto archive = sh::archive::Archive::open(disc->silent, sh::archive::Region::Silent);
    SH_CHECK(archive.has_value());
    if (!archive) {
        return;
    }

    std::size_t total = 0;
    std::size_t exact = 0;
    std::size_t png_files = 0;
    std::size_t colours_checked = 0;
    std::map<std::string, std::size_t> by_kind;
    std::map<std::size_t, std::size_t> by_tail;
    std::size_t tails_nonzero = 0;
    std::size_t multi_palette = 0;
    std::size_t indexed_tims = 0;
    std::size_t indexed_same = 0;
    std::size_t rgba_row0 = 0;
    for (const auto& entry : disc->table) {
        if (entry.name.size() < 4 || entry.name.substr(entry.name.size() - 4) != ".TIM") {
            continue;
        }
        ++total;
        const auto raw = archive->read(entry);
        SH_CHECK(raw.has_value());
        if (!raw) {
            continue;
        }
        if (!converter().accepts(*raw, entry.name)) {
            std::fprintf(stderr, "not accepted: %s\n", entry.name.c_str());
            SH_CHECK(false);
            continue;
        }
        const auto decoded = converter().decode(*raw, entry.name);
        SH_CHECK(static_cast<bool>(decoded));
        if (!decoded) {
            std::fprintf(stderr, "decode(%s): %s\n", entry.name.c_str(), decoded.error().c_str());
            continue;
        }
        const auto encoded = converter().encode(decoded.value());
        SH_CHECK(static_cast<bool>(encoded));
        if (!encoded || encoded.value() != *raw) {
            std::fprintf(stderr, "round trip differs: %s\n", entry.name.c_str());
            SH_CHECK(false);
            continue;
        }
        ++exact;

        // Kind and tail statistics, from the header of the raw file.
        const std::uint32_t flags = (*raw)[4] | ((*raw)[5] << 8);
        const char* kind = (flags & 7) == 0 ? ((flags & 8) ? "4bpp+CLUT" : "4bpp, no CLUT")
                           : (flags & 7) == 1 ? "8bpp+CLUT" : (flags & 7) == 2 ? "16bpp" : "24bpp";
        ++by_kind[kind];
        std::size_t pos = 8;
        if (flags & 8) {
            pos += (*raw)[8] | ((*raw)[9] << 8) | ((*raw)[10] << 16);
        }
        pos += (*raw)[pos] | ((*raw)[pos + 1] << 8) | ((*raw)[pos + 2] << 16);
        const std::size_t tail = raw->size() - pos;
        ++by_tail[tail];
        if (std::any_of(raw->begin() + static_cast<std::ptrdiff_t>(pos), raw->end(), [](std::uint8_t b) { return b != 0; })) {
            ++tails_nonzero;
        }

        // Every PNG is valid for an independent reader and shows the colours of the CLUT.
        const std::string stem = entry.name.substr(0, entry.name.size() - 4);
        const std::string base = "textures/" + stem;
        std::size_t palettes = 0;
        for (const auto& out : decoded.value()) {
            if (out.relative_path.size() > 4 && out.relative_path.substr(out.relative_path.size() - 4) == ".png") {
                ++png_files;
            }
        }
        if ((flags & 7) < 2) {
            ++indexed_tims;
            // indexed/: byte for byte the file importer 2 wrote as textures/, made here from the raw bytes.
            const conv::Output* indexed = find(decoded.value(), "indexed/" + stem + ".png");
            SH_CHECK(indexed != nullptr);
            const Bytes want = importer2_png(*raw);
            if (!indexed || indexed->bytes != want) {
                std::fprintf(stderr, "indexed PNG differs from importer 2: %s\n", entry.name.c_str());
                SH_CHECK(false);
            } else {
                ++indexed_same;
            }
            // textures/: RGBA of row 0 (the gray ramp without a CLUT).
            const conv::Output* picture = find(decoded.value(), base + ".png");
            auto header = picture ? png::decode(picture->bytes) : sh::Result<png::Image>::fail("missing");
            SH_CHECK(header && header.value().color_type == png::ColorType::Rgba);
            const auto rgba = picture ? png::decode_rgba8(picture->bytes) : sh::Result<png::Rgba8>::fail("missing");
            SH_CHECK(rgba && rgba.value().pixels == reference_rgba(*raw, 0));
            if (header && header.value().color_type == png::ColorType::Rgba && rgba &&
                rgba.value().pixels == reference_rgba(*raw, 0)) {
                ++rgba_row0;
            }
        }
        // The rows of the CLUT: only palette 0 is a file, the others are variants made from the JSON.
        if (flags & 8) {
            const std::size_t clut_block = (*raw)[8] | ((*raw)[9] << 8) | ((*raw)[10] << 16);
            palettes = std::max<std::size_t>(1, (clut_block - 12) / 2 / ((flags & 7) == 0 ? 16 : 256));
        } else {
            palettes = 1;
        }
        if (palettes > 1) {
            ++multi_palette;
        }
        const conv::Output* indexed = find(decoded.value(), "indexed/" + stem + ".png");
        const conv::Output* json = find(decoded.value(), base + ".tim.json");
        for (std::size_t k = 1; k < palettes && (flags & 8) && (flags & 7) < 2 && indexed && json; ++k) {
            const std::string path = base + ".p" + (k < 10 ? "0" : "") + std::to_string(k) + ".png";
            const conv::Output made = variant(decoded.value(), path);
            const auto image = png::decode_rgba8(made.bytes);
            SH_CHECK(static_cast<bool>(image));
            if (!image) {
                continue;
            }
            const auto want = reference_rgba(*raw, k);
            SH_CHECK(image.value().pixels == want);
            const auto row = png::make_row_rgba(indexed->bytes, json->bytes, static_cast<int>(k));
            SH_CHECK(row && row.value().pixels == want);
            ++colours_checked;
            if (k == 1) {
                // Importer-2 layout: no indexed/, textures/NAME.png is the indexed picture itself.
                const std::vector<conv::Output> legacy = {{base + ".png", indexed->bytes}, *json};
                const auto old = png::read_texture_or_variant(path, reader_of(legacy));
                SH_CHECK(old && *old == made.bytes);
            }
        }
    }

    std::printf("TIM files in the image: %zu, rebuilt byte for byte: %zu, PNG files: %zu, multi-palette TIMs: %zu\n",
                total, exact, png_files, multi_palette);
    std::printf("  4/8-bit TIMs: indexed PNG equal to importer 2: %zu, RGBA row-0 picture correct: %zu\n",
                indexed_same, rgba_row0);
    for (const auto& [kind, count] : by_kind) {
        std::printf("  %-14s %zu\n", kind.c_str(), count);
    }
    std::printf("  tails after the pixel block: %zu files non-zero; sizes:", tails_nonzero);
    for (const auto& [size, count] : by_tail) {
        std::printf(" %zu:%zu", size, count);
    }
    std::printf("\n  palette PNGs compared with the colour rules: %zu\n", colours_checked);
    SH_CHECK(total == 996);
    SH_CHECK(exact == total);
    SH_CHECK(indexed_tims > 0 && indexed_same == indexed_tims && rgba_row0 == indexed_tims);
}
