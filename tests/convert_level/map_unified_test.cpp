// The unified map file (formats/convert/level/map_unified.h) and the engine's layer over it
// (engine/maps/unified_layer.h): synthetic round trips and the real image.

#include "engine/maps/unified_layer.h"
#include "formats/common/byte_source.h"
#include "formats/convert/level/level_gltf.h"
#include "formats/convert/level/level_parse.h"
#include "formats/convert/level/map_unified.h"
#include "formats/convert/level/map_unified_files.h"
#include "formats/gltf/gltf_inspect.h"
#include "framework/sh_test.h"
#include "sh-import/run.h"

#include <algorithm>
#include <climits>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#ifndef SH_PROJECT_ROOT
#define SH_PROJECT_ROOT "../.."
#endif

namespace {

namespace fs = std::filesystem;
namespace lvl = sh::convert::level;

// ---- synthetic LM (the same as level_test.cpp) ----------------------------------------------------------------------

void put16(std::vector<std::uint8_t>& b, std::size_t at, std::uint32_t v) {
    b[at] = static_cast<std::uint8_t>(v);
    b[at + 1] = static_cast<std::uint8_t>(v >> 8);
}

void put32(std::vector<std::uint8_t>& b, std::size_t at, std::uint32_t v) {
    put16(b, at, v & 0xFFFF);
    put16(b, at + 2, v >> 16);
}

// One material, one model "BOX" (vertex window starts at 2), one mesh with one quad, 4 vertices, 2 normals.
// Layout: header 0, material 0x14, model 0x2C, order 0x3C, mesh 0x40, prim 0x58, xy 0x6C, z 0x7C, normals 0x84, shade 0x8C.
std::vector<std::uint8_t> make_plm() {
    std::vector<std::uint8_t> b(0x90, 0);
    b[0] = 0x30;
    b[1] = 6;
    b[3] = 1;
    put32(b, 4, 0x14);
    b[8] = 1;
    put32(b, 12, 0x2C);
    put32(b, 16, 0x3C);
    std::memcpy(&b[0x14], "SHEET", 5);
    put16(b, 0x14 + 0x10, 0x1234);
    std::memcpy(&b[0x2C], "BOX", 3);
    b[0x2C + 8] = 1;   // meshes
    b[0x2C + 9] = 2;   // vertex window start
    b[0x2C + 10] = 0;  // normal window start
    put32(b, 0x2C + 12, 0x40);
    b[0x3C] = 0;
    b[0x40] = 1;  // primitives
    b[0x41] = 4;  // vertices
    b[0x42] = 2;  // normals
    b[0x43] = 4;  // shade entries
    put32(b, 0x40 + 4, 0x58);
    put32(b, 0x40 + 8, 0x6C);
    put32(b, 0x40 + 12, 0x7C);
    put32(b, 0x40 + 16, 0x84);
    put32(b, 0x40 + 20, 0x8C);
    // quad: uv corners, clut 0x0140, flags: material 0, transparent
    const std::uint8_t uv[4][2] = {{0, 0}, {255, 0}, {0, 255}, {255, 255}};
    const std::size_t uv_at[4] = {0, 4, 8, 10};
    for (int c = 0; c < 4; ++c) {
        b[0x58 + uv_at[c]] = uv[c][0];
        b[0x58 + uv_at[c] + 1] = uv[c][1];
    }
    put16(b, 0x58 + 2, 0x0140);
    put16(b, 0x58 + 6, 0x8000);
    const std::uint8_t verts[4] = {2, 3, 4, 5};
    const std::uint8_t slots[4] = {0, 1, 2, 3};
    for (int c = 0; c < 4; ++c) {
        b[0x58 + 12 + c] = verts[c];
        b[0x58 + 16 + c] = slots[c];
    }
    // vertices (x, y) then z; a unit square in the XZ plane at y = 0, Q7.8
    const std::int16_t xy[4][2] = {{0, 0}, {256, 0}, {0, 0}, {256, 0}};
    const std::int16_t z[4] = {0, 0, 256, 256};
    for (int v = 0; v < 4; ++v) {
        put16(b, 0x6C + 4 * v, static_cast<std::uint16_t>(xy[v][0]));
        put16(b, 0x6C + 4 * v + 2, static_cast<std::uint16_t>(xy[v][1]));
        put16(b, 0x7C + 2 * v, static_cast<std::uint16_t>(z[v]));
    }
    // normal 0 covers shade slots 0..1 (vertices 2, 3), normal 1 covers 2..3 (vertices 4, 5)
    const std::int8_t normals[2][4] = {{0, 127, 0, 2}, {0, 127, 0, 2}};
    for (int n = 0; n < 2; ++n) {
        for (int k = 0; k < 4; ++k) {
            b[0x84 + 4 * n + k] = static_cast<std::uint8_t>(normals[n][k]);
        }
    }
    for (int s = 0; s < 4; ++s) {
        b[0x8C + s] = verts[s];
    }
    return b;
}

fs::path scratch(const std::string& name) {
    const fs::path dir = fs::path(SH_PROJECT_ROOT) / ".tmp" / "map-unified" / "tests" / name;
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    return dir;
}

void write_bytes(const fs::path& path, const std::vector<std::uint8_t>& bytes) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(bytes.data()),
                                                 static_cast<std::streamsize>(bytes.size()));
}

