#include "engine/maps/map_store.h"
#include "engine/maps/sh_maps.h"
#include "formats/convert/mapdata/mapdata.h"
#include "formats/convert/mapdata/mapdata_files.h"
#include "framework/sh_test.h"
#include "sh-import/run.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>
#include "framework/mod_fixture.h"

#ifndef SH_PROJECT_ROOT
#define SH_PROJECT_ROOT "../.."
#endif

namespace {

namespace fs = std::filesystem;
namespace md = sh::convert::mapdata;

fs::path scratch(const std::string& name) {
    const fs::path dir = fs::path(SH_PROJECT_ROOT) / ".tmp" / "mapdata" / "tests" / name;
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    return dir;
}

void write_text(const fs::path& path, const std::string& text) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    std::ofstream(path, std::ios::binary) << text;
    sh::test::mod_file_written(path);
}

const md::MapLayout& layout(std::string_view map) {
    const md::MapLayout* l = md::find_layout(map);
    SH_CHECK(l != nullptr);
    return *l;
}

// An overlay-sized buffer whose header points at the layout's arrays, as on the disc.
std::vector<std::uint8_t> blank_overlay(const md::MapLayout& l) {
    std::vector<std::uint8_t> bytes(0x40000, 0);
    auto put32 = [&](std::uint32_t at, std::uint32_t v) {
        for (int i = 0; i < 4; ++i) {
            bytes[at + i] = static_cast<std::uint8_t>(v >> (8 * i));
        }
    };
    put32(md::kHeaderOffset + 0x1C, l.points_va);
    put32(md::kHeaderOffset + 0x24, l.events_va);
    return bytes;
}

// Every field at its extremes, so the bit packing is checked in both directions.
md::MapData extreme_data(const md::MapLayout& l) {
    md::MapData d;
    d.map = std::string(l.map);
    d.header = ShMapHeaderInfo{-128, {1, 2, 3}, 127, 255, -1, 5};
    d.spawns.charaGroupIds[0] = 8;
    d.spawns.charaGroupIds[3] = -1;
    d.spawns.slots[0][0] = ShMapSpawn{-2147483647 - 1, -128, 255, 127, -8, 15, 2147483647};
    d.spawns.slots[1][15] = ShMapSpawn{534118, 0, 64, 3, 7, 0, -4096};
    d.points.assign(l.point_count, ShMapPoint{-1, 31, 15, 7, 15, 255, 255, 123456});
    d.events.assign(l.event_count, ShMapEvent{-32768, 32767, -8, 15, 255, 255, 255, 31, 255, 63, 31, 1, 63, 1});
    d.cameras[0] = ShMapCamera{{-32768, 32767, 1, -1}, {2, 3, 4, 5}, 255, 3, 7, 7, -128, 127, -1, 15, -8, -128, 127};
    d.cameras[99] = ShMapCamera{{1, 2, 3, 4}, {5, 6, 7, 8}, 1, 1, 1, 1, 1, 1, 1, 1, 7, 1, 1};
    d.collision[0] = ShMapCollision{1, -512, 511, 15, 15, 7};
    d.collision[199] = ShMapCollision{0, 3, -3, 1, 2, 3};
    return d;
}

}  // namespace

SH_TEST(mapdata_layout_lists_43_maps) {
    const auto all = md::map_layouts();
    SH_CHECK(all.size() == 43);
    std::set<std::string_view> names;
    for (const md::MapLayout& l : all) {
        names.insert(l.map);
        SH_CHECK(l.points_va > md::kOverlayBase && l.events_va > l.points_va);
        SH_CHECK(l.point_count >= 1 && l.point_count <= SH_MAP_POINTS_MAX);
        SH_CHECK(l.event_count >= 1 && l.event_count <= SH_MAP_EVENTS_MAX);
    }
    SH_CHECK(names.size() == 43);
    SH_CHECK(md::overlay_path("map1_s02") == "VIN/MAP1_S02.BIN");
    SH_CHECK(md::map_file_path("map1_s02", md::Kind::Spawns).generic_string() == "maps/map1_s02/spawns.json");
}

