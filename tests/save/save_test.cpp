// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Save store (src/engine/save): the file format, atomic writes, the list of saves (new, overwrite, delete, more
// than two memory cards held), format 1 files read through the conversion, memory card image import, the one-time
// migration of the old gamedata/save/N.MCD files and of the card1/card2 layout. SH_TEST_MEMCARD=<image> also
// imports a real card. The save game itself: save_game_test.cpp.
#include "framework/sh_test.h"
#include "engine/save/memory_card.h"
#include "engine/save/console_records.h"
#include "engine/save/save_file.h"
#include "engine/save/save_game.h"
#include "engine/save/save_store.h"

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <random>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;
using namespace sh::save;

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

void write_bytes(const fs::path& path, const std::vector<std::uint8_t>& bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream os(path, std::ios::binary | std::ios::trunc);
    os.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

std::vector<std::uint8_t> pattern(std::size_t size, std::uint8_t seed) {
    std::vector<std::uint8_t> out(size);
    for (std::size_t i = 0; i < size; ++i) {
        out[i] = static_cast<std::uint8_t>(seed + i * 7);
    }
    return out;
}

// The game's record footer: XOR checksum in the first two bytes, 0xDCDC after them.
void seal(std::uint8_t* record, std::size_t size) {
    std::uint8_t* footer = record + size - 4;
    footer[0] = footer[1] = 0;
    footer[2] = footer[3] = 0xDC;
    std::uint8_t x = 0;
    for (std::size_t i = 0; i < size; ++i) {
        x ^= record[i];
    }
    footer[0] = footer[1] = x;
}

// A formatted 128 KiB card; `files` puts one-block Silent Hill files on it.
struct CardSave {
    int slot;
    std::int32_t sequence;
    std::uint8_t seed;
};

std::vector<std::uint8_t> make_card(const std::vector<std::pair<std::string, std::vector<CardSave>>>& files,
                                    bool damage_list = false) {
    std::vector<std::uint8_t> card(128 * 1024, 0);
    card[0] = 'M';
    card[1] = 'C';
    for (int i = 1; i <= 15; ++i) {
        card[i * 128] = 0xA0;
        card[i * 128 + 8] = card[i * 128 + 9] = 0xFF;
    }
    int block = 1;
    for (const auto& [name, saves] : files) {
        std::uint8_t* dir = card.data() + block * 128;
        dir[0] = 0x51;
        dir[4] = 0x00;
        dir[5] = 0x20;  // 8192 bytes
        dir[8] = dir[9] = 0xFF;
        std::memcpy(dir + 10, name.data(), name.size());
        std::uint8_t* data = card.data() + block * 8192;
        std::memset(data, 0xFF, 8192);
        std::uint8_t* list = data + 0x200;
        std::memset(list, 0, 256);
        for (const CardSave& s : saves) {
            std::uint8_t* e = list + 4 + s.slot * 12;
            e[0] = static_cast<std::uint8_t>(s.sequence);
            e[4] = 0x10;           // play time
            e[8] = 3;              // save count
            e[10] = 7;             // location
            e[11] = 1 | (2 << 1);  // next fear, extra hours 2
            std::uint8_t* save = data + 0x380 + s.slot * 640;
            const auto bytes = pattern(640, s.seed);
            std::memcpy(save, bytes.data(), 640);
            seal(save, 640);
        }
        seal(list, 256);
        if (damage_list) {
            list[17] ^= 0x40;
        }
        std::uint8_t* options = data + 0x300;
        std::memset(options, 0, 128);
        options[0] = 0x42;
        seal(options, 128);
        ++block;
    }
    return card;
}

// A game record of a format 1 save (or a card): the pattern with its footer.
std::vector<std::uint8_t> legacy_record(std::uint8_t seed) {
    auto bytes = pattern(640, seed);
    seal(bytes.data(), bytes.size());
    return bytes;
}

// What a save of that record holds: the same bytes, the four the game never uses (0xA1..0xA3, 0x23E) zero.
std::vector<std::uint8_t> expected_record(std::uint8_t seed) {
    auto bytes = pattern(640, seed);
    bytes[0xA1] = bytes[0xA2] = bytes[0xA3] = bytes[0x23E] = 0;
    seal(bytes.data(), bytes.size());
    return bytes;
}

// The game record a read save stands for (no names installed: everything stays a number).
std::vector<std::uint8_t> record_bytes(const SlotRead& r) {
    auto game = from_json(r.record.state);
    if (!game) {
        return {};
    }
    Warnings warnings;
    auto bytes = to_legacy_record(game.value(), NameTable{}, warnings);
    return bytes ? bytes.value() : std::vector<std::uint8_t>{};
}

std::optional<GameOptions> options_of(const SlotRead& r) {
    auto game = from_json(r.record.state);
    return game ? game.value().options : std::nullopt;
}

std::vector<std::uint8_t> options_record(std::uint8_t first) {
    std::vector<std::uint8_t> bytes(128, 0);
    bytes[0] = first;
    seal(bytes.data(), bytes.size());
    return bytes;
}

SlotRecord record_of(std::uint8_t seed, std::int32_t sequence) {
    SlotRecord r;
    r.summary.sequence = sequence;
    r.summary.play_time = 4096 * 90;
    r.summary.save_count = 2;
    r.summary.location = 5;
    r.legacy_game = legacy_record(seed);
    return r;
}

}  // namespace

