#include "framework/sh_test.h"
#include "formats/convert/text/text_db.h"
#include "formats/convert/text/text_markup.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#ifndef SH_PROJECT_ROOT
#define SH_PROJECT_ROOT "../.."
#endif

namespace {

namespace fs = std::filesystem;
namespace txt = sh::convert::text;
namespace mk = sh::convert::text::markup;

std::string encoded(std::string_view text, std::string_view ws, mk::Style style) {
    auto r = mk::encode(text, ws, style);
    return r ? r.value() : std::string("<error: ") + r.error() + ">";
}

void check_round_trip(std::string_view engine, mk::Style style = mk::Style::Message) {
    const mk::Decoded d = mk::decode(engine, style);
    const std::string back = encoded(d.text, d.ws, style);
    if (back != engine) {
        std::fprintf(stderr, "round trip:\n  engine %s\n  text   %s\n  ws     %s\n  back   %s\n",
                     std::string(engine).c_str(), d.text.c_str(), d.ws.c_str(), back.c_str());
    }
    SH_CHECK(back == engine);
}

fs::path temp_dir(const char* name) {
    const fs::path dir = fs::temp_directory_path() / name;
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

void write(const fs::path& file, const std::string& content) {
    fs::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary) << content;
}

}  // namespace

SH_TEST(TextMarkupReadableForms) {
    const mk::Decoded a = mk::decode("~J0(2.0)\tWas_I_dreaming? ~E ", mk::Style::Message);
    SH_CHECK(a.text == "{wait 2.0}Was I dreaming?{end}");
    SH_CHECK(a.ws.empty());  // canonical layout
    const mk::Decoded b =
        mk::decode("\tThere_is_a_ ~C2 Kitchen_knife ~C7 . ~N\n\tTake_it? ~S4 ", mk::Style::Message);
    SH_CHECK(b.text == "There is a {color 2}Kitchen knife{/color}.\nTake it?{choice 4}");
    SH_CHECK(mk::decode("~J1(4.5)\tHuh? ~N\n\t\t\tRadio? ~E ", mk::Style::Message).text == "{wait1 4.5}Huh?\nRadio?{end}");
    SH_CHECK(mk::decode("~D ~H\tA~M B~L4 ~P x", mk::Style::Message).text == "{instant}{hires}A{center}B{pos 4}{page}x");
    SH_CHECK(mk::decode("a{b}c", mk::Style::Message).text == "a{{b}}c");
    // An argument byte that is not layout would be swallowed by the renderer: kept raw.
    SH_CHECK(mk::decode("x~N_y", mk::Style::Message).text == "x{raw ~N_}y");
    // Ui: '\n' is a line break there, \v and \x01 are spacing.
    SH_CHECK(mk::decode("Health_drink\n\t\tok\v\x01", mk::Style::Ui).text == "Health drink\nok{wide}{kern}");
}

SH_TEST(TextMarkupEncodeCanonicalAndExact) {
    // No ws record: the canonical layout, which keeps every code's argument byte.
    SH_CHECK(encoded("{wait 2.0}Was I dreaming?{end}", "", mk::Style::Message) == "~J0(2.0)\tWas_I_dreaming? ~E ");
    SH_CHECK(encoded("Line one\nline two{end}", "", mk::Style::Message) == "\tLine_one ~N\n\tline_two ~E ");
    SH_CHECK(encoded("{wait 1.0}A\nB", "", mk::Style::Message) == "~J0(1.0)\tA ~N\n\t\t\tB ");
    // The ws record restores retail's exact bytes...
    const std::string engine = "~J0(2.0)\tWas_I_dreaming?  ~E ";
    const mk::Decoded d = mk::decode(engine, mk::Style::Message);
    SH_CHECK(!d.ws.empty());
    SH_CHECK(encoded(d.text, d.ws, mk::Style::Message) == engine);
    // ...for that text only: an edited string falls back to the canonical layout.
    SH_CHECK(encoded("{wait 2.0}Was I asleep?{end}", d.ws, mk::Style::Message) == "~J0(2.0)\tWas_I_asleep? ~E ");
    // A malformed record is ignored, an unknown tag fails.
    SH_CHECK(encoded("A{end}", "zzzz:0=q", mk::Style::Message) == "\tA ~E ");
    SH_CHECK(!mk::encode("A{blink}", "", mk::Style::Message));
    SH_CHECK(!mk::encode("A{color", "", mk::Style::Message));
}

