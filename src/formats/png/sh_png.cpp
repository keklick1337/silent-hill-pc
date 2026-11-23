#include "formats/png/sh_png.h"

#include "formats/common/crc32.h"
#include "formats/png/stb_png.h"
#include "formats/png/zlib_codec.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <utility>

namespace sh::png {
namespace {

constexpr std::array<std::uint8_t, 8> kSignature = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
constexpr std::uint32_t kMaxChunk = 0x7FFFFFFFu;
// Raw (unfiltered) size cap for decode(): a 1 GiB image is a corrupt header, not a texture.
constexpr std::uint64_t kMaxRawBytes = 1ull << 30;

template <typename T>
Result<T> fail(std::string message) {
    return Result<T>::fail(std::move(message));
}

void put_be32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>(value >> 24));
    out.push_back(static_cast<std::uint8_t>(value >> 16));
    out.push_back(static_cast<std::uint8_t>(value >> 8));
    out.push_back(static_cast<std::uint8_t>(value));
}

std::uint32_t get_be32(const std::uint8_t* p) {
    return (std::uint32_t{p[0]} << 24) | (std::uint32_t{p[1]} << 16) | (std::uint32_t{p[2]} << 8) | std::uint32_t{p[3]};
}

// length, type, data, CRC-32 over type and data.
void append_chunk(std::vector<std::uint8_t>& out, const char (&type)[5], std::span<const std::uint8_t> data) {
    put_be32(out, static_cast<std::uint32_t>(data.size()));
    const std::size_t crc_start = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), data.begin(), data.end());
    put_be32(out, crc32(out.data() + crc_start, out.size() - crc_start));
}

std::uint8_t paeth(int a, int b, int c) {
    const int p = a + b - c;
    const int pa = std::abs(p - a);
    const int pb = std::abs(p - b);
    const int pc = std::abs(p - c);
    if (pa <= pb && pa <= pc) {
        return static_cast<std::uint8_t>(a);
    }
    return static_cast<std::uint8_t>(pb <= pc ? b : c);
}

// Filter `type` of one scanline. `prior` is the previous raw row, or zeros for the first.
void filter_row(int type, const std::uint8_t* row, const std::uint8_t* prior, std::size_t length, std::size_t bpp,
                std::uint8_t* out) {
    for (std::size_t i = 0; i < length; ++i) {
        const int a = i >= bpp ? row[i - bpp] : 0;
        const int b = prior[i];
        const int c = i >= bpp ? prior[i - bpp] : 0;
        int predictor = 0;
        switch (type) {
            case 1: predictor = a; break;
            case 2: predictor = b; break;
            case 3: predictor = (a + b) / 2; break;
            case 4: predictor = paeth(a, b, c); break;
            default: break;
        }
        out[i] = static_cast<std::uint8_t>(row[i] - predictor);
    }
}

std::uint64_t filter_score(const std::uint8_t* filtered, std::size_t length) {
    std::uint64_t sum = 0;
    for (std::size_t i = 0; i < length; ++i) {
        const int v = static_cast<std::int8_t>(filtered[i]);
        sum += static_cast<std::uint64_t>(v < 0 ? -v : v);
    }
    return sum;
}

std::vector<std::uint8_t> filter_image(const Image& image, std::size_t bpp) {
    const std::size_t length = std::size_t{image.width} * bpp;
    std::vector<std::uint8_t> raw(image.height * (length + 1));
    const std::vector<std::uint8_t> zeros(length, 0);
    std::vector<std::uint8_t> candidate(length);
    std::vector<std::uint8_t> best(length);
    for (std::size_t y = 0; y < image.height; ++y) {
        const std::uint8_t* row = image.pixels.data() + y * length;
        const std::uint8_t* prior = y == 0 ? zeros.data() : row - length;
        int best_type = 0;
        std::uint64_t best_score = std::numeric_limits<std::uint64_t>::max();
        for (int type = 0; type <= 4; ++type) {
            filter_row(type, row, prior, length, bpp, candidate.data());
            const std::uint64_t score = filter_score(candidate.data(), length);
            if (score < best_score) {
                best_score = score;
                best_type = type;
                best.swap(candidate);
            }
        }
        std::uint8_t* dst = raw.data() + y * (length + 1);
        dst[0] = static_cast<std::uint8_t>(best_type);
        std::memcpy(dst + 1, best.data(), length);
    }
    return raw;
}

}  // namespace

