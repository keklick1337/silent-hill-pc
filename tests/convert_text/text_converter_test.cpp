#include "framework/sh_test.h"
#include "formats/archive/sh_archive.h"
#include "formats/common/byte_source.h"
#include "formats/convert/text/mini_json.h"
#include "formats/convert/text/text_codec.h"
#include "formats/convert/text/text_converter.h"
#include "formats/convert/text/text_db.h"
#include "formats/convert/text/text_markup.h"
#include "formats/disc/disc_image.h"
#include "formats/iso9660/iso9660.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#ifndef SH_PROJECT_ROOT
#define SH_PROJECT_ROOT "../.."
#endif

namespace {

namespace fs = std::filesystem;
namespace arc = sh::archive;
namespace txt = sh::convert::text;

using sh::convert::Output;

std::vector<std::uint8_t> bytes_of(std::string_view s) {
    return std::vector<std::uint8_t>(s.begin(), s.end());
}

std::string text_of(txt::Dialect dialect, std::string_view raw) {
    const auto bytes = bytes_of(raw);
    const auto text = txt::bytes_to_text(bytes, dialect);
    return text ? *text : std::string("<none>");
}

// Raw -> text -> bytes -> text gives the same text, and for Ascii and Game the same bytes.
void check_codec_round_trip(txt::Dialect dialect, std::string_view raw) {
    const auto bytes = bytes_of(raw);
    const auto text = txt::bytes_to_text(bytes, dialect);
    SH_CHECK(text.has_value());
    if (!text) {
        return;
    }
    const auto back = txt::text_to_bytes(*text, dialect);
    SH_CHECK(static_cast<bool>(back));
    if (!back) {
        return;
    }
    if (dialect != txt::Dialect::Markup) {
        SH_CHECK(back.value() == bytes);
    }
    SH_CHECK(txt::bytes_to_text(back.value(), dialect) == text);
}

// A small map overlay: header at offset 4 with mapMessages at +0x30, strings first, then the pointer array.
constexpr std::uint32_t kMapBase = 0x800C9578;

void put_u32(std::vector<std::uint8_t>& v, std::size_t at, std::uint32_t value) {
    for (int i = 0; i < 4; ++i) {
        v[at + i] = static_cast<std::uint8_t>(value >> (8 * i));
    }
}

std::vector<std::uint8_t> make_map_overlay() {
    const std::vector<std::string> messages = {
        "Yes", "No", "It's_locked.~E ", "~J0(2.0)\tHello_there.~N\n\t\tHi. ~E ", "There_is_a_ ~C2 Key ~C7 . ~S4 ",
        "5",   "6",  "7",               "8",                                    "9",
        "10",  "11", "12",              "13",                                   "~C6Now_Making!~C7~E",
        "Extra_one ~E "};
    std::vector<std::uint8_t> image(0x600, 0);
    const std::size_t array_at = 0x500;
    put_u32(image, 0x34, kMapBase + static_cast<std::uint32_t>(array_at));
    std::size_t at = 0x100;
    for (std::size_t i = 0; i < messages.size(); ++i) {
        std::copy(messages[i].begin(), messages[i].end(), image.begin() + at);
        put_u32(image, array_at + 4 * i, kMapBase + static_cast<std::uint32_t>(at));
        at += 48;
    }
    put_u32(image, array_at + 4 * messages.size(), 0x00010000);
    return image;
}

std::string json_of(const std::vector<Output>& outputs) {
    return std::string(outputs[0].bytes.begin(), outputs[0].bytes.end());
}

void replace_in_json(std::vector<Output>& outputs, std::string_view from, std::string_view to) {
    std::string json = json_of(outputs);
    const std::size_t at = json.find(from);
    SH_CHECK(at != std::string::npos);
    if (at == std::string::npos) {
        return;
    }
    json.replace(at, from.size(), to);
    outputs[0].bytes.assign(json.begin(), json.end());
}

}  // namespace

