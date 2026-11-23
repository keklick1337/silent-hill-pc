// The world module (engine/world): the game's integer math, reading SH_collision, and the ground query on
// synthetic cells; the unified map files of a real import when SH_TEST_GAMEDATA points at one.

#include "engine/world/area_collision.h"
#include "engine/world/fixed_math.h"
#include "engine/world/fixed_trig.h"
#include "engine/world/ground_query.h"
#include "engine/world/move_query.h"
#include "engine/world/mover_setup.h"
#include "engine/world/ray_query.h"
#include "engine/world/sweep.h"
#include "engine/world/world.h"
#include "framework/sh_test.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace fs = std::filesystem;
namespace w = sh::world;

SH_TEST(world_game_sqrt) {
    SH_CHECK(w::game_sqrt(0) == 0);
    SH_CHECK(w::game_sqrt(-5) == 0);
    SH_CHECK(w::game_sqrt(1) == 1);
    SH_CHECK(w::game_sqrt(4) == 2);
    SH_CHECK(w::game_sqrt(65536) == 256);
    SH_CHECK(w::game_sqrt(4096 * 4096) == 4096);
    // The mantissa table is rounded down: large non-squares come out low, never high.
    for (std::int32_t v : {2, 3, 1000, 123456, 99999999, 2147395600}) {
        const std::int64_t r = w::game_sqrt(v);
        SH_CHECK(r * r <= v);
    }
    SH_CHECK(w::planar_length(3, 4) == 5);
}

SH_TEST(world_edge_frame) {
    // Along +x: the perpendicular row of the original's matrix is (-1, 4096), not (0, 4096).
    w::EdgeFrame f = w::to_edge_frame(4096, 0, 100, 50);
    SH_CHECK(f.along == 100);
    SH_CHECK(f.across == 49);
    // Along +z with dir_x = 0 there is no borrow.
    f = w::to_edge_frame(0, 4096, 100, 50);
    SH_CHECK(f.along == 50);
    SH_CHECK(f.across == -100);
    // Inputs are 16-bit: 70000 wraps to 4464.
    f = w::to_edge_frame(4096, 0, 70000, 0);
    SH_CHECK(f.along == 4464);
}

// A 2x1 grid of 512-unit squares at origin (1024, 2048): floor 0 (height -100, type 3) on the left of an edge along
// +z at x = 512, floor 1 (height -300 rising along x, type 5) on its right; square 1 also lists floor 1 directly.
std::string synthetic_cell_json() {
    return R"({"position_x": 1024, "position_z": 2048, "subcell_size": 512, "subcell_count_x": 2,
        "subcell_count_z": 1, "surface_lut_size": 1, "subcell_check_count": 0, "pad_31": "000000",
        "subcell_check_idx": ")" + std::string(512, '0') + R"(",
        "split_vertices": {"offset": 0, "count": 2, "fields": ["x","y","z"], "items": [[512, -100, 0], [512, -100, 512]]},
        "surfaces": {"offset": 0, "count": 2, "fields": [], "items": [[256, -100, 256, 3, 0, 0], [768, -300, 256, 5, 409, 0]]},
        "subcells": {"offset": 0, "count": 1, "fields": [], "items": [[0, 4096, 512, 1, 0, 0, 1]]},
        "elements18": {"offset": 0, "count": 0, "fields": [], "items": []},
        "ranges": {"offset": 0, "count": 3, "fields": [], "items": [[0, 0], [1, 0], [2, 1]]},
        "index_pool": {"offset": 0, "items": [0, 0]},
        "table_2c": {"offset": 0, "items": [1]}})";
}

