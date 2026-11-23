// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
#include "engine/save/save_game.h"

#include "engine/save/footer_checksum.h"
#include "engine/save/console_records.h"
#include "formats/common/json.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>

namespace sh::save {

namespace {

// What the game leaves in a slot it emptied (item 0xFF, sort index 0xFF): not listed in a save.
constexpr game::SaveItem kBlankSlot = {0xFF, 0, 0, 0xFF};
constexpr int kEmptyItem = 0xFF;

// Display names of the save list (the original's English words); the ids are these without spaces and marks.
constexpr const char* kLocationDisplay[] = {
    "Anywhere",     "Cafe",       "Bus",       "Store",        "Infirmary",   "Doghouse",          "Gordon",
    "Church",       "Garage",     "Police",    "Reception",    "Room 302",    "Director's Office", "Jewelry Shop",
    "Pool Hall",    "Antique Shop", "Theme Park", "Boat",      "Bridge",      "Motel",             "Lighthouse",
    "Sewer",        "Nowhere",    "Child's Room", "Next Fear"};
constexpr const char* kLocationIds[] = {
    "Anywhere", "Cafe",      "Bus",         "Store",       "Infirmary",  "Doghouse",        "Gordon",
    "Church",   "Garage",    "Police",      "Reception",   "Room302",    "DirectorsOffice", "JewelryShop",
    "PoolHall", "AntiqueShop", "ThemePark", "Boat",        "Bridge",     "Motel",           "Lighthouse",
    "Sewer",    "Nowhere",   "ChildsRoom",  "NextFear"};
static_assert(std::size(kLocationDisplay) == std::size(kLocationIds));

Ref ref_of(int number, const std::function<const char*(int)>& name_of) {
    if (name_of) {
        if (const char* name = name_of(number); name != nullptr && name[0] != '\0') {
            return Ref::of_name(name);
        }
    }
    return Ref::of_number(number);
}

// The number of a ref in this build; -1 for a name it does not know.
int number_of(const Ref& ref, const std::function<int(const char*)>& number_from) {
    if (!ref.named()) {
        return static_cast<int>(ref.number);
    }
    return number_from ? number_from(ref.name.c_str()) : -1;
}

// Set bits of `words` as names (when the bit has one) or bit numbers.
void bits_to_names(std::span<const std::uint32_t> words, const std::function<const char*(int)>& name_of,
                   std::vector<std::string>& names, std::vector<int>& bits) {
    for (std::size_t w = 0; w < words.size(); ++w) {
        const std::uint32_t word = words[w];
        for (int i = 0; i < 32; ++i) {
            if ((word >> i & 1u) == 0) {
                continue;
            }
            const int bit = static_cast<int>(w) * 32 + i;
            const char* name = name_of ? name_of(bit) : nullptr;
            if (name != nullptr && name[0] != '\0') {
                names.emplace_back(name);
            } else {
                bits.push_back(bit);
            }
        }
    }
}

void names_to_bits(std::span<std::uint32_t> out, const std::vector<std::string>& names, const std::vector<int>& bits,
                   const std::function<int(const char*)>& number_from, const char* what, Warnings& warnings) {
    const int words = static_cast<int>(out.size());
    auto set = [&](int bit) { out[static_cast<std::size_t>(bit / 32)] |= 1u << (bit % 32); };
    for (const std::string& name : names) {
        const int bit = number_from ? number_from(name.c_str()) : -1;
        if (bit < 0 || bit >= words * 32) {
            warnings.push_back(std::string("the save has ") + what + " " + name +
                               ", which no installed data has; it is dropped");
            continue;
        }
        set(bit);
    }
    for (int bit : bits) {
        if (bit >= 0 && bit < words * 32) {
            set(bit);
        }
    }
}

// The flag names of a save written before kFlagNamesVersion as this build names those flags: a name that has moved
// to another flag since is the flag it meant then; every other name (current or a former alias) stays.
std::vector<std::string> current_flag_names(const std::vector<std::string>& saved, const NameTable& names) {
    std::vector<std::string> out = saved;
    if (names.flag_name_in_old_save) {
        for (std::string& n : out) {
            if (const char* meant = names.flag_name_in_old_save(n.c_str())) {
                n = meant;
            }
        }
    }
    return out;
}

// ---- JSON ----

void write_ref(json::Writer& w, std::string_view key, const Ref& ref) {
    w.key(key);
    if (ref.named()) {
        w.value(ref.name);
    } else {
        w.value(ref.number);
    }
}

void write_names(json::Writer& w, std::string_view key, const std::vector<std::string>& names) {
    w.key(key).begin_array();
    for (const std::string& n : names) {
        w.value(n);
    }
    w.end_array();
}

void write_ints(json::Writer& w, std::string_view key, const std::vector<int>& values) {
    w.key(key).inline_next().begin_array();
    for (int v : values) {
        w.value(v);
    }
    w.end_array();
}

const char* difficulty_name(int d) {
    return d == -1 ? "easy" : d == 0 ? "normal" : d == 1 ? "hard" : nullptr;
}

// Reads members with type checks; the first problem is kept and makes the whole read fail.
class Reader {
public:
    std::string error;

