// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/chara/behavior_host.h"

#include "engine/chara/chara_library.h"
#include "engine/game_state/game_ids.h"
#include "engine/script/sh_script.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <vector>

namespace sh::chara::behavior {
namespace {

using NamedCall = int (*)(const char*, int, const ShScriptArg*, long long*, int, char*, int);
using NamedList = const char* const* (*)(void);

constexpr std::string_view kPrefix = "entity.";
// The host's own calls; every other name belongs to an extension.
constexpr std::array<const char*, 2> kOwnNames = {"entity.get", "entity.set"};

struct HostState {
    Extension* extensions = nullptr;
    Ports ports;
    NamedCall next_call = nullptr;
    NamedList next_list = nullptr;
    int variant = 0;
    // Behaviour names by type, valid for `names_of`.
    const CharaLibrary* names_of = nullptr;
    std::array<std::string, sh::game_state::chara::kMaxTypes> names;
    std::array<bool, sh::game_state::chara::kMaxTypes> named{};
};

// Extensions register from static initialisers of other translation units, so the state is built on first use.
HostState& host() {
    static HostState s;
    return s;
}

bool valid_type(int chara_id) {
    return chara_id > sh::game_state::chara::kNone && chara_id < sh::game_state::chara::kMaxTypes;
}

const std::string& cached_name(int chara_id) {
    static const std::string empty;
    HostState& h = host();
    const CharaLibrary* lib = h.ports.library != nullptr ? h.ports.library() : nullptr;
    if (!valid_type(chara_id) || lib == nullptr) {
        return empty;
    }
    if (lib != h.names_of) {
        h.names_of = lib;
        h.named.fill(false);
    }
    const auto i = static_cast<std::size_t>(chara_id);
    if (!h.named[i]) {
        h.names[i] = behaviour_name(*lib, chara_id);
        h.named[i] = true;
    }
    return h.names[i];
}

int get_or_set(bool get, const Args& a, long long* out, char* err, int errCap) {
    const Ports& p = host().ports;
    const int slot = static_cast<int>(a.integer(0, -1));
    std::uint8_t* record = p.record != nullptr ? p.record(slot) : nullptr;
    if (record == nullptr) {
        return fail(err, errCap, "bad character slot");
    }
    const char* name = a.text(1);
    if (name == nullptr) {
        return fail(err, errCap, "unknown character field '%s'", "?");
    }
    if (const Field* f = find_field(name)) {
        if (get) {
            out[0] = field_read(f->kind, record + f->offset);
            return 1;
        }
        field_write(f->kind, record + f->offset, a.integer(2));
        if (p.commit != nullptr) {
            p.commit(slot);
        }
        return 0;
    }
    // A behaviour variable; a map event may set a boss's variables before the slot becomes the boss, so any
    // type's layout answers.
    if (get) {
        if (p.var_get != nullptr) {
            if (const std::optional<long long> v = p.var_get(slot, name)) {
                out[0] = *v;
                return 1;
            }
        }
    } else if (p.var_set != nullptr && p.var_set(slot, name, a.integer(2))) {
        return 0;
    }
    return fail(err, errCap, "unknown character field '%s'", name);
}

int named_call(const char* name, int argc, const ShScriptArg* argv, long long* out, int outCap, char* err,
               int errCap) {
    HostState& h = host();
    if (name != nullptr && std::strncmp(name, kPrefix.data(), kPrefix.size()) == 0 && outCap >= 8) {
        const Args args{argc, argv};
        const std::string_view fn(name + kPrefix.size());
        if (fn == "get" || fn == "set") {
            return get_or_set(fn == "get", args, out, err, errCap);
        }
        for (const Extension* e = h.extensions; e != nullptr; e = e->next) {
            for (std::size_t i = 0; i < e->name_count; ++i) {
                if (std::strcmp(e->names[i], name) == 0) {
                    const int n = e->call(fn, args, out, err, errCap);
                    if (n != kNotMine) {
                        return n;
                    }
                }
            }
        }
    }
    return h.next_call != nullptr ? h.next_call(name, argc, argv, out, outCap, err, errCap)
                                  : fail(err, errCap, "not available");
}

const char* const* named_list() {
    static std::vector<const char*> names;
    if (names.empty()) {
        HostState& h = host();
        if (h.next_list != nullptr) {
            for (const char* const* n = h.next_list(); n != nullptr && *n != nullptr; ++n) {
                names.push_back(*n);
            }
        }
        names.insert(names.end(), kOwnNames.begin(), kOwnNames.end());
        for (const Extension* e = h.extensions; e != nullptr; e = e->next) {
            names.insert(names.end(), e->names, e->names + e->name_count);
        }
        names.push_back(nullptr);
    }
    return names.data();
}

}  // namespace

long long Args::integer(int i, long long fallback) const {
    return i < argc && (argv[i].kind == 1 || argv[i].kind == 3) ? argv[i].i : fallback;
}

const char* Args::text(int i) const {
    return i < argc && argv[i].kind == 2 ? argv[i].s : nullptr;
}

void register_extension(Extension* ext) {
    Extension** tail = &host().extensions;
    while (*tail != nullptr) {
        tail = &(*tail)->next;
    }
    ext->next = nullptr;
    *tail = ext;
}

void register_base_extension(Extension* ext) {
    ext->next = host().extensions;
    host().extensions = ext;
}

void install_ports(const Ports& ports) {
    host().ports = ports;
}

void bind_script_api(ShScriptGameApi* api) {
    HostState& h = host();
    h.next_call = api->named_call;
    h.next_list = api->named_list;
    api->named_call = named_call;
    api->named_list = named_list;
}

bool clip_table(int chara_id, int index, ClipTable* out) {
    int seen = 0;
    if (const auto from_data = host().ports.clip_table) {
        for (int i = 0; from_data(chara_id, i, out); ++i) {
            if (seen++ == index) {
                return true;
            }
        }
    }
    for (const Extension* e = host().extensions; e != nullptr; e = e->next) {
        for (std::size_t i = 0; i < e->clip_count; ++i) {
            if (e->clips[i].chara_id == chara_id && seen++ == index) {
                *out = e->clips[i];
                return true;
            }
        }
    }
    return false;
}

bool active_clip_table(int chara_id, const void* active, ClipTable* out) {
    ClipTable t;
    for (int i = 0; clip_table(chara_id, i, &t); ++i) {
        if (t.table == active) {
            *out = t;
            return true;
        }
    }
    return clip_table(chara_id, 0, out);
}

bool has_clips(int chara_id) {
    ClipTable t;
    return valid_type(chara_id) && clip_table(chara_id, 0, &t);
}

const Field* find_field(std::string_view name) {
    for (const Extension* e = host().extensions; e != nullptr; e = e->next) {
        for (std::size_t i = 0; i < e->field_count; ++i) {
            if (name == e->fields[i].name) {
                return &e->fields[i];
            }
        }
    }
    return nullptr;
}

int fail(char* err, int cap, const char* msg, const char* what) {
    std::snprintf(err, static_cast<std::size_t>(cap), msg, what);
    return -1;
}

std::string behaviour_name(const CharaLibrary& library, int chara_id) {
    if (!valid_type(chara_id)) {
        return {};
    }
    const CharacterDef* def = library.find(chara_id);
    if (def == nullptr) {
        return {};
    }
    return def->behavior.empty() ? def->folder : def->behavior;
}

bool behaviour_exists(int chara_id) {
    return ShScript_CharaBehaviorExists(cached_name(chara_id).c_str()) != 0;
}

void run_behaviour(int slot, int chara_id, int variant) {
    HostState& h = host();
    if (chara_id < 0) {
        chara_id = h.ports.chara_id != nullptr ? h.ports.chara_id(slot) : -1;
    }
    const std::string& name = cached_name(chara_id);
    if (name.empty()) {
        return;
    }
    if (variant < 0) {
        ShScript_CharaBehaviorUpdate(chara_id, name.c_str(), slot);
        return;
    }
    h.variant = variant;
    ShScript_CharaBehaviorUpdate(chara_id, name.c_str(), slot);
    h.variant = 0;
}

void run_fallback(int slot) {
    HostState& h = host();
    const int chara_id = h.ports.chara_id != nullptr ? h.ports.chara_id(slot) : -1;
    const CharaLibrary* lib = h.ports.library != nullptr ? h.ports.library() : nullptr;
    const CharacterDef* def = lib != nullptr ? lib->find(chara_id) : nullptr;
    ShScript_CharaFallbackUpdate(chara_id, def != nullptr ? def->folder.c_str() : "", slot);
}

int update_variant() {
    return host().variant;
}

SlotUpdate choose_update(int chara_id, bool map_has_update, bool engine_spawned, SpawnRun run) {
    if (!valid_type(chara_id)) {
        return SlotUpdate::None;
    }
    if (!engine_spawned) {
        return map_has_update ? SlotUpdate::Map : SlotUpdate::None;
    }
    if (run == SpawnRun::None) {
        return SlotUpdate::None;
    }
    if (run == SpawnRun::Fallback) {
        return has_clips(chara_id) ? SlotUpdate::Fallback : SlotUpdate::None;
    }
    if (map_has_update) {
        return SlotUpdate::Map;
    }
    const std::string& name = cached_name(chara_id);
    if (!name.empty() && ShScript_CharaBehaviorHas(chara_id, name.c_str()) != 0) {
        return SlotUpdate::Behaviour;
    }
    return run == SpawnRun::Own && has_clips(chara_id) ? SlotUpdate::Fallback : SlotUpdate::None;
}

}  // namespace sh::chara::behavior
