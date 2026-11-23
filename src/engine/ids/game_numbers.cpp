// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/ids/game_numbers.h"

#include "engine/audio/sound_registry.h"
#include "engine/chara/chara_library.h"
#include "engine/flags/flag_registry.h"
#include "engine/ids/ids.h"
#include "engine/items/item_library.h"
#include "engine/log/sh_log.h"
#include "formats/common/json.h"
#include "formats/convert/mapdata/mapdata.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace sh::ids {

namespace fs = std::filesystem;

namespace {

constexpr int kGroupSize = 32;        // items and weapons come in groups of 32 (e_InvItemGroup)
constexpr int kFirstTone = 1281;      // e_SfxId of the first bank tone of the sound table
constexpr int kFirstXa = 4096;        // e_SfxId of XA table entry 0
constexpr int kFlagBits = 52 * 32;    // the save's event flag words
// 0 is no item and 0xFF an empty slot (the console's byte); a mod's item past the group ranges takes the first free
// number of 1..254, then numbers from 256 on, without an upper bound.
constexpr int kEmptySlotItem = 0xFF;
// A mod's weapon takes its packed attacks (number + 10 * input) from rows past the original's attack table: numbers
// whose three rows are all free and past what the original and its characters address (row 70 is Kaufmann's).
// Harry's code decodes these rows through mod_attack_weapon / attack_is_gun, not by arithmetic. These first (highest
// first, away from rows others may append; the numbers the console's signed byte could hold), then
// kMoreModWeaponsFrom on without a limit.
constexpr int kModWeaponNumbers[] = {107, 106, 105, 104, 103, 102, 101, 100, 79, 78, 77, 76, 75, 74, 73, 72, 71};
constexpr int kMoreModWeaponsFrom = 130;
constexpr int kFirstGunAttack = 32;  // the handgun's tap: the original's packed attacks from here on are guns'
constexpr int kInputStride = 10;

std::string& preferred_namespace() {
    thread_local std::string ns;
    return ns;
}

struct Registry {
    std::map<std::string, int, std::less<>> by_id;
    std::map<int, std::string> by_number;

    void add(std::string id, int number) {
        by_number.emplace(number, id);
        by_id.emplace(std::move(id), number);
    }
    void alias(std::string id, int number) { by_id.emplace(std::move(id), number); }
    int find(std::string_view name) const {
        if (name.find(':') != std::string_view::npos) {
            const auto it = by_id.find(name);
            return it != by_id.end() ? it->second : -1;
        }
        if (const std::string& ns = preferred_namespace(); !ns.empty()) {
            if (const auto it = by_id.find(qualify(name, ns)); it != by_id.end()) {
                return it->second;
            }
        }
        if (const auto it = by_id.find(qualify(name)); it != by_id.end()) {
            return it->second;
        }
        int found = -1;
        for (const auto& [id, number] : by_id) {
            if (local_name(id) == name) {
                if (found >= 0) {
                    return -1;  // more than one mod has the name: the caller has to say which
                }
                found = number;
            }
        }
        return found;
    }
    const char* name(int number) const {
        const auto it = by_number.find(number);
        return it != by_number.end() ? it->second.c_str() : nullptr;
    }
};

struct Tables {
    Registry items;
    Registry weapons;
    Registry charas;
    Registry pickups;
    Registry flags;
    Registry sounds;
    Registry paper_maps;
    std::map<std::string, int, std::less<>> item_group_base;  // group name -> first number
    int chara_count = 0;
    std::map<std::string, int, std::less<>> old_save_flags;  // former full id -> the bit it named in old saves
    std::set<int> paper_map_mark_bits;
    struct ModRow {
        int weapon = -1;
        int input = 0;
        bool gun = false;
    };
    std::map<int, ModRow> weapon_rows;  // attack rows the mods' weapons took
    struct MessageIndex {
        int first = 0;
        std::vector<std::string> keys;  // "" = an unused number
    };
    // "common" (the block every map's messages open with) and each map's own messages, from gamedata/text/en.
    std::map<std::string, MessageIndex, std::less<>> messages;
};

std::mutex& lock() {
    static std::mutex m;
    return m;
}

struct State {
    fs::path gamedata;
    fs::path mods;
    bool loaded = false;
    Tables tables;
};

State& state() {
    static State s;
    return s;
}

bool read_json(const fs::path& file, json::Value& out) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        return false;
    }
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    auto doc = json::parse(text);
    if (!doc) {
        SH_LOG_WARN(GAME, "[IDS] %s: %s", file.string().c_str(), doc.error().c_str());
        return false;
    }
    out = std::move(doc.value());
    return true;
}