SH_TEST(world_parse_cell) {
    sh::Result<w::CellCollision> cell = w::parse_cell_collision(synthetic_cell_json(), "TST0000");
    SH_CHECK(cell);
    if (!cell) {
        return;
    }
    const w::CellCollision& c = cell.value();
    SH_CHECK(c.origin_x == 1024 && c.origin_z == 2048 && c.columns == 2 && c.rows == 1);
    SH_CHECK(c.edges.size() == 1 && c.edges[0].dir_z == 4096 && c.edges[0].dir_x == 0);
    SH_CHECK(c.edges[0].start_vertex == 0 && c.edges[0].end_vertex == 1);
    SH_CHECK(c.floors[1].ground_type == 5 && c.floors[1].slope_x == 409);

    // A table entry that points nowhere is rejected with the cell and the reason.
    std::string bad = synthetic_cell_json();
    bad.replace(bad.find("[0, 4096, 512, 1, 0, 0, 1]"), std::string("[0, 4096, 512, 1, 0, 0, 1]").size(), "[0, 4096, 512, 9, 0, 0, 1]");
    sh::Result<w::CellCollision> broken = w::parse_cell_collision(bad, "TST0000");
    SH_CHECK(!broken);
    SH_CHECK(!broken && broken.error().find("TST0000") != std::string::npos);
}

SH_TEST(world_ground_query) {
    sh::Result<w::CellCollision> cell = w::parse_cell_collision(synthetic_cell_json(), "TST0000");
    SH_CHECK(cell);
    if (!cell) {
        return;
    }
    const w::PlacedCell placed{&cell.value(), 1024, 2048};
    std::uint8_t pass = 0;
    std::array<std::uint8_t, 256> marks{};
    const w::VisitLedger ledger{&pass, std::span<std::uint8_t, 256>(marks)};
    auto at = [](std::int32_t local_q8) { return local_q8 << 4; };

    // Square 0, left of the edge: floor 0. The edge's left side is the one where `across` >= 0, which for an edge
    // along +z is x below it.
    w::GroundSample s = w::sample_ground(&placed, ledger, at(1024 + 200), at(2048 + 100), 0xFFFF);
    SH_CHECK(s.height == -100 * 16 && s.ground_type == 3 && s.slope_x == 0);
    SH_CHECK(pass == 2 && marks[0] == 2);

    // Square 1: the slot gives floor 1 (right of the edge), the square lists floor 1 too; sloped height.
    s = w::sample_ground(&placed, ledger, at(1024 + 700), at(2048 + 100), 0xFFFF);
    SH_CHECK(s.ground_type == 5 && s.slope_x == 409);
    SH_CHECK(s.height == (-300 + ((409 * (700 - 768)) >> 12)) * 16);

    // The edge's group bit off: square 0 has no ground any more, square 1 still its listed floor.
    s = w::sample_ground(&placed, ledger, at(1024 + 200), at(2048 + 100), 0xFFFE);
    SH_CHECK(s.height == 8 * 256 * 16 && s.ground_type == 0);
    // Floor 1 is in group 0 as well, so the square list does not give it either.
    s = w::sample_ground(&placed, ledger, at(1024 + 700), at(2048 + 100), 0xFFFE);
    SH_CHECK(s.height == 8 * 256 * 16);

    // Outside the grid, and without a cell.
    s = w::sample_ground(&placed, ledger, at(1024 - 10), at(2048 + 100), 0xFFFF);
    SH_CHECK(s.height == 8 * 256 * 16 && s.ground_type == 0);
    s = w::sample_ground(nullptr, ledger, 0, 0, 0xFFFF);
    SH_CHECK(s.height == w::no_ground().height);

    // A stamp above the pass (movement raised it) hides the edge from the next pass.
    pass = 10;
    marks.fill(0);
    marks[0] = 13;
    s = w::sample_ground(&placed, ledger, at(1024 + 200), at(2048 + 100), 0xFFFF);
    SH_CHECK(s.height == 8 * 256 * 16);
    SH_CHECK(pass == 12 && marks[0] == 13);

    // The pass counter restarts after 252 and clears the stamps.
    pass = 252;
    marks.fill(200);
    s = w::sample_ground(&placed, ledger, at(1024 + 200), at(2048 + 100), 0xFFFF);
    SH_CHECK(s.ground_type == 3);
    SH_CHECK(pass == 1 && marks[0] == 1 && marks[5] == 0);
}