SH_TEST(TextMarkupRoundTripsOddBytes) {
    check_round_trip("");
    check_round_trip("~C6Now_Making!~C7~E");
    check_round_trip("A_radio... is_it_broken? ~N\n\tCan't_hear_anything. ~E ");
    check_round_trip("~J0(3.0)\n\t  x~N\t\t\t\t\t\t\t\t\t\t\ty ~S3 ");
    check_round_trip("\xd0\xad\xd1\x82\xd0\xbe_ ~C2 \xd0\xb0\xd0\xbf\xd1\x82\xd0\xb5\xd1\x87\xd0\xba\xd0\xb0 ~C7 . ~N \xd0\x92~S4 ");
    check_round_trip("tail~");
    check_round_trip("~Q1 odd~J0(x) end");
    check_round_trip("Supplies_nutrition_to\n\t\t\t\t\trecover.", mk::Style::Ui);
    check_round_trip("\x01" "Off\v__ ", mk::Style::Ui);
}

// The disc's bytes of a text table (convert/text of an import) come in under the keys of the index tables,
// unchanged: how the tests compare the repository's English with the disc.
SH_TEST(TextDiscMessagesComeInUnderTheIndexKeys) {
    const fs::path dir = temp_dir("sh_text_disc_test");
    write(dir / "en" / "common.json",
          R"({"format":"sh-text","version":1,"lang":"en","description":"keys only",
              "index":{"map":{"first":0,"keys":["common.yes","common.no"]}}})");
    write(dir / "en" / "maps" / "map9_s01.json",
          R"({"format":"sh-text","version":1,"lang":"en","map":"map9_s01",
              "index":{"map":{"first":2,"keys":["map9_s01.hello","","map9_s01.gone","map9_s01.past_the_end"]}}})");
    // Entries: 0 "Yes", 1 "No", 2 "~J0(1.0)\tHello. ~E " (odd layout kept), 3 "x", 4 a null pointer.
    write(dir / "convert" / "text" / "VIN" / "MAP9_S01.json",
          R"({"format":"sh-text/1","tables":[{"name":"other","entries":[{"id":0,"raw":"41"}]},
              {"name":"map_messages","entries":[
                {"id":0,"text":"Yes","raw":"596573"},{"id":1,"text":"No","raw":"4e6f"},
                {"id":2,"raw":"7e4a3028312e302909202048656c6c6f2e207e4520"},{"id":3,"raw":"78"},
                {"id":4,"null":true}]}]})");

    auto table = txt::read_imported_table(dir / "convert" / "text" / "VIN" / "MAP9_S01.json", "map_messages");
    SH_CHECK(table && table.value().size() == 5 && table.value()[0] == std::string("Yes") && !table.value()[4]);
    SH_CHECK(!txt::read_imported_table(dir / "convert" / "text" / "VIN" / "MAP9_S01.json", "no_such_table"));
    SH_CHECK(!txt::read_imported_table(dir / "convert" / "text" / "VIN" / "MAP0_S00.json", "map_messages"));

    std::vector<std::string> warnings;
    txt::TextLayers layers;
    layers.set_index_layers(1u);
    layers.add_dir(dir / "en", 0, warnings);
    SH_CHECK(warnings.empty());
    SH_CHECK(layers.find("common.yes", 1u) == nullptr);
    SH_CHECK(layers.add_disc_messages(dir / "convert" / "text", 0, warnings) == 3);
    SH_CHECK(warnings.size() == 2);  // the null entry and the key past the disc's table
    const auto* yes = layers.find("common.yes", 1u);
    SH_CHECK(yes != nullptr && yes->engine == "Yes" && yes->text == "Yes");
    const auto* hello = layers.find(*layers.map_key("map9_s01", 2), 1u);
    SH_CHECK(hello != nullptr && hello->engine == "~J0(1.0)\t  Hello. ~E " && hello->text == "{wait 1.0}Hello.{end}");
    // The readable text and its layout record give the disc's bytes back.
    SH_CHECK(hello != nullptr && encoded(hello->text, hello->ws, mk::Style::Message) == hello->engine);
    SH_CHECK(layers.find("map9_s01.gone", 1u) == nullptr);

    layers.add_engine("item.Thing.desc", "Line_one\n\tline_two.", 0);
    const auto* item = layers.find("item.Thing.desc", 1u);
    SH_CHECK(item != nullptr && item->engine == "Line_one\n\tline_two." &&
             encoded(item->text, item->ws, mk::Style::Ui) == item->engine);
    fs::remove_all(dir);
}