    const json::Value* member(const json::Value& obj, const char* key, bool required = true) {
        const json::Value* v = obj.find(key);
        if (v == nullptr && required) {
            fail(std::string("missing \"") + key + "\"");
        }
        return v;
    }

    template <typename T>
    void integer(const json::Value& obj, const char* key, T& out, bool required = true) {
        const json::Value* v = member(obj, key, required);
        if (v == nullptr) {
            return;
        }
        if (!v->is_integer()) {
            fail(std::string("\"") + key + "\" is not an integer");
            return;
        }
        const std::int64_t n = v->integer();
        // Unsigned fields accept their whole range; signed ones too; anything outside is a damaged save.
        if (n < static_cast<std::int64_t>(std::numeric_limits<T>::min()) ||
            n > static_cast<std::int64_t>(std::numeric_limits<T>::max())) {
            fail(std::string("\"") + key + "\" is out of range");
            return;
        }
        out = static_cast<T>(n);
    }

    void boolean(const json::Value& obj, const char* key, bool& out) {
        const json::Value* v = member(obj, key, false);
        if (v == nullptr) {
            return;
        }
        if (!v->is_bool()) {
            fail(std::string("\"") + key + "\" is not true or false");
            return;
        }
        out = v->boolean();
    }

    bool ref(const json::Value* v, Ref& out, const char* key) {
        if (v == nullptr) {
            return false;
        }
        if (v->is_string() && !v->string().empty()) {
            out = Ref::of_name(v->string());
            return true;
        }
        if (v->is_integer()) {
            out = Ref::of_number(v->integer());
            return true;
        }
        fail(std::string("\"") + key + "\" is neither a name nor a number");
        return false;
    }

    void ref(const json::Value& obj, const char* key, Ref& out, bool required = true) {
        ref(member(obj, key, required), out, key);
    }

    void names(const json::Value& obj, const char* key, std::vector<std::string>& out) {
        const json::Value* v = member(obj, key, false);
        if (v == nullptr) {
            return;
        }
        if (!v->is_array()) {
            fail(std::string("\"") + key + "\" is not a list");
            return;
        }
        for (const json::Value& e : v->array()) {
            if (!e.is_string()) {
                fail(std::string("\"") + key + "\" holds a value that is not a name");
                return;
            }
            out.push_back(e.string());
        }
    }

    void ints(const json::Value& obj, const char* key, std::vector<int>& out) {
        const json::Value* v = member(obj, key, false);
        if (v == nullptr) {
            return;
        }
        if (!v->is_array()) {
            fail(std::string("\"") + key + "\" is not a list");
            return;
        }
        for (const json::Value& e : v->array()) {
            if (!e.is_integer() || e.integer() < 0 || e.integer() > 0xFFFF) {
                fail(std::string("\"") + key + "\" holds a value that is not a bit number");
                return;
            }
            out.push_back(static_cast<int>(e.integer()));
        }
    }

    const json::Value* object(const json::Value& obj, const char* key, bool required = true) {
        const json::Value* v = member(obj, key, required);
        if (v != nullptr && !v->is_object()) {
            fail(std::string("\"") + key + "\" is not an object");
            return nullptr;
        }
        return v;
    }