SH_TEST(save_format_round_trip) {
    SaveFile file;
    file.kind = FileKind::Slot;
    file.put(kTagInfo, encode_summary(SlotSummary{12, 345, 6, 7, true, 2, 31}));
    file.put(kTagGame, pattern(641, 3));
    file.put(kTagMeta, {'{', '}'});
    const auto bytes = encode(file);
    SH_CHECK(bytes.size() % 4 == 0);
    auto back = decode(bytes);
    SH_CHECK(static_cast<bool>(back));
    if (!back) {
        return;
    }
    SH_CHECK(back.value().kind == FileKind::Slot);
    SH_CHECK(back.value().chunks.size() == 3);
    SH_CHECK(back.value().find(kTagGame)->data == pattern(641, 3));
    auto summary = decode_summary(back.value().find(kTagInfo)->data);
    SH_CHECK(summary && summary.value() == (SlotSummary{12, 345, 6, 7, true, 2, 31}));
}

SH_TEST(save_format_rejects_damage) {
    SaveFile file;
    file.put(kTagGame, pattern(100, 1));
    auto bytes = encode(file);

    auto flipped = bytes;
    flipped[kHeaderSize + 20] ^= 1;
    auto r = decode(flipped);
    SH_CHECK(!r && r.error().find("CRC") != std::string::npos);

    auto cut = bytes;
    cut.pop_back();
    SH_CHECK(!decode(cut));

    auto newer = bytes;
    newer[8] = static_cast<std::uint8_t>(kFormatVersion + 1);
    r = decode(newer);
    SH_CHECK(!r && r.error().find("newer") != std::string::npos);

    std::vector<std::uint8_t> junk(64, 0x11);
    SH_CHECK(!decode(junk));
}

SH_TEST(save_atomic_write_replaces_and_leaves_no_temp) {
    const fs::path dir = fresh_dir("sh_save_atomic");
    const fs::path path = dir / "a" / "b.sav";
    SH_CHECK(static_cast<bool>(write_file_atomic(path, pattern(10, 1))));
    SH_CHECK(static_cast<bool>(write_file_atomic(path, pattern(20, 2))));
    auto back = read_whole_file(path);
    SH_CHECK(back && back.value() == pattern(20, 2));
    SH_CHECK(!fs::exists(dir / "a" / "b.sav.tmp"));

    // The parent is a file: nothing can be written, and the old file stays.
    write_bytes(dir / "blocker", {1});
    SH_CHECK(!write_file_atomic(dir / "blocker" / "c.sav", pattern(4, 4)));
    fs::remove_all(dir);
}