// encode -> decode -> JSON -> read -> encode gives the same bytes for values at the ends of every field.
SH_TEST(mapdata_extreme_values_round_trip) {
    const md::MapLayout& l = layout("map1_s02");
    const md::MapData d = extreme_data(l);
    std::vector<std::uint8_t> bytes = blank_overlay(l);
    for (md::Kind kind : md::kAllKinds) {
        SH_CHECK(static_cast<bool>(md::encode_kind(d, kind, l, bytes)));
    }
    const auto decoded = md::decode_overlay(bytes, l);
    SH_CHECK(static_cast<bool>(decoded));
    if (!decoded) {
        return;
    }
    for (md::Kind kind : md::kAllKinds) {
        SH_CHECK(md::compare_kind(decoded.value(), kind, l, bytes).empty());
        md::MapData back;
        back.map = d.map;
        std::vector<std::string> warnings;
        const auto read = md::read_json(md::write_json(decoded.value(), kind), kind, l, back, warnings);
        SH_CHECK(static_cast<bool>(read));
        SH_CHECK(warnings.empty());
        SH_CHECK(md::compare_kind(back, kind, l, bytes).empty());
    }
    SH_CHECK(decoded.value().spawns.slots[0][0].gameDifficultyMin == -8);
    SH_CHECK(decoded.value().collision[0].positionX == -512);
    SH_CHECK(decoded.value().cameras[0].cameraMoveType == -8);
    SH_CHECK(decoded.value().events[0].triggerType == -8);
}

SH_TEST(mapdata_json_human_units_and_errors) {
    const md::MapLayout& l = layout("map1_s02");
    md::MapData d = extreme_data(l);
    const std::string base = md::write_json(d, md::Kind::Spawns);
    SH_CHECK(base.find("\"x\": 130.39990234375") != std::string::npos);
    SH_CHECK(base.find("\"rotation_deg\": 90") != std::string::npos);
    SH_CHECK(base.find("\"chara\": \"GreyChild\"") != std::string::npos);

    // Only metres given: the integer follows; both given and different: metres win with a warning.
    md::MapData back;
    back.map = d.map;
    std::vector<std::string> warnings;
    std::string text = base;
    text.replace(text.find("\"x\": 130.39990234375"), std::strlen("\"x\": 130.39990234375"), "\"x\": 150.5");
    SH_CHECK(static_cast<bool>(md::read_json(text, md::Kind::Spawns, l, back, warnings)));
    SH_CHECK(back.spawns.slots[1][15].positionX == 616448);
    SH_CHECK(warnings.size() == 1 && warnings[0].find("using \"x\"") != std::string::npos);

    // Broken values name the line and the field, and nothing is changed.
    warnings.clear();
    md::MapData untouched = back;
    std::string bad = base;
    bad.replace(bad.find("\"flags\": 3"), std::strlen("\"flags\": 3"), "\"flags\": 300");
    const auto r1 = md::read_json(bad, md::Kind::Spawns, l, back, warnings);
    SH_CHECK(!r1 && r1.error().find("groups[1].slots[15].flags") != std::string::npos &&
             r1.error().find("line ") == 0);
    SH_CHECK(std::memcmp(&back.spawns, &untouched.spawns, sizeof back.spawns) == 0);
    const auto r2 = md::read_json("{ \"format\": \"sh-map-spawns\", ", md::Kind::Spawns, l, back, warnings);
    SH_CHECK(!r2 && r2.error().find("line 1") != std::string::npos);
    const auto r3 = md::read_json(md::write_json(d, md::Kind::Spawns), md::Kind::Points, l, back, warnings);
    SH_CHECK(!r3 && r3.error().find("sh-map-points") != std::string::npos);

    // The count of points and events is the size of the game's arrays.
    std::string points = md::write_json(d, md::Kind::Points);
    const std::size_t last = points.rfind("    {");
    points.erase(last, points.find('\n', last) - last + 1);
    points.replace(points.rfind("},"), 2, "}");
    const auto r4 = md::read_json(points, md::Kind::Points, l, back, warnings);
    SH_CHECK(!r4 && r4.error().find("cannot change") != std::string::npos);
}