// Every shipped string encodes, and decoding the bytes gives back the committed text and ws:
// the C++ codec agrees with tools/scripts/sh_text.py, which wrote the files from the sources.
SH_TEST(TextShippedFilesRoundTrip) {
    const fs::path root = fs::path(SH_PROJECT_ROOT) / "gamedata" / "text";
    if (!fs::is_directory(root)) {
        SH_SKIP("no gamedata/text");
    }
    std::size_t strings = 0;
    std::size_t files = 0;
    for (const auto& it : fs::recursive_directory_iterator(root)) {
        if (!it.is_regular_file() || it.path().extension() != ".json" || it.path().filename() == "lang.json") {
            continue;
        }
        auto doc = txt::read_text_doc(it.path());
        SH_CHECK(static_cast<bool>(doc));
        if (!doc) {
            std::fprintf(stderr, "%s\n", doc.error().c_str());
            continue;
        }
        ++files;
        for (const auto& [key, text] : doc.value().strings) {
            const auto ws_it = doc.value().ws.find(key);
            const std::string ws = ws_it != doc.value().ws.end() ? ws_it->second : std::string();
            const mk::Style style = mk::style_for_key(key);
            auto engine = mk::encode(text, ws, style);
            SH_CHECK(static_cast<bool>(engine));
            if (!engine) {
                continue;
            }
            const mk::Decoded back = mk::decode(engine.value(), style);
            if (back.text != text || back.ws != ws) {
                std::fprintf(stderr, "%s %s: decode(encode) differs\n", it.path().string().c_str(), key.c_str());
            }
            SH_CHECK(back.text == text && back.ws == ws);
            ++strings;
        }
    }
    SH_CHECK(files >= 40);
    SH_CHECK(strings > 1000);
}

