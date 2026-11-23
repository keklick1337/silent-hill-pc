// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The engine's save game (src/engine/save/save_game.h): the game's running state -> SaveGame -> JSON -> SaveGame ->
// the running state gives the same bytes, over many generated states; an old (format 1) record with its NAME and
// OPTS chunks gives the same state as the running record it was made from; names this build does not know.
#include "framework/sh_test.h"
#include "engine/save/footer_checksum.h"
#include "engine/save/console_records.h"
#include "engine/save/save_file.h"
#include "engine/save/save_game.h"
#include "engine/save/save_store.h"

#include <cstdint>
#include <filesystem>
#include <random>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;
using namespace sh::save;

// A build's names: items 1..120 "test:ItemN", every flag bit but each fifth "test:FlagN", maps 0..44 "mapN",
// paper maps 0..23 "PaperN". Strings live in static tables.
const std::vector<std::string>& texts(const char* prefix, int count) {
    static std::vector<std::vector<std::string>> all;
    static std::vector<std::string> prefixes;
    for (std::size_t i = 0; i < prefixes.size(); ++i) {
        if (prefixes[i] == prefix) {
            return all[i];
        }
    }
    std::vector<std::string> out;
    for (int i = 0; i < count; ++i) {
        out.push_back(prefix + std::to_string(i));
    }
    prefixes.emplace_back(prefix);
    all.push_back(std::move(out));
    return all.back();
}

int parse(const char* name, const char* prefix, int count) {
    const std::string s(name), p(prefix);
    if (!s.starts_with(p) || s.size() == p.size()) {
        return -1;
    }
    int n = 0;
    for (std::size_t i = p.size(); i < s.size(); ++i) {
        if (s[i] < '0' || s[i] > '9') {
            return -1;
        }
        n = n * 10 + (s[i] - '0');
    }
    return n < count ? n : -1;
}

NameTable test_names() {
    NameTable t;
    t.item_name = [](int n) -> const char* {
        return n >= 1 && n <= 120 ? texts("test:Item", 121)[n].c_str() : nullptr;
    };
    t.item_number = [](const char* s) {
        const int n = parse(s, "test:Item", 121);
        return n >= 1 ? n : -1;
    };
    t.flag_name = [](int n) -> const char* {
        return n % 5 != 0 && n < 52 * 32 ? texts("test:Flag", 52 * 32)[n].c_str() : nullptr;
    };
    t.flag_number = [](const char* s) {
        const int n = parse(s, "test:Flag", 52 * 32);
        return n >= 0 && n % 5 != 0 ? n : -1;
    };
    t.map_name = [](int n) -> const char* { return n >= 0 && n < 45 ? texts("map", 45)[n].c_str() : nullptr; };
    t.map_number = [](const char* s) { return parse(s, "map", 45); };
    t.paper_map_name = [](int n) -> const char* {
        return n >= 0 && n < 24 ? texts("Paper", 24)[n].c_str() : nullptr;
    };
    t.paper_map_number = [](const char* s) { return parse(s, "Paper", 24); };
    return t;
}

// A running state as the game could hold it: random bytes, with blank and empty slots, untouched enemy words and
// the four padding bytes zero (the game never writes them, and a save does not keep them).
std::vector<std::uint8_t> random_state(std::mt19937& rng) {
    std::uniform_int_distribution<int> byte(0, 255);
    std::vector<std::uint8_t> b(layout::kStateSize);
    for (auto& v : b) {
        v = static_cast<std::uint8_t>(byte(rng));
    }
    for (int slot = 0; slot < layout::kItemSlots; ++slot) {
        const int kind = byte(rng) % 4;
        if (kind == 0) {  // blank
            b[slot * 4] = 0xFF;
            b[slot * 4 + 1] = b[slot * 4 + 2] = 0;
            b[slot * 4 + 3] = 0xFF;
        } else if (kind == 1) {  // empty with a stale count
            b[slot * 4] = 0xFF;
        }
    }
    for (int m = 0; m < layout::kEnemyStateWords; ++m) {
        if (byte(rng) % 2 == 0) {
            for (int i = 0; i < 4; ++i) {
                b[0xB0 + 4 * m + i] = 0xFF;
            }
        }
    }
    b[0xA1] = b[0xA2] = b[0xA3] = b[0x23E] = 0;
    if (byte(rng) % 2 == 0) {
        b[0xA4] = static_cast<std::uint8_t>(byte(rng) % 45);  // a real map
    }
    return b;
}