// The synthetic cell with a wall: floor 0 only on the left of the edge at x = 512, nothing on its right.
w::CellCollision wall_cell() {
    std::string json = synthetic_cell_json();
    const std::string edge = "[0, 4096, 512, 1, 0, 0, 1]";
    json.replace(json.find(edge), edge.size(), "[0, 4096, 512, 1, 0, 0, 255]");
    sh::Result<w::CellCollision> cell = w::parse_cell_collision(json, "TST0001");
    return cell ? cell.value() : w::CellCollision{};
}

SH_TEST(world_sweep_circle) {
    // Moving +x for 100 units, a point 50 ahead and 10 aside, radius 20: contact before the point.
    const std::int16_t t = w::sweep_circle_to_point(4096, 0, 100, 50, 10, 20);
    SH_CHECK(t > 0 && t < 50 * 4096 / 100);
    SH_CHECK(w::sweep_circle_to_point(4096, 0, 100, 50, 30, 20) == w::kNoSweepHit);
    SH_CHECK(w::sweep_circle_to_point(4096, 0, 0, 5, 0, 20) == 0);
    SH_CHECK(w::scale_q12_16(2048, 1000) == 500);
    SH_CHECK(w::scale_q12_16(4096 * 4, 0x7000) == 0x7FFF);  // saturates like the vector unit
}

SH_TEST(world_ray_query) {
    const w::CellCollision cell = wall_cell();
    SH_CHECK(cell.edges.size() == 1 && cell.edges[0].floor_right == 255 && cell.floor_pool_size_field == 1);
    std::uint8_t pass = 0;
    std::array<std::uint8_t, 256> marks{};
    const w::RayCell rc{w::PlacedCell{&cell, 1024, 2048}, w::VisitLedger{&pass, std::span<std::uint8_t, 256>(marks)}};
    w::RayRequest r;
    r.from = w::Vec3{(1024 + 200) << 4, 0, (2048 + 100) << 4};
    r.offset = w::Vec3{600 << 4, 0, 0};
    r.enabled_groups = 0xFFFF;
    w::RayHit hit = w::trace_ray(r, std::span<const w::RayCell>(&rc, 1), {});
    SH_CHECK(hit.hit && hit.target == -1 && hit.ground_type == 3);
    // The game's table square root is slightly low, so the unit direction is slightly long: 312 reads as 314.
    SH_CHECK(hit.point.x == (1024 + 512) << 4 && hit.distance >= 312 << 4 && hit.distance <= 316 << 4);
    SH_CHECK(pass == 1 && marks[0] == 2);

    // Sight: seen from the floor's side the wall is no obstacle; a character behind it is hit.
    r.mode = w::RayMode::Sight;
    const w::RayTarget target{1024 + 450, 2048 + 100, -400, 400, 30, 7};
    hit = w::trace_ray(r, std::span<const w::RayCell>(&rc, 1), std::span<const w::RayTarget>(&target, 1));
    SH_CHECK(hit.hit && hit.target == 7 && hit.distance >= 218 << 4 && hit.distance <= 224 << 4);

    // No horizontal extent: the ray is rejected.
    r.offset = w::Vec3{0, 4096, 0};
    hit = w::trace_ray(r, std::span<const w::RayCell>(&rc, 1), {});
    SH_CHECK(!hit.hit && hit.rejected);
}

