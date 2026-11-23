#include "formats/convert/model/model_converter.h"

#include "formats/convert/model/ilm.h"
#include "formats/convert/model/model_build.h"
#include "formats/convert/model/tmd.h"

#include <algorithm>
#include <cctype>

namespace sh::convert::model {
namespace {

enum class Kind { None, Ilm, Tmd, Plm };

// The format is decided by the extension and confirmed by the bytes: a .ILM that is not an ILM is not accepted.
Kind classify(std::span<const std::uint8_t> raw, std::string_view game_path) {
    const std::size_t dot = game_path.rfind('.');
    if (dot == std::string_view::npos) {
        return Kind::None;
    }
    std::string ext(game_path.substr(dot + 1));
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    if (ext == "ILM" && looks_like_ilm(raw)) {
        return Kind::Ilm;
    }
    if (ext == "TMD" && looks_like_tmd(raw)) {
        return Kind::Tmd;
    }
    // Held items; the other PLMs (BG/) are model libraries of the maps and belong to the level converter.
    if (ext == "PLM" && upper_copy(path_directory(game_path)) == "ITEM" && looks_like_plm(raw)) {
        return Kind::Plm;
    }
    return Kind::None;
}

class ModelConverter final : public sh::convert::Converter {
public:
    explicit ModelConverter(Options options) : options_(std::move(options)) {}

    std::string_view name() const override { return "model"; }

    bool lossless() const override { return false; }

    bool accepts(std::span<const std::uint8_t> raw, std::string_view game_path) const override {
        return classify(raw, game_path) != Kind::None;
    }

    Result<std::vector<Output>> decode(std::span<const std::uint8_t> raw, std::string_view game_path) const override {
        auto converted = convert_model(raw, game_path, options_);
        if (!converted) {
            return Result<std::vector<Output>>::fail(std::string(game_path) + ": " + converted.error());
        }
        const std::string base = "models/" + path_without_extension(game_path);
        std::vector<Output> outputs;
        outputs.push_back({base + ".glb", std::move(converted.value().glb)});
        const std::string& json = converted.value().sidecar_json;
        outputs.push_back({base + ".model.json", std::vector<std::uint8_t>(json.begin(), json.end())});
        return Result<std::vector<Output>>::ok(std::move(outputs));
    }

    Result<std::vector<std::uint8_t>> encode(std::span<const Output>) const override {
        return Result<std::vector<std::uint8_t>>::fail(
            "model converter is not lossless: glTF cannot be turned back into ILM/TMD");
    }

private:
    Options options_;
};

}  // namespace

Result<ConvertedModel> convert_model(std::span<const std::uint8_t> raw, std::string_view game_path,
                                     const Options& options) {
    switch (classify(raw, game_path)) {
        case Kind::Ilm: return build_ilm(raw, game_path, options);
        case Kind::Tmd: return build_tmd(raw, game_path, options);
        case Kind::Plm: return build_plm(raw, game_path, options);
        case Kind::None: break;
    }
    return Result<ConvertedModel>::fail("not an ILM, TMD or held-item PLM file");
}

std::unique_ptr<sh::convert::Converter> make_converter() {
    return std::make_unique<ModelConverter>(Options{});
}

std::unique_ptr<sh::convert::Converter> make_converter(Options options) {
    return std::make_unique<ModelConverter>(std::move(options));
}

}  // namespace sh::convert::model