SH_TEST(TextLayersPriorityAndIndex) {
    const fs::path dir = temp_dir("sh_text_layers_test");
    write(dir / "en" / "common.json",
          R"({"format":"sh-text","version":1,"lang":"en",
              "index":{"map":{"first":0,"keys":["common.yes","common.no"]}},
              "strings":{"common.yes":"Yes","common.no":"No"}})");
    write(dir / "en" / "maps" / "map9_s01.json",
          R"({"format":"sh-text","version":1,"lang":"en","map":"map9_s01",
              "index":{"map":{"first":2,"keys":["map9_s01.hello","","map9_s01.bye"]}},
              "strings":{"map9_s01.hello":"{wait 1.0}Hello.{end}","map9_s01.bye":"Bye."}})");
    write(dir / "mod" / "en" / "lines.json",
          R"({"format":"sh-text","version":1,"lang":"en",
              "index":{"map":{"first":0,"keys":["ignored"]}},
              "strings":{"map9_s01.bye":"See you.","mymod.extra":"Extra line","map9_s01.hello":"{oops}"}})");
    write(dir / "ru" / "maps" / "map9_s01.json",
          R"({"format":"sh-text","version":1,"lang":"ru","strings":{"map9_s01.hello":"Привет."}})");

    std::vector<std::string> warnings;
    txt::TextLayers layers;
    layers.set_index_layers(1u);
    layers.add_dir(dir / "en", 0, warnings);
    layers.add_dir(dir / "mod" / "en", 1, warnings);
    layers.add_dir(dir / "ru", 2, warnings);
    SH_CHECK(warnings.size() == 1);  // the mod's {oops}: logged, and does not shadow the base

    SH_CHECK(layers.map_slot("map9_s01", "common.no") == 1);
    SH_CHECK(layers.map_slot("map9_s01", "map9_s01.bye") == 4);
    SH_CHECK(layers.map_slot("map9_s01", "mymod.extra") == -1);
    SH_CHECK(layers.map_key("map9_s01", 3) == nullptr);
    SH_CHECK(layers.map_index("map9_s01") != nullptr && layers.map_index("common")->keys.size() == 2);

    const auto* bye = layers.find(*layers.map_key("map9_s01", 4), 0x3u);
    SH_CHECK(bye != nullptr && bye->engine == "\tSee_you. ");
    SH_CHECK(layers.find("map9_s01.bye", 0x1u)->engine == "\tBye. ");
    const auto* hello = layers.find("map9_s01.hello", 0x3u);
    SH_CHECK(hello != nullptr && hello->layer == 0 && hello->engine == "~J0(1.0)\tHello. ~E ");
    SH_CHECK(layers.find("map9_s01.hello", 0x7u)->engine == "\t\xd0\x9f\xd1\x80\xd0\xb8\xd0\xb2\xd0\xb5\xd1\x82. ");
    layers.remove_layer(2);
    SH_CHECK(layers.find("map9_s01.hello", 0x4u) == nullptr);
    SH_CHECK(layers.find("mymod.extra", 0x3u) != nullptr);
    fs::remove_all(dir);
}

// The game puts every mod's text/<lang>/ in one layer, in load order (text_bridge.cpp): two mods with the same
// file both apply, key by key, and on a key both name the later mod wins.
SH_TEST(TextLayersTwoModsStackKeyByKey) {
    const fs::path dir = temp_dir("sh_text_two_mods_test");
    write(dir / "en" / "common.json",
          R"({"format":"sh-text","version":1,"lang":"en","strings":{"common.yes":"Yes","common.no":"No","common.ok":"OK"}})");
    write(dir / "a" / "en" / "common.json",
          R"({"format":"sh-text","version":1,"lang":"en","strings":{"common.yes":"Aye","common.no":"Nay","a.new":"From A"}})");
    write(dir / "b" / "en" / "common.json",
          R"({"format":"sh-text","version":1,"lang":"en","strings":{"common.no":"Nope","b.new":"From B"}})");
    std::vector<std::string> warnings;
    txt::TextLayers layers;
    layers.set_index_layers(1u);
    layers.add_dir(dir / "en", 0, warnings);
    layers.add_dir(dir / "a" / "en", 1, warnings);
    layers.add_dir(dir / "b" / "en", 1, warnings);
    SH_CHECK(warnings.empty());
    SH_CHECK(layers.find("common.yes", 0x3u)->engine == "\tAye ");   // only mod A
    SH_CHECK(layers.find("common.no", 0x3u)->engine == "\tNope ");   // both: B is later
    SH_CHECK(layers.find("common.ok", 0x3u)->engine == "\tOK ");     // the base
    SH_CHECK(layers.find("a.new", 0x3u) != nullptr && layers.find("b.new", 0x3u) != nullptr);
    fs::remove_all(dir);
}