SH_TEST(save_store_new_overwrite_delete) {
    const fs::path dir = fresh_dir("sh_save_store");
    SaveStore store(dir / ".savegames");
    SH_CHECK(store.list().empty() && store.next_number() == 1 && store.newest_sequence() == 0);
    SH_CHECK(store.read(1).state == SlotState::Empty);

    // META comes from the state: save point 1 (Cafe), Easy (-1 is a difficulty, not "none"), 90 s.
    SaveGame cafe;
    cafe.map = Ref::of_number(5);
    cafe.location = Ref::of_name("Cafe");
    cafe.difficulty = -1;
    cafe.play_time = 4096 * 90;
    cafe.options = GameOptions{};
    cafe.options->volume_bgm = 16;
    SlotRecord first;
    first.summary = record_of(9, 4).summary;
    first.state = to_json(cafe);
    auto n = store.write(0, first);
    SH_CHECK(n && n.value() == 1);
    SH_CHECK(fs::exists(dir / ".savegames" / "save_0001.sav"));
    n = store.write(0, record_of(8, 5));
    SH_CHECK(n && n.value() == 2 && store.newest_sequence() == 5);

    SlotRead r = store.read(1);
    SH_CHECK(r.state == SlotState::Ok && r.record.state == first.state && r.record.summary.sequence == 4);
    SH_CHECK(options_of(r) && options_of(r)->volume_bgm == 16);
    SH_CHECK(record_bytes(store.read(2)) == expected_record(8) && !options_of(store.read(2)));
    // A new file holds the state only: no GAME, NAME or OPTS chunk.
    auto file = decode(read_whole_file(store.save_path(2)).value());
    SH_CHECK(file && file.value().version == kFormatVersion && file.value().find(kTagState) != nullptr);
    SH_CHECK(file && file.value().find(kTagGame) == nullptr && file.value().find(kTagNames) == nullptr &&
             file.value().find(kTagOptions) == nullptr);

    std::vector<SaveEntry> list = store.list();
    SH_CHECK(list.size() == 2 && list[0].number == 2 && list[1].number == 1);  // newest first
    SH_CHECK(list[1].location_name == "Cafe" && list[1].has_difficulty && list[1].difficulty == -1);
    SH_CHECK(list[1].difficulty_name == "Easy" && list[1].play_time_seconds == 90 && list[1].saved_at > 1700000000);
    SH_CHECK(list[1].map_index == 5 && list[1].location_id == 1);

    // Overwrite: same number, the newer sequence puts it on top.
    n = store.write(1, record_of(7, 6));
    SH_CHECK(n && n.value() == 1);
    list = store.list();
    SH_CHECK(list.size() == 2 && list[0].number == 1 && list[0].summary.sequence == 6);
    SH_CHECK(record_bytes(store.read(1)) == expected_record(7));

    // Delete: the number is not given out again.
    SH_CHECK(static_cast<bool>(store.remove(2)));
    SH_CHECK(!store.remove(2));
    SH_CHECK(store.list().size() == 1 && store.next_number() == 2);
    SH_CHECK(static_cast<bool>(store.remove(1)));
    SH_CHECK(store.next_number() == 1);

    // Temporary and foreign files are no saves; a damaged one is listed as damaged, not dropped.
    write_bytes(dir / ".savegames" / "save_0007.sav.tmp", {1});
    write_bytes(dir / ".savegames" / "notes.txt", {1});
    write_bytes(dir / ".savegames" / "save_.sav", {1});
    SH_CHECK(store.list().empty());
    write_bytes(dir / ".savegames" / "save_0003.sav", {1, 2, 3});
    list = store.list();
    SH_CHECK(list.size() == 1 && list[0].number == 3 && list[0].state == SlotState::Damaged);
    SH_CHECK(store.read(3).state == SlotState::Damaged && store.next_number() == 4);

    SH_CHECK(static_cast<bool>(store.write_quick(0, record_of(1, 1))));
    SH_CHECK(store.read_quick(0).state == SlotState::Ok);
    SH_CHECK(fs::exists(dir / ".savegames" / "quick" / "quick_01.sav"));
    SH_CHECK(store.list().size() == 1);  // a quick save is not in the list
    fs::remove_all(dir);
}