// A list of names (null: a gap) numbered from `first`.
void add_list(Registry& r, const json::Value* list, int first) {
    if (list == nullptr || !list->is_array()) {
        return;
    }
    int number = first;
    for (const json::Value& v : list->array()) {
        if (v.is_string()) {
            r.add(qualify(v.string()), number);
        }
        ++number;
    }
}

// {"group": [names]}: the k-th group starts at 32 * k.
void add_groups(Registry& r, const json::Value* groups, std::map<std::string, int, std::less<>>* bases) {
    if (groups == nullptr || !groups->is_object()) {
        return;
    }
    int base = 0;
    for (const auto& [group, list] : groups->members()) {
        if (bases != nullptr) {
            bases->emplace(group, base);
        }
        add_list(r, &list, base);
        base += kGroupSize;
    }
}

// The original save layout (original_order.json "event_flags"): the n-th name is save bit n, from 1; null is a
// bit without a flag. Only the records of the original need these bits (the PS1 memory cards, the save record).
void load_flag_layout(Tables& t, const json::Value* layout) {
    if (layout == nullptr || !layout->is_array()) {
        return;
    }
    int bit = 1;
    for (const json::Value& v : layout->array()) {
        if (v.is_string() && bit < kFlagBits) {
            t.flags.add(qualify(v.string()), bit);
        }
        ++bit;
    }
}

void load_sounds(Tables& t, const json::Value& doc) {
    const json::Value* sounds = doc.find("sounds");
    if (sounds == nullptr || !sounds->is_object()) {
        return;
    }
    int tone = kFirstTone;
    // "aliases": former names of the entry; they resolve to its number but never name it.
    const auto add_aliases = [&t](const json::Value& entry, int number) {
        if (const json::Value* aliases = entry.find("aliases"); aliases != nullptr && aliases->is_array()) {
            for (const json::Value& a : aliases->array()) {
                if (a.is_string()) {
                    t.sounds.alias(qualify(a.string()), number);
                }
            }
        }
    };
    for (const auto& [name, entry] : sounds->members()) {
        const json::Value* slot = entry.find("slot");
        if (slot != nullptr && slot->is_string() && slot->string() == "xa") {
            const json::Value* item = entry.find("xa_item");
            if (item != nullptr && item->is_integer()) {
                const int number = kFirstXa + static_cast<int>(item->integer());
                t.sounds.add(qualify(name), number);
                add_aliases(entry, number);
            }
            continue;
        }
        add_aliases(entry, tone);
        t.sounds.add(qualify(name), tone++);
    }
}

// A mod's item with "pickup_once" counts once through a flag of its own: the save's byte of once-only pickups has a
// bit only for the original's items, while flags are saved by name. Declared here so the flag gets its bit with the
// rest; the library is read without numbers (this runs inside the numbering's own lock).
void declare_pickup_once_flags(flags::FlagRegistry& registry, const fs::path& gamedata, const fs::path& mods,
                               std::vector<std::string>& warnings) {
    if (mods.empty()) {
        return;
    }
    std::vector<std::string> item_warnings;
    const items::Numbering none;
    const auto library = items::ItemLibrary::load(gamedata, mods, item_warnings, nullptr, &none);
    if (!library) {
        return;
    }
    for (const items::ItemDef& item : library.value().items()) {
        if (!item.inventory.pickup_once || is_base(item.id)) {
            continue;
        }
        const std::string flag = pickup_once_flag_name(item.id);
        json::Writer w;
        w.begin_object().member("format", "sh-flags").member("version", 1).key("flags").begin_object();
        w.key(local_name(flag)).begin_object();
        w.member("description", item.id + " was picked up once (items/<Name>.json \"pickup_once\")");
        w.end_object().end_object().end_object();
        const auto applied = registry.apply(w.str(), namespace_of(flag), item.id, warnings);
        if (!applied) {
            warnings.push_back(item.id + ": " + applied.error());
        }
    }
}