// "chara" names the spawn character (gamedata has no numeric ids); the old numeric
// "chara_id" key still reads, the name wins over a mismatching number.
SH_TEST(mapdata_spawns_chara_names) {
    const md::MapLayout& l = layout("map1_s02");
    const md::MapData d = extreme_data(l);
    const std::string base = md::write_json(d, md::Kind::Spawns);
    SH_CHECK(base.find("\"chara_id\"") == std::string::npos);
    SH_CHECK(base.find("\"chara\": \"GreyChild\"") != std::string::npos);

    md::MapData back;
    back.map = d.map;
    std::vector<std::string> warnings;

    // Only the old key: it still reads, with one warning.
    std::string legacy = base;
    legacy.replace(legacy.find("\"chara\": \"GreyChild\""), std::strlen("\"chara\": \"GreyChild\""), "\"chara_id\": 8");
    SH_CHECK(static_cast<bool>(md::read_json(legacy, md::Kind::Spawns, l, back, warnings)));
    SH_CHECK(back.spawns.charaGroupIds[0] == 8);
    SH_CHECK(warnings.size() == 1 && warnings[0].find("\"chara_id\"") != std::string::npos);

    // Both keys and they disagree: the name wins, the mismatch warns.
    warnings.clear();
    std::string both = base;
    both.replace(both.find("\"chara\": \"GreyChild\""), std::strlen("\"chara\": \"GreyChild\""),
                 "\"chara\": \"GreyChild\", \"chara_id\": 9");
    SH_CHECK(static_cast<bool>(md::read_json(both, md::Kind::Spawns, l, back, warnings)));
    SH_CHECK(back.spawns.charaGroupIds[0] == 8);
    SH_CHECK(warnings.size() == 1 && warnings[0].find("using \"chara\"") != std::string::npos);

    // An unknown name is a parse error naming the field, and nothing changes.
    md::MapData untouched = back;
    warnings.clear();
    std::string bad = base;
    bad.replace(bad.find("\"chara\": \"GreyChild\""), std::strlen("\"chara\": \"GreyChild\""), "\"chara\": \"Nobody\"");
    const auto r = md::read_json(bad, md::Kind::Spawns, l, back, warnings);
    SH_CHECK(!r && r.error().find("unknown character") != std::string::npos);
    SH_CHECK(std::memcmp(&back.spawns, &untouched.spawns, sizeof back.spawns) == 0);
}