std::vector<std::uint8_t> read_bytes(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

class Memory final : public sh::ByteSource {
public:
    explicit Memory(std::vector<std::uint8_t> data) : data_(std::move(data)) {}
    std::uint64_t size() const override { return data_.size(); }
    bool read_at(std::uint64_t offset, void* dst, std::size_t count) const override {
        if (offset > data_.size() || count > data_.size() - offset) {
            return false;
        }
        std::memcpy(dst, data_.data() + offset, count);
        return true;
    }

private:
    std::vector<std::uint8_t> data_;
};

// A synthetic area: one cell that does not parse (packed without geometry), a global PLM, a stand-alone PLM, and
// one map scene. Stored bytes carry a non-zero sector tail.
lvl::UnifiedInput synthetic_input() {
    lvl::UnifiedInput in;
    in.group = "TST";
    std::vector<std::uint8_t> cell(0x800, 0);
    cell[0] = 0x14;
    put32(cell, 4, 0x4000);  // LM outside the file: parse_ipd fails
    for (std::size_t i = 0x700; i < cell.size(); ++i) {
        cell[i] = static_cast<std::uint8_t>(i * 7);  // tail past the logical size
    }
    in.cells.push_back({"BG/TST0000.IPD", 0x700, cell});
    std::vector<std::uint8_t> plm = make_plm();
    const std::uint64_t plm_size = plm.size();
    plm.resize(0x100, 0xEE);
    in.global = lvl::SourceFile{"BG/TST_GLB.PLM", plm_size, plm};
    in.libraries.push_back({"BG/LIB.PLM", plm_size, plm});
    lvl::MapScene scene;
    scene.map = "map9_s99";
    scene.data.points.resize(2);
    scene.data.points[1].positionX = 4096 * 3;
    scene.data.events.resize(1);
    scene.data.events[0].mapPointIdx = 1;
    scene.data.events[0].sysState = 5;
    scene.data.spawns.slots[0][0].charaId = 14;
    scene.data.spawns.slots[0][0].rotationY = 64;
    scene.data.collision[1].isEndOfArray = 1;
    scene.data.collision[0].sizeX = 2;
    in.maps.push_back(std::move(scene));
    return in;
}

}  // namespace

SH_TEST(map_unified_groups) {
    SH_CHECK(lvl::unified_group_of("BG/ER0000.IPD") == "ER");
    SH_CHECK(lvl::unified_group_of("BG/THRFF0A.IPD") == "THR");
    SH_CHECK(lvl::unified_group_of("BG/THR_GLB.PLM") == "THR");
    SH_CHECK(lvl::unified_group_of("BG/MGR.PLM") == "MGR");
    SH_CHECK(lvl::unified_group_of("BG/BG_ITEM.PLM") == "BG_ITEM");
    SH_CHECK(lvl::unified_group_of("BG/THR02H2F.TIM").empty());
    SH_CHECK(lvl::unified_group_of("TEST/BTT_0000.IPD").empty());
    SH_CHECK(lvl::unified_group_of("ITEM/KNIFE.PLM").empty());
    SH_CHECK(lvl::unified_relative_path("ER") == "maps/areas/ER.glb");
}