// No memory card limit: far more saves than two cards of 15 files held, in order, numbered on.
SH_TEST(save_store_keeps_the_remains_chunk) {
    const fs::path dir = fresh_dir("sh_save_remains");
    SaveStore store(dir / ".savegames");
    // Without remains the file has no such chunk: a save is what it was before the option existed.
    SH_CHECK(store.write(0, record_of(7, 3)));
    const auto plain = read_whole_file(dir / ".savegames" / "save_0001.sav");
    SH_CHECK(static_cast<bool>(plain));
    const auto plain_file = decode(plain.value());
    SH_CHECK(static_cast<bool>(plain_file) && plain_file.value().find(kTagRemains) == nullptr);
    SH_CHECK(store.read(1).state == SlotState::Ok && store.read(1).record.remains.empty());

    SlotRecord with = record_of(7, 4);
    with.remains = "{\"format\": \"sh-remains/1\", \"maps\": []}";
    SH_CHECK(store.write(0, with));
    const SlotRead back = store.read(2);
    SH_CHECK(back.state == SlotState::Ok && back.record.remains == with.remains);
    SH_CHECK(record_bytes(back) == expected_record(7));
    // The quick slot carries it the same way.
    SH_CHECK(store.write_quick(0, with));
    SH_CHECK(store.read_quick(0).record.remains == with.remains);
}

SH_TEST(save_store_many_saves) {
    const fs::path dir = fresh_dir("sh_save_many");
    SaveStore store(dir / ".savegames");
    constexpr int kCount = 40;
    for (int i = 1; i <= kCount; ++i) {
        auto n = store.write(0, record_of(static_cast<std::uint8_t>(i), store.newest_sequence() + 1));
        SH_CHECK(n && n.value() == i);
    }
    SH_CHECK(fs::exists(dir / ".savegames" / "save_0040.sav"));
    std::vector<SaveEntry> list = store.list();
    SH_CHECK(static_cast<int>(list.size()) == kCount);
    bool ordered = true;
    for (int i = 0; i < static_cast<int>(list.size()); ++i) {
        ordered = ordered && list[i].number == kCount - i && list[i].summary.sequence == kCount - i &&
                  list[i].state == SlotState::Ok;
    }
    SH_CHECK(ordered);
    SH_CHECK(record_bytes(store.read(17)) == expected_record(17));

    // Overwriting an old one moves it to the top; deleting from the middle keeps the rest.
    SH_CHECK(static_cast<bool>(store.write(5, record_of(99, store.newest_sequence() + 1))));
    SH_CHECK(static_cast<bool>(store.remove(20)));
    list = store.list();
    SH_CHECK(static_cast<int>(list.size()) == kCount - 1 && list[0].number == 5 && list[1].number == kCount);
    SH_CHECK(store.next_number() == kCount + 1);
    fs::remove_all(dir);
}