// The store: a mod's file before gamedata/maps, a broken file falls back to the disc, a missing file is written.
SH_TEST(mapdata_store_sources_and_fallback) {
    const md::MapLayout& l = layout("map1_s02");
    const fs::path dir = scratch("store");
    const fs::path gamedata = dir / "gamedata";
    const md::MapData d = extreme_data(l);
    std::vector<std::uint8_t> bytes = blank_overlay(l);
    for (md::Kind kind : md::kAllKinds) {
        md::encode_kind(d, kind, l, bytes);
    }
    std::error_code ec;
    fs::create_directories(gamedata / "raw" / "VIN", ec);
    std::ofstream(gamedata / "raw" / "VIN" / "MAP1_S02.BIN", std::ios::binary)
        .write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));

    std::vector<std::string> log;
    sh::maps::StoreConfig config;
    config.gamedata_dir = gamedata;
    config.mods_dir = dir / "mods";
    config.log = [&](int, const std::string& m) { log.push_back(m); };
    sh::maps::MapStore store(config);

    // First load: the files are written from the disc and then read.
    sh::maps::MapLoad a = store.load("map1_s02");
    for (const auto& k : a.kinds) {
        SH_CHECK(k.source == sh::maps::Source::File);
    }
    SH_CHECK(fs::exists(gamedata / "maps" / "map1_s02" / "collision.json"));
    SH_CHECK(a.data.spawns.slots[1][15].positionX == 534118);

    // A broken file: the disc values, and a warning with the file and line.
    write_text(gamedata / "maps" / "map1_s02" / "spawns.json", "{\n  \"format\": \"sh-map-spawns\",\n  \"version\": 1,\n}\n");
    log.clear();
    sh::maps::MapLoad b = store.load("map1_s02");
    SH_CHECK(b.kinds[static_cast<int>(md::Kind::Spawns)].source == sh::maps::Source::Disc);
    SH_CHECK(b.data.spawns.slots[1][15].positionX == 534118);
    bool warned = false;
    for (const std::string& m : log) {
        warned = warned || (m.find("spawns.json: line 4") != std::string::npos && m.find("disc") != std::string::npos);
    }
    SH_CHECK(warned);

    // A mod's spawns win.
    std::string mod_text = md::write_json(d, md::Kind::Spawns);
    mod_text.replace(mod_text.find("\"x\": 130.39990234375"), std::strlen("\"x\": 130.39990234375"), "\"x\": 10");
    mod_text.replace(mod_text.find("\"x_q12\": 534118"), std::strlen("\"x_q12\": 534118"), "\"x_q12\": 40960");
    write_text(dir / "mods" / "moved" / "maps" / "map1_s02" / "spawns.json", mod_text);
    write_text(dir / "mods" / "moved" / "mod.json", R"({"id": "moved", "version": "1.0.0", "gameplay": true})");
    sh::maps::MapStore with_mod(config);
    sh::maps::MapLoad c = with_mod.load("map1_s02");
    SH_CHECK(c.kinds[static_cast<int>(md::Kind::Spawns)].source == sh::maps::Source::Mod);
    SH_CHECK(c.kinds[static_cast<int>(md::Kind::Spawns)].mod == "moved");
    SH_CHECK(c.data.spawns.slots[1][15].positionX == 40960);

    // Without the disc and without files: no source, the game keeps its compiled values.
    fs::remove_all(gamedata, ec);
    fs::remove_all(dir / "mods", ec);
    sh::maps::MapStore empty(config);
    sh::maps::MapLoad e = empty.load("map1_s02");
    for (const auto& k : e.kinds) {
        SH_CHECK(k.source == sh::maps::Source::None);
    }

    // The C face.
    const std::string gamedata_str = gamedata.string();  // cc keeps only the pointer
    const ShMapsConfig cc{gamedata_str.c_str(), nullptr, [](int, const char*) {}};
    ShMaps_Configure(&cc);
    static ShMapData out;
    SH_CHECK(ShMaps_Load("map1_s02", &out) == 0);
    SH_CHECK(std::string(ShMaps_KindName(SH_MAP_KIND_COLLISION)) == "collision");
    const ShMapsConfig reset{};
    ShMaps_Configure(&reset);
    fs::remove_all(dir, ec);
}

// All 43 overlays of the disc: sh-import writes gamedata/maps, every fresh JSON gives the disc bytes back, and the
// C face returns the same records as the disc for every map.
SH_TEST(mapdata_import_round_trips_all_maps) {
    const fs::path image = fs::path(SH_PROJECT_ROOT) / "data" / "Silent Hill (v1.1).cue";
    if (!fs::exists(image)) {
        SH_SKIP("data/Silent Hill (v1.1).cue is not present");
    }
    const fs::path dir = scratch("import");
    const fs::path out_dir = dir / "gamedata";
    std::ostringstream out;
    std::ostringstream err;
    const int code = sh::import_tool::run(
        std::vector<std::string>{image.string(), "--out", out_dir.string(), "--skip", "textures", "--skip", "models",
                                 "--skip", "audio", "--skip", "text", "--skip", "levels", "--no-verify", "--keep-raw"},
        out, err, nullptr);
    SH_CHECK(code == 0);
    SH_CHECK(out.str().find("maps:           43 maps, 258 written, 0 kept; disc check: 258 round trips, 258 files, "
                            "0 differ") != std::string::npos);
    SH_CHECK(err.str().find("warning: maps") == std::string::npos);

    ShMapsConfig cc{nullptr, nullptr, [](int, const char*) {}};
    const std::string gd = out_dir.string();
    cc.gamedataDir = gd.c_str();
    ShMaps_Configure(&cc);
    static ShMapData loaded;
    for (const md::MapLayout& l : md::map_layouts()) {
        const auto bytes = md::read_overlay(out_dir, l);
        SH_CHECK(static_cast<bool>(bytes));
        if (!bytes) {
            continue;
        }
        const auto disc = md::decode_overlay(bytes.value(), l);
        SH_CHECK(static_cast<bool>(disc));
        if (!disc) {
            continue;
        }
        SH_CHECK(ShMaps_Load(std::string(l.map).c_str(), &loaded) == SH_MAP_KIND_COUNT);
        for (int k = 0; k < SH_MAP_KIND_COUNT; ++k) {
            SH_CHECK(loaded.source[k] == SH_MAP_SOURCE_FILE);
        }
        // The records of the files, written back into the overlay, are the disc's bytes.
        md::MapData from_files;
        from_files.map = std::string(l.map);
        from_files.header = loaded.header;
        from_files.spawns = loaded.spawns;
        from_files.points.assign(loaded.points, loaded.points + loaded.pointCount);
        from_files.events.assign(loaded.events, loaded.events + loaded.eventCount);
        std::memcpy(from_files.cameras.data(), loaded.cameras, sizeof loaded.cameras);
        std::memcpy(from_files.collision.data(), loaded.collision, sizeof loaded.collision);
        for (md::Kind kind : md::kAllKinds) {
            const std::vector<std::string> diffs = md::compare_kind(from_files, kind, l, bytes.value());
            SH_CHECK(diffs.empty());
        }
    }
    const ShMapsConfig reset{};
    ShMaps_Configure(&reset);

    // A second pass keeps the files.
    const md::GenerateReport again = md::generate_map_files(out_dir, false);
    SH_CHECK(again.written == 0 && again.kept == 258 && again.failed == 0 && again.files_differ == 0);
    std::error_code ec;
    fs::remove_all(dir, ec);
}