SH_TEST(world_move_query) {
    // The queries run on the engine's own fixed-point trigonometry.
    const w::CellCollision cell = wall_cell();
    std::uint8_t pass = 0;
    std::array<std::uint8_t, 256> marks{};
    const w::ActiveCell ac{w::PlacedCell{&cell, 1024, 2048},
                           w::VisitLedger{&pass, std::span<std::uint8_t, 256>(marks)}};
    const std::span<const w::ActiveCell> cells(&ac, 1);
    w::MoveCylinder cyl;
    cyl.position = w::Vec3{(1024 + 620) << 4, 0, (2048 + 256) << 4};
    cyl.bottom = 0;
    cyl.top = -6000;
    cyl.radius = 819;  // 0.2 units, 51 in Q.8
    cyl.collision_state = 1;
    const w::MoveOptions options;

    // The wall face is on the side without a floor (x > 512); walking from there into it, the move ends at the
    // wall. The ground is square 1's own floor.
    w::MoveResult m = w::move_cylinder(cyl, w::Vec3{-(200 << 4), 0, 0}, cells, {}, {}, {}, 0xFFFF, options);
    SH_CHECK(m.code == 1);
    SH_CHECK(m.offset.x < 0 && (620 << 4) + m.offset.x >= (512 + 51 - 2) << 4);
    SH_CHECK(m.ground_type == 5);

    // Walking away from it: unhindered.
    m = w::move_cylinder(cyl, w::Vec3{100 << 4, 0, 0}, cells, {}, {}, {}, 0xFFFF, options);
    SH_CHECK(m.code == 0 && m.offset.x == 100 << 4);

    // Ceiling boxes: a box counts only when it is lower than the default ceiling (-16 units), which no trigger height
    // (-1.5 .. -5 units) is, so the original never clamps; the push off a box's edge still applies.
    const w::CeilingBox box{(1024 + 300) << 4, (1024 + 500) << 4, (2048 + 200) << 4, (2048 + 300) << 4, -8192};
    w::Vec3 up{0, -4096, 0};
    const std::int32_t ceiling = w::ceiling_clamp(up, cyl, std::span<const w::CeilingBox>(&box, 1));
    SH_CHECK(ceiling == -16 * 4096 && up.y == -4096);

    // A collision state that ignores the world passes straight through.
    cyl.collision_state = 5;
    m = w::move_cylinder(cyl, w::Vec3{200 << 4, 0, 0}, cells, {}, {}, {}, 0xFFFF, options);
    SH_CHECK(m.code == 0 && m.offset.x == 200 << 4);
}

// A switchable part of a cell (a pushed cabinet's first place, a gate) is an edge of a group above 0: it blocks
// only while that group's bit is in the mask. A map script that means "objects" must therefore pass 2, not 1:
// bit 0 is the map geometry and is always on.
SH_TEST(world_move_group_polarity) {
    w::CellCollision cell = wall_cell();
    SH_CHECK(cell.edges.size() == 1);
    if (cell.edges.size() != 1) {
        return;
    }
    cell.edges[0].group_low = 1;
    std::uint8_t pass = 0;
    std::array<std::uint8_t, 256> marks{};
    const w::ActiveCell ac{w::PlacedCell{&cell, 1024, 2048},
                           w::VisitLedger{&pass, std::span<std::uint8_t, 256>(marks)}};
    const std::span<const w::ActiveCell> cells(&ac, 1);
    w::MoveCylinder cyl;
    cyl.position = w::Vec3{(1024 + 620) << 4, 0, (2048 + 256) << 4};
    cyl.bottom = 0;
    cyl.top = -6000;
    cyl.radius = 819;
    cyl.collision_state = 1;
    const w::Vec3 into_wall{-(200 << 4), 0, 0};
    const w::MoveOptions options;

    // Only the map geometry on (what the mask is after every clear): the wall is not there.
    w::MoveResult m = w::move_cylinder(cyl, into_wall, cells, {}, {}, {}, w::clear_groups(0xFFFF, 0xFFFF), options);
    SH_CHECK(m.code == 0 && m.offset.x == into_wall.x);
    // Setting bit 0 again changes nothing.
    m = w::move_cylinder(cyl, into_wall, cells, {}, {}, {}, w::kMapGeometryGroup | 1, options);
    SH_CHECK(m.code == 0 && m.offset.x == into_wall.x);
    // The group's own bit set: the wall stops the mover.
    m = w::move_cylinder(cyl, into_wall, cells, {}, {}, {}, w::kMapGeometryGroup | 2, options);
    SH_CHECK(m.code == 1 && m.offset.x > into_wall.x);
    // Another group's bit does not switch it on.
    m = w::move_cylinder(cyl, into_wall, cells, {}, {}, {}, w::kMapGeometryGroup | 4, options);
    SH_CHECK(m.code == 0 && m.offset.x == into_wall.x);
}

