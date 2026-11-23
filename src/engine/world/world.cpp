#include "engine/world/world.h"

#include "engine/world/cell_grid.h"

#include <cctype>
#include <cstdio>
#include <span>

namespace sh::world {

namespace {

std::string upper(std::string_view text) {
    std::string out(text);
    for (char& c : out) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return out;
}

}  // namespace

std::string area_of_cell(std::string_view stem) {
    const auto dot = stem.rfind('.');
    if (dot != std::string_view::npos) {
        stem = stem.substr(0, dot);
    }
    const auto slash = stem.find_last_of("/\\");
    if (slash != std::string_view::npos) {
        stem.remove_prefix(slash + 1);
    }
    if (stem.size() <= 4) {
        return {};
    }
    return upper(stem.substr(0, stem.size() - 4));
}

void World::configure(FileReader reader, LogFn log) {
    reader_ = std::move(reader);
    log_ = std::move(log);
    areas_.clear();
    failed_areas_.clear();
    material_areas_.clear();
    placed_.clear();
    stats_ = QueryStats{};
}

void World::log(int level, const std::string& message) const {
    if (log_) {
        log_(level, message);
    }
}

const std::vector<std::array<char, 8>>* World::materials(std::string_view stem) {
    if (!reader_) {
        return nullptr;  // not booted: nothing is cached yet
    }
    std::string name = upper(stem);
    if (const auto dot = name.rfind('.'); dot != std::string::npos) {
        name.resize(dot);
    }
    if (const auto slash = name.find_last_of("/\\"); slash != std::string::npos) {
        name.erase(0, slash + 1);
    }
    // A cell file is named after its area plus four cell digits, a global library "<AREA>_GLB"; any other library
    // is an area file of its own.
    const bool global = name.size() > 4 && name.ends_with("_GLB");
    std::string area = global ? name.substr(0, name.size() - 4) : area_of_cell(name);
    if (area.empty()) {
        area = name;
    }
    auto it = material_areas_.find(area);
    if (it == material_areas_.end()) {
        const std::string relative = "maps/areas/" + area + ".glb";
        std::optional<std::vector<std::uint8_t>> bytes;
        if (reader_) {
            bytes = reader_(relative);
        }
        std::optional<AreaMaterials> lists;
        if (!bytes) {
            log(1, "[WORLD] " + relative + " is missing; its cells have no material lists");
        } else if (Result<AreaMaterials> parsed = read_area_materials(std::span<const std::uint8_t>(*bytes)); !parsed) {
            log(1, "[WORLD] " + relative + ": " + parsed.error());
        } else {
            lists = std::move(parsed.value());
        }
        it = material_areas_.emplace(area, std::move(lists)).first;
    }
    if (!it->second) {
        return nullptr;
    }
    const auto found = it->second->find(name);
    return found != it->second->end() ? &found->second : nullptr;
}

std::shared_ptr<const CellCollision> World::cell(std::string_view stem) {
    std::string name = upper(stem);
    const auto dot = name.rfind('.');
    if (dot != std::string::npos) {
        name.resize(dot);
    }
    const auto slash = name.find_last_of("/\\");
    if (slash != std::string::npos) {
        name.erase(0, slash + 1);
    }
    const std::string area = area_of_cell(name);
    if (area.empty()) {
        return nullptr;
    }
    auto it = areas_.find(area);
    if (it == areas_.end()) {
        if (failed_areas_.count(area) != 0) {
            return nullptr;
        }
        const std::string relative = "maps/areas/" + area + ".glb";
        std::optional<std::vector<std::uint8_t>> bytes;
        if (reader_) {
            bytes = reader_(relative);
        }
        if (!bytes) {
            log(1, "[WORLD] " + relative + " is missing; the cells of " + area + " have no native collision");
            failed_areas_[area] = true;
            return nullptr;
        }
        Result<AreaCollision> parsed = read_area_collision(std::span<const std::uint8_t>(*bytes));
        if (!parsed) {
            log(1, "[WORLD] " + relative + ": " + parsed.error());
            failed_areas_[area] = true;
            return nullptr;
        }
        log(0, "[WORLD] " + relative + ": " + std::to_string(parsed.value().size()) + " cells with collision");
        it = areas_.emplace(area, std::move(parsed.value())).first;
    }
    const auto found = it->second.find(name);
    return found != it->second.end() ? found->second : nullptr;
}

const CellCollision* World::place(std::size_t slot, std::string_view stem, std::int32_t cell_x, std::int32_t cell_z) {
    std::shared_ptr<const CellCollision> c = cell(stem);
    if (!c) {
        placed_.erase(slot);
        ++stats_.cells_missing;
        return nullptr;
    }
    std::int32_t shift_x = 0;
    std::int32_t shift_z = 0;
    if (stem.size() >= 4) {
        const std::string_view digits = stem.substr(stem.size() - 4);
        const std::optional<std::int32_t> file_x = parse_cell_digits(digits[0], digits[1]);
        const std::optional<std::int32_t> file_z = parse_cell_digits(digits[2], digits[3]);
        if (file_x && file_z) {
            shift_x = (cell_x - *file_x) * kCellSizeGeometry;
            shift_z = (cell_z - *file_z) * kCellSizeGeometry;
        }
    }
    const CellCollision* placed = place(slot, std::move(c));
    Placement& p = placed_[slot];
    p.origin_x += shift_x;
    p.origin_z += shift_z;
    return placed;
}

const CellCollision* World::place(std::size_t slot, std::shared_ptr<const CellCollision> c) {
    if (!c) {
        placed_.erase(slot);
        return nullptr;
    }
    ++stats_.cells_attached;
    Placement& p = placed_[slot];
    p.origin_x = c->origin_x;
    p.origin_z = c->origin_z;
    p.pass = c->stored_pass;
    p.marks = c->stored_marks;
    p.cell = std::move(c);
    return p.cell.get();
}

void World::unplace(std::size_t slot) {
    placed_.erase(slot);
}

const CellCollision* World::placed(std::size_t slot) const {
    const auto it = placed_.find(slot);
    return it != placed_.end() ? it->second.cell.get() : nullptr;
}

Placement* World::placement(std::size_t slot) {
    const auto it = placed_.find(slot);
    return it != placed_.end() ? &it->second : nullptr;
}

std::string World::stats_line() const {
    const QueryStats& s = stats_;
    return "[WORLD] cells attached " + std::to_string(s.cells_attached) + ", missing " +
           std::to_string(s.cells_missing) + "; ground queries " + std::to_string(s.ground_queries) + "; rays " +
           std::to_string(s.ray_queries) + "; moves " + std::to_string(s.move_queries);
}

World& world() {
    static World instance;
    return instance;
}

FileReader path_file_reader(PathFinder find) {
    return [find](std::string_view relative) -> std::optional<std::vector<std::uint8_t>> {
        char path[1024];
        if (find == nullptr || !find(std::string(relative).c_str(), path, sizeof(path))) {
            return std::nullopt;
        }
        std::FILE* f = std::fopen(path, "rb");
        if (f == nullptr) {
            return std::nullopt;
        }
        std::vector<std::uint8_t> bytes;
        std::uint8_t chunk[65536];
        std::size_t n;
        while ((n = std::fread(chunk, 1, sizeof(chunk), f)) > 0) {
            bytes.insert(bytes.end(), chunk, chunk + n);
        }
        std::fclose(f);
        return bytes;
    };
}

void World::boot(FileReader reader, LogFn log) {
    if (booted_) {
        return;
    }
    booted_ = true;
    configure(std::move(reader), std::move(log));
    this->log(0, "[WORLD] the world module answers the collision queries");
}

void World::log_stats_on_growth() {
    const QueryStats& s = stats_;
    if (s.ground_queries + s.ray_queries + s.move_queries >= next_stats_at_) {
        next_stats_at_ *= 4;
        log(0, stats_line());
    }
}

}  // namespace sh::world