// map.json: the unified scene view joins the six kinds into typed entities and
// resolves callback handlers from handlers.json.
SH_TEST(mapdata_unified_scene_json) {
    const md::MapLayout& l = layout("map1_s02");
    md::MapData d = extreme_data(l);
    // One event of every interesting kind: a door to map0_s00, a room warp, a callback.
    d.events.assign(l.event_count, ShMapEvent{});
    d.events[0] = ShMapEvent{1, 2, 1, 0, 7, 0, 0, 5, 3, 0, 0, 0, 0, 0};
    d.events[1] = ShMapEvent{-1, -1, 0, 0, 0, 0, 0, 6, 9, 0, 0, 0, 0, 0};
    d.events[2] = ShMapEvent{-1, -1, 0, 2, 0, 0, 0, 10, 4, 0, 0, 0, 0, 0};

    // Without handlers.json the callback entity still gets its kind, but no handler.
    const std::string bare = md::write_unified_json(d, {});
    SH_CHECK(bare.find("\"kind\": \"door\"") != std::string::npos);
    SH_CHECK(bare.find("\"map\": \"map0_s00\"") != std::string::npos);
    SH_CHECK(bare.find("\"arrival_point\": 3") != std::string::npos);
    SH_CHECK(bare.find("\"kind\": \"room_warp\"") != std::string::npos);
    SH_CHECK(bare.find("\"kind\": \"callback\"") != std::string::npos);
    SH_CHECK(bare.find("\"handler\"") != std::string::npos);

    // handlers.json is parsed and joined by event param.
    const char* handlers_text = R"({"map": "map1_s02", "handlers": [)"
                                R"({"index": 4, "symbol": "MapEvent_Opening", "name": "MapEvent_CutsceneOpening",)"
                                R"( "shape": "cutscene"},)"
                                R"({"index": 5, "symbol": null, "name": null, "shape": null}]})"
                                "\n";
    const auto handlers = md::read_handlers_json(handlers_text);
    SH_CHECK(static_cast<bool>(handlers));
    if (handlers) {
        SH_CHECK(handlers.value().size() == 1);  // the null slot is skipped
        const std::string joined = md::write_unified_json(d, handlers.value());
        SH_CHECK(joined.find("\"handler\": \"MapEvent_Opening\"") != std::string::npos);
        SH_CHECK(joined.find("\"handler_name\": \"MapEvent_CutsceneOpening\"") != std::string::npos);
        SH_CHECK(joined.find("\"handler_shape\": \"cutscene\"") != std::string::npos);
    }

    // Malformed handlers.json is an error, not a crash.
    SH_CHECK(!md::read_handlers_json("{\"handlers\": [3]}"));
}