// Synthetic round trip: every packed file comes back with its stored bytes (tail included), the document is valid
// glTF for an independent reader, and the map scene is there.
SH_TEST(map_unified_synthetic_round_trip) {
    const lvl::UnifiedInput in = synthetic_input();
    lvl::UnifiedStats stats;
    auto glb = lvl::write_unified(in, &stats);
    SH_CHECK(glb);
    if (!glb) {
        return;
    }
    SH_CHECK(stats.files == 3);
    SH_CHECK(stats.warnings.size() == 1);  // the cell that does not parse
    auto inspected = sh::gltf::inspect_glb(glb.value());
    SH_CHECK(inspected);
    if (inspected) {
        SH_CHECK(inspected.value().meshes.size() == 2);  // BOX of the stand-alone library; the global PLM has no cells
    }
    const std::string text(glb.value().begin(), glb.value().end());
    SH_CHECK(text.find("\"SH_map_files\"") != std::string::npos);
    // Library models name their index and OT group (the scene draws them by name, render/scene/area_cells.h).
    SH_CHECK(text.find("\"model_index\":0,\"ot_group\":") != std::string::npos);
    SH_CHECK(text.find("\"name\":\"map9_s99\"") != std::string::npos);
    SH_CHECK(text.find("\"name\":\"event 0 door\"") != std::string::npos);
    SH_CHECK(text.find("\"name\":\"spawn 0.0\"") != std::string::npos);
    SH_CHECK(text.find("\"name\":\"trigger 0\"") != std::string::npos);
    SH_CHECK(text.find("\"name\":\"trigger 1\"") == std::string::npos);  // the terminator is not placed

    const Memory source(glb.value());
    auto index = lvl::read_unified_index(source);
    SH_CHECK(index);
    if (!index) {
        return;
    }
    SH_CHECK(index.value().group == "TST");
    SH_CHECK(index.value().files.size() == 3);
    const std::vector<const lvl::SourceFile*> expected = {&in.cells[0], &*in.global, &in.libraries[0]};
    for (std::size_t i = 0; i < expected.size() && i < index.value().files.size(); ++i) {
        const lvl::UnifiedEntry& e = index.value().files[i];
        SH_CHECK(e.game_path == expected[i]->game_path);
        SH_CHECK(e.size == expected[i]->size);
        auto back = lvl::read_unified_entry(source, e);
        SH_CHECK(back && back.value() == expected[i]->stored);
    }

    // A damaged byte inside a packed file is caught by its CRC-32.
    std::vector<std::uint8_t> damaged = glb.value();
    damaged[static_cast<std::size_t>(index.value().files[0].glb_offset) + 3] ^= 0x55;
    const Memory bad(damaged);
    auto bad_index = lvl::read_unified_index(bad);
    SH_CHECK(bad_index);
    if (bad_index) {
        SH_CHECK(!lvl::read_unified_entry(bad, bad_index.value().files[0]));
        SH_CHECK(lvl::read_unified_entry(bad, bad_index.value().files[1]));
    }
    // Not a unified file: a plain .glb, a truncated one.
    auto plain = lvl::plm_to_glb(lvl::parse_plm(make_plm()).value(), "BG/LIB.PLM", "levels/BG/LIB.glb", nullptr);
    SH_CHECK(plain);
    if (plain) {
        const Memory plain_source(plain.value());
        auto r = lvl::read_unified_index(plain_source);
        SH_CHECK(!r && r.error().find("not a unified map file") != std::string::npos);
    }
    const Memory cut(std::vector<std::uint8_t>(glb.value().begin(), glb.value().begin() + 40));
    SH_CHECK(!lvl::read_unified_index(cut));
}

// The engine side: the VFS layer answers the packed paths with the logical bytes (read) and the stored bytes
// (open_stored, what DiscSectors reads by LBA); a mod's file wins over the base one through the VFS priorities.
SH_TEST(map_unified_vfs_layer) {
    const fs::path dir = scratch("layer");
    auto glb = lvl::write_unified(synthetic_input());
    SH_CHECK(glb);
    if (!glb) {
        return;
    }
    write_bytes(dir / "base" / "maps" / "areas" / "TST.glb", glb.value());
    write_bytes(dir / "base" / "maps" / "areas" / "BROKEN.glb", std::vector<std::uint8_t>(64, 1));

    lvl::UnifiedInput mod_in = synthetic_input();
    mod_in.libraries[0].stored[0x10] ^= 0xFF;  // the mod changes BG/LIB.PLM
    mod_in.cells.clear();
    mod_in.global.reset();
    auto mod_glb = lvl::write_unified(mod_in);
    SH_CHECK(mod_glb);
    write_bytes(dir / "mod" / "maps" / "areas" / "TST.glb", mod_glb.value());

    std::vector<std::string> warnings;
    const sh::maps::UnifiedLogFn log = [&](int level, const std::string& m) {
        if (level) {
            warnings.push_back(m);
        }
    };
    auto base = sh::maps::make_unified_maps_layer(dir / "base", "maps", log);
    SH_CHECK(base != nullptr);
    SH_CHECK(warnings.size() == 1);  // BROKEN.glb is skipped with a warning
    SH_CHECK(sh::maps::make_unified_maps_layer(dir / "nothing", "maps", log) == nullptr);
    if (!base) {
        return;
    }
    const lvl::UnifiedInput in = synthetic_input();
    SH_CHECK(base->exists("BG/TST0000.IPD") && base->exists("bg\\tst0000.ipd"));
    SH_CHECK(!base->exists("BG/TST0001.IPD"));
    auto logical = base->read("BG/TST0000.IPD");
    SH_CHECK(logical && logical->size() == 0x700 &&
             std::equal(logical->begin(), logical->end(), in.cells[0].stored.begin()));
    auto stored = base->open_stored("BG/TST0000.IPD");
    SH_CHECK(stored && stored->size() == in.cells[0].stored.size());
    if (stored) {
        std::vector<std::uint8_t> all(static_cast<std::size_t>(stored->size()));
        SH_CHECK(stored->read_at(0, all.data(), all.size()) && all == in.cells[0].stored);
    }
    const std::vector<std::string> listed = base->list("BG");
    SH_CHECK(listed.size() == 3);

    sh::vfs::Vfs vfs;
    vfs.add_layer(sh::vfs::make_directory_layer(dir / "empty", "raw"), INT_MIN);
    vfs.add_layer(std::move(base), INT_MIN + 1);
    vfs.add_layer(sh::maps::make_unified_maps_layer(dir / "mod", "mod maps", log), 0);
    auto lib = vfs.read("BG/LIB.PLM");
    SH_CHECK(lib && (*lib)[0x10] == static_cast<std::uint8_t>(in.libraries[0].stored[0x10] ^ 0xFF));
    auto cell = vfs.read("BG/TST0000.IPD");  // not in the mod: the base answers
    SH_CHECK(cell && cell->size() == 0x700);
    std::error_code ec;
    fs::remove_all(dir, ec);
}

