#pragma once

// gamedata/text/<lang>/*.json ("sh-text" v1): the game's text by key, in
// readable markup (text_markup.h). The base English files carry the original's text and index
// tables, which tie keys to the numeric slots the engine addresses text by (map message index).
// <lang>/messages.json holds messages that belong to no map: every map can show them by key.
// add_disc_messages()/add_engine() put a disc's text tables (convert/text of an import with the
// text converter) under the index's keys, which is how the tests compare the two. Layers stack:
// the base, then a language pack, then mods; the last layer to name a key wins it.

#include "formats/common/status.h"
#include "formats/convert/text/text_markup.h"

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sh::convert::text {

// One numeric table: slot `first + i` is keys[i]; an empty key is an unused slot.
struct TextIndex {
    int first = 0;
    std::vector<std::string> keys;
};

struct TextDoc {
    std::string lang;
    std::string map;                                          // "map0_s01" for a map's file, else empty
    bool shared_messages = false;                             // <lang>/messages.json: messages of every map
    std::map<std::string, TextIndex> index;                   // "map", "item_name", "item_desc"
    std::vector<std::pair<std::string, std::string>> strings; // key -> readable text, file order
    std::map<std::string, std::string> ws;                    // key -> exact layout record
};

Result<TextDoc> parse_text_doc(std::string_view json);
Result<TextDoc> read_text_doc(const std::filesystem::path& path);

// One table of a text file of the import (gamedata/convert/text/**.json, text_converter.h): the engine bytes of
// every entry by its id, std::nullopt where the game's pointer is null. Fails when the file or the table is missing.
Result<std::vector<std::optional<std::string>>> read_imported_table(const std::filesystem::path& file,
                                                                    std::string_view table);

class TextLayers {
public:
    struct Entry {
        std::string text;    // readable
        std::string ws;
        std::string engine;  // encoded for the renderer (empty when the markup did not parse)
        int layer = 0;
        bool ok = false;
    };

    // Adds every string of the document under `layer` (0..31). Index tables are kept from
    // documents of the layers in `index_layers` only (the base English). Markup errors are
    // appended to `warnings` with the key; such an entry does not shadow lower layers.
    void add(const TextDoc& doc, int layer, std::vector<std::string>& warnings);

    // Adds a string given as the renderer's bytes (the disc's own text): the readable text and its layout record
    // are derived from them, the bytes are kept as they are.
    void add_engine(const std::string& key, std::string_view engine, int layer);

    // The disc's messages under the keys of the index tables added so far: the common block and every map's own
    // lines from `import_text`/VIN/<MAP>.json (import_text = gamedata/convert/text). A file that is missing or
    // shorter than its index goes to `warnings`. Returns the number of strings added.
    int add_disc_messages(const std::filesystem::path& import_text, int layer, std::vector<std::string>& warnings);

    // Every *.json under `dir` except lang.json (the language's descriptor, i18n/core/language_set.h), in path order. A missing directory adds nothing.
    // messages.json directly in `dir` is the language's file of shared messages (TextDoc::shared_messages).
    // Files of a directory sort by path; a mod's documents therefore add in a stable order.
    void add_dir(const std::filesystem::path& dir, int layer, std::vector<std::string>& warnings);

    // Drops every entry of the given layer (switching languages).
    void remove_layer(int layer);

    // The winning entry among the layers whose bit is set in `mask`, or null.
    const Entry* find(std::string_view key, unsigned mask) const;

    // Index tables: map -> its message keys from slot `first`; "common" is the shared block.
    const TextIndex* map_index(std::string_view map) const;
    const TextIndex* item_index(std::string_view table) const;  // "item_name" / "item_desc"
    // The key of slot `slot` of the named map's messages (0..14 resolve through "common",
    // slots past the base table through the added messages below).
    const std::string* map_key(std::string_view map, int slot) const;
    // The slot of `key` in `map` (or the common block), -1 when it has none.
    int map_slot(std::string_view map, std::string_view key) const;

    // New messages of a map: a document of a layer in `extra_layers` whose "map" names a map
    // with a base table gets a slot after that table for every key of its index.map.keys and
    // strings that the base does not number (mods' events reach them through say("key")).
    // Slots are handed out in add order and never reused.
    void set_index_layers(unsigned mask) { index_layers_ = mask; }
    void set_extra_layers(unsigned mask) { extra_layers_ = mask; }
    // Shared messages: every string of a document with `shared_messages` set, of a layer in `index_layers` or
    // `extra_layers`, is a message of every map. A map's slots for them follow its own added messages, in add
    // order; a key the map's own table, the common block or its added messages already number keeps that slot, so
    // a map's own message wins over a shared one of the same key.
    const std::vector<std::string>& shared_messages() const { return shared_; }
    // Slots the map's messages span: base table, added messages, then the shared ones (0 without a base table).
    int map_total(std::string_view map) const;
    // A slot past the map's base table: an added or a shared message.
    bool is_extra_slot(std::string_view map, int slot) const;
    const std::vector<std::string>* map_extras(std::string_view map) const;

    // Keys present in the given layers, for walking a pack.
    std::vector<std::string> keys_in(unsigned mask) const;

private:
    std::map<std::string, std::vector<Entry>, std::less<>> entries_;
    std::map<std::string, TextIndex, std::less<>> maps_;   // "common" and "map0_s01"...
    std::map<std::string, TextIndex, std::less<>> items_;
    std::map<std::string, std::vector<std::string>, std::less<>> extras_;  // map -> added keys
    std::vector<std::string> shared_;                                      // keys of the shared messages
    unsigned index_layers_ = 1u;
    unsigned extra_layers_ = 0u;
};

}  // namespace sh::convert::text