// The unified scene view is lossless: writing it, reading it back and encoding every
// kind gives the same overlay bytes as the source data.
SH_TEST(mapdata_unified_round_trip) {
    const md::MapLayout& l = layout("map1_s02");
    const md::MapData d = extreme_data(l);
    const std::string scene = md::write_unified_json(d, {});

    md::MapData back;
    back.map = std::string(l.map);
    std::vector<std::string> warnings;
    const sh::Result<void> read = md::read_unified_json(scene, l, back, warnings);
    SH_CHECK(read);
    SH_CHECK(warnings.empty());

    std::vector<std::uint8_t> a = blank_overlay(l);
    std::vector<std::uint8_t> b = blank_overlay(l);
    for (md::Kind kind : md::kAllKinds) {
        SH_CHECK(md::encode_kind(d, kind, l, a));
        SH_CHECK(md::encode_kind(back, kind, l, b));
        const std::vector<std::string> diffs = md::compare_kind(back, kind, l, a);
        for (const std::string& diff : diffs) {
            SH_CHECK(diff.empty());
        }
    }

    // A wrong points count is an error naming the file.
    md::MapData bad;
    bad.map = std::string(l.map);
    std::string broken = scene;
    const std::size_t where = broken.find("\"points\": [");
    SH_CHECK(where != std::string::npos);
    SH_CHECK(!md::read_unified_json(broken.substr(0, where), l, bad, warnings));
    SH_CHECK(!md::read_unified_json("{\"format\": \"sh-map-header\"}", l, bad, warnings));
}

// A message event carries its message key beside the number (gamedata/text/en: common block 0..14, then the map's
// own lines); reading prefers the key, and an unknown key falls back to "param" with a warning.
SH_TEST(mapdata_message_event_key) {
    const md::MapLayout& l = layout("map0_s01");
    md::MapData d = extreme_data(l);
    d.events[0].sysState = 7;
    d.events[0].eventParam = 73;
    d.events[1].sysState = 7;
    d.events[1].eventParam = 2;
    std::string scene = md::write_unified_json(d, {});
    const std::string key = "\"message\": \"map0_s01.pinball_machine_nothing\"";
    SH_CHECK(scene.find(key) != std::string::npos);
    SH_CHECK(scene.find("\"message\": \"common.notes_useful\"") != std::string::npos);

    // The key wins over a "param" that says otherwise.
    const std::size_t at = scene.find("\"param\": 73");
    SH_CHECK(at != std::string::npos);
    scene.replace(at, 11, "\"param\": 74");
    std::vector<std::string> warnings;
    md::MapData back;
    back.map = std::string(l.map);
    SH_CHECK(md::read_unified_json(scene, l, back, warnings));
    SH_CHECK(warnings.empty());
    SH_CHECK(!back.events.empty() && back.events[0].eventParam == 73);

    std::string unknown = scene;
    unknown.replace(unknown.find(key), key.size(), "\"message\": \"map0_s01.no_such_line\"");
    md::MapData fallback;
    fallback.map = std::string(l.map);
    SH_CHECK(md::read_unified_json(unknown, l, fallback, warnings));
    SH_CHECK(warnings.size() == 1);
    SH_CHECK(!fallback.events.empty() && fallback.events[0].eventParam == 74);
}

// Files written before "field_4_5" got its name still read, to the same bytes, with one warning.
SH_TEST(mapdata_old_point_key_reads) {
    const md::MapLayout& l = layout("map1_s02");
    md::MapData d = extreme_data(l);
    for (ShMapPoint& p : d.points) {
        p.weatherOnEnter = 3;
    }
    std::string scene = md::write_unified_json(d, {});
    std::string points = md::write_json(d, md::Kind::Points);
    for (std::string* text : {&scene, &points}) {
        for (std::size_t at = text->find("\"weather_on_enter\""); at != std::string::npos;
             at = text->find("\"weather_on_enter\"", at)) {
            text->replace(at, 18, "\"field_4_5\"");
        }
        SH_CHECK(text->find("\"weather_on_enter\"") == std::string::npos);
    }

    std::vector<std::string> warnings;
    md::MapData back;
    back.map = std::string(l.map);
    SH_CHECK(md::read_unified_json(scene, l, back, warnings));
    SH_CHECK(warnings.size() == 1);
    SH_CHECK(!warnings.empty() && warnings[0].find("field_4_5") != std::string::npos);
    SH_CHECK(!back.points.empty() && back.points[0].weatherOnEnter == 3);

    warnings.clear();
    md::MapData per_kind = d;
    for (ShMapPoint& p : per_kind.points) {
        p.weatherOnEnter = 0;
    }
    SH_CHECK(md::read_json(points, md::Kind::Points, l, per_kind, warnings));
    SH_CHECK(warnings.size() == 1);
    SH_CHECK(!per_kind.points.empty() && per_kind.points.back().weatherOnEnter == 3);
}