// The real image: sh-import writes one unified file per area, every IPD and PLM of BG/ is packed and comes back
// equal to raw/ through the engine's layer, and every file is valid glTF for cgltf.
SH_TEST(map_unified_import_real_image) {
    const fs::path image = fs::path(SH_PROJECT_ROOT) / "data" / "Silent Hill (v1.1).cue";
    if (!fs::exists(image)) {
        SH_SKIP("data/Silent Hill (v1.1).cue is not present");
    }
    const fs::path dir = scratch("import");
    const fs::path out_dir = dir / "gamedata";
    std::ostringstream out;
    std::ostringstream err;
    const int code = sh::import_tool::run(
        std::vector<std::string>{image.string(), "--out", out_dir.string(), "--skip", "models", "--skip", "audio",
                                 "--skip", "text", "--no-verify"},
        out, err, nullptr);
    SH_CHECK(code == 0);
    SH_CHECK(out.str().find("map areas:      16 areas, 16 written, 0 kept; 504 files packed, 504 round trips") !=
             std::string::npos);
    SH_CHECK(err.str().find("warning: map areas") == std::string::npos);
    SH_CHECK(!fs::exists(out_dir / "convert" / "levels"));  // import_levels = unified: no per-cell views

    std::vector<std::string> warnings;
    auto layer = sh::maps::make_unified_maps_layer(out_dir, "maps", [&](int level, const std::string& m) {
        if (level) {
            warnings.push_back(m);
        }
    });
    SH_CHECK(layer != nullptr && warnings.empty());
    std::size_t compared = 0;
    for (const auto& entry : fs::directory_iterator(out_dir / "raw" / "BG")) {
        const std::string name = entry.path().filename().string();
        const std::string game_path = "BG/" + name;
        if (lvl::unified_group_of(game_path).empty() || !layer) {
            continue;
        }
        const std::vector<std::uint8_t> raw = read_bytes(entry.path());
        auto stored = layer->open_stored(game_path);
        SH_CHECK(stored != nullptr);
        if (!stored) {
            continue;
        }
        std::vector<std::uint8_t> back(static_cast<std::size_t>(stored->size()));
        SH_CHECK(stored->read_at(0, back.data(), back.size()));
        SH_CHECK(back == raw);
        ++compared;
    }
    SH_CHECK(compared == 504);
    for (const auto& entry : fs::directory_iterator(out_dir / "maps" / "areas")) {
        const std::vector<std::uint8_t> bytes = read_bytes(entry.path());
        auto inspected = sh::gltf::inspect_glb(bytes);
        SH_CHECK(inspected);
        if (!inspected) {
            std::fprintf(stderr, "%s: %s\n", entry.path().string().c_str(), inspected.error().c_str());
        }
    }
    // A second pass keeps the files and checks them again.
    const lvl::UnifiedReport again = lvl::generate_unified_maps(out_dir, false);
    SH_CHECK(again.written == 0 && again.kept == 16 && again.failed == 0 && again.round_trips == 504);
    std::error_code ec;
    fs::remove_all(dir, ec);
}