SH_TEST(TextCodecAsciiAndGame) {
    SH_CHECK(text_of(txt::Dialect::Game, "Health_drink(s)") == "Health drink(s)");
    SH_CHECK(text_of(txt::Dialect::Game, "Key_to_exit\n\t\tto_the") == "Key to exit\\n\\t\\tto the");
    SH_CHECK(text_of(txt::Dialect::Game, "a b") == "a\\sb");
    SH_CHECK(text_of(txt::Dialect::Ascii, "Makoto Y\x18\x61no") == "Makoto Y\\x18ano");
    SH_CHECK(text_of(txt::Dialect::Ascii, "a_b\\c") == "a_b\\\\c");
    SH_CHECK(text_of(txt::Dialect::Game, "\x07MEMORY_CARD\nis_\x01not") == "\\x07MEMORY CARD\\nis \\x01not");
    for (const auto dialect : {txt::Dialect::Ascii, txt::Dialect::Game}) {
        check_codec_round_trip(dialect, "");
        check_codec_round_trip(dialect, "Defeated_\x01\x01""enemy_\x01\x01""by_\x01\x01shooting");
        check_codec_round_trip(dialect, "\xD0\xF0""Executive Producer\xD8");
        check_codec_round_trip(dialect, "tab\there \\ and\r\x7f");
    }
}

SH_TEST(TextCodecMarkup) {
    using D = txt::Dialect;
    SH_CHECK(text_of(D::Markup, "There_is_a_ ~C2 First_aid_kit ~C7 . ~N Take_it? ~S4 ") ==
             "There is a {C2}First aid kit{C7}.{N}Take it?{S4}");
    SH_CHECK(text_of(D::Markup, "~J0(2.0)\tIt's_quiet.__Too_quiet. ~E ") == "{J0(2.0)}It's quiet.  Too quiet.{E}");
    SH_CHECK(text_of(D::Markup, "~J1(2.4)\n\tThat_was. ~N\n\tHm. ") == "{J1(2.4)}That was.{N}Hm.");
    SH_CHECK(!txt::bytes_to_text(bytes_of("~J1(2.4)\nThat"), D::Markup).has_value());
    SH_CHECK(text_of(D::Markup, "~C6Now_Making!~C7~E") == "{C6}Now Making!{C7}{E}");
    SH_CHECK(text_of(D::Markup, "a ~N\n\t\t\tb") == "a{N}b");
    SH_CHECK(!txt::bytes_to_text(bytes_of("~Zx"), D::Markup).has_value());
    SH_CHECK(!txt::bytes_to_text(bytes_of("bad ~"), D::Markup).has_value());
    for (const char* raw : {"Yes", "~D I_don't_have_the_map ~N\t\t\tfor_this_place.~E ", "~H\tThere_is_a ~E ",
                            "~L4 ~M ~C3 10:00 ~N\n\t[Alchemy_laboratory] ~E "}) {
        check_codec_round_trip(D::Markup, raw);
    }

    const auto bytes = txt::text_to_bytes("Take it?{N}{C2}x{J1(3.5)}y{E}", D::Markup);
    SH_CHECK(static_cast<bool>(bytes));
    if (bytes) {
        SH_CHECK(bytes.value() == bytes_of("Take_it?~N ~C2 x~J1(3.5) y~E "));
    }
    SH_CHECK(!txt::text_to_bytes("tilde ~ here", D::Markup));
    SH_CHECK(!txt::text_to_bytes("open {N", D::Markup));
    SH_CHECK(!txt::text_to_bytes("{n}", D::Markup));
    SH_CHECK(!txt::text_to_bytes("caf\xC3\xA9", D::Game));
}

SH_TEST(TextJsonParsesAndQuotes) {
    const auto doc = txt::json::parse(R"({"a": [1, -2, true, null, "x\"é\n"], "b": {"c": 5}})");
    SH_CHECK(static_cast<bool>(doc));
    if (doc) {
        const auto* a = doc.value().find("a");
        SH_CHECK(a != nullptr && a->items.size() == 5);
        if (a != nullptr && a->items.size() == 5) {
            SH_CHECK(a->items[1].integer == -2);
            SH_CHECK(a->items[4].string == "x\"\xC3\xA9\n");
        }
        SH_CHECK(doc.value().find("b")->find("c")->integer == 5);
    }
    SH_CHECK(!txt::json::parse("{\"a\": 1.5}"));
    SH_CHECK(!txt::json::parse("{\"a\": 1} x"));
    SH_CHECK(!txt::json::parse("[1,"));
    SH_CHECK(txt::json::quote("a\"b\\\x01") == "\"a\\\"b\\\\\\u0001\"");
}

