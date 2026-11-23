// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/scripting/map_vars.h"
#include "engine/loader/active_map.h"

#include <cstdio>
#include <map>
#include <optional>
#include <vector>

#include "engine/maps/map_runtime.h"
#include "engine/runtime/world_object_runtime.h"
#include "engine/scripting/script_game_link.h"
#include "engine/vfs/vfs_data_json.h"
#include "formats/common/json.h"
#include "engine/mods/data_reload.h"

extern "C" {
const char* PcPort_GetGameDataPath(void);
}

namespace sh::scripting {

namespace {

enum class Source : std::uint8_t { Symbol, Object, Storage };

enum class Layout : std::uint8_t { None, Vec3, Svec3, Pose, Placement };

// One addressable name. Pose and placement offsets depend on the build's record layout: they are taken at access.
struct VarDef {
    Source source = Source::Storage;
    std::string where;    // symbol, first object or storage key
    std::string storage;  // the block a symbol falls back to ("" none)
    MapVarType type = MapVarType::S32;
    std::size_t offset = 0;
    std::size_t stride = 0;
    int count = 1;
    Layout layout = Layout::None;
    enum class Part : std::uint8_t { Whole, Position, Rotation } part = Part::Whole;
    int component = -1;  // 0..2 for .x/.y/.z
};

using VarSet = std::map<std::string, VarDef, std::less<>>;

bool type_of(std::string_view s, MapVarType* t) {
    static constexpr struct {
        std::string_view name;
        MapVarType type;
    } kTypes[] = {{"s8", MapVarType::S8},   {"u8", MapVarType::U8},   {"s16", MapVarType::S16}, {"u16", MapVarType::U16},
                  {"s32", MapVarType::S32}, {"u32", MapVarType::U32}, {"ref", MapVarType::Ref}};
    for (const auto& k : kTypes) {
        if (s == k.name) {
            *t = k.type;
            return true;
        }
    }
    return false;
}

bool layout_of(std::string_view s, Layout* l) {
    static constexpr struct {
        std::string_view name;
        Layout layout;
    } kLayouts[] = {{"vec3", Layout::Vec3}, {"svec3", Layout::Svec3}, {"pose", Layout::Pose},
                    {"placement", Layout::Placement}};
    for (const auto& k : kLayouts) {
        if (s == k.name) {
            *l = k.layout;
            return true;
        }
    }
    return false;
}

const std::string* text_member(const sh::json::Value& v, const char* key) {
    const sh::json::Value* m = v.find(key);
    return m != nullptr && m->type() == sh::json::Value::Type::String ? &m->string() : nullptr;
}

std::optional<long long> int_member(const sh::json::Value& v, const char* key) {
    const sh::json::Value* m = v.find(key);
    if (m == nullptr || !m->is_integer()) {
        return std::nullopt;
    }
    return static_cast<long long>(m->integer());
}

// The names one declaration makes.
void expand(const std::string& name, const VarDef& base, VarSet& out) {
    auto put = [&](const std::string& n, MapVarType type, std::size_t offset, std::size_t stride, VarDef::Part part,
                   int component) {
        VarDef d = base;
        d.type = type;
        d.offset = offset;
        d.stride = stride;
        d.part = part;
        d.component = component;
        out[n] = d;
    };
    static const char* const kAxes[] = {".x", ".y", ".z"};
    switch (base.layout) {
        case Layout::None:
            out[name] = base;
            break;
        case Layout::Vec3:
            put(name, MapVarType::Ref, 0, 12, VarDef::Part::Whole, -1);
            for (int k = 0; k < 3; ++k) {
                put(name + kAxes[k], MapVarType::S32, static_cast<std::size_t>(k) * 4, 12, VarDef::Part::Whole, -1);
            }
            break;
        case Layout::Svec3:
            put(name, MapVarType::Ref, 0, 6, VarDef::Part::Whole, -1);
            for (int k = 0; k < 3; ++k) {
                put(name + kAxes[k], MapVarType::S16, static_cast<std::size_t>(k) * 2, 6, VarDef::Part::Whole, -1);
            }
            break;
        case Layout::Pose:
        case Layout::Placement:
            put(name, MapVarType::Ref, 0, 0, VarDef::Part::Whole, -1);
            put(name + ".pos", MapVarType::Ref, 0, 0, VarDef::Part::Position, -1);
            for (int k = 0; k < 3; ++k) {
                put(name + ".pos" + kAxes[k], MapVarType::S32, 0, 0, VarDef::Part::Position, k);
            }
            if (base.layout == Layout::Pose) {
                put(name + ".rot", MapVarType::Ref, 0, 0, VarDef::Part::Rotation, -1);
                for (int k = 0; k < 3; ++k) {
                    put(name + ".rot" + kAxes[k], MapVarType::S16, 0, 0, VarDef::Part::Rotation, k);
                }
            }
            break;
    }
}

void read_file(const std::string& path, VarSet& out) {
    // Base and every active mod's patch, in load order.
    const std::optional<std::string> text = ShVfs_ReadDataJsonText(path.c_str(), PcPort_GetGameDataPath());
    if (!text) {
        return;
    }
    auto doc = sh::json::parse(*text);
    if (!doc) {
        std::fprintf(stderr, "[SCRIPT] %s: %s\n", path.c_str(), doc.error().c_str());
        return;
    }
    const sh::json::Value* vars = doc.value().find("vars");
    if (vars == nullptr || vars->type() != sh::json::Value::Type::Object) {
        std::fprintf(stderr, "[SCRIPT] %s: no \"vars\" object\n", path.c_str());
        return;
    }
    for (const auto& [name, v] : vars->members()) {
        VarDef d;
        const std::string* symbol = text_member(v, "symbol");
        const std::string* object = text_member(v, "object");
        const std::string* storage = text_member(v, "storage");
        const std::string* type = text_member(v, "type");
        const std::string* layout = text_member(v, "layout");
        bool ok = (symbol != nullptr) + (object != nullptr) + (storage != nullptr && symbol == nullptr) == 1;
        if (symbol != nullptr) {
            d.source = Source::Symbol;
            d.where = *symbol;
            d.storage = storage != nullptr ? *storage : std::string();
        } else if (object != nullptr) {
            d.source = Source::Object;
            d.where = *object;
        } else if (storage != nullptr) {
            d.where = *storage;
        }
        ok = ok && (type == nullptr || type_of(*type, &d.type)) && (layout == nullptr || layout_of(*layout, &d.layout));
        d.offset = static_cast<std::size_t>(int_member(v, "offset").value_or(0));
        d.count = static_cast<int>(int_member(v, "count").value_or(1));
        d.stride = static_cast<std::size_t>(int_member(v, "stride").value_or(map_var_type_size(d.type)));
        ok = ok && d.count >= 1 && (d.layout == Layout::None || (type == nullptr && d.offset == 0));
        if (!ok) {
            std::fprintf(stderr, "[SCRIPT] %s: variable '%s' needs one of symbol/object/storage and a known type or "
                                 "layout\n", path.c_str(), name.c_str());
            continue;
        }
        expand(name, d, out);
    }
}

struct Tables {
    std::string map;
    bool read = false;
    VarSet own;     // maps/<map>/map_vars.json
    VarSet shared;  // config/map_vars.json
};

const Tables& tables(std::string_view map) {
    static Tables cache;
    static std::uint32_t epoch = sh::mods::kNeverRead;
    if (!sh::mods::data_stale(epoch) && cache.read && cache.map == map) {
        return cache;
    }
    cache = Tables{};
    cache.read = true;
    cache.map = std::string(map);
    if (!map.empty()) {
        read_file("maps/" + cache.map + "/map_vars.json", cache.own);
    }
    read_file("config/map_vars.json", cache.shared);
    return cache;
}

}  // namespace

MapVarLookup map_variable(std::string_view map, std::string_view name, long long index, MapVarAddress* out,
                          std::string* why) {
    const Tables& t = tables(map);
    auto it = t.own.find(name);
    const VarDef* d = it != t.own.end() ? &it->second : nullptr;
    if (d == nullptr) {
        it = t.shared.find(name);
        d = it != t.shared.end() ? &it->second : nullptr;
    }
    if (d == nullptr) {
        return MapVarLookup::Unknown;
    }
    out->type = d->type;
    const std::string label(name);
    if (index < 0 || index >= d->count) {
        *why = "map variable '" + label + "': index out of range";
        return MapVarLookup::Error;
    }

    std::size_t offset = d->offset;
    std::size_t stride = d->stride;
    if (d->layout == Layout::Pose || d->layout == Layout::Placement) {
        const link::ObjectRecordLayout l = link::object_record_layout();
        stride = d->layout == Layout::Pose || d->source == Source::Object ? l.pose_bytes : l.placement_bytes;
        offset = d->part == VarDef::Part::Position   ? l.position
                 : d->part == VarDef::Part::Rotation ? l.rotation
                                                     : 0;
        if (d->component >= 0) {
            offset += static_cast<std::size_t>(d->component) * (d->part == VarDef::Part::Rotation ? 2 : 4);
        }
    }

    char* base = nullptr;
    switch (d->source) {
        case Source::Object: {
            const sh::Result<void*> bound = sh::world::world_object_runtime().binding(d->where, d->count);
            base = bound ? static_cast<char*>(bound.value()) : nullptr;
            if (base == nullptr) {
                *why = "map variable '" + label + "': " + (bound ? std::string("no record") : bound.error());
                return MapVarLookup::Error;
            }
            break;
        }
        case Source::Symbol:
            base = static_cast<char*>(sh::loader::active_map_symbol(d->where.c_str()));
            if (base != nullptr || d->storage.empty()) {
                break;
            }
            [[fallthrough]];
        case Source::Storage: {
            const std::string& key = d->source == Source::Storage ? d->where : d->storage;
            const std::size_t width = stride != 0 ? stride : static_cast<std::size_t>(map_var_type_size(d->type));
            base = static_cast<char*>(sh::maps::map_variables().block(key, offset + width * static_cast<std::size_t>(d->count)));
            break;
        }
    }
    if (base == nullptr) {
        *why = "map variable '" + label + "' is not defined on this map";
        return MapVarLookup::Error;
    }
    out->address = base + offset + static_cast<std::size_t>(index) * stride;
    return MapVarLookup::Found;
}

}  // namespace sh::scripting
