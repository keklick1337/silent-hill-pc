#include "engine/world/world_objects.h"

#include "formats/common/json.h"

#include <cstdlib>
#include <fstream>
#include <iterator>

namespace sh::world {

namespace {

using json::Value;

std::string where(const Value& v) {
    return "line " + std::to_string(v.line()) + ", column " + std::to_string(v.column());
}

Result<void> fail_at(const Value& v, const std::string& what) {
    return Result<void>::fail("world_objects.json: " + where(v) + ": " + what);
}

Result<void> read_vector3(const Value& v, const char* key, bool angles, WorldObjectDef& out) {
    if (!v.is_array() || v.array().size() != 3) {
        return fail_at(v, std::string("\"") + key + "\" must be an array of three numbers");
    }
    for (std::size_t i = 0; i < 3; ++i) {
        const Value& c = v.array()[i];
        if (!c.is_number()) {
            return fail_at(c, std::string("\"") + key + "\" must hold numbers");
        }
        if (angles) {
            out.rotation[i] = world_degrees_to_angle(c.number());
        } else {
            out.position[i] = world_metres_to_q12(c.number());
        }
    }
    return Result<void>::ok();
}

// An axis is one cell number or an array of them.
Result<void> read_cells(const Value& v, std::vector<int>& out) {
    if (v.is_integer()) {
        out.push_back(static_cast<int>(v.integer()));
        return Result<void>::ok();
    }
    if (!v.is_array() || v.array().empty()) {
        return fail_at(v, "a cell axis must be an integer or a non-empty array of integers");
    }
    for (const Value& c : v.array()) {
        if (!c.is_integer()) {
            return fail_at(c, "cells are integers");
        }
        out.push_back(static_cast<int>(c.integer()));
    }
    return Result<void>::ok();
}

Result<void> read_names(const Value& v, const char* key, std::vector<std::string>& out) {
    if (!v.is_array()) {
        return fail_at(v, std::string("\"") + key + "\" must be an array of flag names");
    }
    for (const Value& n : v.array()) {
        if (!n.is_string() || n.string().empty()) {
            return fail_at(n, std::string("\"") + key + "\" holds flag names (strings)");
        }
        out.push_back(n.string());
    }
    return Result<void>::ok();
}

Result<void> read_pose(const Value& v, WorldObjectDef& out) {
    if (!v.is_object()) {
        return fail_at(v, "\"pose\" must be an object with \"disc_table\" and \"index\"");
    }
    bool has_table = false;
    bool has_index = false;
    for (const Value::Member& m : v.members()) {
        const std::string& key = m.first;
        const Value& e = m.second;
        if (key == "disc_table") {
            if (!e.is_string() || e.string().empty()) {
                return fail_at(e, "\"disc_table\" must be a non-empty string");
            }
            out.pose->disc_table = e.string();
            has_table = true;
        } else if (key == "index") {
            if (!e.is_integer() || e.integer() < 0) {
                return fail_at(e, "\"index\" must be an integer >= 0");
            }
            out.pose->index = static_cast<int>(e.integer());
            has_index = true;
        } else if (key == "disc_address") {
            // Hex string ("0x800DE12C"): where row 0 lies on the disc when the map's table registry does not
            // name the table.
            if (!e.is_string()) {
                return fail_at(e, "\"disc_address\" must be a hex string");
            }
            char* end = nullptr;
            const unsigned long long a = std::strtoull(e.string().c_str(), &end, 16);
            if (e.string().empty() || end == nullptr || *end != '\0' || a == 0 || a > 0xFFFFFFFFull) {
                return fail_at(e, "\"disc_address\" must be a non-zero 32-bit hex number");
            }
            out.pose->disc_address = static_cast<std::uint32_t>(a);
        } else if (key.rfind('_', 0) != 0) {
            return fail_at(e, "unknown pose key \"" + key + "\"");
        }
    }
    if (!has_table || !has_index) {
        return fail_at(v, "\"pose\" needs both \"disc_table\" and \"index\"");
    }
    return Result<void>::ok();
}

Result<void> read_object(const Value& v, WorldObjectDef& out) {
    if (!v.is_object()) {
        return fail_at(v, "an object entry must be a JSON object");
    }
    bool has_position = false;
    bool has_rotation = false;
    bool has_pose = false;
    for (const Value::Member& m : v.members()) {
        const std::string& key = m.first;
        const Value& e = m.second;
        Result<void> r = Result<void>::ok();
        if (key == "name" || key == "model") {
            if (!e.is_string() || e.string().empty()) {
                return fail_at(e, "\"" + key + "\" must be a non-empty string");
            }
            if (key == "model" && e.string().size() > 8) {
                return fail_at(e, "\"model\" names are at most 8 characters");
            }
            (key == "name" ? out.name : out.model) = e.string();
        } else if (key == "position") {
            has_position = true;
            r = read_vector3(e, "position", false, out);
        } else if (key == "rotation") {
            has_rotation = true;
            r = read_vector3(e, "rotation", true, out);
        } else if (key == "item_model") {
            const Value* file = e.is_object() ? e.find("file") : nullptr;
            const Value* scale = e.is_object() ? e.find("scale") : nullptr;
            if (file == nullptr || !file->is_string() || file->string().empty() || e.members().size() > 2 ||
                (e.members().size() == 2 && scale == nullptr) ||
                (scale != nullptr && (!scale->is_number() || scale->number() <= 0.0))) {
                return fail_at(e, "\"item_model\" must be {\"file\": \"ITEM/<NAME>.TMD\", \"scale\": > 0}");
            }
            out.item_model = file->string();
            out.item_model_scale = scale != nullptr ? scale->number() : 1.0;
        } else if (key == "pose") {
            has_pose = true;
            out.pose = WorldObjectPoseRef{};
            r = read_pose(e, out);
        } else if (key == "script") {
            if (!e.is_bool()) {
                return fail_at(e, "\"script\" must be true or false");
            }
            out.script = e.boolean();
        } else if (key == "pc_near_pickup") {
            if (!e.is_bool()) {
                return fail_at(e, "\"pc_near_pickup\" must be true or false");
            }
            out.pc_near_pickup = e.boolean();
        } else if (key == "zones") {
            if (!e.is_array()) {
                return fail_at(e, "\"zones\" must be an array");
            }
            for (const Value& z : e.array()) {
                if (!z.is_object()) {
                    return fail_at(z, "a zone is an object with \"x\" and/or \"z\" cells");
                }
                WorldObjectZone zone;
                for (const Value::Member& axis : z.members()) {
                    if (axis.first == "x") {
                        r = read_cells(axis.second, zone.x_cells);
                    } else if (axis.first == "z") {
                        r = read_cells(axis.second, zone.z_cells);
                    } else if (axis.first.rfind('_', 0) != 0) {
                        return fail_at(axis.second, "unknown zone key \"" + axis.first + "\"");
                    }
                    if (!r) {
                        return r;
                    }
                }
                out.zones.push_back(std::move(zone));
            }
        } else if (key == "flags_set") {
            r = read_names(e, "flags_set", out.flags_set);
        } else if (key == "flags_clear") {
            r = read_names(e, "flags_clear", out.flags_clear);
        } else if (key != "description" && key.rfind('_', 0) != 0) {
            // "description" and "_" keys are notes for people; anything else is a typo.
            return fail_at(e, "unknown key \"" + key + "\"");
        }
        if (!r) {
            return r;
        }
    }
    if (out.name.empty() || out.model.empty()) {
        return fail_at(v, "an object needs \"name\" and \"model\"");
    }
    if (has_pose) {
        // The placement comes from the disc overlay: exactly one of "position" and "pose", and a
        // "rotation" next to a pose would silently override the disc one.
        if (has_position) {
            return fail_at(v, "object \"" + out.name + "\" has both \"position\" and \"pose\"");
        }
        if (has_rotation) {
            return fail_at(v, "object \"" + out.name + "\" has \"rotation\" next to \"pose\"");
        }
    } else if (!has_position) {
        return fail_at(v, "object \"" + out.name + "\" has neither \"position\" nor \"pose\"");
    }
    return Result<void>::ok();
}

}  // namespace

std::int32_t world_metres_to_q12(double metres) {
    const float f = static_cast<float>(metres);
    return static_cast<std::int32_t>(f * static_cast<float>(1 << 12));
}

std::int16_t world_degrees_to_angle(double degrees) {
    const float turns = static_cast<float>(degrees) / 360.0f;
    return static_cast<std::int16_t>(static_cast<std::int32_t>(turns * static_cast<float>(1 << 12)));
}

bool world_position_in_cell(std::int32_t position, int cell) {
    const std::int32_t chunk = position / kWorldObjectCellSize;
    if (cell >= 0) {
        return position > 0 && chunk == cell;
    }
    return position <= 0 && chunk == cell + 1;
}

bool world_zone_contains(const WorldObjectZone& zone, std::int32_t player_x, std::int32_t player_z) {
    const auto axis = [](const std::vector<int>& cells, std::int32_t p) {
        if (cells.empty()) {
            return true;
        }
        for (int c : cells) {
            if (world_position_in_cell(p, c)) {
                return true;
            }
        }
        return false;
    };
    return axis(zone.x_cells, player_x) && axis(zone.z_cells, player_z);
}

Result<WorldObjectTable> parse_world_objects(std::string_view json_text) {
    using R = Result<WorldObjectTable>;
    const Result<Value> parsed = json::parse(json_text);
    if (!parsed) {
        return R::fail("world_objects.json: " + parsed.error());
    }
    const Value& root = parsed.value();
    const Value* format = root.find("format");
    if (!format || !format->is_string() || format->string() != "sh-map-world-objects") {
        return R::fail("world_objects.json: \"format\" is not \"sh-map-world-objects\"");
    }
    const Value* version = root.find("version");
    if (!version || !version->is_integer() || version->integer() != 2) {
        return R::fail("world_objects.json: only version 2 is read (version 1 is the old inventory written from the code)");
    }
    WorldObjectTable table;
    if (const Value* map = root.find("map"); map && map->is_string()) {
        table.map = map->string();
    }
    const Value* objects = root.find("objects");
    if (!objects || !objects->is_array()) {
        return R::fail("world_objects.json: no \"objects\" array");
    }
    for (const Value& o : objects->array()) {
        WorldObjectDef def;
        const Result<void> r = read_object(o, def);
        if (!r) {
            return R::fail(r.error());
        }
        for (const WorldObjectDef& prev : table.objects) {
            if (prev.name == def.name) {
                return R::fail("world_objects.json: " + where(o) + ": the name \"" + def.name + "\" is used twice");
            }
        }
        table.objects.push_back(std::move(def));
    }
    return R::ok(std::move(table));
}

Result<WorldObjectTable> load_world_objects(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        return Result<WorldObjectTable>::fail("cannot open " + file.string());
    }
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    Result<WorldObjectTable> r = parse_world_objects(text);
    if (!r) {
        return Result<WorldObjectTable>::fail(file.string() + ": " + r.error());
    }
    return r;
}