SH_TEST(TextDocRejectsBadInput) {
    SH_CHECK(!txt::parse_text_doc("{}"));
    SH_CHECK(!txt::parse_text_doc(R"({"format":"sh-text","version":2,"strings":{}})"));
    SH_CHECK(!txt::parse_text_doc(R"({"format":"sh-text","version":1,"strings":{"a":1}})"));
    SH_CHECK(!txt::parse_text_doc(R"({"format":"sh-text","version":1,"index":{"map":{"first":0}},"strings":{}})"));
    SH_CHECK(static_cast<bool>(txt::parse_text_doc(R"({"format":"sh-text","version":1,"strings":{}})")));
}

SH_TEST(TextMarkupPlainInterfaceText) {
    // ui.* is PC interface text: braces are the overlays' placeholders, not markup.
    SH_CHECK(mk::style_for_key("ui.quick.music_volume") == mk::Style::Plain);
    SH_CHECK(encoded("{key} or Esc close", "", mk::Style::Plain) == "{key}_or_Esc_close");
    SH_CHECK(mk::decode("RETURN_TO_THE_TITLE_SCREEN?", mk::Style::Plain).text == "RETURN TO THE TITLE SCREEN?");
    check_round_trip("Reset_controls?", mk::Style::Plain);
}

SH_TEST(TextLayersModAddsMapMessages) {
    const fs::path dir = temp_dir("sh_text_extra_test");
    write(dir / "en" / "common.json",
          R"({"format":"sh-text","version":1,"lang":"en",
              "index":{"map":{"first":0,"keys":["common.yes","common.no"]}},
              "strings":{"common.yes":"Yes","common.no":"No"}})");
    write(dir / "en" / "maps" / "map9_s01.json",
          R"({"format":"sh-text","version":1,"lang":"en","map":"map9_s01",
              "index":{"map":{"first":2,"keys":["map9_s01.hello"]}},
              "strings":{"map9_s01.hello":"Hello.{end}"}})");
    // A mod's map file: a new line by index, one by strings only, and an override of a base key.
    write(dir / "mod" / "en" / "maps" / "map9_s01.json",
          R"({"format":"sh-text","version":1,"lang":"en","map":"map9_s01",
              "index":{"map":{"first":99,"keys":["mymod.first"]}},
              "strings":{"map9_s01.hello":"Hi.{end}","mymod.second":"Two.{end}","mymod.first":"One.{end}"}})");
    write(dir / "ru" / "maps" / "map9_s01.json",
          R"({"format":"sh-text","version":1,"lang":"ru","map":"map9_s01","strings":{"mymod.first":"Раз.{end}"}})");

    std::vector<std::string> warnings;
    txt::TextLayers layers;
    layers.set_index_layers(1u);
    layers.set_extra_layers(2u);
    layers.add_dir(dir / "en", 0, warnings);
    layers.add_dir(dir / "mod" / "en", 1, warnings);
    layers.add_dir(dir / "ru", 2, warnings);
    SH_CHECK(warnings.empty());
    SH_CHECK(layers.map_total("map9_s01") == 5);
    SH_CHECK(layers.map_slot("map9_s01", "map9_s01.hello") == 2);
    SH_CHECK(layers.map_slot("map9_s01", "mymod.first") == 3);
    SH_CHECK(layers.map_slot("map9_s01", "mymod.second") == 4);
    SH_CHECK(layers.is_extra_slot("map9_s01", 3) && !layers.is_extra_slot("map9_s01", 2));
    SH_CHECK(layers.map_key("map9_s01", 4) != nullptr && *layers.map_key("map9_s01", 4) == "mymod.second");
    SH_CHECK(layers.map_key("map9_s01", 5) == nullptr);
    // The language layer translates a mod's line by its key; it does not add slots itself.
    SH_CHECK(layers.find("mymod.first", 0x4u) != nullptr);
    SH_CHECK(layers.map_total("map9_s01") == 5);
    fs::remove_all(dir);
}