// The declared flags (gamedata/flags/*.json, the mods'): a bit for every flag the layout does not place, the former
// names, the paper map marks. A layout name nothing declares keeps its bit (and is reported).
void load_flag_declarations(Tables& t, const fs::path& gamedata, const fs::path& mods) {
    std::vector<std::string> warnings;
    auto registry = flags::FlagRegistry::load(gamedata, mods, warnings);
    if (!registry) {
        SH_LOG_WARN(EVENT, "[FLAGS] %s", registry.error().c_str());
        return;
    }
    declare_pickup_once_flags(registry.value(), gamedata, mods, warnings);
    const flags::FlagRegistry& r = registry.value();
    std::map<std::string, int, std::less<>> placed;
    int undeclared = 0;
    for (const auto& [bit, id] : t.flags.by_number) {
        placed.emplace(id, bit);
        if (r.find(id) == nullptr && !r.flags().empty()) {
            ++undeclared;
        }
    }
    if (undeclared > 0) {
        SH_LOG_WARN(EVENT, "[FLAGS] %d flag(s) of original_order.json are declared in no flags file", undeclared);
    }
    const auto bits = r.assign_bits(placed, kFlagBits, warnings);
    int added = 0;
    for (const auto& [id, bit] : bits) {
        if (t.flags.by_id.find(id) == t.flags.by_id.end()) {
            t.flags.add(id, bit);
            SH_LOG_INFO(EVENT, "[FLAGS] %s -> save bit %d", id.c_str(), bit);
            ++added;
        }
    }
    if (added > 0) {
        SH_LOG_INFO(EVENT, "[FLAGS] %d flag(s) outside the original layout", added);
    }
    for (const flags::FlagInfo& f : r.flags()) {
        const auto it = t.flags.by_id.find(f.id);
        if (it == t.flags.by_id.end()) {
            continue;
        }
        const int bit = it->second;
        for (const std::string& a : f.aliases) {
            if (const auto known = t.flags.by_id.find(a); known != t.flags.by_id.end() && known->second != bit) {
                warnings.push_back(f.origin + ": alias " + a + " of " + f.id + " is another flag's name");
                continue;
            }
            t.flags.alias(a, bit);  // a former name: found, never written
        }
        for (const std::string& a : f.old_save_names) {
            t.old_save_flags.emplace(a, bit);
        }
        if (f.paper_map_mark) {
            t.paper_map_mark_bits.insert(bit);
        }
    }
    for (const std::string& w : warnings) {
        SH_LOG_WARN(EVENT, "[FLAGS] %s", w.c_str());
    }
}

// The "index"."map" table of an English text file: which key each message number of a map has.
void add_message_index(Tables& t, const fs::path& file, const std::string& fallback_name) {
    json::Value doc;
    if (!read_json(file, doc)) {
        return;
    }
    const json::Value* index = doc.find("index");
    const json::Value* table = index != nullptr && index->is_object() ? index->find("map") : nullptr;
    const json::Value* first = table != nullptr && table->is_object() ? table->find("first") : nullptr;
    const json::Value* keys = table != nullptr && table->is_object() ? table->find("keys") : nullptr;
    if (first == nullptr || !first->is_integer() || keys == nullptr || !keys->is_array()) {
        return;
    }
    const json::Value* map = doc.find("map");
    Tables::MessageIndex out;
    out.first = static_cast<int>(first->integer());
    for (const json::Value& key : keys->array()) {
        out.keys.push_back(key.is_string() ? key.string() : std::string());
    }
    t.messages[map != nullptr && map->is_string() ? map->string() : fallback_name] = std::move(out);
}

