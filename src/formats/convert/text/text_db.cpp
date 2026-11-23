#include "formats/convert/text/text_db.h"

#include "formats/convert/text/mini_json.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <system_error>

namespace sh::convert::text {

namespace {

using json::Value;

Result<std::string> slurp(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return Result<std::string>::fail("cannot open " + path.string());
    }
    std::ostringstream buf;
    buf << in.rdbuf();
    if (in.bad()) {
        return Result<std::string>::fail("cannot read " + path.string());
    }
    return Result<std::string>::ok(buf.str());
}

const std::string* string_member(const Value& v, std::string_view key) {
    const Value* m = v.find(key);
    return m != nullptr && m->kind == Value::Kind::String ? &m->string : nullptr;
}

bool header_ok(const Value& root, std::string_view format) {
    const std::string* f = string_member(root, "format");
    const Value* ver = root.find("version");
    return f != nullptr && *f == format && ver != nullptr && ver->kind == Value::Kind::Integer &&
           ver->integer == 1;
}

}  // namespace

Result<TextDoc> parse_text_doc(std::string_view text) {
    auto parsed = json::parse(text);
    if (!parsed) {
        return Result<TextDoc>::fail(parsed.error());
    }
    const Value& root = parsed.value();
    if (root.kind != Value::Kind::Object || !header_ok(root, "sh-text")) {
        return Result<TextDoc>::fail("not an sh-text v1 document");
    }
    TextDoc doc;
    if (const std::string* lang = string_member(root, "lang")) {
        doc.lang = *lang;
    }
    if (const std::string* map = string_member(root, "map")) {
        doc.map = *map;
    }
    if (const Value* index = root.find("index"); index != nullptr && index->kind == Value::Kind::Object) {
        for (std::size_t t = 0; t < index->keys.size(); ++t) {
            const Value& table = index->items[t];
            const Value* first = table.find("first");
            const Value* keys = table.find("keys");
            if (first == nullptr || first->kind != Value::Kind::Integer || keys == nullptr ||
                keys->kind != Value::Kind::Array) {
                return Result<TextDoc>::fail("index." + index->keys[t] + ": needs first and keys");
            }
            TextIndex out;
            out.first = static_cast<int>(first->integer);
            for (const Value& k : keys->items) {
                if (k.kind != Value::Kind::String) {
                    return Result<TextDoc>::fail("index." + index->keys[t] + ": keys must be strings");
                }
                out.keys.push_back(k.string);
            }
            doc.index[index->keys[t]] = std::move(out);
        }
    }
    const Value* strings = root.find("strings");
    if (strings == nullptr && !doc.index.empty()) {
        // A base file of keys only: its strings are the disc's (add_disc_messages).
        return Result<TextDoc>::ok(std::move(doc));
    }
    if (strings == nullptr || strings->kind != Value::Kind::Object) {
        return Result<TextDoc>::fail("no strings object");
    }
    for (std::size_t i = 0; i < strings->keys.size(); ++i) {
        if (strings->items[i].kind != Value::Kind::String) {
            return Result<TextDoc>::fail("strings." + strings->keys[i] + ": not a string");
        }
        doc.strings.emplace_back(strings->keys[i], strings->items[i].string);
    }
    if (const Value* ws = root.find("ws"); ws != nullptr && ws->kind == Value::Kind::Object) {
        for (std::size_t i = 0; i < ws->keys.size(); ++i) {
            if (ws->items[i].kind == Value::Kind::String) {
                doc.ws[ws->keys[i]] = ws->items[i].string;
            }
        }
    }
    return Result<TextDoc>::ok(std::move(doc));
}

Result<TextDoc> read_text_doc(const std::filesystem::path& path) {
    auto text = slurp(path);
    if (!text) {
        return Result<TextDoc>::fail(text.error());
    }
    auto doc = parse_text_doc(text.value());
    if (!doc) {
        return Result<TextDoc>::fail(path.string() + ": " + doc.error());
    }
    return doc;
}

