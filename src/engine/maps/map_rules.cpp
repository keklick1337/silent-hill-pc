// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/maps/map_rules.h"

#include "formats/common/json.h"

namespace sh::maps {

namespace {

using sh::json::Value;

void note(std::string* error, const std::string& what) {
    if (error != nullptr) {
        *error += (error->empty() ? "" : "; ") + what;
    }
}

bool flag(const Value& doc, const char* key, std::string* error) {
    const Value* v = doc.find(key);
    if (v == nullptr) {
        return false;
    }
    if (!v->is_bool()) {
        note(error, std::string("\"") + key + "\" must be true or false");
        return false;
    }
    return v->boolean();
}

}  // namespace

MapRules parse_map_rules(std::string_view overlay_json, std::string* error) {
    MapRules rules;
    const auto parsed = sh::json::parse(overlay_json);
    if (!parsed || !parsed.value().is_object()) {
        note(error, parsed ? "overlay.json is not an object" : parsed.error());
        return rules;
    }
    const Value& doc = parsed.value();
    if (const Value* list = doc.find("player_updates")) {
        if (list->is_array()) {
            for (const Value& fn : list->array()) {
                if (fn.is_string()) {
                    rules.player_updates.push_back(fn.string());
                } else {
                    note(error, "\"player_updates\" lists function names");
                }
            }
        } else {
            note(error, "\"player_updates\" is a list of function names");
        }
    }
    rules.pin_reactions = flag(doc, "pin_reactions", error);
    rules.floor_reactions = flag(doc, "floor_reactions", error);
    rules.floor_push = flag(doc, "floor_push", error);
    if (const Value* files = doc.find("harry_anim_files")) {
        if (files->is_array()) {
            for (const Value& f : files->array()) {
                if (f.is_string()) {
                    rules.harry_anim_files.push_back(f.string());
                } else {
                    note(error, "\"harry_anim_files\" lists game file paths");
                }
            }
        } else {
            note(error, "\"harry_anim_files\" is a list of game file paths");
        }
    }
    if (const Value* rooms = doc.find("level_floor_rooms")) {
        if (rooms->is_array()) {
            for (const Value& room : rooms->array()) {
                if (room.is_integer()) {
                    rules.level_floor_rooms.push_back(static_cast<int>(room.integer()));
                } else {
                    note(error, "\"level_floor_rooms\" lists room numbers");
                }
            }
        } else {
            note(error, "\"level_floor_rooms\" is a list of room numbers");
        }
    }
    if (const Value* drop = doc.find("void_drop")) {
        if (drop->is_object()) {
            rules.void_drop.no_ground = flag(*drop, "no_ground", error);
            rules.void_drop.open_ground = flag(*drop, "open_ground", error);
            if (const Value* max_z = drop->find("no_ground_max_z")) {
                if (max_z->is_number()) {
                    rules.void_drop.no_ground_bounded = true;
                    // Q12 the way the original's literal is: float multiply, truncated.
                    rules.void_drop.no_ground_max_z =
                        static_cast<std::int32_t>(static_cast<float>(max_z->number()) * 4096.0f);
                } else {
                    note(error, "\"void_drop.no_ground_max_z\" is a number of metres");
                }
            }
        } else {
            note(error, "\"void_drop\" is an object");
        }
    }
    return rules;
}

}  // namespace sh::maps