// A point's shape bytes are named by use: a box trigger's point holds half extents, any other point a heading and
// a half width; the keys of older files ("param0" / "param1") read to the same bytes with one warning.
SH_TEST(mapdata_point_shape_keys) {
    const md::MapLayout& l = layout("map1_s02");
    md::MapData d = extreme_data(l);
    for (std::size_t i = 0; i < d.points.size(); ++i) {
        d.points[i].triggerParam0 = static_cast<std::uint8_t>(i);
        d.points[i].triggerParam1 = static_cast<std::uint8_t>(200 - i);
    }
    d.events[0].triggerType = 1;
    d.events[0].mapPointIdx = 1;
    std::string scene = md::write_unified_json(d, {});
    std::string points = md::write_json(d, md::Kind::Points);
    for (const std::string* text : {&scene, &points}) {
        SH_CHECK(text->find("\"index\": 1, \"x\"") != std::string::npos);
        SH_CHECK(text->find("\"half_x_q2\": 1, \"half_z_q2\": 199") != std::string::npos);
        SH_CHECK(text->find("\"heading_q8\": 2, \"half_width_q4\": 198") != std::string::npos);
        SH_CHECK(text->find("\"param0\"") == std::string::npos);
    }
    SH_CHECK(points.find("\"description\"") != std::string::npos);

    const auto read_back = [&](const std::string& scene_text, const std::string& points_text, std::size_t warnings) {
        std::vector<std::string> w;
        md::MapData back;
        back.map = std::string(l.map);
        SH_CHECK(md::read_unified_json(scene_text, l, back, w));
        SH_CHECK(w.size() == warnings);
        md::MapData per_kind = d;
        for (ShMapPoint& p : per_kind.points) {
            p.triggerParam0 = p.triggerParam1 = 0;
        }
        w.clear();
        SH_CHECK(md::read_json(points_text, md::Kind::Points, l, per_kind, w));
        SH_CHECK(w.size() == warnings);
        for (const md::MapData* m : {&back, &per_kind}) {
            SH_CHECK(m->points.size() == d.points.size());
            for (std::size_t i = 0; i < d.points.size() && i < m->points.size(); ++i) {
                SH_CHECK(m->points[i].triggerParam0 == d.points[i].triggerParam0 &&
                         m->points[i].triggerParam1 == d.points[i].triggerParam1);
            }
        }
    };
    read_back(scene, points, 0);

    for (std::string* text : {&scene, &points}) {
        for (const auto& [from, to] : {std::pair<std::string, std::string>{"\"heading_q8\"", "\"param0\""},
                                       {"\"half_width_q4\"", "\"param1\""},
                                       {"\"half_x_q2\"", "\"param0\""},
                                       {"\"half_z_q2\"", "\"param1\""}}) {
            for (std::size_t at = text->find(from); at != std::string::npos; at = text->find(from, at)) {
                text->replace(at, from.size(), to);
            }
        }
    }
    read_back(scene, points, 1);

    // Both name pairs on one point are an error.
    std::string both = md::write_json(d, md::Kind::Points);
    both.replace(both.find("\"half_x_q2\""), 11, "\"heading_q8\": 1, \"half_x_q2\"");
    md::MapData back = d;
    std::vector<std::string> w;
    SH_CHECK(!md::read_json(both, md::Kind::Points, l, back, w));
}

