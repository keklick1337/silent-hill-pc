#include "framework/sh_test.h"
#include "formats/png/palette_variant.h"
#include "formats/png/sh_png.h"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

// Palette variants and CLUT rows built from the indexed source (importer 3) or from an importer-2 indexed texture.

namespace {

namespace png = sh::png;
using Bytes = std::vector<std::uint8_t>;

Bytes text(const std::string& s) {
    return Bytes(s.begin(), s.end());
}

// 4x1, indices 0..3; PLTE = row 0. The JSON has two rows of 4 words (a reduced palette is fine for the reader).
Bytes indexed_png() {
    png::Image image;
    image.width = 4;
    image.height = 1;
    image.color_type = png::ColorType::Indexed;
    image.pixels = {0, 1, 2, 3};
    image.palette = {0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255};
    image.palette_alpha = {0};
    return png::encode(image).value();
}

const Bytes kJson = text(R"({"format": "sh-tim", "palettes": [["0000", "001f", "03e0", "7c00"], ["8000", "7fff", "0000", "0421"]]})");

png::ByteReader reader_of(const std::map<std::string, Bytes>& files) {
    return [&files](const std::string& path) -> std::optional<Bytes> {
        const auto it = files.find(path);
        return it == files.end() ? std::nullopt : std::optional<Bytes>(it->second);
    };
}

}  // namespace

SH_TEST(PaletteVariantIndexedPathFor) {
    SH_CHECK(png::indexed_path_for("textures/CHARA/HERO.png") == "indexed/CHARA/HERO.png");
    SH_CHECK(png::indexed_path_for("textures/CHARA/HERO.p03.png") == "indexed/CHARA/HERO.png");
    SH_CHECK(png::indexed_path_for("gamedata/convert/textures/CHARA/HERO.arm.png") ==
             "gamedata/convert/indexed/CHARA/HERO.png");
    // The last directory named exactly "textures" counts; "mytextures" is not one.
    SH_CHECK(png::indexed_path_for("textures/x/textures/BG/A.png") == "textures/x/indexed/BG/A.png");
    SH_CHECK(png::indexed_path_for("mytextures/BG/A.png").empty());
    SH_CHECK(png::indexed_path_for("BG/A.png").empty());
    const auto name = png::parse_variant_name("convert/textures/BG/A.p12.png");
    SH_CHECK(name && name->row == 12 && name->base_png == "convert/textures/BG/A.png" &&
             name->indexed_png == "convert/indexed/BG/A.png" && name->tim_json == "convert/textures/BG/A.tim.json");
}

SH_TEST(PaletteVariantRowRgba) {
    const Bytes source = indexed_png();
    auto row1 = png::make_row_rgba(source, kJson, 1);
    SH_CHECK(row1 && row1.value().width == 4 && row1.value().height == 1);
    if (row1) {
        // 8000: opaque black; 7fff: white; 0000: transparent; 0421: level 1 everywhere = 8.
        const Bytes want = {0, 0, 0, 255, 255, 255, 255, 255, 0, 0, 0, 0, 8, 8, 8, 255};
        SH_CHECK(row1.value().pixels == want);
    }
    auto row0 = png::make_row_rgba(source, kJson, 0);
    SH_CHECK(row0 && (row0.value().pixels == Bytes{0, 0, 0, 0, 255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255}));
    SH_CHECK(!png::make_row_rgba(source, kJson, 2));
    SH_CHECK(!png::make_row_rgba(source, kJson, -1));
    SH_CHECK(!png::make_row_rgba(source, text("{"), 0));
    // A true-colour picture is not a source.
    png::Image rgba;
    rgba.width = 1;
    rgba.height = 1;
    rgba.color_type = png::ColorType::Rgba;
    rgba.pixels = {1, 2, 3, 4};
    SH_CHECK(!png::make_row_rgba(png::encode(rgba).value(), kJson, 0));
    // An index past the row.
    const Bytes short_row = text(R"({"palettes": [["0000", "001f"]]})");
    SH_CHECK(!png::make_row_rgba(source, short_row, 0));
}

SH_TEST(PaletteVariantResolvesFromIndexedThenLegacy) {
    const Bytes source = indexed_png();
    png::Image baked;
    baked.width = 4;
    baked.height = 1;
    baked.color_type = png::ColorType::Rgba;
    baked.pixels.assign(16, 200);
    const Bytes baked_png = png::encode(baked).value();

    // Importer 3: textures/ holds the baked picture, the variant comes from indexed/.
    std::map<std::string, Bytes> files = {{"textures/BG/A.png", baked_png},
                                          {"indexed/BG/A.png", source},
                                          {"textures/BG/A.tim.json", kJson}};
    const auto made = png::read_texture_or_variant("textures/BG/A.p01.png", reader_of(files));
    SH_CHECK(made.has_value());
    if (made) {
        auto rgba = png::decode_rgba8(*made);
        auto want = png::make_row_rgba(source, kJson, 1);
        SH_CHECK(rgba && want && rgba.value().pixels == want.value().pixels);
    }
    // An existing file wins; plain names are not synthesized.
    SH_CHECK(png::read_texture_or_variant("textures/BG/A.png", reader_of(files)) == baked_png);
    SH_CHECK(!png::read_texture_or_variant("textures/BG/B.p01.png", reader_of(files)));
    SH_CHECK(!png::read_texture_or_variant("textures/BG/A.p05.png", reader_of(files)));
    files["textures/BG/A.p01.png"] = text("mod");
    SH_CHECK(png::read_texture_or_variant("textures/BG/A.p01.png", reader_of(files)) == text("mod"));

    // Importer 2: no indexed/, textures/A.png is itself indexed.
    const std::map<std::string, Bytes> legacy = {{"textures/BG/A.png", source}, {"textures/BG/A.tim.json", kJson}};
    const auto old = png::read_texture_or_variant("textures/BG/A.p01.png", reader_of(legacy));
    SH_CHECK(old.has_value() && made.has_value() && *old == *made);

    // Baked picture and no indexed source: nothing to build from.
    const std::map<std::string, Bytes> broken = {{"textures/BG/A.png", baked_png}, {"textures/BG/A.tim.json", kJson}};
    SH_CHECK(!png::read_texture_or_variant("textures/BG/A.p01.png", reader_of(broken)));
}