std::vector<std::uint8_t> random_options(std::mt19937& rng) {
    std::uniform_int_distribution<int> byte(0, 255);
    std::vector<std::uint8_t> b(layout::kOptionsSize);
    for (auto& v : b) {
        v = static_cast<std::uint8_t>(byte(rng));
    }
    b[0x26] = 0;  // unused
    b[0x30] = b[0x31] = b[0x32] = b[0x33] = 0;  // never given a meaning: the settings do not keep them
    return b;
}

fs::path fresh_dir(const char* name) {
    // Per process: test runs started side by side must not clear each other's folders.
    static const unsigned tag = std::random_device{}();
    const fs::path dir = fs::temp_directory_path() / (std::string(name) + "_" + std::to_string(tag));
    // Removed when the test process ends.
    struct Cleanup {
        std::vector<fs::path> dirs;
        ~Cleanup() {
            for (const fs::path& d : dirs) {
                std::error_code e;
                fs::remove_all(d, e);
            }
        }
    };
    static Cleanup cleanup;
    cleanup.dirs.push_back(dir);
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    return dir;
}

}  // namespace

// state -> SaveGame -> JSON -> SaveGame -> state, with names and without.
SH_TEST(save_game_live_round_trip_many) {
    std::mt19937 rng(20261116);
    const NameTable named = test_names();
    const NameTable none;
    int same = 0;
    constexpr int kStates = 600;
    for (int i = 0; i < kStates; ++i) {
        const auto state = random_state(rng);
        const auto options = random_options(rng);
        const NameTable& names = (i % 3 == 0) ? none : named;
        SaveGame game = from_live_state(console::decode_save_record(state), names);
        game.options = from_live_options(console::decode_settings(options));
        const std::string text = to_json(game);
        auto back = from_json(text);
        if (!back) {
            SH_CHECK(!"from_json failed");
            break;
        }
        Warnings warnings;
        auto live = to_live_state(back.value(), names, warnings);
        const bool ok = back.value() == game && live && console::encode_save_record(live.value()) == state && warnings.empty() &&
                        back.value().options && console::encode_settings(to_live_options(*back.value().options)) == options &&
                        to_json(back.value()) == text;
        same += ok ? 1 : 0;
    }
    SH_CHECK(same == kStates);
}

// The names really are names: a named item, flag, map and paper map appear in the text, their numbers do not
// decide anything when another build numbers them differently.
SH_TEST(save_game_names_survive_renumbering) {
    std::vector<std::uint8_t> state(layout::kStateSize, 0);
    for (int slot = 0; slot < layout::kItemSlots; ++slot) {
        state[slot * 4] = 0xFF;
    }
    for (int m = 0; m < layout::kEnemyStateWords; ++m) {
        for (int i = 0; i < 4; ++i) {
            state[0xB0 + 4 * m + i] = 0xFF;
        }
    }
    state[0] = 7;      // slot 0: test:Item7
    state[1] = 15;     // count
    state[0xAA] = 7;   // equipped
    state[0xA4] = 12;  // map12
    state[0xA9] = 3;   // Paper3
    state[0x164] = 1 << 3;  // Paper3 held
    state[0x168] = 1 << 2;  // test:Flag2
    state[0x168 + 1] = 1;   // bit 8: test:Flag8
    state[0xA8] = 11;       // Room302
    const NameTable a = test_names();
    const SaveGame game = from_live_state(console::decode_save_record(state), a);
    const std::string text = to_json(game);
    for (const char* name : {"\"test:Item7\"", "\"map12\"", "\"Paper3\"", "\"test:Flag2\"", "\"test:Flag8\"",
                             "\"Room302\""}) {
        SH_CHECK(text.find(name) != std::string::npos);
    }

    // Another build numbers items and flags one higher.
    NameTable b = a;
    b.item_number = [](const char* s) {
        const int n = test_names().item_number(s);
        return n >= 0 ? n + 1 : -1;
    };
    b.flag_number = [](const char* s) {
        const int n = test_names().flag_number(s);
        return n >= 0 ? n + 1 : -1;
    };
    Warnings warnings;
    auto live = to_live_state(from_json(text).value(), b, warnings);
    SH_CHECK(live && warnings.empty());
    if (live) {
        SH_CHECK(console::encode_save_record(live.value())[0] == 8 && console::encode_save_record(live.value())[1] == 15 && console::encode_save_record(live.value())[0xAA] == 8);
        SH_CHECK(console::encode_save_record(live.value())[0x168] == (1 << 3) && console::encode_save_record(live.value())[0x168 + 1] == (1 << 1));
        SH_CHECK(console::encode_save_record(live.value())[0xA4] == 12 && console::encode_save_record(live.value())[0xA8] == 11);
    }
}

