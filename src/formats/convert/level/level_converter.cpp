#include "formats/convert/level/level_converter.h"

#include "formats/convert/level/level_gltf.h"
#include "formats/convert/level/level_json.h"
#include "formats/convert/level/level_parse.h"
#include "formats/convert/level/map_areas.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <map>
#include <mutex>
#include <optional>
#include <string>

namespace sh::convert::level {
namespace {

enum class Kind { None, Ipd, Plm };

bool has_extension(std::string_view path, std::string_view ext) {
    if (path.size() < ext.size()) {
        return false;
    }
    const std::string_view tail = path.substr(path.size() - ext.size());
    return std::equal(tail.begin(), tail.end(), ext.begin(),
                      [](char a, char b) { return std::toupper(static_cast<unsigned char>(a)) == b; });
}

bool in_folder(std::string_view path, std::string_view folder) {
    return path.size() > folder.size() && path[folder.size()] == '/' &&
           std::equal(folder.begin(), folder.end(), path.begin(),
                      [](char a, char b) { return std::toupper(static_cast<unsigned char>(b)) == a; });
}

// Decided by the bytes and the path: the extension picks the format, the magic confirms it.
Kind classify(std::span<const std::uint8_t> raw, std::string_view path) {
    if (has_extension(path, ".IPD") && (in_folder(path, "BG") || in_folder(path, "TEST"))) {
        return !raw.empty() && raw[0] == 0x14 ? Kind::Ipd : Kind::None;
    }
    // ITEM/*.PLM are held items and belong to the model converter.
    if (has_extension(path, ".PLM") && in_folder(path, "BG")) {
        return !raw.empty() && raw[0] == 0x30 ? Kind::Plm : Kind::None;
    }
    return Kind::None;
}

std::string without_extension(std::string_view game_path) { return std::string(game_path.substr(0, game_path.rfind('.'))); }

std::vector<std::uint8_t> bytes_of(const std::string& text) { return {text.begin(), text.end()}; }

class LevelConverter final : public sh::convert::Converter {
public:
    explicit LevelConverter(Options options) : options_(std::move(options)) {
        if (options_.sibling) {
            catalog_.emplace(options_.sibling);
        }
    }

    std::string_view name() const override { return "level"; }
    bool lossless() const override { return false; }

    bool accepts(std::span<const std::uint8_t> raw, std::string_view game_path) const override {
        return classify(raw, game_path) != Kind::None;
    }

    Result<std::vector<Output>> decode(std::span<const std::uint8_t> raw, std::string_view game_path) const override {
        using Out = Result<std::vector<Output>>;
        const std::string base = "levels/" + without_extension(game_path);
        const TextureCatalog* catalog = catalog_ ? &*catalog_ : nullptr;
        std::vector<Output> outputs;
        switch (classify(raw, game_path)) {
            case Kind::Ipd: {
                auto ipd = parse_ipd(raw);
                if (!ipd) {
                    return Out::fail(std::string(game_path) + ": " + ipd.error());
                }
                const std::string_view area = area_of_ipd(game_path);
                const std::string_view global_path = in_folder(game_path, "BG") ? global_plm_of_area(area) : "";
                const LmFile* global = global_plm(global_path);
                auto cell = cell_to_glb(ipd.value(), game_path, global, global ? global_path : "", catalog);
                if (!cell) {
                    return Out::fail(std::string(game_path) + ": " + cell.error());
                }
                outputs.push_back({base + ".level.json", bytes_of(ipd_to_json(ipd.value(), game_path, cell.value().json_section))});
                outputs.push_back({base + ".glb", std::move(cell.value().glb)});
                if (in_folder(game_path, "BG")) {
                    auto area_glb = area_output(area, game_path, global, global ? global_path : "", catalog);
                    if (!area_glb) {
                        return Out::fail(std::string(game_path) + ": area " + std::string(area) + ": " + area_glb.error());
                    }
                    if (!area_glb.value().relative_path.empty()) {
                        outputs.push_back(std::move(area_glb.value()));
                    }
                }
                break;
            }
            case Kind::Plm: {
                auto plm = parse_plm(raw);
                if (!plm) {
                    return Out::fail(std::string(game_path) + ": " + plm.error());
                }
                outputs.push_back({base + ".level.json", bytes_of(plm_to_json(plm.value(), game_path))});
                auto glb = plm_to_glb(plm.value(), game_path, base + ".glb", catalog);
                if (!glb) {
                    return Out::fail(std::string(game_path) + ": " + glb.error());
                }
                outputs.push_back({base + ".glb", std::move(glb.value())});
                break;
            }
            case Kind::None:
                return Out::fail(std::string(game_path) + ": not a map file the level converter takes");
        }
        return Out::ok(std::move(outputs));
    }

