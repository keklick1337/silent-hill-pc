#include "engine/maps/map_description.h"

#include "engine/log/sh_log.h"
#include "formats/common/json.h"

#include <string>

namespace sh::maps {

namespace md = sh::convert::mapdata;

namespace {

// A list of character names; false (logged) for anything else.
template <typename Set>
bool chara_list(const sh::json::Value& doc, const char* key, const std::string& relative, Set set) {
    const sh::json::Value* list = doc.find(key);
    if (list == nullptr) {
        return true;
    }
    if (!list->is_array()) {
        SH_ERR("[MAPOVERLAY] %s: \"%s\" is a list of character names", relative.c_str(), key);
        return false;
    }
    for (const sh::json::Value& c : list->array()) {
        const int id = c.is_string() ? md::chara_number_of(c.string().c_str()) : -1;
        if (id < 0) {
            SH_ERR("[MAPOVERLAY] %s: unknown character in \"%s\"", relative.c_str(), key);
            return false;
        }
        set(id);
    }
    return true;
}

}  // namespace

bool apply_overlay_description(MapRuntime& runtime, std::string_view relative_view, std::string_view text) {
    const std::string relative(relative_view);
    auto parsed = sh::json::parse(text);
    if (!parsed) {
        SH_ERR("[MAPOVERLAY] %s: %s", relative.c_str(), parsed.error().c_str());
        return false;
    }
    const sh::json::Value& doc = parsed.value();
    const sh::json::Value* type = doc.find("map_type");
    const int type_index = type != nullptr && type->is_string() ? MapRuntime::map_type_of(type->string()) : -1;
    if (type_index < 0) {
        SH_ERR("[MAPOVERLAY] %s: \"map_type\" is one of THR, SC, SU, SPR, SPU, RSR, RSU, APR, APU, ER, ER2, DR, DRU, "
               "HP, HU, XXX",
               relative.c_str());
        return false;
    }
    runtime.set_map_type(type_index);

    if (!chara_list(doc, "characters", relative,
                    [&](int id) { runtime.set_chara_update(id, CharaUpdate::Behaviour); }) ||
        !chara_list(doc, "character_effects", relative, [&](int id) { runtime.set_chara_effects(id, true); })) {
        return false;
    }

    if (const sh::json::Value* effects = doc.find("effect_draws")) {
        if (!effects->is_array()) {
            SH_ERR("[MAPOVERLAY] %s: \"effect_draws\" is a list of effect names", relative.c_str());
            return false;
        }
        for (const sh::json::Value& e : effects->array()) {
            const std::string name = e.is_string() ? e.string() : std::string();
            const int effect = MapRuntime::map_effect_of(name);
            if (effect < 0) {
                SH_ERR("[MAPOVERLAY] %s: unknown effect \"%s\" in \"effect_draws\" (glass_shards, acid, light_glow, "
                       "script, particle_emitter, split_head, smoke_column, water, drips, lake_surface)",
                       relative.c_str(), name.c_str());
                return false;
            }
            runtime.set_draws(static_cast<MapEffect>(effect));
        }
    }
    return true;
}

bool apply_event_handlers(MapRuntime& runtime, std::string_view relative_view, std::string_view text) {
    const std::string relative(relative_view);
    auto parsed = sh::json::parse(text);
    if (!parsed) {
        SH_ERR("[MAPOVERLAY] %s: %s", relative.c_str(), parsed.error().c_str());
        return false;
    }
    const sh::json::Value* list = parsed.value().find("handlers");
    if (list == nullptr || !list->is_array()) {
        SH_ERR("[MAPOVERLAY] %s: needs \"handlers\"", relative.c_str());
        return false;
    }
    for (const sh::json::Value& h : list->array()) {
        const sh::json::Value* index = h.find("index");
        const sh::json::Value* shape = h.find("shape");
        if (index == nullptr || !index->is_integer() || index->integer() < 0 || index->integer() >= kEventHandlers) {
            SH_ERR("[MAPOVERLAY] %s: a handler needs an \"index\" 0..%d", relative.c_str(), kEventHandlers - 1);
            return false;
        }
        runtime.set_event_handler(static_cast<int>(index->integer()), shape != nullptr && !shape->is_null());
    }
    return true;
}

}  // namespace sh::maps