// Format 1 files (GAME, NAME, OPTS) still read: the records become a state, the names win over the numbers, and
// a record that fails the game's checksum is damaged. Saving over one writes format 2.
SH_TEST(save_format1_files_read) {
    const fs::path dir = fresh_dir("sh_save_format1");
    SaveStore store(dir / ".savegames");
    NameTable names;
    names.item_name = [](int n) -> const char* { return n == 3 ? "test:Three" : n == 9 ? "test:Nine" : nullptr; };
    names.item_number = [](const char* s) { return std::string(s) == "test:Three" ? 3 : std::string(s) == "test:Nine" ? 9 : -1; };
    store.set_names(names);

    auto old_file = [&](int number, std::uint8_t seed, const std::string& names_chunk, bool options, bool damage) {
        SaveFile f;
        f.version = 1;
        f.kind = FileKind::Slot;
        const SlotRecord r = record_of(seed, number);
        f.put(kTagInfo, encode_summary(r.summary));
        auto game = r.legacy_game;
        if (damage) {
            game[100] ^= 1;
        }
        f.put(kTagGame, game);
        if (!names_chunk.empty()) {
            f.put(kTagNames, std::vector<std::uint8_t>(names_chunk.begin(), names_chunk.end()));
        }
        if (options) {
            f.put(kTagOptions, options_record(0x42));
        }
        SH_CHECK(static_cast<bool>(write_file_atomic(store.save_path(number), encode(f))));
    };
    old_file(1, 3, "", true, false);
    old_file(2, 3, R"({"format": "sh-save-names/1", "inventory": [{"slot": 0, "item": "test:Nine"}, {"slot": 1, "item": "mod:Gone"}]})", false, false);
    old_file(3, 4, "", false, true);

    SlotRead r = store.read(1);
    SH_CHECK(r.state == SlotState::Ok && r.record.legacy_game == legacy_record(3));
    SH_CHECK(options_of(r) && options_of(r)->bind_enter == 0x42);
    Warnings warnings;
    auto back = to_legacy_record(from_json(r.record.state).value(), names, warnings);
    SH_CHECK(back && back.value() == expected_record(3) && warnings.empty());

    // Slot 0 holds item 3 in the record and test:Nine by name: the name wins; slot 1's unknown name keeps its number.
    r = store.read(2);
    SH_CHECK(r.state == SlotState::Ok && r.warnings.size() == 1);
    auto game = from_json(r.record.state);
    SH_CHECK(game && game.value().items.size() == 40 && game.value().items[0].item == Ref::of_name("test:Nine"));
    SH_CHECK(game && game.value().items[1].item == Ref::of_number(legacy_record(3)[4]));

    SH_CHECK(store.read(3).state == SlotState::Damaged && store.read(3).error.find("checksum") != std::string::npos);

    // Saving over a format 1 file: format 2, same state.
    const std::string state = store.read(1).record.state;
    SlotRecord again;
    again.summary = store.read(1).record.summary;
    again.state = state;
    SH_CHECK(static_cast<bool>(store.write(1, again)));
    auto file = decode(read_whole_file(store.save_path(1)).value());
    SH_CHECK(file && file.value().version == kFormatVersion && file.value().find(kTagGame) == nullptr);
    SH_CHECK(store.read(1).record.state == state);
    fs::remove_all(dir);
}

SH_TEST(save_memory_card_import_synthetic) {
    const fs::path dir = fresh_dir("sh_save_card");
    SaveStore store(dir / ".savegames");
    const auto card = make_card({{"BASLUS-00707SILENT02", {{0, 5, 11}, {10, 7, 22}}},
                                 {"BESLES-01514SILENT04", {{3, 6, 33}}},
                                 {"SOMEOTHERGAME000000", {}}});
    auto files = read_memory_card(card);
    SH_CHECK(files && files.value().size() == 3);

    auto stats = import_memory_card(card, store);
    SH_CHECK(stats && stats.value().files == 2 && stats.value().saves == 3);
    // Oldest first: sequences 5, 6, 7 become saves 1, 2, 3.
    std::vector<SaveEntry> list = store.list();
    SH_CHECK(list.size() == 3 && list[0].number == 3 && list[0].summary.sequence == 7 && list[2].summary.sequence == 5);
    SlotRead r = store.read(3);
    SH_CHECK(r.state == SlotState::Ok && r.record.summary.sequence == 7);
    SH_CHECK(r.record.summary.next_fear && r.record.summary.extra_hours == 2 && r.record.summary.location == 7);
    SH_CHECK(record_bytes(r) == expected_record(22));
    SH_CHECK(options_of(store.read(2)) && options_of(store.read(2))->bind_enter == 0x42);

    // Again: nothing new.
    stats = import_memory_card(card, store);
    SH_CHECK(stats && stats.value().saves == 0 && stats.value().already_there == 3 && store.list().size() == 3);

    // A list that fails its checksum: the file is damaged for the game and is not imported.
    SaveStore other(dir / "other");
    stats = import_memory_card(make_card({{"BASLUS-00707SILENT00", {{0, 1, 1}}}}, true), other);
    SH_CHECK(stats && stats.value().damaged == 1 && stats.value().saves == 0 && other.list().empty());

    // Wrapped (DexDrive) images and junk.
    std::vector<std::uint8_t> gme(3904, 0);
    std::memcpy(gme.data(), "123-456-STD", 11);
    gme.insert(gme.end(), card.begin(), card.end());
    SH_CHECK(read_memory_card(gme) && read_memory_card(gme).value().size() == 3);
    SH_CHECK(!read_memory_card(std::vector<std::uint8_t>(1000, 0)));
    fs::remove_all(dir);
}

