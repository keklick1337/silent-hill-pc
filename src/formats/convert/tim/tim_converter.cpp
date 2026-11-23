#include "formats/convert/tim/tim_converter.h"

#include "formats/common/json.h"
#include "formats/convert/tim/tim_file.h"
#include "formats/png/palette_variant.h"
#include "formats/png/sh_png.h"

#include <algorithm>
#include <cstdio>
#include <optional>
#include <string>
#include <utility>

namespace sh::convert::tim {
namespace {

using Json = sh::json::Value;

constexpr std::string_view kJsonSuffix = ".tim.json";
constexpr std::int64_t kJsonVersion = 1;
constexpr std::string_view kJsonFormat = "sh-tim";

template <typename T>
Result<T> fail(std::string message) {
    return Result<T>::fail("tim: " + std::move(message));
}

bool ends_with_tim(std::string_view path) {
    if (path.size() < 4) {
        return false;
    }
    const std::string_view ext = path.substr(path.size() - 4);
    return ext[0] == '.' && (ext[1] | 0x20) == 't' && (ext[2] | 0x20) == 'i' && (ext[3] | 0x20) == 'm';
}

bool ends_with(std::string_view text, std::string_view suffix) {
    return text.size() >= suffix.size() && text.substr(text.size() - suffix.size()) == suffix;
}

// 5-bit level <-> 8-bit channel. quantize5 is the inverse of expand5 on all 32 levels.
std::uint8_t expand5(unsigned v) {
    return static_cast<std::uint8_t>((v << 3) | (v >> 2));
}

unsigned quantize5(unsigned v) {
    return (v * 31 + 127) / 255;
}

unsigned color15(std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    return quantize5(r) | (quantize5(g) << 5) | (quantize5(b) << 10);
}

// STP is kept from `previous`. A colour that quantises to black must carry STP, or the word would be 0x0000.
std::uint16_t make_word(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a, bool stp) {
    if (a == 0) {
        return 0;
    }
    const unsigned c = color15(r, g, b);
    return static_cast<std::uint16_t>(c | ((stp || c == 0) ? 0x8000u : 0u));
}

bool is_indexed(Depth depth) {
    return depth == Depth::Bpp4 || depth == Depth::Bpp8;
}

// Words in one palette: a row of 16 or 256 for the indexed depths, else the CLUT row.
std::size_t palette_size(const TimFile& tim) {
    switch (tim.depth) {
        case Depth::Bpp4: return 16;
        case Depth::Bpp8: return 256;
        default: return tim.clut.width;
    }
}

std::size_t palette_count(const TimFile& tim) {
    if (!tim.has_clut) {
        return 0;
    }
    return tim.clut.words.size() / palette_size(tim);
}

// Empty when the converter can take the file.
std::string unsupported(const TimFile& tim) {
    if (pixel_width(tim.depth, tim.image_words) == 0) {
        return "a 24-bit picture whose stored width is not a multiple of 3 words";
    }
    if (tim.has_clut && tim.clut.words.size() % palette_size(tim) != 0) {
        return "the CLUT is not a whole number of palettes";
    }
    return {};
}

std::string_view stem_of(std::string_view game_path) {
    return game_path.substr(0, game_path.size() - 4);
}

std::string indexed_png_path(std::string_view game_path) {
    return "indexed/" + std::string(stem_of(game_path)) + ".png";
}

// ---- decode ---------------------------------------------------------------------------------------------------

png::Palette make_palette(const std::uint16_t* words, std::size_t size) {
    png::Palette palette;
    palette.rgb.reserve(size * 3);
    palette.alpha.reserve(size);
    for (std::size_t i = 0; i < size; ++i) {
        const std::uint16_t w = words[i];
        palette.rgb.push_back(expand5(w & 31));
        palette.rgb.push_back(expand5((w >> 5) & 31));
        palette.rgb.push_back(expand5((w >> 10) & 31));
        palette.alpha.push_back(w == 0 ? 0 : 255);
    }
    return palette;
}

std::vector<std::uint8_t> unpack_indices(const TimFile& tim) {
    const std::size_t width = pixel_width(tim.depth, tim.image_words);
    const std::size_t stride = std::size_t{tim.image_words} * 2;
    std::vector<std::uint8_t> out(width * tim.image_height);
    for (std::size_t y = 0; y < tim.image_height; ++y) {
        const std::uint8_t* row = tim.data.data() + y * stride;
        std::uint8_t* dst = out.data() + y * width;
        if (tim.depth == Depth::Bpp8) {
            std::copy(row, row + width, dst);
        } else {
            for (std::size_t i = 0; i < stride; ++i) {
                dst[i * 2] = row[i] & 15;  // the low nibble is the left pixel
                dst[i * 2 + 1] = row[i] >> 4;
            }
        }
    }
    return out;
}

const char kHex[] = "0123456789abcdef";

void append_hex(std::string& out, const std::uint8_t* data, std::size_t size) {
    for (std::size_t i = 0; i < size; ++i) {
        out += kHex[data[i] >> 4];
        out += kHex[data[i] & 15];
    }
}

void append_word(std::string& out, std::uint16_t w) {
    out += kHex[(w >> 12) & 15];
    out += kHex[(w >> 8) & 15];
    out += kHex[(w >> 4) & 15];
    out += kHex[w & 15];
}

std::string quote(std::string_view text) {
    std::string out = "\"";
    for (const char c : text) {
        if (c == '"' || c == '\\') {
            out += '\\';
            out += c;
        } else if (static_cast<unsigned char>(c) < 0x20) {
            char buf[8];
            std::snprintf(buf, sizeof buf, "\\u%04x", static_cast<unsigned>(c));
            out += buf;
        } else {
            out += c;
        }
    }
    return out + '"';
}

// Runs of 16-bit pixels with STP set, as [first pixel, count] over y * width + x.
std::vector<std::pair<std::size_t, std::size_t>> stp_runs(const TimFile& tim) {
    std::vector<std::pair<std::size_t, std::size_t>> runs;
    const std::size_t pixels = tim.data.size() / 2;
    for (std::size_t i = 0; i < pixels; ++i) {
        if ((tim.data[i * 2 + 1] & 0x80) == 0) {
            continue;
        }
        if (!runs.empty() && runs.back().first + runs.back().second == i) {
            ++runs.back().second;
        } else {
            runs.emplace_back(i, 1);
        }
    }
    return runs;
}

std::string make_json(const TimFile& tim, std::string_view game_path) {
    const std::size_t width = pixel_width(tim.depth, tim.image_words);
    std::string j = "{\n";
    j += "  \"format\": \"" + std::string(kJsonFormat) + "\",\n";
    j += "  \"version\": " + std::to_string(kJsonVersion) + ",\n";
    j += "  \"game_path\": " + quote(game_path) + ",\n";
    j += "  \"bpp\": " + std::to_string(bits_per_pixel(tim.depth)) + ",\n";
    j += "  \"width\": " + std::to_string(width) + ",\n";
    j += "  \"height\": " + std::to_string(tim.image_height) + ",\n";
    j += "  \"image_vram\": {\"x\": " + std::to_string(tim.image_x) + ", \"y\": " + std::to_string(tim.image_y) +
         ", \"words\": " + std::to_string(tim.image_words) + "},\n";
    if (tim.has_clut) {
        const std::size_t size = palette_size(tim);
        j += "  \"clut_vram\": {\"x\": " + std::to_string(tim.clut.x) + ", \"y\": " + std::to_string(tim.clut.y) +
             ", \"words\": " + std::to_string(tim.clut.width) + ", \"rows\": " + std::to_string(tim.clut.height) + "},\n";
        j += "  \"palette_size\": " + std::to_string(size) + ",\n";
        j += "  \"palettes\": [\n";
        const std::size_t count = palette_count(tim);
        for (std::size_t k = 0; k < count; ++k) {
            j += "    [";
            for (std::size_t i = 0; i < size; ++i) {
                j += i ? ", \"" : "\"";
                append_word(j, tim.clut.words[k * size + i]);
                j += '"';
            }
            j += k + 1 < count ? "],\n" : "]\n";
        }
        j += "  ],\n";
    }
    if (tim.depth == Depth::Bpp16) {
        const auto runs = stp_runs(tim);
        if (!runs.empty()) {
            j += "  \"stp_runs\": [";
            for (std::size_t i = 0; i < runs.size(); ++i) {
                j += (i ? ", [" : "[") + std::to_string(runs[i].first) + ", " + std::to_string(runs[i].second) + "]";
            }
            j += "],\n";
        }
    }
    j += "  \"tail_size\": " + std::to_string(tim.tail.size());
    if (std::any_of(tim.tail.begin(), tim.tail.end(), [](std::uint8_t b) { return b != 0; })) {
        j += ",\n  \"tail_hex\": \"";
        append_hex(j, tim.tail.data(), tim.tail.size());
        j += '"';
    }
    j += "\n}\n";
    return j;
}

Output png_output(std::string path, const png::Image& image, std::string& error) {
    auto encoded = png::encode(image);
    if (!encoded) {
        error = encoded.error();
        return {};
    }
    return {std::move(path), std::move(encoded.value())};
}

// ---- encode ---------------------------------------------------------------------------------------------------

struct Sidecar {
    Depth depth = Depth::Bpp4;
    std::size_t width = 0;
    std::size_t height = 0;
    std::uint16_t image_x = 0;
    std::uint16_t image_y = 0;
    std::uint16_t image_words = 0;
    bool has_clut = false;
    Clut clut;  // the rectangle only; `palettes` carries the words
    std::size_t palette_size = 0;
    std::vector<std::vector<std::uint16_t>> palettes;
    std::vector<std::pair<std::size_t, std::size_t>> stp_runs;
    std::size_t tail_size = 0;
    std::vector<std::uint8_t> tail;
};

const Json* member(const Json& object, const char* key) {
    return object.find(key);
}

bool read_int(const Json& object, const char* key, std::int64_t lo, std::int64_t hi, std::int64_t& out,
              std::string& error) {
    const Json* v = member(object, key);
    if (!v || !v->is_integer() || v->integer() < lo || v->integer() > hi) {
        error = std::string("the JSON value \"") + key + "\" is missing or out of range";
        return false;
    }
    out = v->integer();
    return true;
}

bool read_rect(const Json& object, const char* key, const char* count_name, std::int64_t count_lo,
               std::uint16_t& x, std::uint16_t& y, std::int64_t& count, std::string& error) {
    const Json* rect = member(object, key);
    if (!rect || !rect->is_object()) {
        error = std::string("the JSON object \"") + key + "\" is missing";
        return false;
    }
    std::int64_t vx = 0;
    std::int64_t vy = 0;
    if (!read_int(*rect, "x", 0, 65535, vx, error) || !read_int(*rect, "y", 0, 65535, vy, error) ||
        !read_int(*rect, count_name, count_lo, 65535, count, error)) {
        return false;
    }
    x = static_cast<std::uint16_t>(vx);
    y = static_cast<std::uint16_t>(vy);
    return true;
}

bool hex_value(char c, unsigned& v) {
    if (c >= '0' && c <= '9') {
        v = static_cast<unsigned>(c - '0');
    } else if (c >= 'a' && c <= 'f') {
        v = static_cast<unsigned>(c - 'a' + 10);
    } else if (c >= 'A' && c <= 'F') {
        v = static_cast<unsigned>(c - 'A' + 10);
    } else {
        return false;
    }
    return true;
}

Result<Sidecar> read_sidecar(const std::vector<std::uint8_t>& bytes) {
    std::string error;
    auto parsed = sh::json::parse(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
    if (!parsed) {
        return fail<Sidecar>(parsed.error());
    }
    const Json& root = parsed.value();
    if (!root.is_object()) {
        return fail<Sidecar>("the JSON root is not an object");
    }
    const Json* format = member(root, "format");
    std::int64_t version = 0;
    if (!format || !format->is_string() || format->string() != kJsonFormat ||
        !read_int(root, "version", 0, 1000, version, error) || version != kJsonVersion) {
        return fail<Sidecar>(error.empty() ? "the JSON is not an sh-tim version 1 file" : error);
    }

    Sidecar s;
    std::int64_t v = 0;
    if (!read_int(root, "bpp", 0, 24, v, error)) {
        return fail<Sidecar>(error);
    }
    switch (v) {
        case 4: s.depth = Depth::Bpp4; break;
        case 8: s.depth = Depth::Bpp8; break;
        case 16: s.depth = Depth::Bpp16; break;
        case 24: s.depth = Depth::Bpp24; break;
        default: return fail<Sidecar>("\"bpp\" is not 4, 8, 16 or 24");
    }
    std::int64_t words = 0;
    if (!read_int(root, "width", 1, 1 << 20, v, error)) {
        return fail<Sidecar>(error);
    }
    s.width = static_cast<std::size_t>(v);
    if (!read_int(root, "height", 1, 65535, v, error)) {
        return fail<Sidecar>(error);
    }
    s.height = static_cast<std::size_t>(v);
    if (!read_rect(root, "image_vram", "words", 1, s.image_x, s.image_y, words, error)) {
        return fail<Sidecar>(error);
    }
    s.image_words = static_cast<std::uint16_t>(words);

    if (member(root, "clut_vram")) {
        s.has_clut = true;
        std::int64_t rows = 0;
        if (!read_rect(root, "clut_vram", "words", 1, s.clut.x, s.clut.y, words, error) ||
            !read_int(*member(root, "clut_vram"), "rows", 1, 65535, rows, error)) {
            return fail<Sidecar>(error);
        }
        s.clut.width = static_cast<std::uint16_t>(words);
        s.clut.height = static_cast<std::uint16_t>(rows);
        std::int64_t size = 0;
        if (!read_int(root, "palette_size", 1, 65535 * 65535LL, size, error)) {
            return fail<Sidecar>(error);
        }
        s.palette_size = static_cast<std::size_t>(size);
        const Json* palettes = member(root, "palettes");
        if (!palettes || !palettes->is_array()) {
            return fail<Sidecar>("the JSON array \"palettes\" is missing");
        }
        for (const Json& palette : palettes->array()) {
            if (!palette.is_array() || palette.array().size() != s.palette_size) {
                return fail<Sidecar>("a palette in the JSON does not have \"palette_size\" entries");
            }
            std::vector<std::uint16_t> list;
            list.reserve(s.palette_size);
            for (const Json& entry : palette.array()) {
                unsigned a = 0;
                if (!entry.is_string() || entry.string().size() != 4) {
                    return fail<Sidecar>("a palette entry in the JSON is not a 4-digit hex string");
                }
                unsigned word = 0;
                for (char c : entry.string()) {
                    if (!hex_value(c, a)) {
                        return fail<Sidecar>("a palette entry in the JSON is not a 4-digit hex string");
                    }
                    word = word * 16 + a;
                }
                list.push_back(static_cast<std::uint16_t>(word));
            }
            s.palettes.push_back(std::move(list));
        }
        if (s.palettes.size() * s.palette_size != std::size_t{s.clut.width} * s.clut.height) {
            return fail<Sidecar>("the palettes in the JSON do not fill the CLUT rectangle");
        }
    }

    if (const Json* runs = member(root, "stp_runs")) {
        if (!runs->is_array()) {
            return fail<Sidecar>("\"stp_runs\" is not an array");
        }
        for (const Json& run : runs->array()) {
            if (!run.is_array() || run.array().size() != 2 || !run.array()[0].is_integer() ||
                !run.array()[1].is_integer() || run.array()[0].integer() < 0 || run.array()[1].integer() < 1) {
                return fail<Sidecar>("a run in \"stp_runs\" is not [first, count]");
            }
            s.stp_runs.emplace_back(static_cast<std::size_t>(run.array()[0].integer()),
                                    static_cast<std::size_t>(run.array()[1].integer()));
        }
    }

    if (!read_int(root, "tail_size", 0, 1 << 20, v, error)) {
        return fail<Sidecar>(error);
    }
    s.tail_size = static_cast<std::size_t>(v);
    s.tail.assign(s.tail_size, 0);
    if (const Json* hex = member(root, "tail_hex")) {
        if (!hex->is_string() || hex->string().size() != s.tail_size * 2) {
            return fail<Sidecar>("\"tail_hex\" does not hold \"tail_size\" bytes");
        }
        for (std::size_t i = 0; i < s.tail_size; ++i) {
            unsigned hi = 0;
            unsigned lo = 0;
            if (!hex_value(hex->string()[i * 2], hi) || !hex_value(hex->string()[i * 2 + 1], lo)) {
                return fail<Sidecar>("\"tail_hex\" is not hexadecimal");
            }
            s.tail[i] = static_cast<std::uint8_t>(hi * 16 + lo);
        }
    }
    return Result<Sidecar>::ok(std::move(s));
}

const Output* find_output(std::span<const Output> outputs, const std::string& path) {
    for (const Output& o : outputs) {
        if (o.relative_path == path) {
            return &o;
        }
    }
    return nullptr;
}

// Applies the PLTE/tRNS of an indexed PNG over the palette taken from the JSON (kept for STP and for missing entries).
Result<void> apply_palette(const png::Image& image, const std::string& name, std::vector<std::uint16_t>& words) {
    const std::size_t entries = image.palette.size() / 3;
    if (image.color_type != png::ColorType::Indexed) {
        return Result<void>::fail("tim: " + name + " is not an indexed PNG; save it with a palette (indexed colour)");
    }
    if (entries > words.size()) {
        return Result<void>::fail("tim: " + name + " has " + std::to_string(entries) + " palette entries, the TIM palette holds " +
                                  std::to_string(words.size()));
    }
    for (std::size_t i = 0; i < entries; ++i) {
        const std::uint8_t alpha = i < image.palette_alpha.size() ? image.palette_alpha[i] : 255;
        const bool stp = (words[i] & 0x8000) != 0;
        words[i] = make_word(image.palette[i * 3], image.palette[i * 3 + 1], image.palette[i * 3 + 2], alpha, stp);
    }
    return Result<void>::ok();
}

Result<png::Image> read_indexed(const Output& file) {
    auto image = png::decode(file.bytes);
    if (!image) {
        return fail<png::Image>(file.relative_path + ": " + image.error());
    }
    return image;
}

class TimConverter final : public Converter {
public:
    std::string_view name() const override { return "tim"; }

    bool lossless() const override { return true; }

    bool accepts(std::span<const std::uint8_t> raw, std::string_view game_path) const override {
        if (!ends_with_tim(game_path)) {
            return false;
        }
        const auto tim = parse_tim(raw);
        return tim && unsupported(tim.value()).empty();
    }

    Result<std::vector<Output>> decode(std::span<const std::uint8_t> raw, std::string_view game_path) const override {
        using R = Result<std::vector<Output>>;
        if (!ends_with_tim(game_path)) {
            return fail<std::vector<Output>>("the path " + std::string(game_path) + " does not end in .TIM");
        }
        auto parsed = parse_tim(raw);
        if (!parsed) {
            return R::fail(parsed.error());
        }
        const TimFile& tim = parsed.value();
        if (const std::string why = unsupported(tim); !why.empty()) {
            return fail<std::vector<Output>>(why);
        }

        const std::string base = "textures/" + std::string(stem_of(game_path));
        const std::size_t width = pixel_width(tim.depth, tim.image_words);
        std::vector<Output> outputs;
        std::string error;

        if (is_indexed(tim.depth)) {
            png::Image image;
            image.width = static_cast<std::uint32_t>(width);
            image.height = tim.image_height;
            image.color_type = png::ColorType::Indexed;
            image.pixels = unpack_indices(tim);
            std::vector<std::uint16_t> row0;
            if (tim.has_clut) {
                row0.assign(tim.clut.words.begin(), tim.clut.words.begin() + static_cast<std::ptrdiff_t>(palette_size(tim)));
            } else {
                // No CLUT: the indices are shown as a gray ramp. encode() does not read the ramp.
                const std::size_t size = tim.depth == Depth::Bpp4 ? 16 : 256;
                row0.resize(size);
                for (std::size_t i = 0; i < size; ++i) {
                    const unsigned level = static_cast<unsigned>(i * 31 / (size - 1));
                    row0[i] = static_cast<std::uint16_t>(0x8000 | level | (level << 5) | (level << 10));
                }
            }
            // The other rows live in the JSON only (png/palette_variant.h turns them into pictures on demand).
            const png::Palette palettes[] = {make_palette(row0.data(), row0.size())};
            auto files = png::encode_palettes(image, palettes);
            if (!files) {
                return R::fail(files.error());
            }

            png::Image picture;
            picture.width = image.width;
            picture.height = image.height;
            picture.color_type = png::ColorType::Rgba;
            picture.pixels.reserve(image.pixels.size() * 4);
            for (const std::uint8_t index : image.pixels) {
                const std::uint16_t w = row0[index];
                picture.pixels.push_back(expand5(w & 31));
                picture.pixels.push_back(expand5((w >> 5) & 31));
                picture.pixels.push_back(expand5((w >> 10) & 31));
                picture.pixels.push_back(w == 0 ? 0 : 255);
            }
            outputs.push_back(png_output(base + ".png", picture, error));
            outputs.push_back({indexed_png_path(game_path), std::move(files.value()[0])});
        } else {
            png::Image image;
            image.width = static_cast<std::uint32_t>(width);
            image.height = tim.image_height;
            const std::size_t stride = std::size_t{tim.image_words} * 2;
            if (tim.depth == Depth::Bpp16) {
                image.color_type = png::ColorType::Rgba;
                image.pixels.reserve(width * tim.image_height * 4);
                for (std::size_t i = 0; i < tim.data.size(); i += 2) {
                    const unsigned w = tim.data[i] | (unsigned{tim.data[i + 1]} << 8);
                    image.pixels.push_back(expand5(w & 31));
                    image.pixels.push_back(expand5((w >> 5) & 31));
                    image.pixels.push_back(expand5((w >> 10) & 31));
                    image.pixels.push_back(w == 0 ? 0 : 255);
                }
            } else {
                // 3 bytes per pixel in the order the GPU reads them: R, G, B.
                image.color_type = png::ColorType::Rgb;
                image.pixels.reserve(width * tim.image_height * 3);
                for (std::size_t y = 0; y < tim.image_height; ++y) {
                    const std::uint8_t* row = tim.data.data() + y * stride;
                    image.pixels.insert(image.pixels.end(), row, row + width * 3);
                }
            }
            outputs.push_back(png_output(base + ".png", image, error));
        }
        if (!error.empty()) {
            return R::fail(error);
        }
        const std::string json = make_json(tim, game_path);
        outputs.push_back({base + std::string(kJsonSuffix), std::vector<std::uint8_t>(json.begin(), json.end())});

        // The contract of a lossless converter, checked for every file instead of assumed.
        auto again = encode(outputs);
        if (!again) {
            return R::fail(again.error());
        }
        if (again.value().size() != raw.size() || !std::equal(raw.begin(), raw.end(), again.value().begin())) {
            return fail<std::vector<Output>>("internal error: " + std::string(game_path) +
                                             " does not rebuild to the same bytes");
        }
        return R::ok(std::move(outputs));
    }

    Result<std::vector<std::uint8_t>> encode(std::span<const Output> outputs) const override {
        using R = Result<std::vector<std::uint8_t>>;
        const Output* sidecar_file = nullptr;
        for (const Output& o : outputs) {
            if (ends_with(o.relative_path, kJsonSuffix)) {
                if (sidecar_file) {
                    return fail<std::vector<std::uint8_t>>("more than one .tim.json in the outputs");
                }
                sidecar_file = &o;
            }
        }
        if (!sidecar_file) {
            return fail<std::vector<std::uint8_t>>("no .tim.json in the outputs");
        }
        const std::string base = sidecar_file->relative_path.substr(0, sidecar_file->relative_path.size() - kJsonSuffix.size());
        auto side = read_sidecar(sidecar_file->bytes);
        if (!side) {
            return R::fail(side.error());
        }
        const Sidecar& s = side.value();

        if (stored_words(s.depth, static_cast<unsigned>(s.width)) != s.image_words || s.image_words == 0) {
            return fail<std::vector<std::uint8_t>>("the JSON width " + std::to_string(s.width) +
                                                   " does not fit a stored width of " + std::to_string(s.image_words) + " words");
        }
        if (is_indexed(s.depth) && s.has_clut && s.palette_size != (s.depth == Depth::Bpp4 ? 16u : 256u)) {
            return fail<std::vector<std::uint8_t>>("\"palette_size\" does not match the depth");
        }
        if (!is_indexed(s.depth) && s.has_clut && s.palette_size != s.clut.width) {
            return fail<std::vector<std::uint8_t>>("\"palette_size\" does not match the CLUT width");
        }

        TimFile tim;
        tim.depth = s.depth;
        tim.has_clut = s.has_clut;
        tim.image_x = s.image_x;
        tim.image_y = s.image_y;
        tim.image_words = s.image_words;
        tim.image_height = static_cast<std::uint16_t>(s.height);
        tim.tail = s.tail;
        const std::size_t stride = std::size_t{s.image_words} * 2;
        tim.data.assign(stride * s.height, 0);

        // 4/8-bit: the indices and row 0 come from the indexed source; textures/...png is a baked picture then.
        const std::string base_png = is_indexed(s.depth) ? png::indexed_path_for(base + ".png") : base + ".png";
        if (base_png.empty()) {
            return fail<std::vector<std::uint8_t>>(sidecar_file->relative_path + " is not under a textures/ directory");
        }
        const Output* picture = find_output(outputs, base_png);
        if (!picture) {
            return fail<std::vector<std::uint8_t>>("missing " + base_png);
        }

        std::vector<std::vector<std::uint16_t>> palettes = s.palettes;
        if (is_indexed(s.depth)) {
            auto image = read_indexed(*picture);
            if (!image) {
                return R::fail(image.error());
            }
            if (image.value().color_type != png::ColorType::Indexed) {
                return fail<std::vector<std::uint8_t>>(base_png + " is not an indexed PNG; save it with a palette (indexed colour)");
            }
            if (image.value().width != s.width || image.value().height != s.height) {
                return fail<std::vector<std::uint8_t>>(base_png + " is " + std::to_string(image.value().width) + "x" +
                                                       std::to_string(image.value().height) + ", the TIM picture is " +
                                                       std::to_string(s.width) + "x" + std::to_string(s.height));
            }
            const std::vector<std::uint8_t>& px = image.value().pixels;
            if (s.depth == Depth::Bpp4 && std::any_of(px.begin(), px.end(), [](std::uint8_t v) { return v >= 16; })) {
                return fail<std::vector<std::uint8_t>>(base_png + " has a pixel index above 15; a 4-bit TIM has 16 colours");
            }
            for (std::size_t y = 0; y < s.height; ++y) {
                const std::uint8_t* src = px.data() + y * s.width;
                std::uint8_t* dst = tim.data.data() + y * stride;
                if (s.depth == Depth::Bpp8) {
                    std::copy(src, src + s.width, dst);
                } else {
                    for (std::size_t i = 0; i < stride; ++i) {
                        dst[i] = static_cast<std::uint8_t>(src[i * 2] | (src[i * 2 + 1] << 4));
                    }
                }
            }
            // Rows 1.. are the JSON words: a NAME.pNN.png beside a baked picture is a baked variant, not a palette.
            if (s.has_clut && !palettes.empty()) {
                if (auto applied = apply_palette(image.value(), base_png, palettes[0]); !applied) {
                    return R::fail(applied.error());
                }
            }
        } else {
            auto rgba = png::decode_rgba8(picture->bytes);
            if (!rgba) {
                return fail<std::vector<std::uint8_t>>(base_png + ": " + rgba.error());
            }
            if (rgba.value().width != s.width || rgba.value().height != s.height) {
                return fail<std::vector<std::uint8_t>>(base_png + " is " + std::to_string(rgba.value().width) + "x" +
                                                       std::to_string(rgba.value().height) + ", the TIM picture is " +
                                                       std::to_string(s.width) + "x" + std::to_string(s.height));
            }
            const std::vector<std::uint8_t>& px = rgba.value().pixels;
            if (s.depth == Depth::Bpp16) {
                std::vector<bool> stp(s.width * s.height, false);
                for (const auto& [first, count] : s.stp_runs) {
                    if (first >= stp.size() || count > stp.size() - first) {
                        return fail<std::vector<std::uint8_t>>("a run in \"stp_runs\" is outside the picture");
                    }
                    std::fill(stp.begin() + static_cast<std::ptrdiff_t>(first),
                              stp.begin() + static_cast<std::ptrdiff_t>(first + count), true);
                }
                for (std::size_t i = 0; i < stp.size(); ++i) {
                    const std::uint16_t w = make_word(px[i * 4], px[i * 4 + 1], px[i * 4 + 2], px[i * 4 + 3], stp[i]);
                    tim.data[i * 2] = static_cast<std::uint8_t>(w);
                    tim.data[i * 2 + 1] = static_cast<std::uint8_t>(w >> 8);
                }
            } else {
                for (std::size_t y = 0; y < s.height; ++y) {
                    for (std::size_t x = 0; x < s.width; ++x) {
                        const std::uint8_t* src = px.data() + (y * s.width + x) * 4;
                        std::copy(src, src + 3, tim.data.data() + y * stride + x * 3);
                    }
                }
            }
        }

        if (s.has_clut) {
            tim.clut = s.clut;
            for (const auto& palette : palettes) {
                tim.clut.words.insert(tim.clut.words.end(), palette.begin(), palette.end());
            }
        }
        return R::ok(write_tim(tim));
    }
};

}  // namespace

std::unique_ptr<sh::convert::Converter> make_converter() {
    return std::make_unique<TimConverter>();
}

}  // namespace sh::convert::tim