std::vector<WorldObjectState> initial_states(const WorldObjectTable& table) {
    std::vector<WorldObjectState> out;
    out.reserve(table.objects.size());
    for (const WorldObjectDef& def : table.objects) {
        WorldObjectState state;
        state.position = def.position;
        state.rotation = def.rotation;
        state.model = def.model;
        out.push_back(std::move(state));
    }
    return out;
}

int find_world_object(const WorldObjectTable& table, std::string_view name) {
    for (std::size_t i = 0; i < table.objects.size(); ++i) {
        if (table.objects[i].name == name) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

std::vector<std::size_t> select_world_objects(const WorldObjectTable& table, const std::vector<WorldObjectState>* states,
                                              std::int32_t player_x, std::int32_t player_z,
                                              const WorldObjectFlagGet& flag_get, const WorldObjectNear& near) {
    std::vector<std::size_t> out;
    for (std::size_t i = 0; i < table.objects.size(); ++i) {
        const WorldObjectDef& def = table.objects[i];
        const WorldObjectState* state = states != nullptr ? &(*states)[i] : nullptr;
        if (state != nullptr && state->visible.has_value()) {
            if (state->visible.value()) {
                out.push_back(i);
            }
            continue;
        }
        // The place test: a zone first (objects without zones are everywhere, so "near" changes
        // nothing for them), then the engine's neighbouring-cell pickup.
        bool place = def.zones.empty();
        for (const WorldObjectZone& z : def.zones) {
            if (world_zone_contains(z, player_x, player_z)) {
                place = true;
                break;
            }
        }
        if (!place && def.pc_near_pickup && near) {
            const std::array<std::int32_t, 3>& position = state != nullptr ? state->position : def.position;
            place = near(def, position);
        }
        if (!place) {
            continue;
        }
        bool ok = true;
        for (const std::string& f : def.flags_set) {
            if (!flag_get || !flag_get(f)) {
                ok = false;
                break;
            }
        }
        for (std::size_t f = 0; ok && f < def.flags_clear.size(); ++f) {
            if (flag_get && flag_get(def.flags_clear[f])) {
                ok = false;
            }
        }
        if (ok) {
            out.push_back(i);
        }
    }
    return out;
}

std::vector<const WorldObjectDef*> visible_objects(const WorldObjectTable& table, std::int32_t player_x,
                                                   std::int32_t player_z, const WorldObjectFlagGet& flag_get) {
    std::vector<const WorldObjectDef*> out;
    for (const std::size_t i : select_world_objects(table, nullptr, player_x, player_z, flag_get, {})) {
        out.push_back(&table.objects[i]);
    }
    return out;
}

}  // namespace sh::world