// What a build does not have: an item slot is left blank, a flag is not set, each with a warning; a map it does not
// have stops the load.
SH_TEST(save_game_unknown_names) {
    SaveGame game;
    game.map = Ref::of_name("map3");
    game.location = Ref::of_name("Cafe");
    game.paper_map = Ref::of_name("Paper1");
    game.equipped = Ref::of_name("mod:GoneGun");
    game.items.push_back({0, Ref::of_name("mod:Gone"), 2, 0, 0});
    game.items.push_back({1, Ref::of_name("test:Item5"), 3, 1, 0});
    game.flags = {"mod:GoneFlag", "test:Flag1"};
    const NameTable names = test_names();
    Warnings warnings;
    auto live = to_live_state(game, names, warnings);
    SH_CHECK(live && warnings.size() == 3);
    if (live) {
        SH_CHECK(console::encode_save_record(live.value())[0] == 0xFF && console::encode_save_record(live.value())[1] == 0);  // blank
        SH_CHECK(console::encode_save_record(live.value())[4] == 5 && console::encode_save_record(live.value())[5] == 3 && console::encode_save_record(live.value())[6] == 1);
        SH_CHECK(console::encode_save_record(live.value())[0xAA] == 0);  // nothing equipped
        SH_CHECK(console::encode_save_record(live.value())[0x168] == (1 << 1));
    }
    game.map = Ref::of_name("mod_map");
    SH_CHECK(!to_live_state(game, names, warnings));
}

// Saves written before the settings became an engine class carry "reserved" among the options (four bytes the game
// never used): they still read, the same as without it, and a save written now does not carry it.
SH_TEST(save_game_json_reads_old_options) {
    SaveGame game;
    game.map = Ref::of_number(1);
    game.location = Ref::of_number(0);
    game.paper_map = Ref::of_number(0);
    game.equipped = Ref::of_number(0);
    GameOptions options;
    options.bind_enter = 0x40;
    options.brightness = 3;
    options.pal_language = 2;
    game.options = options;
    const std::string text = to_json(game);
    SH_CHECK(text.find("\"reserved\"") == std::string::npos);
    std::string old_text = text;
    const auto at = old_text.find("\"pal_language\"");
    SH_CHECK(at != std::string::npos);
    old_text.insert(at, "\"reserved\": 305419896,\n    ");
    auto old_game = from_json(old_text);
    SH_CHECK(old_game && old_game.value() == game);
}