SH_TEST(save_legacy_migration_once_and_files_kept) {
    const fs::path dir = fresh_dir("sh_save_migrate");
    const fs::path old_dir = dir / "gamedata" / "save";
    const auto card1 = make_card({{"BASLUS-00707SILENT00", {{0, 1, 1}, {1, 2, 2}}}});
    const auto card2 = make_card({{"BASLUS-00707SILENT01", {{5, 3, 3}}}});
    write_bytes(old_dir / "0.MCD", card1);
    write_bytes(old_dir / "8.MCD", card2);
    write_bytes(old_dir / "notacard.bin", {1, 2});

    SaveStore store(dir / ".savegames");
    // The same directory twice (as the current and the exe directory): imported once.
    MigrationReport report = migrate_legacy_cards(store, {old_dir, dir / "gamedata" / ".." / "gamedata" / "save"});
    SH_CHECK(report.sources == 2 && report.imported == 2 && report.saves == 3 && store.list().size() == 3);
    SH_CHECK(fs::exists(dir / ".savegames" / "migration.json"));
    SH_CHECK(fs::exists(old_dir / "0.MCD") && fs::exists(old_dir / "8.MCD"));
    auto still = read_whole_file(old_dir / "0.MCD");
    SH_CHECK(still && still.value() == card1);

    report = migrate_legacy_cards(store, {old_dir});
    SH_CHECK(report.imported == 0 && report.saves == 0);

    // A card file that changed since (an old build played again) is imported again: only the new save is added.
    write_bytes(old_dir / "0.MCD", make_card({{"BASLUS-00707SILENT00", {{0, 1, 1}, {1, 2, 2}, {2, 9, 9}}}}));
    report = migrate_legacy_cards(store, {old_dir});
    SH_CHECK(report.imported == 1 && report.saves == 1 && store.list().size() == 4);
    SH_CHECK(store.list()[0].summary.sequence == 9);

    // Images dropped into .savegames/import/ (or its card1/ card2/ of older builds), once each.
    write_bytes(dir / ".savegames" / "import" / "console.mcr", make_card({{"BISLPM-86192SILENT07", {{2, 4, 4}}}}));
    write_bytes(dir / ".savegames" / "import" / "card2" / "old.gme",
                [] {
                    std::vector<std::uint8_t> gme(3904, 0);
                    std::memcpy(gme.data(), "123-456-STD", 11);
                    const auto c = make_card({{"BASLUS-00707SILENT03", {{1, 12, 12}}}});
                    gme.insert(gme.end(), c.begin(), c.end());
                    return gme;
                }());
    report = import_drop_folder(store);
    SH_CHECK(report.imported == 2 && report.saves == 2 && store.list().size() == 6);
    SH_CHECK(import_drop_folder(store).imported == 0);
    fs::remove_all(dir);
}