    Result<std::vector<std::uint8_t>> encode(std::span<const Output>) const override {
        return Result<std::vector<std::uint8_t>>::fail("level: encode is not supported, the raw file is the authority");
    }

private:
    // Parsed once and shared; null when there is no reader, no such file or it does not parse.
    const LmFile* global_plm(std::string_view path) const {
        if (path.empty() || !options_.sibling) {
            return nullptr;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = globals_.find(path);
        if (it == globals_.end()) {
            std::optional<LmFile> plm;
            if (const auto bytes = options_.sibling(path)) {
                if (auto parsed = parse_plm(*bytes)) {
                    plm = std::move(parsed.value());
                }
            }
            it = globals_.emplace(std::string(path), std::move(plm)).first;
        }
        return it->second ? &*it->second : nullptr;
    }

    // Cells of an area in name order, found by asking the reader for every possible cell name (the game scans its
    // file table the same way, WorldMap_MakeGrid). Listed once per area.
    std::vector<std::string> area_cells(std::string_view area) const {
        std::lock_guard<std::mutex> lock(cells_mutex_);
        auto it = cells_.find(area);
        if (it == cells_.end()) {
            std::vector<std::string> names;
            for (int x = 0; x < 256; ++x) {
                for (int z = 0; z < 256; ++z) {
                    char name[32];
                    std::snprintf(name, sizeof name, "BG/%.8s%02X%02X.IPD", std::string(area).c_str(), x, z);
                    if (options_.sibling(name)) {
                        names.emplace_back(name);
                    }
                }
            }
            it = cells_.emplace(std::string(area), std::move(names)).first;
        }
        return it->second;
    }

    // The area .glb when `game_path` is the area's first cell; an output with an empty path otherwise.
    Result<Output> area_output(std::string_view area, std::string_view game_path, const LmFile* global,
                               std::string_view global_path, const TextureCatalog* catalog) const {
        using Out = Result<Output>;
        std::vector<std::string> maps;
        for (const MapArea& m : map_areas()) {
            if (m.area == area) {
                maps.emplace_back(m.map);
            }
        }
        if (maps.empty() || !options_.sibling) {
            return Out::ok(Output{});
        }
        const std::vector<std::string> names = area_cells(area);
        if (names.empty() || names.front() != game_path) {
            return Out::ok(Output{});
        }
        std::vector<AreaCell> cells;
        for (const std::string& name : names) {
            const auto bytes = options_.sibling(name);
            if (!bytes) {
                return Out::fail(name + ": cannot be read");
            }
            auto ipd = parse_ipd(*bytes);
            if (!ipd) {
                return Out::fail(name + ": " + ipd.error());
            }
            cells.push_back({name, std::move(ipd.value())});
        }
        auto glb = area_to_glb(area, cells, global, global_path, maps, catalog);
        if (!glb) {
            return Out::fail(glb.error());
        }
        return Out::ok(Output{"levels/areas/" + std::string(area) + ".glb", std::move(glb.value())});
    }

    Options options_;
    std::optional<TextureCatalog> catalog_;
    mutable std::mutex mutex_;
    mutable std::map<std::string, std::optional<LmFile>, std::less<>> globals_;
    mutable std::mutex cells_mutex_;
    mutable std::map<std::string, std::vector<std::string>, std::less<>> cells_;
};

}  // namespace

std::unique_ptr<sh::convert::Converter> make_converter() { return make_converter(Options{}); }

std::unique_ptr<sh::convert::Converter> make_converter(Options options) {
    return std::make_unique<LevelConverter>(std::move(options));
}

}  // namespace sh::convert::level