SH_TEST(TextConverterOnSyntheticMapOverlay) {
    const auto converter = txt::make_converter();
    SH_CHECK(converter->name() == "text");
    SH_CHECK(converter->lossless());

    const auto raw = make_map_overlay();
    SH_CHECK(converter->accepts(raw, "VIN/MAP0_S00.BIN"));
    SH_CHECK(converter->accepts(raw, "vin\\map7_s03.bin"));
    SH_CHECK(!converter->accepts(raw, "VIN/STREAM.BIN"));
    SH_CHECK(!converter->accepts(raw, "VIN/MAPT_S00.BIN"));
    SH_CHECK(!converter->accepts(std::span<const std::uint8_t>(raw.data(), 0), "VIN/MAP0_S00.BIN"));

    auto short_array = raw;
    put_u32(short_array, 0x500 + 4 * 14, 0);
    put_u32(short_array, 0x500 + 4 * 15, 0);
    SH_CHECK(!converter->accepts(short_array, "VIN/MAP0_S00.BIN"));  // fewer than the 15 common messages

    auto decoded = converter->decode(raw, "VIN/MAP0_S00.BIN");
    SH_CHECK(static_cast<bool>(decoded));
    if (!decoded) {
        return;
    }
    auto& outputs = decoded.value();
    SH_CHECK(outputs.size() == 2);
    SH_CHECK(outputs[0].relative_path == "text/VIN/MAP0_S00.json");
    SH_CHECK(outputs[1].relative_path == "text/VIN/MAP0_S00.frame.bin");
    SH_CHECK(outputs[1].bytes.size() == raw.size());

    const std::string json = json_of(outputs);
    SH_CHECK(json.find("\"text\": \"Hello there.{N}Hi.{E}\"") == std::string::npos);
    SH_CHECK(json.find("{J0(2.0)}Hello there.{N}Hi.{E}") != std::string::npos);
    SH_CHECK(json.find("\"count\": 16") != std::string::npos);

    const auto back = converter->encode(outputs);
    SH_CHECK(static_cast<bool>(back));
    if (back) {
        SH_CHECK(back.value() == raw);
    }

    // Edited text wins over raw, and must fit the slot.
    auto edited = outputs;
    replace_in_json(edited, "\"text\": \"Yes\"", "\"text\": \"Yep\"");
    const auto patched = converter->encode(edited);
    SH_CHECK(static_cast<bool>(patched));
    if (patched) {
        SH_CHECK(patched.value().size() == raw.size());
        std::size_t differing = 0;
        for (std::size_t i = 0; i < raw.size(); ++i) {
            differing += patched.value()[i] != raw[i];
        }
        SH_CHECK(differing == 1);
        SH_CHECK(patched.value()[0x102] == 'p');
    }

    // "No" has room for 3 bytes: "Nope" does not fit.
    auto too_long = outputs;
    replace_in_json(too_long, "\"text\": \"No\"", "\"text\": \"Nope\"");
    SH_CHECK(!converter->encode(too_long));

    // Raw edit: drop "text" and change the hex.
    auto raw_edit = outputs;
    replace_in_json(raw_edit, "\"text\": \"No\", \"raw\": \"4e6f\"", "\"raw\": \"4e61\"");
    const auto raw_patched = converter->encode(raw_edit);
    SH_CHECK(static_cast<bool>(raw_patched));
    if (raw_patched) {
        SH_CHECK(raw_patched.value()[0x130 + 1] == 'a');
    }

    // Text that still matches raw leaves raw alone, also when raw is not what the encoder would write.
    auto odd = outputs;
    replace_in_json(odd, "\"text\": \"It's locked.{E}\", \"raw\": \"4974", "\"text\": \"It's locked.{E}\", \"raw\": \"4974");
    SH_CHECK(static_cast<bool>(converter->encode(odd)));

    auto no_frame = outputs;
    no_frame.pop_back();
    SH_CHECK(!converter->encode(no_frame));
}