void load_message_index(Tables& t, const fs::path& text_en) {
    add_message_index(t, text_en / "common.json", "common");
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(text_en / "maps", ec)) {
        if (entry.path().extension() == ".json") {
            add_message_index(t, entry.path(), entry.path().stem().string());
        }
    }
}

fs::path find_gamedata(const fs::path& configured) {
    std::error_code ec;
    std::vector<fs::path> candidates;
    if (!configured.empty()) {
        candidates.push_back(configured);
    }
    candidates.emplace_back("gamedata");
#ifdef SH_IDS_SOURCE_GAMEDATA
    candidates.emplace_back(SH_IDS_SOURCE_GAMEDATA);
#endif
    for (const fs::path& dir : candidates) {
        if (fs::is_regular_file(dir / "original_order.json", ec)) {
            return dir;
        }
    }
    return configured;
}

// The numbers a mod's thing took before a reload (items, weapons and their attack rows, characters, flags outside the
// original layout) go back into the new tables first: inventory slots, the session and the save hold them.
void keep_numbers(Registry& fresh, const Registry& old) {
    for (const auto& [number, id] : old.by_number) {
        if (fresh.by_id.find(id) == fresh.by_id.end() && fresh.by_number.find(number) == fresh.by_number.end()) {
            fresh.add(id, number);
        }
    }
}

Tables build_tables(const State& s, const Tables* keep) {
    Tables t;
    const fs::path dir = find_gamedata(s.gamedata);
    json::Value doc;
    if (read_json(dir / "original_order.json", doc)) {
        add_groups(t.items, doc.find("items"), &t.item_group_base);
        add_groups(t.weapons, doc.find("weapons"), nullptr);
        add_list(t.charas, doc.find("characters"), 0);
        t.chara_count = static_cast<int>(t.charas.by_number.size());
        add_list(t.pickups, doc.find("common_pickups"), 0);
        load_flag_layout(t, doc.find("event_flags"));
    } else {
        SH_LOG_WARN(GAME, "[IDS] no original_order.json in %s: items and characters have no numbers", dir.string().c_str());
    }
    if (keep != nullptr) {
        keep_numbers(t.items, keep->items);
        keep_numbers(t.weapons, keep->weapons);
        t.weapon_rows.insert(keep->weapon_rows.begin(), keep->weapon_rows.end());
        keep_numbers(t.charas, keep->charas);
        keep_numbers(t.flags, keep->flags);  // before the declarations: they place only the flags still unplaced
    }
    load_flag_declarations(t, dir, s.mods);
    if (read_json(dir / "sounds" / "sounds.json", doc)) {
        load_sounds(t, doc);
    }
    load_message_index(t, dir / "text" / "en");
    if (read_json(dir / "paper_maps.json", doc)) {
        if (const json::Value* pages = doc.find("paper_maps"); pages != nullptr && pages->is_object()) {
            int number = 0;
            for (const auto& member : pages->members()) {
                t.paper_maps.add(qualify(member.first), number++);
            }
        }
    }
    return t;
}

Tables& tables() {
    State& s = state();
    if (!s.loaded) {
        s.loaded = true;
        s.tables = build_tables(s, nullptr);
    }
    return s.tables;
}

int lookup(Registry Tables::*which, const char* name) {
    if (name == nullptr) {
        return -1;
    }
    std::lock_guard<std::mutex> guard(lock());
    return (tables().*which).find(name);
}

const char* reverse(Registry Tables::*which, int number) {
    std::lock_guard<std::mutex> guard(lock());
    return (tables().*which).name(number);
}

// ItemLibrary numbering: a known id keeps its number, a mod's new item takes the first free number of its group,
// else the first free byte number anywhere. Nothing derives an item's weapon, ammo or group from its number
// (the data names them), so any free number serves.
int number_item(std::string_view id, std::string_view group) {
    std::lock_guard<std::mutex> guard(lock());
    Tables& t = tables();
    if (const auto it = t.items.by_id.find(id); it != t.items.by_id.end()) {
        return it->second;
    }
    if (is_base(id)) {
        return -1;  // a base item the original does not have: only a mod can add things
    }
    const auto take = [&](int first, int last) {
        for (int n = std::max(first, 1); n <= last; ++n) {
            if (n != kEmptySlotItem && t.items.by_number.find(n) == t.items.by_number.end()) {
                t.items.add(std::string(id), n);
                return n;
            }
        }
        return -1;
    };
    if (const auto base = t.item_group_base.find(group); base != t.item_group_base.end()) {
        if (const int n = take(base->second, base->second + kGroupSize - 1); n > 0) {
            return n;
        }
    }
    return take(1, std::numeric_limits<int>::max());
}