    void fail(std::string text) {
        if (error.empty()) {
            error = std::move(text);
        }
    }
};

struct BindField {
    const char* key;
    std::uint16_t GameOptions::*field;
    std::uint16_t game::PadBinds::*live;
};

constexpr BindField kBinds[] = {
    {"enter", &GameOptions::bind_enter, &game::PadBinds::enter},
    {"cancel", &GameOptions::bind_cancel, &game::PadBinds::cancel},
    {"skip", &GameOptions::bind_skip, &game::PadBinds::skip},
    {"action", &GameOptions::bind_action, &game::PadBinds::action},
    {"aim", &GameOptions::bind_aim, &game::PadBinds::aim},
    {"light", &GameOptions::bind_light, &game::PadBinds::light},
    {"run", &GameOptions::bind_run, &game::PadBinds::run},
    {"view", &GameOptions::bind_view, &game::PadBinds::view},
    {"step_left", &GameOptions::bind_step_left, &game::PadBinds::step_left},
    {"step_right", &GameOptions::bind_step_right, &game::PadBinds::step_right},
    {"pause", &GameOptions::bind_pause, &game::PadBinds::pause},
    {"item", &GameOptions::bind_item, &game::PadBinds::item},
    {"map", &GameOptions::bind_map, &game::PadBinds::map},
    {"option", &GameOptions::bind_option, &game::PadBinds::option},
};

// The one-byte options: the save's key, the save's field and the live setting.
struct ByteField {
    const char* key;
    bool is_signed;
    std::int8_t GameOptions::*s;
    std::uint8_t GameOptions::*u;
    std::int8_t game::Settings::*live_s;
    std::uint8_t game::Settings::*live_u;
};

constexpr ByteField kByteOptions[] = {
    {"screen_x", true, &GameOptions::screen_x, nullptr, &game::Settings::screen_x, nullptr},
    {"screen_y", true, &GameOptions::screen_y, nullptr, &game::Settings::screen_y, nullptr},
    {"mono", false, nullptr, &GameOptions::mono, nullptr, &game::Settings::mono},
    {"volume_bgm", false, nullptr, &GameOptions::volume_bgm, nullptr, &game::Settings::music_volume},
    {"volume_se", false, nullptr, &GameOptions::volume_se, nullptr, &game::Settings::sfx_volume},
    {"vibration", false, nullptr, &GameOptions::vibration, nullptr, &game::Settings::vibration},
    {"brightness", false, nullptr, &GameOptions::brightness, nullptr, &game::Settings::brightness},
    {"weapon_control", false, nullptr, &GameOptions::weapon_control, nullptr, &game::Settings::weapon_control_press},
    {"blood_color", false, nullptr, &GameOptions::blood_color, nullptr, &game::Settings::blood_color},
    {"auto_load", true, &GameOptions::auto_load, nullptr, &game::Settings::auto_load, nullptr},
    {"extra_options", false, nullptr, &GameOptions::extra_options, nullptr, &game::Settings::extra_options},
    {"view_control", true, &GameOptions::view_control, nullptr, &game::Settings::view_reverse, nullptr},
    {"view_mode", true, &GameOptions::view_mode, nullptr, &game::Settings::self_view, nullptr},
    {"retreat_turn", true, &GameOptions::retreat_turn, nullptr, &game::Settings::retreat_turn_reverse, nullptr},
    {"walk_run_control", true, &GameOptions::walk_run_control, nullptr, &game::Settings::walk_run_reverse, nullptr},
    {"auto_aiming", true, &GameOptions::auto_aiming, nullptr, &game::Settings::auto_aim_off, nullptr},
    {"bullet_adjust", true, &GameOptions::bullet_adjust, nullptr, &game::Settings::bullet_multiplier, nullptr},
};

void write_options(json::Writer& w, const GameOptions& o) {
    w.key("options").begin_object();
    w.key("binds").begin_object();
    for (const BindField& b : kBinds) {
        w.member(b.key, o.*b.field);
    }
    w.end_object();
    for (const ByteField& f : kByteOptions) {
        if (f.is_signed) {
            w.member(f.key, o.*f.s);
        } else {
            w.member(f.key, o.*f.u);
        }
    }
    w.member("seen_game_over_tips", o.seen_game_over_tips);
    w.member("pal_language", o.pal_language);
    w.end_object();
}

void read_options(Reader& r, const json::Value& v, GameOptions& o) {
    if (const json::Value* binds = r.object(v, "binds")) {
        for (const BindField& b : kBinds) {
            r.integer(*binds, b.key, o.*b.field);
        }
    }
    for (const ByteField& f : kByteOptions) {
        if (f.is_signed) {
            r.integer(v, f.key, o.*f.s);
        } else {
            r.integer(v, f.key, o.*f.u);
        }
    }
    r.integer(v, "seen_game_over_tips", o.seen_game_over_tips);
    // Older saves carry "reserved": four option bytes the game never gave a meaning; read, not kept.
    std::uint32_t reserved = 0;
    r.integer(v, "reserved", reserved, false);
    r.integer(v, "pal_language", o.pal_language, false);
}

struct CounterField {
    const char* key;
    std::uint16_t SaveGame::*field;
};

// The u16 counters from 0x264, in record order.
constexpr CounterField kCounters[] = {
    {"shots_fired", &SaveGame::shots_fired},     {"close_hits", &SaveGame::close_hits},
    {"mid_hits", &SaveGame::mid_hits},           {"long_hits", &SaveGame::long_hits},
    {"far_hits", &SaveGame::far_hits},           {"mid_kill_hits", &SaveGame::mid_kill_hits},
    {"long_kill_hits", &SaveGame::long_kill_hits}, {"far_kill_hits", &SaveGame::far_kill_hits},
    {"melee_hits", &SaveGame::melee_hits},       {"melee_kill_hits", &SaveGame::melee_kill_hits},
    {"other_attacks", &SaveGame::other_attacks},
};

}  // namespace

std::span<const char* const> location_names() { return kLocationIds; }

const char* location_display_name(int location) {
    return location >= 0 && location < static_cast<int>(std::size(kLocationDisplay)) ? kLocationDisplay[location]
                                                                                       : nullptr;
}

int location_number(std::string_view name) {
    for (std::size_t i = 0; i < std::size(kLocationIds); ++i) {
        if (name == kLocationIds[i]) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

std::string to_json(const SaveGame& g) {
    json::Writer w(true);
    w.begin_object();
    w.member("format", kSaveGameFormat);
    w.member("flag_names", kFlagNamesVersion);
    write_ref(w, "map", g.map);
    w.member("room", g.room);
    write_ref(w, "location", g.location);
    write_ref(w, "paper_map", g.paper_map);
    if (const char* d = difficulty_name(g.difficulty)) {
        w.member("difficulty", d);
    } else {
        w.member("difficulty", g.difficulty);
    }

    w.key("player").begin_object();
    w.member("health", g.health);
    w.member("health_saturation", g.health_saturation);
    w.key("position").inline_next().begin_array().value(g.position_x).value(g.position_z).end_array();
    w.member("rotation_y", g.rotation_y);
    w.member("reset_byte", g.player_reset_byte);
    w.end_object();

    w.key("inventory").begin_object();
    w.member("slot_count", g.slot_count);
    write_ref(w, "equipped", g.equipped);
    w.member("item_toggle_flags", g.item_toggle_flags);
    w.member("item_flags", g.inventory_item_flags);
    w.key("items").begin_array();
    for (const InventoryEntry& e : g.items) {
        w.inline_next().begin_object();
        w.member("slot", e.slot);
        if (e.item) {
            write_ref(w, "item", *e.item);
        } else {
            w.key("item").null();
        }
        w.member("count", e.count);
        if (e.command != 0) {
            w.member("command", e.command);
        }
        if (e.extra != 0) {
            w.member("extra", e.extra);
        }
        w.end_object();
    }
    w.end_array();
    w.end_object();

    write_names(w, "flags", g.flags);
    write_ints(w, "flag_bits", g.flag_bits);
    write_names(w, "paper_maps", g.paper_maps);
    write_ints(w, "paper_map_bits", g.paper_map_bits);
    w.key("enemy_states").begin_array();
    for (const EnemyState& e : g.enemy_states) {
        w.inline_next().begin_object();
        write_ref(w, "map", e.map);
        w.member("alive_bits", e.alive_bits);
        w.end_object();
    }
    w.end_array();

    w.key("progress").begin_object();
    w.member("play_time", g.play_time);
    w.member("run_distance", g.run_distance);
    w.member("walk_distance", g.walk_distance);
    w.member("save_count", g.save_count);
    w.member("picked_up_items", g.picked_up_items);
    w.member("special_items", g.special_items);
    w.member("next_fear", g.next_fear);
    w.member("extra_hours", g.extra_hours);
    w.member("clear_count", g.clear_count);
    w.member("endings", g.endings);
    w.member("current_ending_flags", g.current_ending_flags);
    w.member("continue_count", g.continue_count);
    w.end_object();

    w.key("stats").begin_object();
    w.member("melee_kills", g.melee_kills);
    w.member("melee_kills_b", g.melee_kills_b);
    w.member("ranged_kills", g.ranged_kills);
    w.member("hit_distance_total", g.hit_distance_total);
    for (const CounterField& c : kCounters) {
        w.member(c.key, g.*c.field);
    }
    w.end_object();

    if (g.options) {
        write_options(w, *g.options);
    }
    w.end_object();
    return w.take();
}

Result<SaveGame> from_json(std::string_view text) {
    auto doc = json::parse(text);
    if (!doc) {
        return Result<SaveGame>::fail("STAT chunk: " + doc.error());
    }
    const json::Value& v = doc.value();
    if (!v.is_object()) {
        return Result<SaveGame>::fail("STAT chunk: not a JSON object");
    }
    const json::Value* format = v.find("format");
    if (format == nullptr || !format->is_string() || format->string() != kSaveGameFormat) {
        return Result<SaveGame>::fail(std::string("STAT chunk: not ") + kSaveGameFormat);
    }

    SaveGame g;
    Reader r;
    r.ref(v, "map", g.map);
    r.integer(v, "room", g.room);
    r.ref(v, "location", g.location);
    r.ref(v, "paper_map", g.paper_map);
    if (const json::Value* d = r.member(v, "difficulty")) {
        if (d->is_string()) {
            const std::string& s = d->string();
            if (s == "easy") {
                g.difficulty = -1;
            } else if (s == "normal") {
                g.difficulty = 0;
            } else if (s == "hard") {
                g.difficulty = 1;
            } else {
                r.fail("unknown difficulty " + s);
            }
        } else {
            r.integer(v, "difficulty", g.difficulty);
        }
    }

    if (const json::Value* p = r.object(v, "player")) {
        r.integer(*p, "health", g.health);
        r.integer(*p, "health_saturation", g.health_saturation);
        const json::Value* pos = r.member(*p, "position");
        if (pos != nullptr) {
            if (!pos->is_array() || pos->array().size() != 2 || !pos->array()[0].is_integer() ||
                !pos->array()[1].is_integer()) {
                r.fail("\"position\" is not [x, z]");
            } else {
                g.position_x = static_cast<std::int32_t>(pos->array()[0].integer());
                g.position_z = static_cast<std::int32_t>(pos->array()[1].integer());
            }
        }
        r.integer(*p, "rotation_y", g.rotation_y);
        r.integer(*p, "reset_byte", g.player_reset_byte, false);
    }

    if (const json::Value* inv = r.object(v, "inventory")) {
        r.integer(*inv, "slot_count", g.slot_count);
        r.ref(*inv, "equipped", g.equipped);
        r.integer(*inv, "item_toggle_flags", g.item_toggle_flags, false);
        r.integer(*inv, "item_flags", g.inventory_item_flags, false);
        const json::Value* items = r.member(*inv, "items");
        if (items != nullptr && !items->is_array()) {
            r.fail("\"items\" is not a list");
        } else if (items != nullptr) {
            for (const json::Value& e : items->array()) {
                if (!e.is_object()) {
                    r.fail("an inventory entry is not an object");
                    break;
                }
                InventoryEntry entry;
                r.integer(e, "slot", entry.slot);
                if (entry.slot < 0 || entry.slot >= layout::kItemSlots) {
                    r.fail("inventory slot out of range");
                    break;
                }
                const json::Value* item = r.member(e, "item");
                if (item != nullptr && !item->is_null()) {
                    Ref ref;
                    if (r.ref(item, ref, "item")) {
                        entry.item = ref;
                    }
                }
                r.integer(e, "count", entry.count, false);
                r.integer(e, "command", entry.command, false);
                r.integer(e, "extra", entry.extra, false);
                g.items.push_back(std::move(entry));
            }
            std::stable_sort(g.items.begin(), g.items.end(),
                             [](const InventoryEntry& a, const InventoryEntry& b) { return a.slot < b.slot; });
        }
    }

    r.names(v, "flags", g.flags);
    r.ints(v, "flag_bits", g.flag_bits);
    {
        const json::Value* naming = v.find("flag_names");
        g.old_flag_names = naming == nullptr || !naming->is_integer() || naming->integer() < kFlagNamesVersion;
    }
    r.names(v, "paper_maps", g.paper_maps);
    r.ints(v, "paper_map_bits", g.paper_map_bits);
    if (const json::Value* enemies = r.member(v, "enemy_states", false)) {
        if (!enemies->is_array()) {
            r.fail("\"enemy_states\" is not a list");
        } else {
            for (const json::Value& e : enemies->array()) {
                if (!e.is_object()) {
                    r.fail("an enemy state is not an object");
                    break;
                }
                EnemyState s;
                r.ref(e, "map", s.map);
                r.integer(e, "alive_bits", s.alive_bits);
                g.enemy_states.push_back(std::move(s));
            }
        }
    }

    if (const json::Value* p = r.object(v, "progress")) {
        r.integer(*p, "play_time", g.play_time);
        r.integer(*p, "run_distance", g.run_distance, false);
        r.integer(*p, "walk_distance", g.walk_distance, false);
        r.integer(*p, "save_count", g.save_count);
        r.integer(*p, "picked_up_items", g.picked_up_items, false);
        r.integer(*p, "special_items", g.special_items, false);
        r.boolean(*p, "next_fear", g.next_fear);
        r.integer(*p, "extra_hours", g.extra_hours, false);
        r.integer(*p, "clear_count", g.clear_count, false);
        r.integer(*p, "endings", g.endings, false);
        r.integer(*p, "current_ending_flags", g.current_ending_flags, false);
        r.integer(*p, "continue_count", g.continue_count, false);
        if (g.special_items > 31 || g.extra_hours > 3) {
            r.fail("\"special_items\" or \"extra_hours\" out of range");
        }
    }

    if (const json::Value* s = r.object(v, "stats", false)) {
        r.integer(*s, "melee_kills", g.melee_kills, false);
        r.integer(*s, "melee_kills_b", g.melee_kills_b, false);
        r.integer(*s, "ranged_kills", g.ranged_kills, false);
        r.integer(*s, "hit_distance_total", g.hit_distance_total, false);
        if (g.hit_distance_total >= (1u << 28)) {
            r.fail("\"hit_distance_total\" out of range");
        }
        for (const CounterField& c : kCounters) {
            r.integer(*s, c.key, g.*c.field, false);
        }
    }

    if (const json::Value* o = r.object(v, "options", false)) {
        GameOptions options;
        read_options(r, *o, options);
        g.options = options;
    }

    if (!r.error.empty()) {
        return Result<SaveGame>::fail("STAT chunk: " + r.error);
    }
    return Result<SaveGame>::ok(std::move(g));
}

SaveGame from_live_state(const game::SaveRecord& r, const NameTable& names) {
    SaveGame g;
    for (int slot = 0; slot < game::SaveRecord::kItemSlots; ++slot) {
        const game::SaveItem& item = r.items[slot];
        if (item == kBlankSlot) {
            continue;
        }
        InventoryEntry e;
        e.slot = slot;
        if (item.item != kEmptyItem) {
            e.item = ref_of(item.item, names.item_name);
        }
        e.count = item.count;
        e.command = item.command;
        e.extra = item.extra;
        g.items.push_back(std::move(e));
    }
    g.player_reset_byte = r.player_reset;
    g.map = ref_of(r.map, names.map_name);
    g.room = r.room;
    g.save_count = r.save_count;
    const int location = r.location;
    g.location = location >= 0 && location < static_cast<int>(std::size(kLocationIds))
                     ? Ref::of_name(kLocationIds[location])
                     : Ref::of_number(location);
    g.paper_map = ref_of(r.paper_map, names.paper_map_name);
    // 0 is "no weapon", not an item.
    g.equipped = r.equipped_weapon == 0 ? Ref::of_number(0) : ref_of(r.equipped_weapon, names.item_name);
    g.slot_count = r.slot_count;
    g.item_toggle_flags = r.item_toggles;
    for (int m = 0; m < game::SaveRecord::kEnemyStateMaps; ++m) {
        if (r.enemy_states[m] != -1) {
            g.enemy_states.push_back({ref_of(m, names.map_name), r.enemy_states[m]});
        }
    }
    bits_to_names({&r.paper_maps, 1}, names.paper_map_name, g.paper_maps, g.paper_map_bits);
    bits_to_names(r.event_flags, names.flag_name, g.flags, g.flag_bits);
    g.health_saturation = r.health_saturation;
    g.picked_up_items = r.picked_up_items;
    g.inventory_item_flags = r.inventory_flags;
    g.health = r.health;
    g.position_x = r.position_x;
    g.rotation_y = r.rotation_y;
    g.clear_count = r.clear_count;
    g.endings = r.endings;
    g.position_z = r.position_z;
    g.play_time = r.play_time;
    g.run_distance = static_cast<std::int32_t>(r.run_distance);
    g.walk_distance = static_cast<std::int32_t>(r.walk_distance);
    g.next_fear = (r.mode_bits & 1) != 0;
    g.extra_hours = static_cast<std::uint8_t>(r.mode_bits >> 1 & 3);
    g.special_items = static_cast<std::uint8_t>(r.mode_bits >> 3);
    g.melee_kills = r.melee_kills;
    g.melee_kills_b = r.melee_kills_b;
    g.ranged_kills = r.ranged_kills;
    g.hit_distance_total = r.hits_and_difficulty & 0x0FFFFFFF;
    g.difficulty = r.difficulty();
    for (std::size_t i = 0; i < std::size(kCounters); ++i) {
        g.*kCounters[i].field = r.counters[i];
    }
    g.current_ending_flags = r.current_ending;
    g.continue_count = r.continues;
    return g;
}

Result<game::SaveRecord> to_live_state(const SaveGame& g, const NameTable& names, Warnings& warnings) {
    using R = Result<game::SaveRecord>;
    game::SaveRecord r{};

    const int map = number_of(g.map, names.map_number);
    if (g.map.named() && map < 0) {
        return R::fail("the save is on map " + (g.map.named() ? g.map.name : std::to_string(g.map.number)) +
                       ", which this build does not have");
    }
    for (game::SaveItem& item : r.items) {
        item = kBlankSlot;
    }
    for (const InventoryEntry& e : g.items) {
        if (e.slot < 0 || e.slot >= game::SaveRecord::kItemSlots) {
            continue;
        }
        int item = kEmptyItem;
        if (e.item) {
            item = number_of(*e.item, names.item_number);
            if (item < 0) {
                warnings.push_back("inventory slot " + std::to_string(e.slot) + " holds " +
                                   (e.item->named() ? e.item->name : std::to_string(e.item->number)) +
                                   ", which no installed data has; the slot is left empty");
                continue;
            }
        }
        r.items[e.slot] = {item, e.count, e.command, e.extra};
    }
    r.player_reset = g.player_reset_byte;
    r.map = map;
    r.room = g.room;
    r.save_count = g.save_count;
    int location = g.location.named() ? location_number(g.location.name) : static_cast<int>(g.location.number);
    if (location < -128 || location > 127 || (g.location.named() && location < 0)) {
        warnings.push_back("unknown save point " + g.location.name + "; the save counts as made anywhere");
        location = 0;
    }
    r.location = static_cast<std::int8_t>(location);
    int paper_map = number_of(g.paper_map, names.paper_map_number);
    if (paper_map < 0) {
        warnings.push_back("unknown paper map " + g.paper_map.name + "; the map screen opens the first one");
        paper_map = 0;
    }
    r.paper_map = paper_map;
    int equipped = number_of(g.equipped, names.item_number);
    if (equipped < 0) {
        warnings.push_back("the equipped weapon " + g.equipped.name + " has no installed data; nothing is equipped");
        equipped = 0;
    }
    r.equipped_weapon = equipped;
    r.slot_count = static_cast<std::uint8_t>(g.slot_count);
    r.item_toggles = g.item_toggle_flags;
    for (std::int32_t& word : r.enemy_states) {
        word = -1;
    }
    for (const EnemyState& e : g.enemy_states) {
        const int m = number_of(e.map, names.map_number);
        if (m < 0 || m >= game::SaveRecord::kEnemyStateMaps) {
            warnings.push_back("enemy state of an unknown map " + e.map.name + " is dropped");
            continue;
        }
        r.enemy_states[m] = e.alive_bits;
    }
    names_to_bits({&r.paper_maps, 1}, g.paper_maps, g.paper_map_bits, names.paper_map_number, "paper map", warnings);
    names_to_bits(r.event_flags, g.old_flag_names ? current_flag_names(g.flags, names) : g.flags, g.flag_bits,
                  names.flag_number, "event flag", warnings);
    r.health_saturation = g.health_saturation;
    r.picked_up_items = g.picked_up_items;
    r.inventory_flags = g.inventory_item_flags;
    r.health = g.health;
    r.position_x = g.position_x;
    r.rotation_y = g.rotation_y;
    r.clear_count = g.clear_count;
    r.endings = g.endings;
    r.position_z = g.position_z;
    r.play_time = g.play_time;
    r.run_distance = static_cast<std::uint32_t>(g.run_distance);
    r.walk_distance = static_cast<std::uint32_t>(g.walk_distance);
    r.mode_bits = static_cast<std::uint8_t>((g.next_fear ? 1 : 0) | (g.extra_hours & 3) << 1 |
                                            (g.special_items & 31) << 3);
    r.melee_kills = g.melee_kills;
    r.melee_kills_b = g.melee_kills_b;
    r.ranged_kills = g.ranged_kills;
    r.hits_and_difficulty =
        (g.hit_distance_total & 0x0FFFFFFFu) | (static_cast<std::uint32_t>(g.difficulty) & 0xFu) << 28;
    for (std::size_t i = 0; i < std::size(kCounters); ++i) {
        r.counters[i] = g.*kCounters[i].field;
    }
    r.current_ending = g.current_ending_flags;
    r.continues = g.continue_count;
    return R::ok(r);
}

GameOptions from_live_options(const game::Settings& s) {
    GameOptions o;
    for (const BindField& b : kBinds) {
        o.*b.field = s.binds.*b.live;
    }
    for (const ByteField& f : kByteOptions) {
        if (f.is_signed) {
            o.*f.s = s.*f.live_s;
        } else {
            o.*f.u = s.*f.live_u;
        }
    }
    o.seen_game_over_tips = s.seen_game_over_tips;
    o.pal_language = s.pal_language;
    return o;
}

game::Settings to_live_options(const GameOptions& o) {
    game::Settings s{};
    for (const BindField& b : kBinds) {
        s.binds.*b.live = o.*b.field;
    }
    for (const ByteField& f : kByteOptions) {
        if (f.is_signed) {
            s.*f.live_s = o.*f.s;
        } else {
            s.*f.live_u = o.*f.u;
        }
    }
    s.seen_game_over_tips = o.seen_game_over_tips;
    s.pal_language = o.pal_language;
    return s;
}

namespace {

// The NAME chunk of format 1 ("sh-save-names/1"): inventory slots, the equipped weapon and the set event flags by
// name. Names win over the record's numbers; a name this build does not know keeps the number (inventory) or is
// dropped (flags), as the format 1 reader did.
void apply_legacy_names(std::string_view text, SaveGame& g, const NameTable& names, Warnings& warnings) {
    auto doc = json::parse(text);
    if (!doc || !doc.value().is_object()) {
        warnings.push_back("its NAME chunk does not parse; the numbers of the GAME chunk are used");
        return;
    }
    const json::Value& v = doc.value();
    const json::Value* format = v.find("format");
    if (format == nullptr || !format->is_string() || format->string() != "sh-save-names/1") {
        warnings.push_back("its NAME chunk is not sh-save-names/1; the numbers of the GAME chunk are used");
        return;
    }
    auto known = [&](const std::string& name, const std::function<int(const char*)>& number_from) {
        return number_from && number_from(name.c_str()) >= 0;
    };
    if (const json::Value* inventory = v.find("inventory"); inventory != nullptr && inventory->is_array()) {
        for (const json::Value& entry : inventory->array()) {
            const json::Value* slot = entry.find("slot");
            const json::Value* item = entry.find("item");
            if (slot == nullptr || !slot->is_integer() || item == nullptr || !item->is_string()) {
                continue;
            }
            auto it = std::find_if(g.items.begin(), g.items.end(),
                                   [&](const InventoryEntry& e) { return e.slot == slot->integer(); });
            if (it == g.items.end() || !it->item) {
                continue;
            }
            if (known(item->string(), names.item_number)) {
                it->item = Ref::of_name(item->string());
            } else {
                warnings.push_back("the save holds " + item->string() + ", which no installed data has; it keeps its "
                                   "number");
            }
        }
    }
    if (const json::Value* equipped = v.find("equipped"); equipped != nullptr && equipped->is_string()) {
        if (known(equipped->string(), names.item_number)) {
            g.equipped = Ref::of_name(equipped->string());
        }
    }
    if (const json::Value* flags = v.find("flags"); flags != nullptr && flags->is_array()) {
        // Every bit with a name in this build is what the list says; bits without a name keep the record's.
        // The NAME chunk predates kFlagNamesVersion: its names are read as they were meant then.
        std::vector<std::string> listed;
        for (const json::Value& f : flags->array()) {
            if (f.is_string()) {
                const std::string name = current_flag_names({f.string()}, names).front();
                if (known(name, names.flag_number)) {
                    listed.push_back(name);
                } else {
                    warnings.push_back("the save has event flag " + f.string() +
                                       ", which no installed data has; it is dropped");
                }
            }
        }
        g.flags = std::move(listed);
    }
}

}  // namespace

Result<SaveGame> from_legacy(std::span<const std::uint8_t> game_record, std::string_view names_chunk,
                             std::span<const std::uint8_t> options_record, const NameTable& names, Warnings& warnings) {
    if (game_record.size() < layout::kStateRecord) {
        return Result<SaveGame>::fail("GAME chunk shorter than the game's record");
    }
    if (!verify(game_record.first(layout::kStateRecord))) {
        return Result<SaveGame>::fail("GAME chunk fails the game's checksum");
    }
    SaveGame g = from_live_state(console::decode_save_record(game_record.first(layout::kStateSize)), names);
    if (!names_chunk.empty()) {
        apply_legacy_names(names_chunk, g, names, warnings);
        // The listed names were in bit order when written; keep that order for a stable text.
        if (names.flag_number) {
            std::stable_sort(g.flags.begin(), g.flags.end(), [&](const std::string& a, const std::string& b) {
                return names.flag_number(a.c_str()) < names.flag_number(b.c_str());
            });
        }
    }
    if (options_record.size() >= layout::kOptionsRecord && verify(options_record.first(layout::kOptionsRecord))) {
        g.options = from_live_options(console::decode_settings(options_record.first(layout::kOptionsSize)));
    }
    return Result<SaveGame>::ok(std::move(g));
}

Result<std::vector<std::uint8_t>> to_legacy_record(const SaveGame& game, const NameTable& names, Warnings& warnings) {
    auto state = to_live_state(game, names, warnings);
    if (!state) {
        return Result<std::vector<std::uint8_t>>::fail(state.error());
    }
    std::vector<std::uint8_t> record = console::encode_save_record(state.value());
    record.resize(layout::kStateRecord, 0);
    seal(record);
    return Result<std::vector<std::uint8_t>>::ok(std::move(record));
}

}  // namespace sh::save