// The card1/card2 layout of the previous builds is copied into the list once; the old files stay.
SH_TEST(save_card_layout_migration) {
    const fs::path dir = fresh_dir("sh_save_layout");
    const fs::path root = dir / ".savegames";
    SaveStore store(root);
    NameTable names;
    names.item_number = [](const char* s) { return std::string(s) == "test:Old" ? 7 : -1; };
    names.item_name = [](int n) -> const char* { return n == 7 ? "test:Old" : nullptr; };
    store.set_names(names);

    auto legacy_slot = [&](int card, int file, int slot, std::uint8_t seed, std::int32_t sequence, bool names) {
        SaveFile f;
        f.kind = FileKind::Slot;
        const SlotRecord r = record_of(seed, sequence);
        f.put(kTagInfo, encode_summary(r.summary));
        f.version = 1;
        f.put(kTagGame, r.legacy_game);
        if (names) {
            const std::string n = R"({"format": "sh-save-names/1", "inventory": [{"slot": 2, "item": "test:Old"}]})";
            f.put(kTagNames, std::vector<std::uint8_t>(n.begin(), n.end()));
        }
        SH_CHECK(static_cast<bool>(write_file_atomic(store.legacy_slot_path(card, file, slot), encode(f))));
    };
    legacy_slot(0, 0, 0, 1, 3, true);
    legacy_slot(0, 0, 4, 2, 1, false);
    legacy_slot(1, 14, 10, 3, 2, false);
    SaveFile group;
    group.kind = FileKind::Group;
    group.version = 1;
    group.put(kTagOptions, options_record(6));
    SH_CHECK(static_cast<bool>(write_file_atomic(store.legacy_group_path(0, 0), encode(group))));
    write_bytes(store.legacy_slot_path(1, 2, 0), {9, 9, 9});  // damaged: reported, not copied

    // A save already in the list (the same summary and state) is not doubled.
    SH_CHECK(static_cast<bool>(store.write(0, record_of(3, 2))));

    MigrationReport report = migrate_card_layout(store);
    SH_CHECK(report.saves == 2 && report.imported == 3);
    std::vector<SaveEntry> list = store.list();
    SH_CHECK(list.size() == 3);
    // Oldest first: sequence 1 -> save 2, sequence 3 -> save 3 (save 1 was there).
    SH_CHECK(list[0].number == 3 && list[0].summary.sequence == 3 && list[2].summary.sequence == 1);
    SlotRead r = store.read(3);
    SH_CHECK(options_of(r) && options_of(r)->bind_enter == 6);
    // Slot 2 is named test:Old: the store knows the name, so the save holds it by name.
    SH_CHECK(r.record.state.find("\"test:Old\"") != std::string::npos);
    SH_CHECK(options_of(store.read(2)) && options_of(store.read(2))->bind_enter == 6);  // same group: same options

    // The old files stay; a second run copies nothing.
    SH_CHECK(fs::exists(store.legacy_slot_path(0, 0, 0)) && fs::exists(store.legacy_group_path(0, 0)));
    report = migrate_card_layout(store);
    SH_CHECK(report.saves == 0 && store.list().size() == 3);

    // A slot changed by an old build since: copied again, as a new save.
    legacy_slot(0, 0, 4, 5, 8, false);
    report = migrate_card_layout(store);
    SH_CHECK(report.saves == 1 && store.list().size() == 4 && store.list()[0].summary.sequence == 8);
    fs::remove_all(dir);
}

SH_TEST(save_real_memory_card) {
    const char* path = std::getenv("SH_TEST_MEMCARD");
    if (path == nullptr || path[0] == '\0') {
        SH_SKIP("SH_TEST_MEMCARD not set");
    }
    auto bytes = read_whole_file(path);
    SH_CHECK(static_cast<bool>(bytes));
    if (!bytes) {
        return;
    }
    const fs::path dir = fresh_dir("sh_save_real");
    SaveStore store(dir / ".savegames");
    auto stats = import_memory_card(bytes.value(), store);
    SH_CHECK(stats && stats.value().saves > 0 && stats.value().damaged == 0);
    if (stats) {
        int ok = 0;
        for (const SaveEntry& e : store.list()) {
            SlotRead r = store.read(e.number);
            if (r.state == SlotState::Ok) {
                // The state reads and gives back a record with a valid footer.
                std::vector<std::uint8_t> game = record_bytes(r);
                std::uint8_t x = 0;
                for (std::uint8_t b : game) {
                    x ^= b;
                }
                SH_CHECK(game.size() == 640 && x == game[636] && game[638] == 0xDC && game[639] == 0xDC);
                ++ok;
            }
        }
        SH_CHECK(ok == stats.value().saves);
    }
    fs::remove_all(dir);
}