// The observer of a pass (the collision view's tap) is told what stopped the mover and changes nothing: the answer
// and the visit ledger are the ones of an unobserved pass.
SH_TEST(world_move_observer) {
    const w::CellCollision cell = wall_cell();
    w::MoveCylinder cyl;
    cyl.position = w::Vec3{(1024 + 620) << 4, 0, (2048 + 256) << 4};
    cyl.top = -6000;
    cyl.radius = 819;
    cyl.collision_state = 1;
    const w::Vec3 into_wall{-(200 << 4), 0, 0};

    struct Seen {
        int stopped = 0, others = 0, rounds = 0;
        std::uint8_t edge = 255;
        w::EdgeTop top;
        std::int32_t normal_x = 0;
    };
    const auto run = [&](const w::MoveObserver* observer, std::uint8_t& pass, std::array<std::uint8_t, 256>& marks) {
        const w::ActiveCell ac{w::PlacedCell{&cell, 1024, 2048},
                               w::VisitLedger{&pass, std::span<std::uint8_t, 256>(marks)}};
        w::MoveOptions options;
        options.observer = observer;
        return w::move_cylinder(cyl, into_wall, std::span<const w::ActiveCell>(&ac, 1), {}, {}, {}, 0xFFFF, options);
    };

    std::uint8_t plain_pass = 0, seen_pass = 0;
    std::array<std::uint8_t, 256> plain_marks{}, seen_marks{};
    const w::MoveResult plain = run(nullptr, plain_pass, plain_marks);

    Seen seen;
    w::MoveObserver observer;
    observer.user = &seen;
    observer.wall = [](void* user, const w::ActiveCell&, std::uint8_t edge, const w::EdgeTop& top, w::MoveTouch touch) {
        auto& s = *static_cast<Seen*>(user);
        if (touch == w::MoveTouch::Stopped) {
            ++s.stopped;
            s.edge = edge;
            s.top = top;
        } else {
            ++s.others;
        }
    };
    observer.round = [](void* user, int round, std::int32_t, std::int32_t, w::Vec3, w::Vec3, std::int32_t nx,
                        std::int32_t) {
        auto& s = *static_cast<Seen*>(user);
        ++s.rounds;
        if (round == 0) {
            s.normal_x = nx;
        }
    };
    const w::MoveResult watched = run(&observer, seen_pass, seen_marks);

    SH_CHECK(watched.code == plain.code && watched.offset.x == plain.offset.x && watched.offset.z == plain.offset.z);
    SH_CHECK(watched.ground_height == plain.ground_height && watched.ground_type == plain.ground_type);
    SH_CHECK(seen_pass == plain_pass && seen_marks == plain_marks);
    // The wall stopped the first round; its normal points back at the mover (+x), and the second round ran.
    SH_CHECK(seen.stopped == 1 && seen.edge < cell.edges.size() && seen.rounds == 2 && seen.normal_x > 0);
    // The top the pass tested is the rule the view draws walls with.
    const w::EdgeTop top = w::edge_top(cell, cell.edges[seen.edge], false);
    SH_CHECK(top.start.y + top.end.y == seen.top.start.y + seen.top.end.y);
    const std::int32_t highest = top.start.y < top.end.y ? top.start.y : top.end.y;
    SH_CHECK(w::wall_reaches(top, 0) && !w::wall_reaches(top, highest - 1));
}

// A unified map file of one area "TST" with the synthetic cell as TST0102 and two lamp glows on its root.
std::vector<std::uint8_t> synthetic_area_glb() {
    std::string json = R"({"asset": {"version": "2.0"}, "nodes": [
        {"name": "TST0102", "extras": {"source": "BG/TST0102.IPD", "glows": [[10, -20, 30, 0], [40, -50, 60, 257]]}},
        {"name": "TST0102/collision", "extensions": {"SH_collision": )" + synthetic_cell_json() + "}}]}";
    while (json.size() % 4 != 0) {
        json += ' ';
    }
    std::vector<std::uint8_t> glb;
    auto u32 = [&glb](std::uint32_t v) {
        for (int i = 0; i < 4; ++i) {
            glb.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
        }
    };
    glb.insert(glb.end(), {'g', 'l', 'T', 'F'});
    u32(2);
    u32(static_cast<std::uint32_t>(20 + json.size()));
    u32(static_cast<std::uint32_t>(json.size()));
    u32(0x4E4F534Au);
    glb.insert(glb.end(), json.begin(), json.end());
    return glb;
}

