#pragma once

// The world module: map geometry and collision from the unified map files
// (maps/areas/<AREA>.glb) and the queries game logic asks of them. Engine types only; the game code is fed from
// a bridge outside this module.

#include "engine/world/area_collision.h"
#include "engine/world/cell_collision.h"
#include "engine/world/ground_query.h"
#include "engine/world/ray_query.h"

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sh::world {

// Reads a gamedata file by relative path ("maps/areas/ER.glb"), mods first. Empty when there is none.
using FileReader = std::function<std::optional<std::vector<std::uint8_t>>(std::string_view relative)>;
// level 0: info, 1: warning.
using LogFn = std::function<void(int level, const std::string& message)>;

struct QueryStats {
    std::uint64_t ground_queries = 0;      // answered by sample_ground
    std::uint64_t ray_queries = 0;         // answered by trace_ray
    std::uint64_t move_queries = 0;        // answered by move_cylinder
    std::uint64_t cells_attached = 0;
    std::uint64_t cells_missing = 0;       // a cell the game loaded that no area file has
};

// A cell placed in a slot of the streamed window, with where its origin lies and its visit ledger: every pass over the
// cell (ground, rays, movement) stamps the ledger and movement raises stamps to skip what blocked it (ground_query.h,
// VisitLedger).
struct Placement {
    std::shared_ptr<const CellCollision> cell;
    std::int32_t origin_x = 0;  // Q8: the stored origin moved by the cells between the file's cell and the placement
    std::int32_t origin_z = 0;
    std::uint8_t pass = 0;
    std::array<std::uint8_t, 256> marks{};

    VisitLedger ledger() { return VisitLedger{&pass, std::span<std::uint8_t, 256>(marks)}; }
};

// Reads the gamedata files that `find` locates (relative path -> absolute path; false when there is none).
using PathFinder = int (*)(const char* relative, char* out, std::size_t out_size);
FileReader path_file_reader(PathFinder find);

class World {
public:
    void configure(FileReader reader, LogFn log);

    // The first start of the module: configures it and logs that it started. Later calls do nothing.
    void boot(FileReader reader, LogFn log);
    bool booted() const { return booted_; }

    // After a query: logs the stats line whenever the total of queries reaches the next step (1024, then x4).
    void log_stats_on_growth();

    // The visit ledger of the ground queries that find no cell.
    VisitLedger no_cell_ledger() { return VisitLedger{&no_cell_pass_, std::span<std::uint8_t, 256>(no_cell_marks_)}; }

    // Collision of a cell by file stem ("ER0402", any case). Loads the cell's area on first use; null (logged once
    // per area) when no area file has it.
    std::shared_ptr<const CellCollision> cell(std::string_view stem);

    // Texture names of a cell or library file by stem ("ER0402", "ER_GLB"; any case, path and extension ignored),
    // from its area file; null when the area file has no list for it. Loads the area's lists on first use.
    const std::vector<std::array<char, 8>>* materials(std::string_view stem);
    // The streamed window loaded the cell file `stem` into `slot` at grid cell (cell_x, cell_z). A cell file names the
    // cell it was made for ("THR05FD" -> (5, -3)); placed elsewhere (an event can put it anywhere) its origin moves by
    // 40 m per cell. A later placement in the same slot replaces it and starts its ledger from the cell's stored one.
    // Null, and the slot emptied, when no area file has the cell.
    const CellCollision* place(std::size_t slot, std::string_view stem, std::int32_t cell_x, std::int32_t cell_z);
    // The same with a cell built elsewhere (tests), its origin as stored.
    const CellCollision* place(std::size_t slot, std::shared_ptr<const CellCollision> cell);
    void unplace(std::size_t slot);
    const CellCollision* placed(std::size_t slot) const;
    Placement* placement(std::size_t slot);

    QueryStats& stats() { return stats_; }
    const QueryStats& stats() const { return stats_; }
    std::string stats_line() const;

    void log(int level, const std::string& message) const;

private:
    FileReader reader_;
    LogFn log_;
    std::map<std::string, AreaCollision> areas_;       // loaded areas by name
    std::map<std::string, bool> failed_areas_;
    std::map<std::string, std::optional<AreaMaterials>> material_areas_;  // nullopt: the area file is missing or bad
    std::map<std::size_t, Placement> placed_;
    QueryStats stats_;
    bool booted_ = false;
    std::uint64_t next_stats_at_ = 1024;
    std::uint8_t no_cell_pass_ = 0;
    std::array<std::uint8_t, 256> no_cell_marks_{};
};

// The process-wide world.
World& world();

// "ER0402" -> "ER": the area of a cell file stem (the stem without its four hex digits).
std::string area_of_cell(std::string_view stem);

}  // namespace sh::world
