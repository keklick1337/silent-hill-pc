#include "formats/convert/font/font_converter.h"

namespace sh::convert::font {

namespace {

class FontConverter final : public Converter {
public:
    explicit FontConverter(std::vector<std::pair<std::string, std::string>> sheets) : sheets_(std::move(sheets)) {}

    std::string_view name() const override { return "font"; }
    bool lossless() const override { return false; }

    bool accepts(std::span<const std::uint8_t> raw, std::string_view game_path) const override {
        return game_path == "1ST/FONT16.TIM" && raw.size() > 8 && raw[0] == 0x10;
    }

    Result<std::vector<Output>> decode(std::span<const std::uint8_t> raw, std::string_view) const override {
        using R = Result<std::vector<Output>>;
        BuildSources sources;
        sources.font16 = raw;
        sources.sheets = sheets_;
        auto font = build(sources);
        if (!font) return R::fail(font.error());
        auto png = write_atlas_png(font.value());
        if (!png) return R::fail("atlas: " + png.error());
        const std::string metrics = write_metrics_json(font.value(), "game.png");
        std::vector<Output> out;
        out.push_back({kAtlasPath, std::move(png.value())});
        out.push_back({kMetricsPath, std::vector<std::uint8_t>(metrics.begin(), metrics.end())});
        return R::ok(std::move(out));
    }

    Result<std::vector<std::uint8_t>> encode(std::span<const Output>) const override {
        return Result<std::vector<std::uint8_t>>::fail("the game font is a representation, not the disc file");
    }

private:
    std::vector<std::pair<std::string, std::string>> sheets_;
};

}  // namespace

std::unique_ptr<Converter> make_converter(std::vector<std::pair<std::string, std::string>> sheets) {
    return std::make_unique<FontConverter>(std::move(sheets));
}

}  // namespace sh::convert::font