namespace {

// The part of the checks that does not depend on the palette: empty when fine.
std::string check_geometry(const Image& image) {
    if (image.width == 0 || image.height == 0 || image.width > kMaxChunk || image.height > kMaxChunk) {
        return "png: the image size is " + std::to_string(image.width) + "x" + std::to_string(image.height);
    }
    const std::size_t samples = channels(image.color_type);
    if (samples == 0) {
        return "png: unknown colour type";
    }
    const std::uint64_t expected = std::uint64_t{image.width} * image.height * samples;
    if (expected > kMaxRawBytes || image.pixels.size() != expected) {
        return "png: pixel data is " + std::to_string(image.pixels.size()) + " bytes, the size needs " +
               std::to_string(expected);
    }
    return {};
}

std::string check_palette(std::span<const std::uint8_t> pixels, std::span<const std::uint8_t> rgb,
                          std::span<const std::uint8_t> alpha) {
    if (rgb.empty() || rgb.size() % 3 != 0 || rgb.size() > 256 * 3) {
        return "png: an indexed image needs a palette of 1..256 entries";
    }
    const std::size_t entries = rgb.size() / 3;
    if (alpha.size() > entries) {
        return "png: the tRNS alpha list is longer than the palette";
    }
    if (std::any_of(pixels.begin(), pixels.end(), [entries](std::uint8_t v) { return v >= entries; })) {
        return "png: a pixel index is past the end of the palette";
    }
    return {};
}

// IHDR, PLTE and tRNS for an indexed image, IDAT, IEND.
std::vector<std::uint8_t> assemble(const Image& image, std::span<const std::uint8_t> rgb,
                                   std::span<const std::uint8_t> alpha, std::span<const std::uint8_t> idat) {
    std::vector<std::uint8_t> out(kSignature.begin(), kSignature.end());
    std::vector<std::uint8_t> ihdr;
    put_be32(ihdr, image.width);
    put_be32(ihdr, image.height);
    ihdr.push_back(8);
    ihdr.push_back(static_cast<std::uint8_t>(image.color_type));
    ihdr.push_back(0);  // compression
    ihdr.push_back(0);  // filter method
    ihdr.push_back(0);  // no interlace
    append_chunk(out, "IHDR", ihdr);
    if (image.color_type == ColorType::Indexed) {
        append_chunk(out, "PLTE", rgb);
        // Trailing opaque entries are implied; leaving them out keeps tRNS short.
        std::size_t alpha_count = alpha.size();
        while (alpha_count > 0 && alpha[alpha_count - 1] == 255) {
            --alpha_count;
        }
        if (alpha_count > 0) {
            append_chunk(out, "tRNS", alpha.first(alpha_count));
        }
    }
    append_chunk(out, "IDAT", idat);
    append_chunk(out, "IEND", {});
    return out;
}

}  // namespace

Result<std::vector<std::uint8_t>> encode(const Image& image, int level) {
    using R = Result<std::vector<std::uint8_t>>;
    if (std::string error = check_geometry(image); !error.empty()) {
        return R::fail(std::move(error));
    }
    if (image.color_type == ColorType::Indexed) {
        if (std::string error = check_palette(image.pixels, image.palette, image.palette_alpha); !error.empty()) {
            return R::fail(std::move(error));
        }
    } else if (!image.palette.empty() || !image.palette_alpha.empty()) {
        return R::fail("png: only an indexed image has a palette");
    }
    auto compressed = detail::zlib_compress(filter_image(image, channels(image.color_type)), level);
    if (!compressed) {
        return R::fail(compressed.error());
    }
    return R::ok(assemble(image, image.palette, image.palette_alpha, compressed.value()));
}

Result<std::vector<std::vector<std::uint8_t>>> encode_palettes(const Image& image, std::span<const Palette> palettes) {
    using R = Result<std::vector<std::vector<std::uint8_t>>>;
    if (std::string error = check_geometry(image); !error.empty()) {
        return R::fail(std::move(error));
    }
    if (image.color_type != ColorType::Indexed || palettes.empty()) {
        return R::fail("png: encode_palettes needs an indexed image and at least one palette");
    }
    for (const Palette& palette : palettes) {
        if (std::string error = check_palette(image.pixels, palette.rgb, palette.alpha); !error.empty()) {
            return R::fail(std::move(error));
        }
    }
    auto compressed = detail::zlib_compress(filter_image(image, 1));
    if (!compressed) {
        return R::fail(compressed.error());
    }
    std::vector<std::vector<std::uint8_t>> files;
    files.reserve(palettes.size());
    for (const Palette& palette : palettes) {
        files.push_back(assemble(image, palette.rgb, palette.alpha, compressed.value()));
    }
    return R::ok(std::move(files));
}

