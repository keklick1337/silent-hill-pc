#include "engine/tables/anim_table.h"
#include "engine/tables/sh_tables.h"
#include "engine/tables/table_store.h"
#include "formats/common/json.h"
#include "framework/sh_test.h"
#include "engine/tables/anim_table_registry.h"
#include "engine/chara/game/clip_tables.h"
#include "sh-import/run.h"
#include "sh-import/tables.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>
#include "framework/mod_fixture.h"

#ifndef SH_PROJECT_ROOT
#define SH_PROJECT_ROOT "../.."
#endif

// Stand-ins for the engine's three playbacks: only their addresses matter here.
namespace {
void playback_blend(sh::entities::ModelRecord*, void*, sh::entities::BoneCoord*, sh::chara::game::ClipInfo*) {}
void playback_once(sh::entities::ModelRecord*, void*, sh::entities::BoneCoord*, sh::chara::game::ClipInfo*) {}
void playback_loop(sh::entities::ModelRecord*, void*, sh::entities::BoneCoord*, sh::chara::game::ClipInfo*) {}
}  // namespace

namespace {

namespace fs = std::filesystem;
using namespace sh::tables;

std::span<const ShAnimFunc> funcs() {
    return {g_AnimTableFuncs, g_AnimTableFuncCount};
}

const ShAnimTableInfo& table_named(std::string_view name) {
    for (unsigned i = 0; i < g_AnimTableInfoCount; ++i) {
        if (name == g_AnimTableInfos[i]->name) {
            return *g_AnimTableInfos[i];
        }
    }
    static const ShAnimTableInfo none{};
    SH_CHECK(!"no such table");
    return none;
}

fs::path scratch(const std::string& name) {
    const fs::path dir = fs::path(SH_PROJECT_ROOT) / ".tmp" / "tables" / "tests" / name;
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

std::string read_text(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// Store with a log that is kept for the checks.
struct Logged {
    std::vector<std::string> lines;
    std::string all() const {
        std::string s;
        for (const auto& l : lines) {
            s += l + "\n";
        }
        return s;
    }
};

StoreConfig store_config(const fs::path& gamedata, Logged& log) {
    StoreConfig c;
    c.gamedata_dir = gamedata;
    c.funcs.assign(g_AnimTableFuncs, g_AnimTableFuncs + g_AnimTableFuncCount);
    c.log = [&log](int level, const std::string& m) {
        log.lines.push_back((level == SH_TABLES_LOG_WARN ? "W " : level == SH_TABLES_LOG_ERROR ? "E " : "I ") + m);
    };
    return c;
}

std::string replace_once(std::string text, const std::string& from, const std::string& to) {
    const auto at = text.find(from);
    SH_CHECK(at != std::string::npos);
    return at == std::string::npos ? text : text.replace(at, from.size(), to);
}

// No table is compiled in: the tests that need table values write their own small
// fixtures and lay them out as the disc holds them.
AnimEntry entry_of(AnimKind kind, std::uint8_t status, bool variable, std::uint8_t link, std::int32_t speed_q12,
                   std::int16_t start_kf, std::int16_t end_kf) {
    AnimEntry e;
    e.kind = kind;
    e.status = status;
    e.variable_duration = variable;
    e.link = link;
    e.speed_q12 = speed_q12;
    e.start_kf = start_kf;
    e.end_kf = end_kf;
    return e;
}

// CAT as a ten-entry fixture; the edit strings of the tests below name its values.
AnimTable cat_fixture() {
    using K = AnimKind;
    AnimTable t;
    t.symbol = "CAT_ANIM_INFOS";
    t.entries = {
        entry_of(K::BlendLinear, 0, false, 1, 4096, -1, 0),
        entry_of(K::None, 1, false, 0, 0, 0, 0),
        entry_of(K::BlendLinear, 2, false, 3, 8192, -1, 2),
        entry_of(K::PlaybackOnce, 3, false, 255, 64880, 15, 19),
        entry_of(K::BlendLinear, 4, false, 5, 20480, -1, 20),
        entry_of(K::PlaybackLoop, 5, false, 255, 143360, 20, 29),
        entry_of(K::BlendLinear, 6, false, 7, 10240, -1, 30),
        entry_of(K::PlaybackLoop, 7, false, 5, 12288, 30, 35),
        entry_of(K::BlendLinear, 8, false, 9, 4096, -1, 36),
        entry_of(K::PlaybackOnce, 9, false, 255, 8192, 36, 43),
    };
    return t;
}

// MONSTER_CYBIL as a fixture of `count` entries with the one variable-duration entry of the real table.
AnimTable monster_cybil_fixture(const ShAnimTableInfo& info) {
    AnimTable t;
    t.symbol = info.symbol;
    for (std::size_t i = 0; i < info.fileCount; ++i) {
        if (i == 3) {
            AnimEntry e = entry_of(AnimKind::PlaybackLoop, 3, true, 255, 0, 0, 23);
            e.duration_func = "MonsterCybil_GetAnimInfo";
            t.entries.push_back(e);
        } else if (i % 2 == 0) {
            t.entries.push_back(entry_of(AnimKind::BlendLinear, static_cast<std::uint8_t>(i), false,
                                         static_cast<std::uint8_t>(i + 1), 4096, -1, static_cast<std::int16_t>(i)));
        } else {
            t.entries.push_back(entry_of(AnimKind::PlaybackLoop, static_cast<std::uint8_t>(i), false, 255, 4096,
                                         static_cast<std::int16_t>(i), static_cast<std::int16_t>(i + 4)));
        }
    }
    return t;
}

std::vector<ShAnimEntry> game_entries(const AnimTable& table, const ShAnimTableInfo& info) {
    return to_game_entries(table, info, funcs());
}

// gamedata/raw/<overlay> with `entries` as 16-byte PSX records at the source address, as on the disc.
void write_overlay(const fs::path& gamedata, const ShAnimSource& source, const ShAnimEntry* entries, std::size_t count) {
    static constexpr std::uint32_t kFuncs[3] = {0x80044CA4, 0x800449F0, 0x80044B38};
    const std::size_t offset = source.va - 0x800C9578u;
    std::vector<unsigned char> bytes(offset + count * 16 + 64, 0xEE);
    for (std::size_t i = 0; i < count; ++i) {
        const ShAnimEntry& e = entries[i];
        unsigned char* r = bytes.data() + offset + i * 16;
        const std::uint32_t func = e.kind < 3 ? kFuncs[e.kind] : 0;
        std::memcpy(r, &func, 4);
        r[4] = e.status;
        r[5] = static_cast<unsigned char>(e.hasVariableDuration);
        r[6] = e.linkStatus;
        r[7] = 0;
        std::memcpy(r + 8, &e.durationQ12, 4);
        std::memcpy(r + 12, &e.startKeyframeIdx, 2);
        std::memcpy(r + 14, &e.endKeyframeIdx, 2);
    }
    const fs::path path = gamedata / "raw" / source.file;
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(bytes.data()),
                                                static_cast<std::streamsize>(bytes.size()));
}

// Every copy of the table on the fake disc holds the fixture values.
void write_disc(const fs::path& gamedata, const ShAnimTableInfo& info, const AnimTable& table) {
    const std::vector<ShAnimEntry> entries = game_entries(table, info);
    for (std::size_t s = 0; s < info.sourceCount; ++s) {
        write_overlay(gamedata, info.sources[s], entries.data(), info.fileCount);
    }
}

bool same(const std::vector<ShAnimEntry>& a, const AnimTable& table, const ShAnimTableInfo& info) {
    const std::vector<ShAnimEntry> want = game_entries(table, info);
    return a.size() == want.size() && std::memcmp(a.data(), want.data(), sizeof(ShAnimEntry) * want.size()) == 0;
}

}  // namespace

SH_TEST(json_parse_values_and_positions) {
    auto doc = sh::json::parse("\xEF\xBB\xBF{\"a\": [1, -2, 2.5, true, null, \"x\\\"\\u00e9\"],\n  \"b\": {\"c\": 9007199254740993}}");
    SH_CHECK(doc);
    const sh::json::Value& v = doc.value();
    const sh::json::Value* a = v.find("a");
    SH_CHECK(a && a->is_array() && a->array().size() == 6);
    SH_CHECK(a->array()[0].integer() == 1 && a->array()[1].integer() == -2);
    SH_CHECK(!a->array()[2].is_integer() && a->array()[2].number() == 2.5);
    SH_CHECK(a->array()[3].is_bool() && a->array()[3].boolean());
    SH_CHECK(a->array()[4].is_null());
    SH_CHECK(a->array()[5].string() == "x\"\xC3\xA9");
    const sh::json::Value* c = v.find("b")->find("c");
    SH_CHECK(c->is_integer() && c->integer() == 9007199254740993LL);
    SH_CHECK(c->line() == 2 && c->column() == 14);
}

SH_TEST(json_parse_errors_name_line_and_column) {
    auto missing_comma = sh::json::parse("{\n  \"a\": 1\n  \"b\": 2\n}");
    SH_CHECK(!missing_comma);
    SH_CHECK(missing_comma.error().find("line 3, column 3") != std::string::npos);
    SH_CHECK(!sh::json::parse("[1,"));
    SH_CHECK(!sh::json::parse("{\"a\": 1} x"));
    SH_CHECK(!sh::json::parse("{\"a\": 01}"));
    SH_CHECK(!sh::json::parse("\"abc"));
    SH_CHECK(sh::json::parse(std::string(64, '[') + std::string(64, ']')));
    SH_CHECK(!sh::json::parse(std::string(70, '[') + std::string(70, ']')));
}

SH_TEST(json_writer_layouts_and_numbers) {
    sh::json::Writer compact;
    compact.begin_object().member("a", 1).key("b").begin_array().value(true).null().end_array().end_object();
    SH_CHECK(compact.str() == "{\"a\":1,\"b\":[true,null]}");

    sh::json::Writer pretty(true);
    pretty.begin_object().member("x", "y").key("list").begin_array();
    pretty.inline_next().begin_object().member("k", 1).member("v", std::uint8_t{255}).end_object();
    pretty.end_array().end_object();
    SH_CHECK(pretty.str() == "{\n  \"x\": \"y\",\n  \"list\": [\n    {\"k\": 1, \"v\": 255}\n  ]\n}");

    SH_CHECK(sh::json::format_number(64880.0 / 4096.0) == "15.83984375");
    SH_CHECK(sh::json::format_number(64.0) == "64");
    SH_CHECK(sh::json::format_number(-0.5) == "-0.5");
    SH_CHECK(sh::json::quote("a\"b\\\x01") == "\"a\\\"b\\\\\\u0001\"");
}

SH_TEST(tables_list_matches_the_game_arrays) {
    SH_CHECK(g_AnimTableInfoCount == 31);
    unsigned entries = 0;
    for (unsigned i = 0; i < g_AnimTableInfoCount; ++i) {
        const ShAnimTableInfo& info = *g_AnimTableInfos[i];
        SH_CHECK(info.count <= ANIM_TABLE_MAX_ENTRIES && info.fileCount <= info.count && info.sourceCount >= 1);
        entries += info.count;
    }
    SH_CHECK(entries == 844);
    SH_CHECK(table_named("floatstinger").count == 28 && table_named("floatstinger").fileCount == 26);
}

// A fixture table survives the file round trip unchanged and fills the engine's clip table
// through its fill (the byte identity of the real tables is the import test below).
SH_TEST(tables_json_round_trip_fills_the_clip_table) {
    const ShAnimTableInfo& cat = table_named("cat");
    const AnimTable table = cat_fixture();
    const std::string text = write_anim_table(table, cat);
    auto back = read_anim_table(text, cat, funcs());
    SH_CHECK(back && back.value().entries == table.entries);

    const std::vector<ShAnimEntry> entries = game_entries(table, cat);
    SH_CHECK(entries.size() == cat.count);
    std::vector<sh::chara::game::ClipInfo> clips(entries.size());
    sh::chara::game::fill_clip_entries(clips, entries, {&playback_blend, &playback_once, &playback_loop});
    SH_CHECK(clips[3].playback == &playback_once);
    SH_CHECK(clips[3].status == 3);
    SH_CHECK(clips[3].start_keyframe == 15);
}

SH_TEST(tables_special_entries) {
    // A variable duration: the file names the function, the game gets the PSX address back.
    const ShAnimTableInfo& mc = table_named("monster_cybil");
    const AnimTable table = monster_cybil_fixture(mc);
    SH_CHECK(table.entries[3].variable_duration && table.entries[3].duration_func == "MonsterCybil_GetAnimInfo");
    const std::string text = write_anim_table(table, mc);
    SH_CHECK(text.find("\"duration_func\": \"MonsterCybil_GetAnimInfo\"") != std::string::npos);
    auto back = read_anim_table(text, mc, funcs());
    SH_CHECK(back && back.value().entries == table.entries);
    SH_CHECK(game_entries(back.value(), mc)[3].durationQ12 == static_cast<std::int32_t>(0x800D8898u));

    // FLOATSTINGER: 26 entries in the file; the game array keeps `count` entries and the two past
    // fileCount are zero (they are neighbouring rodata no status reaches).
    const ShAnimTableInfo& fs_info = table_named("floatstinger");
    const AnimTable fs_table = cat_fixture();  // any well-formed fileCount-sized table
    const auto entries = game_entries(fs_table, fs_info);
    SH_CHECK(entries.size() == fs_info.count);
    for (std::size_t i = fs_info.fileCount; i < fs_info.count; ++i) {
        const ShAnimEntry& e = entries[i];
        SH_CHECK(e.kind == 0 && e.status == 0 && e.hasVariableDuration == 0 && e.linkStatus == 0 &&
                 e.durationQ12 == 0 && e.startKeyframeIdx == 0 && e.endKeyframeIdx == 0);
    }
}

SH_TEST(tables_read_errors_name_entry_and_field) {
    const ShAnimTableInfo& cat = table_named("cat");
    const std::string good = write_anim_table(cat_fixture(), cat);
    auto edit = [&](const std::string& from, const std::string& to) { return replace_once(good, from, to); };
    auto error_of = [&](const std::string& text) {
        auto r = read_anim_table(text, cat, funcs());
        SH_CHECK(!r);
        return r ? std::string() : r.error();
    };

    SH_CHECK(read_anim_table(good, cat, funcs()));
    std::string e = error_of(edit("\"speed_q12\": 64880", "\"speed_q12\": \"fast\""));
    SH_CHECK(e.find("entry 3, \"speed\"") != std::string::npos || e.find("entry 3, \"speed_q12\"") != std::string::npos);
    e = error_of(edit("\"speed\": 15.83984375, \"speed_q12\": 64880", "\"speed_q12\": \"fast\""));
    SH_CHECK(e.find("entry 3, \"speed_q12\": expected an integer, got string (line") != std::string::npos);
    e = error_of(edit("\"speed\": 15.83984375", "\"speed\": 20"));
    SH_CHECK(e.find("entry 3, \"speed\": is 20 (speed_q12 81920) but \"speed_q12\" is 64880") != std::string::npos);
    e = error_of(edit("\"kind\": \"playback_once\"", "\"kind\": \"once\""));
    SH_CHECK(e.find("entry 3, \"kind\": \"once\" is not one of") != std::string::npos);
    e = error_of(edit("\"index\": 5, \"kind\": \"playback_loop\", \"status\": 5, \"variable_duration\": false, \"link\": 255",
                      "\"index\": 5, \"kind\": \"playback_loop\", \"status\": 5, \"variable_duration\": false, \"link\": 10"));
    SH_CHECK(e.find("entry 5, \"link\": 10 is past the last entry (9)") != std::string::npos);
    e = error_of(edit("\"status\": 5,", "\"status\": 256,"));
    SH_CHECK(e.find("entry 5, \"status\": 256 is outside 0..255") != std::string::npos);
    e = error_of(edit("\"end_kf\": 43}", "\"end_kf\": 43, \"start_kf\": 1.5}"));
    SH_CHECK(e.find("\"start_kf\": expected an integer, got a fraction") != std::string::npos);
    e = error_of(edit("\"variable_duration\": false, \"link\": 3", "\"link\": 3"));
    SH_CHECK(e.find("entry 2, \"variable_duration\": missing") != std::string::npos);
    e = error_of(edit("\"symbol\": \"CAT_ANIM_INFOS\"", "\"symbol\": \"DOG_ANIM_INFOS\""));
    SH_CHECK(e.find("\"symbol\" must be \"CAT_ANIM_INFOS\"") != std::string::npos);
    // An older import named a table by the address of its first disc copy: still this table.
    SH_CHECK(read_anim_table(edit("\"symbol\": \"CAT_ANIM_INFOS\"", "\"symbol\": \"D_800DC924\""), cat, funcs()));
    e = error_of(good.substr(0, good.size() / 2));
    SH_CHECK(e.find("not valid JSON: line ") != std::string::npos);
    e = error_of(edit("\"index\": 9", "\"index\": 8"));
    SH_CHECK(e.find("entry 9, \"index\"") != std::string::npos);

    const auto last_entry = good.rfind(",\n    {\"index\": 9");
    std::string short_table = good;
    short_table.erase(last_entry, short_table.find('}', last_entry + 2) + 1 - last_entry);
    e = error_of(short_table);
    SH_CHECK(e.find("\"entries\" has 9 entries, the game table has 10") != std::string::npos);

    // speed alone, and speed_q12 alone, are both enough; speed is truncated like Q12().
    auto only_speed = read_anim_table(edit("\"speed\": 15.83984375, \"speed_q12\": 64880", "\"speed\": 15.84"), cat, funcs());
    SH_CHECK(only_speed && only_speed.value().entries[3].speed_q12 == 64880);
    auto only_q12 = read_anim_table(edit("\"speed\": 15.83984375, \"speed_q12\": 64880", "\"speed_q12\": 65536"), cat, funcs());
    SH_CHECK(only_q12 && only_q12.value().entries[3].speed_q12 == 65536);

    const ShAnimTableInfo& mc = table_named("monster_cybil");
    const std::string mc_text = write_anim_table(monster_cybil_fixture(mc), mc);
    std::string bad_func = mc_text;
    bad_func.replace(bad_func.find("MonsterCybil_GetAnimInfo"), std::string("MonsterCybil_GetAnimInfo").size(), "func_00000000");
    auto r = read_anim_table(bad_func, mc, funcs());
    SH_CHECK(!r && r.error().find("entry 3, \"duration_func\": \"func_00000000\" is not a function the game knows") !=
                       std::string::npos);
}

SH_TEST(tables_store_creates_keeps_and_rewrites_files) {
    const fs::path dir = scratch("store");
    const ShAnimTableInfo& cat = table_named("cat");
    const AnimTable table = cat_fixture();
    const fs::path file = dir / "tables" / "anim" / "cat.json";

    // gamedata/ that does not exist is not created; without the disc there are no values at all.
    {
        Logged log;
        TableStore store(store_config(dir / "absent", log));
        const auto loaded = store.load_anim(cat);
        SH_CHECK(!loaded.from_file);
        SH_CHECK(loaded.entries.size() == cat.count);
        for (const ShAnimEntry& e : loaded.entries) {
            const ShAnimEntry zero{};
            SH_CHECK(std::memcmp(&e, &zero, sizeof e) == 0);
        }
        SH_CHECK(log.all().find("E [TABLES] tables/anim/cat.json is missing and so are the game files (raw/, "
                                "convert/files/): run the disc import") != std::string::npos);
        SH_CHECK(!fs::exists(dir / "absent"));
    }
    // Missing file: written from the disc (raw/) and read back.
    write_disc(dir, cat, table);
    {
        Logged log;
        TableStore store(store_config(dir, log));
        const auto loaded = store.load_anim(cat);
        SH_CHECK(loaded.from_file && same(loaded.entries, table, cat));
        SH_CHECK(fs::exists(file));
        SH_CHECK(log.all().find("created from the disc") != std::string::npos);
        SH_CHECK(!store.gameplay_changed());
    }
    // An edited speed is used, reported as a gameplay change, and the file is kept.
    const std::string text = replace_once(read_text(file), "\"speed\": 15.83984375, \"speed_q12\": 64880", "\"speed\": 30");
    write_text(file, text);
    {
        Logged log;
        TableStore store(store_config(dir, log));
        const auto loaded = store.load_anim(cat);
        SH_CHECK(loaded.from_file && loaded.entries[3].durationQ12 == 30 * 4096);
        SH_CHECK(std::memcmp(&loaded.entries[4], &game_entries(table, cat)[4], 6 * sizeof(ShAnimEntry)) == 0);
        SH_CHECK(store.gameplay_changed());
        SH_CHECK(log.all().find("1 entries differ from the disc (3): gameplay change") != std::string::npos);
        SH_CHECK(read_text(file) == text);
    }
    // import_overwrite = 1 rewrites it from the disc.
    {
        Logged log;
        StoreConfig c = store_config(dir, log);
        std::vector<std::string> warnings;
        apply_config_text(c, "import_overwrite = 1 # reimport\n", warnings);
        SH_CHECK(warnings.empty() && c.overwrite);
        TableStore store(std::move(c));
        SH_CHECK(same(store.load_anim(cat).entries, table, cat));
        SH_CHECK(read_text(file).find("\"speed\": 15.83984375") != std::string::npos);
    }
    // import_tables is not a key: the line is a warning and changes nothing.
    {
        fs::remove(file);
        Logged log;
        StoreConfig c = store_config(dir, log);
        std::vector<std::string> warnings;
        apply_config_text(c, "import_tables = none\n", warnings);
        SH_CHECK(warnings.size() == 1 && warnings[0].find("import_tables is gone") != std::string::npos);
        TableStore store(std::move(c));
        const auto loaded = store.load_anim(cat);
        SH_CHECK(loaded.from_file && same(loaded.entries, table, cat) && fs::exists(file));
        apply_config_text(c, "import_tables = yaml\n", warnings);
        SH_CHECK(warnings.size() == 2);
    }
    std::error_code ec;
    fs::remove_all(dir, ec);
}

SH_TEST(tables_store_broken_file_falls_back_with_an_error) {
    const fs::path dir = scratch("broken");
    const ShAnimTableInfo& cat = table_named("cat");
    const fs::path file = dir / "tables" / "anim" / "cat.json";
    write_text(file, "{\"format\": \"sh-anim-table\", \"version\": 1, \"entries\": [\n  {\"kind\": \"blend_linear\",}\n]}");
    Logged log;
    TableStore store(store_config(dir, log));
    const auto loaded = store.load_anim(cat);
    SH_CHECK(!loaded.from_file);
    SH_CHECK(loaded.entries.size() == cat.count);
    for (const ShAnimEntry& e : loaded.entries) {
        const ShAnimEntry zero{};
        SH_CHECK(std::memcmp(&e, &zero, sizeof e) == 0);
    }
    const std::string all = log.all();
    SH_CHECK(all.find("E [TABLES] " + file.string() + ": not valid JSON: line 2, column 27") != std::string::npos);
    SH_CHECK(all.find("the table stays zero") != std::string::npos);
    SH_CHECK(read_text(file).find("blend_linear\",}") != std::string::npos);  // a broken file is not replaced
    std::error_code ec;
    fs::remove_all(dir, ec);
}

SH_TEST(tables_store_mod_file_wins_and_marks_gameplay) {
    const fs::path dir = scratch("mods");
    const ShAnimTableInfo& cat = table_named("cat");
    const fs::path gamedata = dir / "gamedata";
    fs::create_directories(gamedata);
    // The disc copy of CAT is there, so the mod's one edited entry is exactly the gameplay change.
    write_disc(gamedata, cat, cat_fixture());
    const std::string text = replace_once(write_anim_table(cat_fixture(), cat),
                                          "\"speed\": 35, \"speed_q12\": 143360", "\"speed_q12\": 70000");
    write_text(dir / "mods" / "fastcat" / "mod.json", R"({"id": "fastcat", "version": "1.0.0", "priority": 5})");
    write_text(dir / "mods" / "fastcat" / "tables" / "anim" / "cat.json", text);

    Logged log;
    StoreConfig c = store_config(gamedata, log);
    c.mods_dir = dir / "mods";
    TableStore store(std::move(c));
    const auto loaded = store.load_anim(cat);
    SH_CHECK(loaded.from_file && loaded.entries[5].durationQ12 == 70000);
    SH_CHECK(store.gameplay_changed());
    const std::string all = log.all();
    SH_CHECK(all.find("gameplay mod 'fastcat' replaces tables/anim/cat.json (1 entries differ") != std::string::npos);
    SH_CHECK(all.find("does not say \"gameplay\": true") != std::string::npos);
    // Tables the mod does not have: their files and disc copies are missing too, so the array stays
    // zero and the error says so.
    const auto lisa = store.load_anim(table_named("lisa"));
    SH_CHECK(!lisa.from_file);
    SH_CHECK(lisa.entries.size() == table_named("lisa").count);
    for (const ShAnimEntry& e : lisa.entries) {
        const ShAnimEntry zero{};
        SH_CHECK(std::memcmp(&e, &zero, sizeof e) == 0);
    }
    SH_CHECK(log.all().find("E [TABLES] tables/anim/lisa.json is missing") != std::string::npos);
    SH_CHECK(!fs::exists(gamedata / "tables" / "anim" / "lisa.json"));
    std::error_code ec;
    fs::remove_all(dir, ec);
}

SH_TEST(tables_c_facade) {
    const fs::path dir = scratch("facade");
    write_text(dir / "config.cfg", "import_overwrite = 0\n");
    const std::string gamedata = dir.string();
    const std::string cfg = (dir / "config.cfg").string();
    ShTablesConfig c{};
    c.gamedataDir = gamedata.c_str();
    c.configPath = cfg.c_str();
    c.funcs = g_AnimTableFuncs;
    c.funcCount = g_AnimTableFuncCount;
    static std::vector<std::string> lines;
    lines.clear();
    c.log = [](int, const char* m) { lines.emplace_back(m); };
    const ShAnimTableInfo& mc = table_named("monster_cybil");
    const AnimTable table = monster_cybil_fixture(mc);
    write_disc(dir, mc, table);
    ShTables_Configure(&c);
    std::vector<ShAnimEntry> out(mc.count);
    SH_CHECK(ShTables_LoadAnim(&mc, out.data()) == 1);
    const std::vector<ShAnimEntry> want = game_entries(table, mc);
    SH_CHECK(std::memcmp(out.data(), want.data(), sizeof(ShAnimEntry) * mc.count) == 0);
    SH_CHECK(ShTables_GameplayChanged() == 0);
    SH_CHECK(fs::exists(dir / "tables" / "anim" / "monster_cybil.json"));
    const ShTablesConfig reset{};
    ShTables_Configure(&reset);
    std::error_code ec;
    fs::remove_all(dir, ec);
}

// A disc copy that differs from the first source becomes <name>@<overlay>.json and is used while that overlay
// is loaded; other overlays keep <name>.json.
SH_TEST(tables_store_overlay_variants) {
    const fs::path dir = scratch("variants");
    const ShAnimTableInfo& kau = table_named("kaufmann");
    SH_CHECK(kau.sourceCount == 5 && std::string(kau.sources[0].file) == "VIN/MAP3_S00.BIN");
    AnimTable table = cat_fixture();  // any well-formed table of kau.fileCount entries
    table.symbol = kau.symbol;  // write_anim_table/read_anim_table check the symbol of this table
    while (table.entries.size() < kau.fileCount) {
        const std::size_t i = table.entries.size();
        table.entries.push_back(entry_of(i % 2 ? AnimKind::PlaybackLoop : AnimKind::BlendLinear,
                                         static_cast<std::uint8_t>(i), false, 255, 4096, 0, 1));
    }
    write_disc(dir, kau, table);
    std::vector<ShAnimEntry> other = game_entries(table, kau);
    other[7].durationQ12 = 32768;
    const ShAnimSource* map5_s02 = nullptr;
    for (std::size_t i = 0; i < kau.sourceCount; ++i) {
        if (std::string(kau.sources[i].file) == "VIN/MAP5_S02.BIN") {
            map5_s02 = &kau.sources[i];
        }
    }
    SH_CHECK(map5_s02 != nullptr);
    write_overlay(dir, *map5_s02, other.data(), kau.fileCount);

    const DefaultAnimTables defaults = default_anim_tables(dir, kau, funcs());
    SH_CHECK(defaults.variants.size() == 1 && defaults.variants[0].first == "MAP5_S02");
    {
        Logged log;
        TableStore store(store_config(dir, log));
        SH_CHECK(same(store.load_anim(kau).entries, table, kau));
        SH_CHECK(same(store.load_anim(kau, "MAP3_S00").entries, table, kau));
        const auto in_map5 = store.load_anim(kau, "MAP5_S02");
        SH_CHECK(in_map5.from_file && in_map5.entries[7].durationQ12 == 32768 &&
                 in_map5.entries[6].durationQ12 == game_entries(table, kau)[6].durationQ12);
        SH_CHECK(fs::exists(dir / "tables" / "anim" / "kaufmann.json"));
        SH_CHECK(fs::exists(dir / "tables" / "anim" / "kaufmann@MAP5_S02.json"));
        SH_CHECK(read_text(dir / "tables" / "anim" / "kaufmann@MAP5_S02.json").find("\"overlay\": \"MAP5_S02\"") !=
                 std::string::npos);
        SH_CHECK(!store.gameplay_changed());
    }
    // A mod's variant file wins in its overlay only.
    write_text(dir / "mods" / "m" / "mod.json", R"({"id": "m", "version": "1.0.0", "gameplay": true})");
    write_text(dir / "mods" / "m" / "tables" / "anim" / "kaufmann@MAP7_S02.json",
               replace_once(read_text(dir / "tables" / "anim" / "kaufmann.json"),
                            "\"index\": 7, \"kind\": \"playback_loop\"", "\"index\": 7, \"kind\": \"playback_once\""));
    {
        Logged log;
        StoreConfig c = store_config(dir, log);
        c.mods_dir = dir / "mods";
        TableStore store(std::move(c));
        SH_CHECK(store.load_anim(kau, "MAP7_S02").entries[7].kind == 1);
        SH_CHECK(store.load_anim(kau, "MAP7_S03").entries[7].kind == 2);
        SH_CHECK(store.load_anim(kau, "MAP5_S02").entries[7].durationQ12 == 32768);
        SH_CHECK(log.all().find("gameplay mod 'm' replaces tables/anim/kaufmann@MAP7_S02.json in MAP7_S02") !=
                 std::string::npos);
    }
    std::error_code ec;
    fs::remove_all(dir, ec);
}

// sh-import writes all tables from the disc (one file per table and one per overlay whose copy differs: DAHLIA in
// five later overlays, CYBIL in MAP6_S01, KAUFMANN in MAP5_S02) and every copy in raw/ (kept for the check) matches
// its file.
SH_TEST(tables_import_writes_and_checks_against_raw) {
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
    SH_CHECK(out.str().find("tables:         38 written, 0 kept; raw/ check: 70 copies, 0 differ in 0 entries") !=
             std::string::npos);
    SH_CHECK(err.str().find("warning: tables") == std::string::npos);
    for (const char* name : {"cat", "groaner", "puppet_nurse_a", "puppet_nurse_b", "dahlia@MAP3_S06", "dahlia@MAP6_S01",
                             "dahlia@MAP6_S04", "dahlia@MAP7_S02", "dahlia@MAP7_S03", "cybil@MAP6_S01", "kaufmann@MAP5_S02"}) {
        SH_CHECK(fs::exists(out_dir / "tables" / "anim" / (std::string(name) + ".json")));
    }

    // Nothing is compiled in, so the files ARE the tables. Every disc copy must equal the file
    // the game uses for that overlay: the <name>@<overlay>.json variant for a copy that differs from
    // the base copy, else <name>.json. A differing copy without its variant file is a failure.
    {
        unsigned copies = 0;
        unsigned differ = 0;
        for (unsigned t = 0; t < g_AnimTableInfoCount; ++t) {
            const ShAnimTableInfo& info = *g_AnimTableInfos[t];
            for (unsigned s = 0; s < info.sourceCount; ++s) {
                const ShAnimSource& source = info.sources[s];
                std::ifstream in(out_dir / "raw" / source.file, std::ios::binary);
                SH_CHECK(in);
                ++copies;
                if (!in) {
                    ++differ;
                    continue;
                }
                const std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(in)),
                                                       std::istreambuf_iterator<char>());
                auto disc = anim_table_from_overlay(info, bytes, source.va, funcs());
                SH_CHECK(disc);
                if (!disc) {
                    ++differ;
                    continue;
                }
                const std::string overlay = overlay_of(source);
                auto read_file = [&](const fs::path& path) {
                    auto file = read_anim_table(read_text(path), info, funcs());
                    SH_CHECK(file);
                    return file;
                };
                const fs::path base_file = out_dir / anim_table_path(info);
                if (s == 0) {
                    auto base = read_file(base_file);
                    if (!base || !diff_anim_tables(disc.value(), base.value()).empty()) {
                        ++differ;
                        std::printf("    %s: raw/%s differs from tables/anim/%s.json\n", info.name, source.file,
                                    info.name);
                    }
                    continue;
                }
                const fs::path variant = out_dir / anim_table_path(info, overlay);
                if (fs::is_regular_file(variant)) {
                    auto file = read_file(variant);
                    if (!file || !diff_anim_tables(disc.value(), file.value()).empty()) {
                        ++differ;
                        std::printf("    %s: raw/%s differs from the variant %s\n", info.name, source.file,
                                    variant.filename().string().c_str());
                    }
                } else {
                    auto base = read_file(base_file);
                    if (!base || !diff_anim_tables(disc.value(), base.value()).empty()) {
                        ++differ;
                        std::printf("    %s: raw/%s differs from the base and %s was not written\n", info.name,
                                    source.file, variant.filename().string().c_str());
                    }
                }
            }
        }
        std::printf("    disc vs game values: %u copies, %u differ\n", copies, differ);
        SH_CHECK(copies == 70);
        SH_CHECK(differ == 0);
    }

    // A second pass keeps the files.
    const sh::import_tool::TablesReport again = sh::import_tool::generate_tables(out_dir, false);
    SH_CHECK(again.written == 0 && again.kept == 38 && again.failed == 0 && again.copies_differ == 0);
    std::error_code ec;
    fs::remove_all(dir, ec);
}

// Without the disc the importer fails every table instead of silently writing the seed values.
SH_TEST(tables_import_without_the_disc_fails_instead_of_writing_seeds) {
    const fs::path dir = scratch("import-no-disc");
    const sh::import_tool::TablesReport report = sh::import_tool::generate_tables(dir, false);
    SH_CHECK(report.failed == g_AnimTableInfoCount);
    SH_CHECK(report.written == 0 && report.kept == 0);
    bool mentions_raw = false;
    for (const std::string& w : report.warnings) {
        mentions_raw = mentions_raw || w.find("raw/") != std::string::npos;
    }
    SH_CHECK(mentions_raw);
    std::error_code ec;
    SH_CHECK(!fs::exists(dir / "tables", ec));
    fs::remove_all(dir, ec);
}