Result<std::vector<std::optional<std::string>>> read_imported_table(const std::filesystem::path& file,
                                                                    std::string_view table) {
    using R = Result<std::vector<std::optional<std::string>>>;
    auto text = slurp(file);
    if (!text) {
        return R::fail(text.error());
    }
    auto parsed = json::parse(text.value());
    if (!parsed) {
        return R::fail(file.string() + ": " + parsed.error());
    }
    const Value* tables = parsed.value().find("tables");
    if (tables == nullptr || tables->kind != Value::Kind::Array) {
        return R::fail(file.string() + ": no tables");
    }
    const auto nibble = [](char c) -> int {
        return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
    };
    for (const Value& t : tables->items) {
        const std::string* name = string_member(t, "name");
        const Value* entries = t.find("entries");
        if (name == nullptr || *name != table || entries == nullptr || entries->kind != Value::Kind::Array) {
            continue;
        }
        std::vector<std::optional<std::string>> out(entries->items.size());
        for (std::size_t i = 0; i < entries->items.size(); ++i) {
            const Value& e = entries->items[i];
            const Value* id = e.find("id");
            const std::string* raw = string_member(e, "raw");
            if (id == nullptr || id->kind != Value::Kind::Integer || id->integer < 0 ||
                static_cast<std::size_t>(id->integer) >= out.size()) {
                return R::fail(file.string() + ": " + std::string(table) + ": an entry without a valid id");
            }
            if (raw == nullptr) {
                continue;  // "null": true
            }
            std::string bytes;
            if (raw->size() % 2 != 0) {
                return R::fail(file.string() + ": " + std::string(table) + ": odd hex in raw");
            }
            for (std::size_t k = 0; k + 1 < raw->size(); k += 2) {
                const int hi = nibble((*raw)[k]);
                const int lo = nibble((*raw)[k + 1]);
                if (hi < 0 || lo < 0) {
                    return R::fail(file.string() + ": " + std::string(table) + ": bad hex in raw");
                }
                bytes += static_cast<char>(hi * 16 + lo);
            }
            out[static_cast<std::size_t>(id->integer)] = std::move(bytes);
        }
        return R::ok(std::move(out));
    }
    return R::fail(file.string() + ": no table '" + std::string(table) + "'");
}

void TextLayers::add(const TextDoc& doc, int layer, std::vector<std::string>& warnings) {
    if ((index_layers_ >> layer) & 1u) {
        for (const auto& [table, idx] : doc.index) {
            if (table == "map") {
                maps_[doc.map.empty() ? std::string("common") : doc.map] = idx;
            } else {
                items_[table] = idx;
            }
        }
    }
    if (((extra_layers_ >> layer) & 1u) && !doc.map.empty() && map_index(doc.map) != nullptr) {
        auto give_slot = [&](const std::string& key) {
            if (!key.empty() && !key.starts_with("common.") && map_slot(doc.map, key) < 0) {
                extras_[doc.map].push_back(key);
            }
        };
        if (const auto it = doc.index.find("map"); it != doc.index.end()) {
            for (const std::string& key : it->second.keys) {
                give_slot(key);
            }
        }
        for (const auto& [key, readable] : doc.strings) {
            give_slot(key);
        }
    }
    for (const auto& [key, readable] : doc.strings) {
        Entry e;
        e.text = readable;
        if (const auto it = doc.ws.find(key); it != doc.ws.end()) {
            e.ws = it->second;
        }
        e.layer = layer;
        auto engine = markup::encode(e.text, e.ws, markup::style_for_key(key));
        if (engine) {
            e.engine = std::move(engine.value());
            e.ok = true;
        } else {
            warnings.push_back(key + ": " + engine.error());
        }
        entries_[key].push_back(std::move(e));
    }
}

void TextLayers::add_engine(const std::string& key, std::string_view engine, int layer) {
    markup::Decoded decoded = markup::decode(engine, markup::style_for_key(key));
    Entry e;
    e.text = std::move(decoded.text);
    e.ws = std::move(decoded.ws);
    e.engine = std::string(engine);
    e.layer = layer;
    e.ok = true;
    entries_[key].push_back(std::move(e));
}

int TextLayers::add_disc_messages(const std::filesystem::path& import_text, int layer,
                                  std::vector<std::string>& warnings) {
    int added = 0;
    bool common_done = false;
    const auto put = [&](const TextIndex& idx, const std::vector<std::optional<std::string>>& table,
                         const std::string& what) {
        for (std::size_t i = 0; i < idx.keys.size(); ++i) {
            const std::size_t slot = static_cast<std::size_t>(idx.first) + i;
            if (idx.keys[i].empty()) {
                continue;
            }
            if (slot >= table.size() || !table[slot]) {
                warnings.push_back(what + ": the disc has no message " + std::to_string(slot) + " (" + idx.keys[i] + ")");
                continue;
            }
            add_engine(idx.keys[i], *table[slot], layer);
            ++added;
        }
    };
    for (const auto& [map, idx] : maps_) {
        if (map == "common") {
            continue;
        }
        std::string file = map;
        for (char& c : file) {
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }
        const std::filesystem::path path = import_text / "VIN" / (file + ".json");
        auto table = read_imported_table(path, "map_messages");
        if (!table) {
            warnings.push_back(table.error());
            continue;
        }
        // The shared block opens every map's table; the first map read supplies it.
        if (const TextIndex* common = map_index("common"); common != nullptr && !common_done) {
            common_done = true;
            put(*common, table.value(), path.string());
        }
        put(idx, table.value(), path.string());
    }
    return added;
}