SH_TEST(save_game_json_rejects_damage) {
    SaveGame game;
    game.map = Ref::of_number(1);
    game.location = Ref::of_number(0);
    game.paper_map = Ref::of_number(0);
    game.equipped = Ref::of_number(0);
    const std::string good = to_json(game);
    SH_CHECK(static_cast<bool>(from_json(good)));
    auto bad = [&](std::string from, std::string to) {
        std::string text = good;
        const auto at = text.find(from);
        SH_CHECK(at != std::string::npos);
        text.replace(at, from.size(), to);
        return !from_json(text);
    };
    SH_CHECK(bad("sh-savegame/1", "sh-savegame/9"));
    SH_CHECK(bad("\"room\": 0", "\"room\": \"x\""));
    SH_CHECK(bad("\"special_items\": 0", "\"special_items\": 32"));
    SH_CHECK(bad("\"save_count\": 0", "\"save_count\": 70000"));
    SH_CHECK(bad("\"difficulty\": \"normal\"", "\"difficulty\": \"brutal\""));
    SH_CHECK(!from_json("{"));
    SH_CHECK(!from_json("[]"));
}

// Old save -> new -> the same state: a format 1 file holding the running record (sealed GAME), its options (sealed
// OPTS) and the NAME chunk an old build wrote reads back as exactly the running state, through the store.
SH_TEST(save_game_old_save_same_state_many) {
    std::mt19937 rng(707);
    const fs::path dir = fresh_dir("sh_save_game_old");
    SaveStore store(dir / ".savegames");
    const NameTable names = test_names();
    store.set_names(names);
    int same = 0;
    constexpr int kSaves = 120;
    for (int i = 1; i <= kSaves; ++i) {
        const auto state = random_state(rng);
        const auto options = random_options(rng);
        std::vector<std::uint8_t> game_record = state;
        game_record.resize(layout::kStateRecord, 0);
        seal(game_record);
        std::vector<std::uint8_t> options_record = options;
        options_record.resize(layout::kOptionsRecord, 0);
        seal(options_record);
        // The NAME chunk of format 1: the slots and flags by name, as the old encoder wrote them.
        std::string names_chunk = R"({"format": "sh-save-names/1", "inventory": [)";
        bool first = true;
        for (int slot = 0; slot < layout::kItemSlots; ++slot) {
            const char* name = names.item_name(state[slot * 4]);
            if (state[slot * 4] == 0xFF || name == nullptr) {
                continue;
            }
            names_chunk += std::string(first ? "" : ", ") + R"({"slot": )" + std::to_string(slot) + R"(, "item": ")" +
                           name + "\"}";
            first = false;
        }
        names_chunk += R"(], "flags": [)";
        first = true;
        for (int bit = 0; bit < 52 * 32; ++bit) {
            const char* name = names.flag_name(bit);
            if (name != nullptr && (state[0x168 + bit / 8] >> (bit % 8) & 1)) {
                names_chunk += std::string(first ? "\"" : ", \"") + name + "\"";
                first = false;
            }
        }
        names_chunk += "]}";

        SaveFile f;
        f.version = 1;
        f.kind = FileKind::Slot;
        SlotSummary summary;
        summary.sequence = i;
        f.put(kTagInfo, encode_summary(summary));
        f.put(kTagGame, game_record);
        f.put(kTagNames, std::vector<std::uint8_t>(names_chunk.begin(), names_chunk.end()));
        f.put(kTagOptions, options_record);
        if (!write_file_atomic(store.save_path(i), encode(f))) {
            break;
        }

        const SlotRead r = store.read(i);
        auto game = from_json(r.record.state);
        Warnings warnings;
        auto live = game ? to_live_state(game.value(), names, warnings) : sh::Result<sh::game::SaveRecord>::fail("");
        const bool ok = r.state == SlotState::Ok && r.warnings.empty() && live && console::encode_save_record(live.value()) == state &&
                        game.value().options && console::encode_settings(to_live_options(*game.value().options)) == options &&
                        game.value() == [&] {
                            SaveGame g = from_live_state(console::decode_save_record(state), names);
                            g.options = from_live_options(console::decode_settings(options));
                            return g;
                        }();
        same += ok ? 1 : 0;

        // Saved again (format 2): still the same state.
        if (ok && i % 10 == 0) {
            SlotRecord again;
            again.summary = r.record.summary;
            again.state = r.record.state;
            SH_CHECK(static_cast<bool>(store.write(i, again)));
            SH_CHECK(store.read(i).record.state == r.record.state);
        }
    }
    SH_CHECK(same == kSaves);
    fs::remove_all(dir);
}