SH_TEST(world_area_glows) {
    sh::Result<w::AreaCollision> area = w::read_area_collision(synthetic_area_glb());
    SH_CHECK(area);
    if (!area) {
        return;
    }
    const auto it = area.value().find("TST0102");
    SH_CHECK(it != area.value().end());
    if (it == area.value().end()) {
        return;
    }
    const std::vector<w::CellCollision::Glow>& glows = it->second->glows;
    SH_CHECK(glows.size() == 2);
    SH_CHECK(glows.size() == 2 && glows[0].position.x == 10 && glows[0].position.y == -20 && glows[0].kind == 0);
    SH_CHECK(glows.size() == 2 && glows[1].position.z == 60 && glows[1].kind == 257);
}

SH_TEST(world_place_cells_in_slots) {
    w::World world;
    const std::vector<std::uint8_t> glb = synthetic_area_glb();
    world.configure([&glb](std::string_view relative) -> std::optional<std::vector<std::uint8_t>> {
                        if (relative == "maps/areas/TST.glb") {
                            return glb;
                        }
                        return std::nullopt;
                    },
                    nullptr);
    // At the cell its name gives (1, 2): the stored origin.
    const w::CellCollision* cell = world.place(3, "TST0102.IPD", 1, 2);
    SH_CHECK(cell != nullptr && world.placed(3) == cell);
    w::Placement* p = world.placement(3);
    SH_CHECK(p != nullptr && p->origin_x == 1024 && p->origin_z == 2048);
    // Placed one cell east and two north (an event moved the file): the origin moves 40 m per cell.
    world.place(5, "tst0102", 2, 0);
    p = world.placement(5);
    SH_CHECK(p != nullptr && p->origin_x == 1024 + 10240 && p->origin_z == 2048 - 2 * 10240);
    // A new placement in a slot restarts its ledger from the stored one.
    p = world.placement(3);
    p->pass = 7;
    p->marks[4] = 9;
    world.place(3, "TST0102", 1, 2);
    p = world.placement(3);
    SH_CHECK(p != nullptr && p->pass == 0 && p->marks[4] == 0);
    // A cell no area file has empties the slot.
    SH_CHECK(world.place(3, "XYZ0000", 0, 0) == nullptr);
    SH_CHECK(world.placed(3) == nullptr && world.placement(3) == nullptr);
    SH_CHECK(world.stats().cells_missing == 1);
    world.unplace(5);
    SH_CHECK(world.placed(5) == nullptr);
}

SH_TEST(world_area_files_of_import) {
    const char* root = std::getenv("SH_TEST_GAMEDATA");
    if (root == nullptr || *root == '\0') {
        SH_SKIP("SH_TEST_GAMEDATA is not set");
    }
    const fs::path areas = fs::path(root) / "maps" / "areas";
    std::error_code ec;
    if (!fs::is_directory(areas, ec)) {
        SH_SKIP("no maps/areas in SH_TEST_GAMEDATA");
    }
    std::size_t cells = 0;
    for (const fs::directory_entry& e : fs::directory_iterator(areas)) {
        if (e.path().extension() != ".glb") {
            continue;
        }
        std::ifstream in(e.path(), std::ios::binary);
        const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        sh::Result<w::AreaCollision> area = w::read_area_collision(bytes);
        SH_CHECK(area);
        if (!area) {
            std::fprintf(stderr, "%s: %s\n", e.path().string().c_str(), area.error().c_str());
            continue;
        }
        for (const auto& [name, cell] : area.value()) {
            SH_CHECK(w::area_of_cell(name) == e.path().stem().string());
        }
        cells += area.value().size();
    }
    // 493 cells on the USA 1.1 disc carry collision.
    SH_CHECK(cells == 493);
}

}  // namespace
