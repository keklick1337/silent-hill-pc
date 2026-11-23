#include "formats/png/palette_variant.h"

#include "formats/common/json.h"

#include <cstdlib>
#include <string>

namespace sh::png {
namespace {

std::uint8_t expand5(unsigned v) {
    return static_cast<std::uint8_t>((v << 3) | (v >> 2));
}

// PLTE/tRNS of CLUT row `row`. `has_palettes` is false when the JSON has no "palettes" (a TIM without CLUT).
Result<Palette> row_palette(std::span<const std::uint8_t> tim_json, int row, bool& has_palettes) {
    using R = Result<Palette>;
    auto doc = json::parse(std::string_view(reinterpret_cast<const char*>(tim_json.data()), tim_json.size()));
    if (!doc) {
        return R::fail("palette variant: " + doc.error());
    }
    const json::Value* palettes = doc.value().is_object() ? doc.value().find("palettes") : nullptr;
    has_palettes = palettes != nullptr;
    if (!has_palettes) {
        return R::ok(Palette{});
    }
    if (!palettes->is_array() || row < 0 || static_cast<std::size_t>(row) >= palettes->array().size() ||
        !palettes->array()[static_cast<std::size_t>(row)].is_array()) {
        return R::fail("palette variant: the TIM has no palette " + std::to_string(row));
    }
    Palette out;
    for (const json::Value& entry : palettes->array()[static_cast<std::size_t>(row)].array()) {
        const unsigned w = entry.is_string() ? static_cast<unsigned>(std::strtoul(entry.string().c_str(), nullptr, 16)) : 0;
        out.rgb.push_back(expand5(w & 31));
        out.rgb.push_back(expand5((w >> 5) & 31));
        out.rgb.push_back(expand5((w >> 10) & 31));
        out.alpha.push_back(w == 0 ? 0 : 255);
    }
    if (out.alpha.empty()) {
        return R::fail("palette variant: palette " + std::to_string(row) + " is empty");
    }
    return R::ok(std::move(out));
}

Result<Image> decode_indexed(std::span<const std::uint8_t> png) {
    auto image = decode(png);
    if (!image) {
        return image;
    }
    if (image.value().color_type != ColorType::Indexed) {
        return Result<Image>::fail("palette variant: the picture is not an indexed PNG");
    }
    return image;
}

// A "textures" component starts the path or follows a '/'.
std::size_t last_textures_component(std::string_view path) {
    constexpr std::string_view kDir = "textures/";
    std::size_t found = std::string_view::npos;
    for (std::size_t at = path.find(kDir); at != std::string_view::npos; at = path.find(kDir, at + 1)) {
        if (at == 0 || path[at - 1] == '/') {
            found = at;
        }
    }
    return found;
}

}  // namespace

std::optional<VariantName> parse_variant_name(std::string_view path) {
    constexpr std::string_view kExt = ".png";
    if (path.size() < kExt.size() + 4 || path.substr(path.size() - kExt.size()) != kExt) {
        return std::nullopt;
    }
    const std::string_view stem = path.substr(0, path.size() - kExt.size());
    std::size_t digits = 0;
    while (digits < stem.size() && stem[stem.size() - 1 - digits] >= '0' && stem[stem.size() - 1 - digits] <= '9') {
        ++digits;
    }
    if (digits < 2 || digits > 4 || stem.size() < digits + 2 || stem[stem.size() - digits - 1] != 'p' ||
        stem[stem.size() - digits - 2] != '.') {
        return std::nullopt;
    }
    VariantName name;
    name.row = std::atoi(std::string(stem.substr(stem.size() - digits)).c_str());
    const std::string base(stem.substr(0, stem.size() - digits - 2));
    name.base_png = base + ".png";
    name.indexed_png = indexed_path_for(name.base_png);
    name.tim_json = base + ".tim.json";
    return name;
}

std::string indexed_path_for(std::string_view texture_png) {
    const std::size_t at = last_textures_component(texture_png);
    if (at == std::string_view::npos) {
        return {};
    }
    const std::size_t slash = texture_png.rfind('/');
    const std::size_t file = slash == std::string_view::npos ? 0 : slash + 1;
    const std::size_t dot = texture_png.find('.', file);
    const std::string_view stem = texture_png.substr(0, dot == std::string_view::npos ? texture_png.size() : dot);
    std::string out(stem.substr(0, at));
    out += "indexed/";
    out += stem.substr(at + std::string_view("textures/").size());
    return out + ".png";
}

Result<std::vector<std::uint8_t>> make_variant_png(const std::vector<std::uint8_t>& base_png,
                                                   const std::vector<std::uint8_t>& tim_json, int row) {
    using R = Result<std::vector<std::uint8_t>>;
    auto image = decode_indexed(base_png);
    if (!image) {
        return R::fail(image.error());
    }
    bool has_palettes = false;
    auto palette = row_palette(tim_json, row, has_palettes);
    if (!palette) {
        return R::fail(palette.error());
    }
    if (!has_palettes) {
        return R::fail("palette variant: the TIM has no palette " + std::to_string(row));
    }
    Image out = std::move(image.value());
    out.palette = std::move(palette.value().rgb);
    out.palette_alpha = std::move(palette.value().alpha);
    return encode(out);
}

Result<Rgba8> make_row_rgba(std::span<const std::uint8_t> indexed_png, std::span<const std::uint8_t> tim_json,
                            int row) {
    using R = Result<Rgba8>;
    auto image = decode_indexed(indexed_png);
    if (!image) {
        return R::fail(image.error());
    }
    bool has_palettes = false;
    auto palette = row_palette(tim_json, row, has_palettes);
    if (!palette) {
        return R::fail(palette.error());
    }
    Palette colours = std::move(palette.value());
    if (!has_palettes) {
        if (row != 0) {
            return R::fail("palette variant: the TIM has no palette " + std::to_string(row));
        }
        colours.rgb = image.value().palette;
        colours.alpha = image.value().palette_alpha;
        colours.alpha.resize(colours.rgb.size() / 3, 255);
    }
    const Image& src = image.value();
    Rgba8 out;
    out.width = src.width;
    out.height = src.height;
    out.pixels.resize(src.pixels.size() * 4);
    const std::size_t entries = colours.alpha.size();
    for (std::size_t i = 0; i < src.pixels.size(); ++i) {
        const std::size_t index = src.pixels[i];
        if (index >= entries) {
            return R::fail("palette variant: pixel index " + std::to_string(index) + " is past palette " +
                           std::to_string(row));
        }
        out.pixels[i * 4] = colours.rgb[index * 3];
        out.pixels[i * 4 + 1] = colours.rgb[index * 3 + 1];
        out.pixels[i * 4 + 2] = colours.rgb[index * 3 + 2];
        out.pixels[i * 4 + 3] = colours.alpha[index];
    }
    return R::ok(std::move(out));
}

std::optional<std::vector<std::uint8_t>> read_texture_or_variant(const std::string& path, const ByteReader& reader) {
    if (auto bytes = reader(path)) {
        return bytes;
    }
    const auto name = parse_variant_name(path);
    if (!name) {
        return std::nullopt;
    }
    auto json = reader(name->tim_json);
    if (!json) {
        return std::nullopt;
    }
    // indexed/ first: in the importer-3 layout base_png is the baked true-colour picture.
    for (const std::string* source : {&name->indexed_png, &name->base_png}) {
        if (source->empty()) {
            continue;
        }
        auto picture = reader(*source);
        if (!picture) {
            continue;
        }
        if (auto made = make_variant_png(*picture, *json, name->row)) {
            return std::move(made.value());
        }
    }
    return std::nullopt;
}

}  // namespace sh::png