// A base weapon keeps the original's number; a mod's weapon of either class takes a free number of
// kModWeaponNumbers, then of kMoreModWeaponsFrom on, and its three rows.
int number_weapon(std::string_view id, std::string_view cls) {
    std::lock_guard<std::mutex> guard(lock());
    Tables& t = tables();
    if (const auto it = t.weapons.by_id.find(id); it != t.weapons.by_id.end()) {
        return it->second;
    }
    if (is_base(id) || (cls != "gun" && cls != "melee")) {
        return -1;
    }
    constexpr int kInputs = 3;
    const auto take = [&](int n) {
        for (int input = 0; input < kInputs; ++input) {
            if (t.weapon_rows.count(n + input * kInputStride) != 0) {
                return false;
            }
        }
        t.weapons.add(std::string(id), n);
        for (int input = 0; input < kInputs; ++input) {
            t.weapon_rows[n + input * kInputStride] = Tables::ModRow{n, input, cls == "gun"};
        }
        return true;
    };
    for (const int n : kModWeaponNumbers) {
        if (take(n)) {
            return n;
        }
    }
    for (int n = kMoreModWeaponsFrom;; ++n) {
        if (take(n)) {
            return n;
        }
    }
}

const Tables::ModRow* mod_row(int packed) {
    const auto& rows = tables().weapon_rows;
    const auto it = rows.find(packed);
    return it != rows.end() ? &it->second : nullptr;
}

// A mod's character takes the first free number after the original's; no upper bound (character numbers are int).
int number_chara(std::string_view id) {
    std::lock_guard<std::mutex> guard(lock());
    Tables& t = tables();
    if (const auto it = t.charas.by_id.find(id); it != t.charas.by_id.end()) {
        return it->second;
    }
    if (is_base(id)) {
        return -1;
    }
    for (int n = t.chara_count;; ++n) {
        if (t.charas.by_number.find(n) == t.charas.by_number.end()) {
            t.charas.add(std::string(id), n);
            return n;
        }
    }
}

// Sound names are snake_case without a namespace; only the base game's have numbers.
int number_sound(std::string_view name) {
    std::lock_guard<std::mutex> guard(lock());
    const Registry& r = tables().sounds;
    const auto it = r.by_id.find(qualify(name));
    return it != r.by_id.end() ? it->second : -1;
}

// A map's message numbers open with the common block; the map's own table takes over from its "first".
const char* message_key(std::string_view map, int number) {
    std::lock_guard<std::mutex> guard(lock());
    const auto& messages = tables().messages;
    for (const std::string_view table : {map, std::string_view("common")}) {
        const auto it = messages.find(table);
        if (it == messages.end()) {
            continue;
        }
        const int i = number - it->second.first;
        if (i >= 0 && i < static_cast<int>(it->second.keys.size())) {
            const std::string& key = it->second.keys[static_cast<std::size_t>(i)];
            return key.empty() ? nullptr : key.c_str();
        }
    }
    return nullptr;
}

int message_number(std::string_view map, std::string_view key) {
    std::lock_guard<std::mutex> guard(lock());
    const auto& messages = tables().messages;
    for (const std::string_view table : {map, std::string_view("common")}) {
        const auto it = messages.find(table);
        if (it == messages.end()) {
            continue;
        }
        const auto& keys = it->second.keys;
        const auto at = std::find(keys.begin(), keys.end(), key);
        if (!key.empty() && at != keys.end()) {
            return it->second.first + static_cast<int>(at - keys.begin());
        }
    }
    return -1;
}

[[maybe_unused]] const bool kInstalled = [] {
    install_game_numbering();
    return true;
}();

}  // namespace