// A PS1 card save reaches the engine's classes by name: the console bytes of the save are read at their offsets
// (console_records.h) into sh::game::SaveRecord, and the card's options into sh::game::Settings.
SH_TEST(save_memory_card_import_to_engine_records) {
    const fs::path dir = fresh_dir("sh_save_card_records");
    SaveStore store(dir / ".savegames");
    auto stats = import_memory_card(make_card({{"BASLUS-00707SILENT00", {{2, 9, 0x31}}}}), store);
    SH_CHECK(stats && stats.value().saves == 1);
    const SlotRead r = store.read(1);
    auto game = from_json(r.record.state);
    SH_CHECK(r.state == SlotState::Ok && game);
    if (!game) {
        fs::remove_all(dir);
        return;
    }
    Warnings warnings;
    auto live = to_live_state(game.value(), NameTable{}, warnings);
    SH_CHECK(static_cast<bool>(live));
    if (!live) {
        fs::remove_all(dir);
        return;
    }
    const sh::game::SaveRecord& s = live.value();
    const std::vector<std::uint8_t> b = pattern(640, 0x31);
    auto le32 = [&](std::size_t at) {
        return static_cast<std::uint32_t>(b[at] | b[at + 1] << 8 | b[at + 2] << 16 | static_cast<std::uint32_t>(b[at + 3]) << 24);
    };
    auto le16 = [&](std::size_t at) { return static_cast<std::uint16_t>(b[at] | b[at + 1] << 8); };
    SH_CHECK(s.items[1].item == b[4] && s.items[1].count == b[5] && s.items[1].command == b[6] &&
             s.items[1].extra == b[7]);
    SH_CHECK(s.map == static_cast<std::int8_t>(b[0xA4]) && s.room == static_cast<std::int8_t>(b[0xA5]));
    SH_CHECK(s.save_count == static_cast<std::int16_t>(le16(0xA6)) && s.equipped_weapon == b[0xAA]);
    SH_CHECK(s.event_flags[3] == le32(0x168 + 12) && s.paper_maps == le32(0x164));
    SH_CHECK(s.health == static_cast<std::int32_t>(le32(0x240)) && s.position_x == static_cast<std::int32_t>(le32(0x244)));
    SH_CHECK(s.rotation_y == static_cast<std::int16_t>(le16(0x248)) && s.play_time == le32(0x250));
    SH_CHECK(s.counters[10] == le16(0x264 + 20) && s.continues == b[0x27B]);

    const std::optional<GameOptions> options = options_of(r);
    SH_CHECK(options.has_value());
    if (options) {
        const sh::game::Settings settings = to_live_options(*options);
        SH_CHECK(settings.binds.enter == 0x42 && settings.binds.cancel == 0 && settings.brightness == 0);
    }
    fs::remove_all(dir);
}

// The console byte form and the engine's records convert both ways without loss for every value the game uses.
SH_TEST(save_console_records_round_trip) {
    std::vector<std::uint8_t> bytes = pattern(console::kSaveRecordBytes, 0x5A);
    for (const std::size_t unused : {0xA1, 0xA2, 0xA3, 0x23E}) {
        bytes[unused] = 0;
    }
    const sh::game::SaveRecord record = console::decode_save_record(bytes);
    SH_CHECK(console::encode_save_record(record) == bytes);
    SH_CHECK(console::decode_save_record(console::encode_save_record(record)) == record);
    SH_CHECK(record.map == static_cast<std::int8_t>(bytes[0xA4]) && record.continues == bytes[0x27B]);

    std::vector<std::uint8_t> options = pattern(console::kSettingsBytes, 0x13);
    for (const std::size_t unused : {0x26, 0x30, 0x31, 0x32, 0x33}) {
        options[unused] = 0;
    }
    const sh::game::Settings settings = console::decode_settings(options);
    SH_CHECK(console::encode_settings(settings) == options);
    SH_CHECK(settings.binds.option == static_cast<std::uint16_t>(options[0x1A] | options[0x1B] << 8));
    SH_CHECK(settings.pal_language == static_cast<std::uint32_t>(options[0x34] | options[0x35] << 8 |
                                                                 options[0x36] << 16 | options[0x37] << 24));
}