// The store prefers a map.json: a base one fills all six kinds (no per-kind files
// needed), a mod's one replaces the map wholesale, and a mod's per-kind file still
// beats a base map.json.
SH_TEST(mapdata_store_prefers_unified_scene) {
    const md::MapLayout& l = layout("map1_s02");
    const fs::path dir = scratch("scene");
    const fs::path gamedata = dir / "gamedata";
    const md::MapData d = extreme_data(l);
    std::vector<std::uint8_t> bytes = blank_overlay(l);
    for (md::Kind kind : md::kAllKinds) {
        md::encode_kind(d, kind, l, bytes);
    }
    std::error_code ec;
    fs::create_directories(gamedata / "raw" / "VIN", ec);
    std::ofstream(gamedata / "raw" / "VIN" / "MAP1_S02.BIN", std::ios::binary)
        .write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));

    // A map.json alone (no six kind files anywhere): every kind comes from it.
    const fs::path map_dir = gamedata / "maps" / "map1_s02";
    fs::create_directories(map_dir, ec);
    write_text(map_dir / "map.json", md::write_unified_json(d, {}));

    std::vector<std::string> log;
    sh::maps::StoreConfig config;
    config.gamedata_dir = gamedata;
    config.mods_dir = dir / "mods";
    config.log = [&](int, const std::string& m) { log.push_back(m); };
    sh::maps::MapStore store(config);
    sh::maps::MapLoad a = store.load("map1_s02");
    for (const auto& k : a.kinds) {
        SH_CHECK(k.source == sh::maps::Source::File);
        SH_CHECK(k.path == "maps/map1_s02/map.json");
    }
    SH_CHECK(a.data.spawns.slots[1][15].positionX == 534118);
    SH_CHECK(!fs::exists(map_dir / "spawns.json", ec));  // the store did not create kind files

    // A mod's map.json wins over the base one.
    md::MapData modded = d;
    modded.header.bgmIdx = 99;
    fs::create_directories(dir / "mods" / "newmap" / "maps" / "map1_s02", ec);
    write_text(dir / "mods" / "newmap" / "maps" / "map1_s02" / "map.json", md::write_unified_json(modded, {}));
    write_text(dir / "mods" / "newmap" / "mod.json", R"({"id": "newmap", "version": "1.0.0", "gameplay": true})");
    sh::maps::MapStore with_mod(config);
    sh::maps::MapLoad c = with_mod.load("map1_s02");
    for (const auto& k : c.kinds) {
        SH_CHECK(k.source == sh::maps::Source::Mod && k.mod == "newmap");
    }
    SH_CHECK(c.data.header.bgmIdx == 99);

    // A mod's per-kind file beats the base map.json (existing mods keep working).
    fs::remove_all(dir / "mods" / "newmap", ec);  // only the "moved" mod stays now
    std::string spawns_text = md::write_json(d, md::Kind::Spawns);
    spawns_text.replace(spawns_text.find("\"x\": 130.39990234375"), std::strlen("\"x\": 130.39990234375"), "\"x\": 10");
    spawns_text.replace(spawns_text.find("\"x_q12\": 534118"), std::strlen("\"x_q12\": 534118"), "\"x_q12\": 40960");
    write_text(dir / "mods" / "moved" / "maps" / "map1_s02" / "spawns.json", spawns_text);
    write_text(dir / "mods" / "moved" / "mod.json", R"({"id": "moved", "version": "1.0.0", "gameplay": true})");
    sh::maps::MapStore mixed(config);
    sh::maps::MapLoad m = mixed.load("map1_s02");
    SH_CHECK(m.kinds[static_cast<int>(md::Kind::Spawns)].source == sh::maps::Source::Mod);
    SH_CHECK(m.kinds[static_cast<int>(md::Kind::Spawns)].mod == "moved");
    SH_CHECK(m.kinds[static_cast<int>(md::Kind::Header)].source == sh::maps::Source::File);
    SH_CHECK(m.data.spawns.slots[1][15].positionX == 40960);

    // A broken map.json falls back to the per-kind files/disc with a warning.
    write_text(map_dir / "map.json", "{\"format\": \"sh-map-scene\", \"version\": 1, \"nope\": true}\n");
    fs::remove_all(dir / "mods", ec);
    log.clear();
    sh::maps::MapStore broken(config);
    sh::maps::MapLoad e = broken.load("map1_s02");
    bool warned = false;
    for (const std::string& w : log) {
        warned = warned || w.find("map.json") != std::string::npos;
    }
    SH_CHECK(warned);
    for (const auto& k : e.kinds) {
        SH_CHECK(k.source == sh::maps::Source::File || k.source == sh::maps::Source::Disc);
    }
    fs::remove_all(dir, ec);
}