NamespaceScope::NamespaceScope(std::string_view ns) : previous_(preferred_namespace()) {
    preferred_namespace() = std::string(ns);
}
NamespaceScope::~NamespaceScope() { preferred_namespace() = std::move(previous_); }

void configure(const fs::path& gamedata_dir, const fs::path& mods_dir) {
    std::lock_guard<std::mutex> guard(lock());
    State& s = state();
    s.gamedata = gamedata_dir;
    s.mods = mods_dir;
    s.loaded = false;
    tables();
}

void refresh() {
    std::lock_guard<std::mutex> guard(lock());
    State& s = state();
    if (!s.loaded) {
        tables();
        return;
    }
    Tables kept = std::move(s.tables);
    s.tables = build_tables(s, &kept);
}

int item_number(const char* name) { return lookup(&Tables::items, name); }
const char* item_name(int number) { return reverse(&Tables::items, number); }
int common_pickup_number(const char* name) { return lookup(&Tables::pickups, name); }
int weapon_number(const char* name) { return lookup(&Tables::weapons, name); }
int mod_attack_weapon(int packed_attack) {
    std::lock_guard<std::mutex> guard(lock());
    const Tables::ModRow* row = mod_row(packed_attack);
    return row != nullptr ? row->weapon : -1;
}
int mod_attack_input(int packed_attack) {
    std::lock_guard<std::mutex> guard(lock());
    const Tables::ModRow* row = mod_row(packed_attack);
    return row != nullptr ? row->input : -1;
}
bool attack_is_gun(int packed_attack) {
    std::lock_guard<std::mutex> guard(lock());
    const Tables::ModRow* row = mod_row(packed_attack);
    return row != nullptr ? row->gun : packed_attack >= kFirstGunAttack;
}
int attack_weapon(int packed_attack) {
    std::lock_guard<std::mutex> guard(lock());
    if (const Tables::ModRow* row = mod_row(packed_attack); row != nullptr) {
        return row->weapon;
    }
    if (packed_attack < 0) {
        return packed_attack;
    }
    return packed_attack < kFirstGunAttack ? packed_attack % kInputStride : packed_attack;
}
int chara_number(const char* name) {
    if (const int known = lookup(&Tables::charas, name); known >= 0 || name == nullptr) {
        return known;
    }
    // A mod's character by its full id gets its number on first use, as when the library loads it.
    return std::string_view(name).find(':') != std::string_view::npos ? number_chara(name) : -1;
}
int flag_number(const char* name) { return lookup(&Tables::flags, name); }
const char* flag_name(int bit) { return reverse(&Tables::flags, bit); }
const char* flag_name_in_old_save(const char* name) {
    if (name == nullptr) {
        return nullptr;
    }
    std::lock_guard<std::mutex> guard(lock());
    Tables& t = tables();
    const std::string_view n(name);
    auto it = t.old_save_flags.find(n);
    if (it == t.old_save_flags.end() && n.find(':') == std::string_view::npos) {
        it = t.old_save_flags.find(qualify(n));
    }
    return it != t.old_save_flags.end() ? t.flags.name(it->second) : nullptr;
}
bool flag_is_paper_map_mark(int bit) {
    std::lock_guard<std::mutex> guard(lock());
    return tables().paper_map_mark_bits.count(bit) != 0;
}
int sound_number(const char* name) { return name != nullptr ? number_sound(name) : -1; }
std::string pickup_once_flag_name(std::string_view item_id) { return qualify(item_id) + "_PickedOnce"; }
int paper_map_number(const char* name) { return lookup(&Tables::paper_maps, name); }
const char* paper_map_name(int number) { return reverse(&Tables::paper_maps, number); }

void install_game_numbering() {
    items::ItemLibrary::set_default_numbering({number_item, number_weapon});
    chara::CharaLibrary::set_default_numbering(number_chara);
    audio::SoundRegistry::set_default_numbering(number_sound);
    convert::mapdata::set_id_naming({flag_name, flag_number, item_name, item_number, message_key, message_number});
}

}  // namespace sh::ids