namespace {

struct Disc {
    std::shared_ptr<const sh::disc::Image> image;
    std::shared_ptr<const sh::ByteSource> exe;
    std::shared_ptr<const sh::ByteSource> silent;
    std::vector<arc::Entry> table;
};

std::optional<Disc> open_disc() {
    const fs::path path = fs::path(SH_PROJECT_ROOT) / "data" / "Silent Hill (v1.1).bin";
    if (!fs::exists(path)) {
        SH_SKIP("data/Silent Hill (v1.1).bin is not present");
    }
    const std::shared_ptr<const sh::disc::Image> image(sh::disc::Image::open(path));
    if (!image) {
        return std::nullopt;
    }
    const auto volume = sh::iso9660::Volume::open(image);
    if (!volume) {
        return std::nullopt;
    }
    const auto slus = volume->find("SLUS_007.07");
    const auto silent = volume->find("SILENT.");
    const auto hill = volume->find("HILL.");
    if (!slus || !silent || !hill) {
        return std::nullopt;
    }
    const auto sectors = [](std::uint32_t size) { return (size + 2047) / 2048; };
    Disc disc;
    disc.image = image;
    disc.exe = std::shared_ptr<const sh::ByteSource>(volume->open_file(*slus));
    disc.silent = std::shared_ptr<const sh::ByteSource>(volume->open_file(*silent));
    if (!disc.exe || !disc.silent) {
        return std::nullopt;
    }
    const auto table = arc::read_file_table(*disc.exe, {silent->lba, sectors(silent->size)},
                                            {hill->lba, sectors(hill->size)});
    if (!table) {
        return std::nullopt;
    }
    disc.table = *table;
    return disc;
}

}  // namespace

SH_TEST(TextConverterRoundTripsEveryAcceptedFileOfTheDisc) {
    auto disc = open_disc();
    SH_CHECK(disc.has_value());
    if (!disc) {
        return;
    }
    const auto converter = txt::make_converter();

    std::size_t files = 0;
    std::size_t tables = 0;
    std::size_t entries = 0;
    std::size_t nulls = 0;
    std::size_t aliases = 0;
    std::size_t ascii_fallbacks = 0;
    std::set<std::string> accepted;
    std::vector<std::string> declined;

    for (const arc::Entry& entry : disc->table) {
        if (entry.region != arc::Region::Silent) {
            continue;
        }
        const auto raw = arc::read_entry(*disc->silent, entry);
        SH_CHECK(raw.has_value());
        if (!raw) {
            continue;
        }
        if (!converter->accepts(*raw, entry.name)) {
            if (entry.name.starts_with("VIN/") || entry.name.starts_with("1ST/")) {
                declined.push_back(entry.name);
            }
            continue;
        }
        accepted.insert(entry.name);
        ++files;

        const auto decoded = converter->decode(*raw, entry.name);
        SH_CHECK(static_cast<bool>(decoded));
        if (!decoded) {
            continue;
        }
        const auto back = converter->encode(decoded.value());
        SH_CHECK(static_cast<bool>(back));
        SH_CHECK(back && back.value() == *raw);
        if (!back || back.value() != *raw) {
            std::fprintf(stderr, "round trip differs: %s\n", entry.name.c_str());
        }

        // The JSON is valid and its entries carry what the report counts.
        const auto doc = txt::json::parse(json_of(decoded.value()));
        SH_CHECK(static_cast<bool>(doc));
        if (!doc) {
            continue;
        }
        for (const auto& table : doc.value().find("tables")->items) {
            ++tables;
            const auto* list = table.find("entries");
            SH_CHECK(list->items.size() == static_cast<std::size_t>(table.find("count")->integer));
            for (const auto& e : list->items) {
                ++entries;
                nulls += e.find("null") != nullptr;
                aliases += e.find("alias_of") != nullptr;
                ascii_fallbacks += e.find("dialect") != nullptr;
            }
        }
    }

    std::fprintf(stderr, "text: %zu files, %zu tables, %zu entries (%zu null, %zu aliases, %zu ascii fallbacks)\n", files,
                 tables, entries, nulls, aliases, ascii_fallbacks);
    for (const auto& name : declined) {
        std::fprintf(stderr, "text: declined %s\n", name.c_str());
    }

    SH_CHECK(ascii_fallbacks == 0);
    SH_CHECK(files == 47);  // 43 map overlays, BODYPROG, OPTION, SAVELOAD, STF_ROLL
    SH_CHECK(tables == 43 + 9 + 5 + 5 + 1);
    SH_CHECK(accepted.count("1ST/BODYPROG.BIN") == 1);
    SH_CHECK(accepted.count("VIN/MAP0_S00.BIN") == 1);
    SH_CHECK(accepted.count("VIN/STF_ROLL.BIN") == 1);
    SH_CHECK(accepted.count("VIN/STREAM.BIN") == 0);
    SH_CHECK(accepted.count("1ST/B_KONAMI.BIN") == 0);
}

