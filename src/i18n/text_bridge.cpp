// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/ids/game_numbers.h"
#include "i18n/text_bridge.h"

#include "engine/app/pc_paths.h"
#include "engine/ids/ids.h"
#include "engine/mods/mods.h"
#include "formats/convert/text/text_db.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

extern "C" const char* PcPort_GetGameDataPath(void);

namespace {

namespace fs = std::filesystem;
namespace txt = sh::convert::text;

enum Layer { kBase = 0, kEnMods = 1, kLang = 2, kLangMods = 3 };

struct State {
    bool loaded = false;
    txt::TextLayers layers;
    std::string lang = "en";
    std::string map;
    std::vector<sh::mods::ModInfo> mods;  // ascending priority
    // Base English -> keys, for the chokepoints that see compiled literals.
    bool reverse_built = false;
    std::unordered_map<std::string, std::vector<std::string>> menu_keys;  // menu.* by menu_norm(engine)
    std::unordered_map<std::string, std::vector<std::string>> ui_menu_keys;  // ui.* by menu_norm(engine)
    std::unordered_map<std::string, std::vector<std::string>> ui_keys;    // ui.* by readable text
};

std::unique_ptr<State>& current() {
    static std::unique_ptr<State> s = std::make_unique<State>();
    return s;
}

// The stores a reload replaced: the game may still hold strings of them.
std::vector<std::unique_ptr<State>>& retired() {
    static std::vector<std::unique_ptr<State>> list;
    return list;
}

State& state() { return *current(); }

void report(const std::vector<std::string>& warnings, const char* what) {
    for (const auto& w : warnings) {
        std::fprintf(stderr, "[TEXT] %s: %s\n", what, w.c_str());
    }
}

std::vector<sh::mods::ModInfo> discover_mods(std::vector<std::string>& warnings) {
    char mods[1024];
    PcPaths_ExeFile(mods, sizeof mods, "mods");
    std::error_code ec;
    if (!fs::is_directory(mods, ec)) {
        return {};
    }
    return sh::mods::discover(mods, warnings);
}

void load_lang(State& s, int base_layer, int mods_layer, const std::string& code) {
    std::vector<std::string> warnings;
    const char* gamedata = PcPort_GetGameDataPath();
    if (gamedata != nullptr && *gamedata) {
        s.layers.add_dir(fs::path(gamedata) / "text" / code, base_layer, warnings);
    }
    for (const auto& mod : s.mods) {
        s.layers.add_dir(mod.root / "text" / code, mods_layer, warnings);
    }
    report(warnings, code.c_str());
}

// Item text slots count from the first inventory number (slot 0 = item 32), like the disc's tables.
constexpr int kItemFirst = 32;
constexpr int kItemSlots = 256 - kItemFirst;

std::string item_key(int slot, bool desc);

State& ready() {
    State& s = state();
    const char* gamedata = PcPort_GetGameDataPath();
    // Not latched before the game data path is known: a call that early loads nothing.
    if (!s.loaded && gamedata != nullptr && *gamedata) {
        s.loaded = true;
        s.layers.set_index_layers(1u << kBase);
        s.layers.set_extra_layers(1u << kEnMods);
        std::vector<std::string> warnings;
        s.mods = discover_mods(warnings);
        report(warnings, "mods");
        load_lang(s, kBase, kEnMods, "en");
    }
    return s;
}

bool is_pad(char c) {
    return c == '_' || c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

// The form literals are compared in: no \x01 kerning, no outer padding, '=' as '-' (the
// dash of the font, which some literals write either way).
std::string menu_norm(std::string_view s) {
    std::size_t b = 0;
    while (b < s.size() && is_pad(s[b])) {
        ++b;
    }
    std::string out;
    for (std::size_t i = b; i < s.size(); ++i) {
        if (s[i] != '\x01') {
            out += s[i] == '=' ? '-' : s[i];
        }
    }
    while (!out.empty() && is_pad(out.back())) {
        out.pop_back();
    }
    return out;
}

void build_reverse(State& s) {
    if (s.reverse_built || !s.loaded) {
        return;
    }
    s.reverse_built = true;
    for (const std::string& key : s.layers.keys_in(PC_TEXT_BASE)) {
        const bool menu = key.starts_with("menu.");
        const bool ui = key.starts_with("ui.");
        if (!menu && !ui) {
            continue;
        }
        const auto* e = s.layers.find(key, PC_TEXT_BASE);
        if (e == nullptr) {
            continue;
        }
        // A literal is a menu.* key's first; a ui.* key with the same English stands for it
        // only when no menu.* key does.
        if (menu) {
            s.menu_keys[menu_norm(e->engine)].push_back(key);
        } else {
            s.ui_keys[e->text].push_back(key);
            s.ui_menu_keys[menu_norm(e->engine)].push_back(key);
        }
    }
}

// The English layers' winner among keys that share one base literal: the highest layer
// (a mod's English) wins, so an override of any of them reaches every place drawing it.
const txt::TextLayers::Entry* english_winner(State& s, const std::vector<std::string>& keys) {
    const txt::TextLayers::Entry* best = nullptr;
    for (const std::string& key : keys) {
        const auto* e = s.layers.find(key, PC_TEXT_EN);
        if (e != nullptr && (best == nullptr || e->layer > best->layer)) {
            best = e;
        }
    }
    return best;
}

// The selected language's winner among the same keys.
const txt::TextLayers::Entry* language_winner(State& s, const std::vector<std::string>& keys) {
    const txt::TextLayers::Entry* best = nullptr;
    for (const std::string& key : keys) {
        const auto* e = s.layers.find(key, PC_TEXT_UPPER);
        if (e != nullptr && (best == nullptr || e->layer > best->layer)) {
            best = e;
        }
    }
    return best;
}

const txt::TextLayers::Entry* lookup(State& s, std::string_view key) {
    if (const auto* e = s.layers.find(key, PC_TEXT_UPPER)) {
        return e;
    }
    return s.layers.find(key, PC_TEXT_EN);
}

}  // namespace

extern "C" void Pc_TextSetLang(const char* code) {
    State& s = ready();
    std::string want = code != nullptr && *code ? code : "en";
    if (want == s.lang) {
        return;
    }
    s.layers.remove_layer(kLang);
    s.layers.remove_layer(kLangMods);
    s.lang = want;
    if (want != "en") {
        load_lang(s, kLang, kLangMods, want);
    }
}

extern "C" int Pc_TextReload(void) {
    const std::string lang = state().lang;
    const std::string map = state().map;
    retired().push_back(std::move(current()));
    current() = std::make_unique<State>();
    State& s = ready();
    s.map = map;
    Pc_TextSetLang(lang.c_str());
    return static_cast<int>(s.mods.size());
}

extern "C" const char* Pc_TextLang(void) {
    return ready().lang.c_str();
}

extern "C" void Pc_TextSetMap(const char* map) {
    ready().map = map != nullptr ? map : "";
}

extern "C" int Pc_TextMapCount(const char* map) {
    State& s = ready();
    const txt::TextIndex* common = s.layers.map_index("common");
    const txt::TextIndex* own = map != nullptr ? s.layers.map_index(map) : nullptr;
    if (common == nullptr || own == nullptr) {
        return 0;
    }
    return own->first + static_cast<int>(own->keys.size());
}

extern "C" int Pc_TextMapTotal(const char* map) {
    State& s = ready();
    return Pc_TextMapCount(map) > 0 ? s.layers.map_total(map) : 0;
}

extern "C" const char* Pc_TextMapMsg(const char* map, int slot, int layers) {
    State& s = ready();
    const std::string_view name = map != nullptr ? map : "";
    const std::string* key = s.layers.map_key(name, slot);
    if (key == nullptr) {
        return nullptr;
    }
    const auto* e = s.layers.find(*key, static_cast<unsigned>(layers));
    if (e == nullptr && s.layers.is_extra_slot(name, slot)) {
        e = s.layers.find(*key, PC_TEXT_EN_MODS);
    }
    return e != nullptr ? e->engine.c_str() : nullptr;
}

extern "C" int Pc_TextMsgIndex(const char* key) {
    State& s = ready();
    return key != nullptr ? s.layers.map_slot(s.map, key) : -1;
}

extern "C" const char* Pc_TextMapAddedKey(const char* map, int slot) {
    State& s = ready();
    const std::string_view name = map != nullptr ? map : "";
    if (!s.layers.is_extra_slot(name, slot)) {
        return nullptr;
    }
    const std::string* key = s.layers.map_key(name, slot);
    return key != nullptr ? key->c_str() : nullptr;
}

extern "C" const char* Pc_TextGet(const char* key) {
    State& s = ready();
    const auto* e = key != nullptr ? lookup(s, key) : nullptr;
    return e != nullptr ? e->text.c_str() : nullptr;
}

extern "C" const char* Pc_TextGetIn(const char* key, int layers, int engine) {
    State& s = ready();
    const auto* e = key != nullptr ? s.layers.find(key, static_cast<unsigned>(layers)) : nullptr;
    if (e == nullptr) {
        return nullptr;
    }
    return engine ? e->engine.c_str() : e->text.c_str();
}

extern "C" const char* Pc_TextMenuText(const char* literal) {
    State& s = ready();
    if (literal == nullptr || *literal == '\0') {
        return nullptr;
    }
    build_reverse(s);
    const std::string norm = menu_norm(literal);
    auto it = s.menu_keys.find(norm);
    if (it == s.menu_keys.end()) {
        it = s.ui_menu_keys.find(norm);
        if (it == s.ui_menu_keys.end()) {
            return nullptr;
        }
    }
    if (const auto* e = language_winner(s, it->second)) {
        return e->engine.c_str();
    }
    const auto* e = english_winner(s, it->second);
    if (e == nullptr || menu_norm(e->engine) == norm) {
        return nullptr;
    }
    return e->engine.c_str();
}

extern "C" const char* Pc_TextUiText(const char* en) {
    State& s = ready();
    if (en == nullptr || *en == '\0') {
        return nullptr;
    }
    build_reverse(s);
    const auto it = s.ui_keys.find(en);
    if (it == s.ui_keys.end()) {
        return nullptr;
    }
    if (const auto* e = language_winner(s, it->second)) {
        return e->text.c_str();
    }
    const auto* e = english_winner(s, it->second);
    if (e == nullptr || e->text == en) {
        return nullptr;
    }
    return e->text.c_str();
}

namespace {

// "item.HealthDrink.name" for slot 0, "item.mymod:Crowbar.desc" for a mod's item: the key by the
// item's name. Empty when no item has the number.
std::string item_key(int slot, bool desc) {
    const char* id = sh::ids::item_name(slot + kItemFirst);
    if (id == nullptr) {
        return {};
    }
    return "item." + std::string(sh::ids::display(id)) + (desc ? ".desc" : ".name");
}

// The keys before items were keyed by name: "item.health_drink.name", the number table of an old
// base file ("index": {"item_name": ...}) and "item.<number>.name". Still found, with one warning
// per key.
const txt::TextLayers::Entry* old_item_entry(State& s, int slot, bool desc, unsigned mask) {
    std::vector<std::string> keys;
    if (const char* id = sh::ids::item_name(slot + kItemFirst); id != nullptr && sh::ids::is_base(id)) {
        std::string snake;
        for (const char c : sh::ids::local_name(id)) {
            if (std::isupper(static_cast<unsigned char>(c)) && !snake.empty()) {
                snake += '_';
            }
            snake += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        keys.push_back("item." + snake + (desc ? ".desc" : ".name"));
    }
    if (const txt::TextIndex* idx = s.layers.item_index(desc ? "item_desc" : "item_name")) {
        const int i = slot + kItemFirst - idx->first;
        if (i >= 0 && i < static_cast<int>(idx->keys.size()) && !idx->keys[static_cast<std::size_t>(i)].empty()) {
            keys.push_back(idx->keys[static_cast<std::size_t>(i)]);
        }
    }
    keys.push_back("item." + std::to_string(slot + kItemFirst) + (desc ? ".desc" : ".name"));
    static std::set<std::string> warned;
    for (const std::string& key : keys) {
        if (const auto* e = s.layers.find(key, mask)) {
            if (warned.insert(key).second) {
                std::fprintf(stderr, "[TEXT] %s is an old item key; the current one is %s\n", key.c_str(),
                             item_key(slot, desc).c_str());
            }
            return e;
        }
    }
    return nullptr;
}

const txt::TextLayers::Entry* item_entry(State& s, int slot, bool desc, unsigned mask) {
    if (slot < 0 || slot >= kItemSlots) {
        return nullptr;
    }
    const std::string key = item_key(slot, desc);
    if (const auto* e = key.empty() ? nullptr : s.layers.find(key, mask)) {
        return e;
    }
    return old_item_entry(s, slot, desc, mask);
}

}  // namespace

extern "C" const char* Pc_TextItemEnglish(int itemIdx, int desc) {
    return Pc_TextItemIn(itemIdx, desc, PC_TEXT_EN);
}

extern "C" const char* Pc_TextItemIn(int itemIdx, int desc, int layers) {
    State& s = ready();
    const auto* e = item_entry(s, itemIdx, desc != 0, static_cast<unsigned>(layers));
    return e != nullptr ? e->engine.c_str() : nullptr;
}

extern "C" int Pc_TextEachDir(void (*fn)(const char*, void*), void* user) {
    int count = 0;
    const char* gamedata = PcPort_GetGameDataPath();
    if (gamedata != nullptr && *gamedata) {
        fn((fs::path(gamedata) / "text").string().c_str(), user);
        ++count;
    }
    std::vector<std::string> warnings;
    for (const auto& mod : discover_mods(warnings)) {
        fn((mod.root / "text").string().c_str(), user);
        ++count;
    }
    return count;
}

extern "C" int Pc_TextCountKeys(const char* prefix) {
    State& s = ready();
    int count = 0;
    for (const std::string& key : s.layers.keys_in(PC_TEXT_UPPER)) {
        if (prefix == nullptr || key.starts_with(prefix)) {
            ++count;
        }
    }
    return count;
}