namespace {

struct Header {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint8_t depth = 0;
    ColorType color = ColorType::Rgba;
};

bool depth_allowed(ColorType color, std::uint8_t depth) {
    switch (color) {
        case ColorType::Gray: return depth == 1 || depth == 2 || depth == 4 || depth == 8 || depth == 16;
        case ColorType::Indexed: return depth == 1 || depth == 2 || depth == 4 || depth == 8;
        default: return depth == 8 || depth == 16;
    }
}

}  // namespace

Result<Image> decode(std::span<const std::uint8_t> png) {
    using R = Result<Image>;
    if (png.size() < kSignature.size() || !std::equal(kSignature.begin(), kSignature.end(), png.begin())) {
        return fail<Image>("png: bad signature");
    }

    Header header;
    bool have_header = false;
    bool have_palette = false;
    bool in_data = false;
    bool data_done = false;
    bool have_end = false;
    std::vector<std::uint8_t> palette;
    std::vector<std::uint8_t> palette_alpha;
    std::vector<std::uint8_t> idat;

    std::size_t pos = kSignature.size();
    while (!have_end) {
        if (png.size() - pos < 12) {
            return fail<Image>("png: truncated before IEND");
        }
        const std::uint32_t length = get_be32(png.data() + pos);
        if (length > kMaxChunk || length > png.size() - pos - 12) {
            return fail<Image>("png: a chunk runs past the end of the file");
        }
        const std::uint8_t* type = png.data() + pos + 4;
        const std::uint8_t* data = png.data() + pos + 8;
        if (crc32(type, std::size_t{length} + 4) != get_be32(data + length)) {
            return fail<Image>("png: chunk CRC mismatch in " + std::string(reinterpret_cast<const char*>(type), 4));
        }
        pos += std::size_t{length} + 12;
        const std::string name(reinterpret_cast<const char*>(type), 4);

        if (!have_header && name != "IHDR") {
            return fail<Image>("png: the first chunk is not IHDR");
        }
        if (name != "IDAT" && in_data) {
            in_data = false;
            data_done = true;
        }
        if (name == "IHDR") {
            if (have_header || length != 13) {
                return fail<Image>("png: bad IHDR");
            }
            header.width = get_be32(data);
            header.height = get_be32(data + 4);
            header.depth = data[8];
            const std::uint8_t color = data[9];
            if (header.width == 0 || header.height == 0 || header.width > kMaxChunk || header.height > kMaxChunk) {
                return fail<Image>("png: bad image size");
            }
            if (color != 0 && color != 2 && color != 3 && color != 4 && color != 6) {
                return fail<Image>("png: unknown colour type " + std::to_string(color));
            }
            header.color = static_cast<ColorType>(color);
            if (!depth_allowed(header.color, header.depth)) {
                return fail<Image>("png: bit depth " + std::to_string(header.depth) + " is not valid for colour type " +
                                   std::to_string(color));
            }
            if (data[10] != 0 || data[11] != 0) {
                return fail<Image>("png: unknown compression or filter method");
            }
            if (data[12] != 0) {
                return fail<Image>("png: interlaced images are not supported by decode(), use decode_rgba8()");
            }
            have_header = true;
        } else if (name == "PLTE") {
            if (have_palette || in_data || data_done || length == 0 || length % 3 != 0 || length > 256 * 3) {
                return fail<Image>("png: bad PLTE");
            }
            if (header.color == ColorType::Gray || header.color == ColorType::GrayAlpha) {
                return fail<Image>("png: PLTE in a gray image");
            }
            palette.assign(data, data + length);
            have_palette = true;
        } else if (name == "tRNS") {
            if (header.color == ColorType::Indexed) {
                if (!have_palette || in_data || data_done || length > palette.size() / 3) {
                    return fail<Image>("png: bad tRNS");
                }
                palette_alpha.assign(data, data + length);
            }
            // The colour key of Gray and Rgb is ignored, see sh_png.h.
        } else if (name == "IDAT") {
            if (data_done) {
                return fail<Image>("png: IDAT chunks are not contiguous");
            }
            in_data = true;
            idat.insert(idat.end(), data, data + length);
        } else if (name == "IEND") {
            if (length != 0) {
                return fail<Image>("png: bad IEND");
            }
            have_end = true;
        } else if ((type[0] & 0x20) == 0) {
            return fail<Image>("png: unknown critical chunk " + name);
        }
    }
    if (idat.empty()) {
        return fail<Image>("png: no image data");
    }
    if (header.color == ColorType::Indexed && !have_palette) {
        return fail<Image>("png: an indexed image without PLTE");
    }
    if (header.depth == 16) {
        return fail<Image>("png: 16-bit samples are not supported by decode(), use decode_rgba8()");
    }

    const std::size_t samples = channels(header.color);
    const std::uint64_t row_bits = std::uint64_t{header.width} * samples * header.depth;
    const std::uint64_t row_bytes = (row_bits + 7) / 8;
    const std::uint64_t raw_size = (row_bytes + 1) * header.height;
    if (raw_size > kMaxRawBytes) {
        return fail<Image>("png: the image is too large");
    }
    auto raw = detail::zlib_decompress(idat, static_cast<std::size_t>(raw_size));
    if (!raw) {
        return R::fail(raw.error());
    }

    const std::size_t stride = static_cast<std::size_t>(row_bytes);
    const std::size_t bpp = std::max<std::size_t>(1, samples * header.depth / 8);
    std::vector<std::uint8_t>& scan = raw.value();
    for (std::size_t y = 0; y < header.height; ++y) {
        std::uint8_t* row = scan.data() + y * (stride + 1);
        const int type = row[0];
        std::uint8_t* cur = row + 1;
        const std::uint8_t* prior = y == 0 ? nullptr : row - stride;  // the previous row, already unfiltered
        if (type > 4) {
            return fail<Image>("png: unknown filter type " + std::to_string(type) + " in row " + std::to_string(y));
        }
        for (std::size_t i = 0; i < stride; ++i) {
            const int a = i >= bpp ? cur[i - bpp] : 0;
            const int b = prior ? prior[i] : 0;
            const int c = (prior && i >= bpp) ? prior[i - bpp] : 0;
            int predictor = 0;
            switch (type) {
                case 1: predictor = a; break;
                case 2: predictor = b; break;
                case 3: predictor = (a + b) / 2; break;
                case 4: predictor = paeth(a, b, c); break;
                default: break;
            }
            cur[i] = static_cast<std::uint8_t>(cur[i] + predictor);
        }
    }

    Image image;
    image.width = header.width;
    image.height = header.height;
    image.color_type = header.color;
    const std::size_t row_samples = std::size_t{header.width} * samples;
    image.pixels.resize(row_samples * header.height);
    for (std::size_t y = 0; y < header.height; ++y) {
        const std::uint8_t* src = scan.data() + y * (stride + 1) + 1;
        std::uint8_t* dst = image.pixels.data() + y * row_samples;
        if (header.depth == 8) {
            std::memcpy(dst, src, row_samples);
            continue;
        }
        const unsigned mask = (1u << header.depth) - 1;
        for (std::size_t i = 0; i < row_samples; ++i) {
            const std::size_t bit = i * header.depth;
            const unsigned shift = 8 - header.depth - static_cast<unsigned>(bit % 8);
            dst[i] = static_cast<std::uint8_t>((src[bit / 8] >> shift) & mask);
        }
    }
    if (header.color == ColorType::Indexed) {
        const std::size_t entries = palette.size() / 3;
        if (std::any_of(image.pixels.begin(), image.pixels.end(), [entries](std::uint8_t v) { return v >= entries; })) {
            return fail<Image>("png: a pixel index is past the end of the palette");
        }
        image.palette = std::move(palette);
        image.palette_alpha = std::move(palette_alpha);
    }
    return R::ok(std::move(image));
}

Result<Rgba8> decode_rgba8(std::span<const std::uint8_t> png) {
    if (png.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return fail<Rgba8>("png: the file is too large");
    }
    int width = 0;
    int height = 0;
    unsigned char* pixels = sh_png_stb_load_rgba(png.data(), static_cast<int>(png.size()), &width, &height);
    if (!pixels) {
        return fail<Rgba8>(std::string("png: ") + sh_png_stb_error());
    }
    Rgba8 image;
    image.width = static_cast<std::uint32_t>(width);
    image.height = static_cast<std::uint32_t>(height);
    image.pixels.assign(pixels, pixels + std::size_t{image.width} * image.height * 4);
    sh_png_stb_free(pixels);
    return Result<Rgba8>::ok(std::move(image));
}

}  // namespace sh::png