void TextLayers::add_dir(const std::filesystem::path& dir, int layer, std::vector<std::string>& warnings) {
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) {
        return;
    }
    std::vector<std::filesystem::path> files;
    for (auto it = std::filesystem::recursive_directory_iterator(dir, ec);
         !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
        if (it->is_regular_file(ec) && it->path().extension() == ".json" && it->path().filename() != "lang.json") {
            files.push_back(it->path());
        }
    }
    std::sort(files.begin(), files.end());
    for (const auto& file : files) {
        auto doc = read_text_doc(file);
        if (!doc) {
            warnings.push_back(doc.error());
            continue;
        }
        add(doc.value(), layer, warnings);
    }
}

void TextLayers::remove_layer(int layer) {
    for (auto it = entries_.begin(); it != entries_.end();) {
        auto& list = it->second;
        list.erase(std::remove_if(list.begin(), list.end(), [layer](const Entry& e) { return e.layer == layer; }),
                   list.end());
        it = list.empty() ? entries_.erase(it) : std::next(it);
    }
}

const TextLayers::Entry* TextLayers::find(std::string_view key, unsigned mask) const {
    const auto it = entries_.find(key);
    if (it == entries_.end()) {
        return nullptr;
    }
    const Entry* best = nullptr;
    // Higher layers win; within one layer the later document (a higher-priority mod) wins.
    for (const Entry& e : it->second) {
        if (e.ok && ((mask >> e.layer) & 1u) && (best == nullptr || e.layer >= best->layer)) {
            best = &e;
        }
    }
    return best;
}

const TextIndex* TextLayers::map_index(std::string_view map) const {
    const auto it = maps_.find(map);
    return it == maps_.end() ? nullptr : &it->second;
}

const TextIndex* TextLayers::item_index(std::string_view table) const {
    const auto it = items_.find(table);
    return it == items_.end() ? nullptr : &it->second;
}

const std::vector<std::string>* TextLayers::map_extras(std::string_view map) const {
    const auto it = extras_.find(map);
    return it == extras_.end() ? nullptr : &it->second;
}

int TextLayers::map_total(std::string_view map) const {
    const TextIndex* own = map_index(map);
    if (own == nullptr) {
        return 0;
    }
    const auto* extra = map_extras(map);
    return own->first + static_cast<int>(own->keys.size()) + (extra != nullptr ? static_cast<int>(extra->size()) : 0);
}

bool TextLayers::is_extra_slot(std::string_view map, int slot) const {
    const TextIndex* own = map_index(map);
    return own != nullptr && slot >= own->first + static_cast<int>(own->keys.size()) && slot < map_total(map);
}

const std::string* TextLayers::map_key(std::string_view map, int slot) const {
    for (const TextIndex* idx : {map_index("common"), map_index(map)}) {
        if (idx != nullptr && slot >= idx->first && slot - idx->first < static_cast<int>(idx->keys.size())) {
            const std::string& key = idx->keys[static_cast<std::size_t>(slot - idx->first)];
            return key.empty() ? nullptr : &key;
        }
    }
    if (is_extra_slot(map, slot)) {
        const TextIndex* own = map_index(map);
        return &(*map_extras(map))[static_cast<std::size_t>(slot - own->first) - own->keys.size()];
    }
    return nullptr;
}

int TextLayers::map_slot(std::string_view map, std::string_view key) const {
    for (const TextIndex* idx : {map_index("common"), map_index(map)}) {
        if (idx == nullptr) {
            continue;
        }
        for (std::size_t i = 0; i < idx->keys.size(); ++i) {
            if (idx->keys[i] == key) {
                return idx->first + static_cast<int>(i);
            }
        }
    }
    if (const auto* extra = map_extras(map)) {
        const TextIndex* own = map_index(map);
        for (std::size_t i = 0; i < extra->size(); ++i) {
            if ((*extra)[i] == key) {
                return own->first + static_cast<int>(own->keys.size() + i);
            }
        }
    }
    return -1;
}

std::vector<std::string> TextLayers::keys_in(unsigned mask) const {
    std::vector<std::string> out;
    for (const auto& [key, list] : entries_) {
        for (const Entry& e : list) {
            if ((mask >> e.layer) & 1u) {
                out.push_back(key);
                break;
            }
        }
    }
    return out;
}

}  // namespace sh::convert::text