SH_TEST(TextConverterEditsRealFiles) {
    auto disc = open_disc();
    SH_CHECK(disc.has_value());
    if (!disc) {
        return;
    }
    const auto converter = txt::make_converter();
    const auto read = [&](std::string_view name) -> std::vector<std::uint8_t> {
        for (const arc::Entry& e : disc->table) {
            if (e.name == name) {
                auto bytes = arc::read_entry(*disc->silent, e);
                return bytes ? *bytes : std::vector<std::uint8_t>();
            }
        }
        return {};
    };

    // Map message 5 of MAP0_S00 in the game's own words.
    const auto map = read("VIN/MAP0_S00.BIN");
    auto decoded = converter->decode(map, "VIN/MAP0_S00.BIN");
    SH_CHECK(static_cast<bool>(decoded));
    if (!decoded) {
        return;
    }
    const std::string json = json_of(decoded.value());
    SH_CHECK(json.find("\"id\": 0, \"text\": \"Yes\", \"raw\": \"596573\"") != std::string::npos);
    SH_CHECK(json.find("There is a {C2}First aid kit{C7}.{N}Take it?{S4}") != std::string::npos);

    // BODYPROG.BIN is encrypted on the disc: editing a string must go through the gamma both ways.
    const auto bodyprog = read("1ST/BODYPROG.BIN");
    auto body = converter->decode(bodyprog, "1ST/BODYPROG.BIN");
    SH_CHECK(static_cast<bool>(body));
    if (!body) {
        return;
    }
    SH_CHECK(json_of(body.value()).find("\"text\": \"Health drink(s)\", \"raw\": \"") != std::string::npos);
    auto edited = body.value();
    replace_in_json(edited, "\"text\": \"Health drink(s)\"", "\"text\": \"Heal(s)\"");
    const auto patched = converter->encode(edited);
    SH_CHECK(static_cast<bool>(patched));
    if (!patched) {
        return;
    }
    SH_CHECK(patched.value().size() == bodyprog.size());
    auto again = converter->decode(patched.value(), "1ST/BODYPROG.BIN");
    SH_CHECK(static_cast<bool>(again));
    if (again) {
        SH_CHECK(json_of(again.value()).find("\"text\": \"Heal(s)\"") != std::string::npos);
        SH_CHECK(json_of(again.value()).find("Health drink(s)") == std::string::npos);
    }

    // Other game files are not text tables.
    SH_CHECK(!converter->accepts(read("VIN/STREAM.BIN"), "VIN/STREAM.BIN"));
    SH_CHECK(!converter->accepts(read("1ST/B_KONAMI.BIN"), "1ST/B_KONAMI.BIN"));
    SH_CHECK(!converter->accepts(map, "VIN/OPTION.BIN"));
}

// The English of gamedata/text/en is the disc's text byte for byte: every key of every map encodes to the bytes of
// its message on the disc, and every item text of the disc is the English of an item key.
SH_TEST(TextBaseEnglishOfTheRepositoryIsTheDiscs) {
    const fs::path keys = fs::path(SH_PROJECT_ROOT) / "gamedata" / "text" / "en";
    if (!fs::is_directory(keys)) {
        SH_SKIP("no gamedata/text/en");
    }
    auto disc = open_disc();
    SH_CHECK(disc.has_value());
    if (!disc) {
        return;
    }
    const fs::path dir = fs::temp_directory_path() / "sh_text_disc_english_test";
    fs::remove_all(dir);
    const auto converter = txt::make_converter();
    for (const arc::Entry& entry : disc->table) {
        if (entry.region != arc::Region::Silent || !(entry.name.starts_with("VIN/MAP") || entry.name == "1ST/BODYPROG.BIN")) {
            continue;
        }
        const auto raw = arc::read_entry(*disc->silent, entry);
        SH_CHECK(raw.has_value());
        if (!raw || !converter->accepts(*raw, entry.name)) {
            continue;
        }
        const auto decoded = converter->decode(*raw, entry.name);
        SH_CHECK(static_cast<bool>(decoded));
        if (!decoded) {
            continue;
        }
        for (const auto& out : decoded.value()) {
            if (!out.relative_path.ends_with(".json")) {
                continue;
            }
            const fs::path file = dir / "convert" / out.relative_path;
            fs::create_directories(file.parent_path());
            std::ofstream(file, std::ios::binary).write(reinterpret_cast<const char*>(out.bytes.data()),
                                                        static_cast<std::streamsize>(out.bytes.size()));
        }
    }

    // Layer 0 is the repository's English, layer 1 the disc's bytes under the same keys.
    std::vector<std::string> warnings;
    txt::TextLayers layers;
    layers.set_index_layers(1u);
    layers.add_dir(keys, 0, warnings);
    const int added = layers.add_disc_messages(dir / "convert" / "text", 1, warnings);
    for (const std::string& w : warnings) {
        std::fprintf(stderr, "text: %s\n", w.c_str());
    }
    SH_CHECK(warnings.empty());
    SH_CHECK(added == 1365);  // 15 shared messages and the 1350 lines of the 43 maps

    std::size_t maps = 0;
    std::size_t checked = 0;
    for (const auto& it : fs::directory_iterator(keys / "maps")) {
        const std::string map = it.path().stem().string();
        const int count = layers.map_index(map) != nullptr ? layers.map_total(map) : 0;
        SH_CHECK(count >= 15);
        ++maps;
        for (int slot = 0; slot < count; ++slot) {
            const std::string* key = layers.map_key(map, slot);
            SH_CHECK(key != nullptr);
            const auto* repo = key != nullptr ? layers.find(*key, 1u) : nullptr;
            const auto* disc_entry = key != nullptr ? layers.find(*key, 2u) : nullptr;
            SH_CHECK(repo != nullptr && disc_entry != nullptr);
            if (repo == nullptr || disc_entry == nullptr) {
                continue;
            }
            if (repo->engine != disc_entry->engine) {
                std::fprintf(stderr, "%s: the repository's English is not the disc's\n", key->c_str());
            }
            SH_CHECK(repo->engine == disc_entry->engine && repo->text == disc_entry->text &&
                     repo->ws == disc_entry->ws);
            ++checked;
        }
    }
    SH_CHECK(maps == 43);
    SH_CHECK(checked == 1365 + 42 * 15);  // every map's table opens with the shared block

    // The item names and descriptions: the English of every key of items.json is a text of the disc's tables (the
    // tables also hold items the game data does not name).
    std::multiset<std::string> disc_items;
    for (const char* name : {"item_names", "item_descriptions"}) {
        const auto table = txt::read_imported_table(dir / "convert" / "text" / "1ST" / "BODYPROG.json", name);
        SH_CHECK(table && table.value().size() == 195);
        std::size_t with_text = 0;
        for (const auto& entry : table ? table.value() : std::vector<std::optional<std::string>>{}) {
            if (entry.has_value()) {
                ++with_text;
                disc_items.insert(*entry);
            }
        }
        SH_CHECK(with_text == 78);
    }
    std::size_t items = 0;
    for (const std::string& key : layers.keys_in(1u)) {
        if (key.starts_with("item.")) {
            const auto it = disc_items.find(layers.find(key, 1u)->engine);
            if (it == disc_items.end()) {
                std::fprintf(stderr, "%s: not a text of the disc\n", key.c_str());
            }
            SH_CHECK(it != disc_items.end());
            if (it != disc_items.end()) {
                disc_items.erase(it);
                ++items;
            }
        }
    }
    SH_CHECK(items == 150);  // 75 names, 75 descriptions
    fs::remove_all(dir);
}
