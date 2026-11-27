// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// What the placement editor can select on the loaded map, read from the structures the game runs on: world
// objects, the scripts' pickups, the characters in the world, the scripts' zones (sh.world.mark), the map's event
// triggers, map points and spawn rows. For each: its live change (apply), where it is written (load_origin: the
// JSON files that have its record, or the script call that made it and the lines of that script holding its
// numbers) and the text to put there after an edit (corrections). No rule of the game is here and nothing is
// written to a file.

#include "features/tools/edit_internal.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <utility>

#include "engine/chara/chara_library.h"
#include "engine/chara/runtime/chara_data.h"
#include "engine/devtools/map_snippets.h"
#include "engine/devtools/position_snippets.h"
#include "engine/game_state/game_state.h"
#include "engine/maps/map_runtime.h"
#include "engine/mods/load_order.h"
#include "engine/mods/registry.h"
#include "engine/runtime/world_object_runtime.h"
#include "engine/vfs/vfs_c.h"
#include "engine/world/runtime_pickups.h"
#include "engine/world/script_origin.h"
#include "engine/world/world_objects.h"
#include "features/tools/tools_internal.h"
#include "formats/convert/mapdata/mapdata.h"

namespace sh::tools::edit {

namespace {

namespace gs = sh::game_state;
namespace fs = std::filesystem;

constexpr double kQ12Step = 1.5 / 4096.0;  // how far a file's literal may be from the engine's integer
constexpr double kBoxStep = 0.25;          // a box trigger's half extents are stored in quarter metres
constexpr double kWidthStep = 1.0 / 16.0;  // a segment's half width in sixteenths
constexpr double kFacingReach = 0.8;       // the reach of a facing-point trigger (drawn, not stored)

sh::maps::MapRuntime& rt() { return sh::maps::map_runtime(); }

double number_of(const std::string& text) { return std::strtod(text.c_str(), nullptr); }

std::string trimmed(const std::string& s) {
    const std::size_t a = s.find_first_not_of(" \t");
    if (a == std::string::npos) return {};
    return s.substr(a, s.find_last_not_of(" \t\r") - a + 1);
}

std::string read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

std::string gamedata_dir() {
    const char* dir = ShVfs_GameDataDir();
    return dir != nullptr ? dir : "";
}

std::vector<std::pair<std::string, std::string>> mod_roots() {
    std::vector<std::pair<std::string, std::string>> out;
    for (const sh::mods::ResolvedMod* m : sh::mods::registry().active())
        out.emplace_back(m->candidate.manifest.id, m->candidate.root.generic_string());
    return out;
}

dt::FileOwner owner_of(const std::string& file) { return dt::file_owner(file, gamedata_dir(), mod_roots()); }

// The base file of a gamedata path and every active mod's copy, lowest layer first.
std::vector<std::string> layers_of(const std::string& relative) {
    std::vector<std::string> out;
    const std::string base = gamedata_dir() + "/" + relative;
    std::error_code ec;
    if (!gamedata_dir().empty() && fs::is_regular_file(base, ec)) out.push_back(base);
    for (const fs::path& p : sh::mods::registry().find_all(relative)) out.push_back(p.generic_string());
    return out;
}

dt::Wanted wanted(double metres) { return {metres, kQ12Step, true}; }

const sh::world::ScriptMark* mark_of(const Thing& t) {
    for (const sh::world::ScriptMark& m : sh::world::script_marks())
        if (m.id == t.id && m.owner == t.owner) return &m;
    return nullptr;
}

std::string chara_folder(int id) {
    const sh::chara::CharaLibrary* lib = Pc_CharaData_Library();
    const sh::chara::CharacterDef* def = lib != nullptr ? lib->find(id) : nullptr;
    return def != nullptr ? def->folder : std::string("chara");
}

// The script spawn of a slot, while the character it made still stands there.
const sh::world::ScriptSpawn* live_spawn(int slot) {
    const sh::world::ScriptSpawn* s = sh::world::script_spawn(slot);
    if (s == nullptr) return nullptr;
    const std::string folder = chara_folder(gs::chara_id(slot));
    const std::size_t colon = s->chara.rfind(':');
    return (colon == std::string::npos ? s->chara : s->chara.substr(colon + 1)) == folder ? s : nullptr;
}

void script_rows(Origin& o, const char* call, const sh::world::ScriptOrigin& made) {
    if (!made.known()) {
        o.rows.push_back(std::string("made by ") + call + " (not from a script file: the console or a snippet)");
        return;
    }
    const dt::FileOwner owner = owner_of(made.file);
    o.lua_file = made.file;
    o.lua_mod = owner.mod;
    o.lua_where = dt::file_owner_text(owner);
    o.lua_text = read_file(made.file);
    o.rows.push_back(std::string("made by ") + call + " at " + o.lua_where + ":" + std::to_string(made.line) +
                     (made.via.empty() ? std::string() : "  (called from " + made.via + ")"));
}

void line_rows(Origin& o, const char* what, const std::vector<dt::SourceLine>& lines) {
    if (lines.empty()) return;
    o.rows.push_back(std::string(what) + ": " + o.lua_where + ":" + std::to_string(lines[0].line) +
                     (lines.size() > 1 ? "  (" + std::to_string(lines.size()) + " lines match)" : std::string()));
    o.rows.push_back("    " + trimmed(lines[0].text));
}

bool same_q12(double a, double b) { return q12(a) == q12(b); }
bool same_angle(double a, double b) {
    return sh::world::world_degrees_to_angle(a) == sh::world::world_degrees_to_angle(b);
}

std::string triple(const std::array<double, 3>& v, int decimals) {
    return "[" + dt::tidy_number(v[0], decimals) + ", " + dt::tidy_number(v[1], decimals) + ", " +
           dt::tidy_number(v[2], decimals) + "]";
}

int point_param(double value, double step) { return std::clamp(static_cast<int>(std::lround(value / step)), 0, 255); }

}  // namespace

const char* kind_name(Kind k) {
    static const char* names[] = {"object", "pickup", "monster", "zone", "trigger", "point", "spawn"};
    return names[static_cast<int>(k)];
}

const char* kind_title(Kind k) {
    static const char* titles[] = {"world objects", "pickups",    "monsters, characters", "script zones",
                                   "map triggers",  "map points", "spawn points"};
    return titles[static_cast<int>(k)];
}

std::vector<Thing> collect(double ground_y) {
    std::vector<Thing> out;
    if (!rt().loaded()) return out;
    const std::int32_t ground = q12(ground_y);

    sh::world::WorldObjectRuntime& wo = sh::world::world_object_runtime();
    if (wo.map() == rt().name()) {
        for (const auto& o : wo.describe()) {
            Thing t;
            t.kind = Kind::Object;
            t.key = "object:" + o.name;
            t.id = o.name;
            t.label = o.name + " (" + o.model + ")";
            t.does =
                o.item_model.empty() ? "world object, model " + o.model : "world object, model file " + o.item_model;
            t.position = o.position;
            t.rotation = o.rotation;
            t.caps = kMove | kHeight | kYaw | kTilt;
            t.shown = o.queued;
            out.push_back(std::move(t));
        }
    }
    for (const sh::world::Pickup& p : sh::world::pickups().all()) {
        Thing t;
        t.kind = Kind::Pickup;
        t.key = "pickup:" + p.spec.id;
        t.id = p.spec.id;
        t.owner = p.spec.owner;
        t.label = p.spec.id + " (" + p.spec.item + ")";
        t.does = "gives " + p.spec.item + ", sets flag " + p.spec.flag +
                 (p.spec.unseen ? "; drawn by a world object" : "; model " + p.spec.model);
        t.position = p.spec.position;
        if (p.spec.ground) t.position[1] = ground;
        t.rotation = p.spec.rotation;
        t.caps = kMove | kHeight | kYaw | kTilt;
        t.shown = !(p.resolved.flag > 0 && gs::event_flag(p.resolved.flag));
        out.push_back(std::move(t));
    }
    for (int slot = 1; slot <= gs::kNpcSlots; ++slot) {
        const int id = gs::chara_id(slot);
        if (id <= 0) continue;
        const sh::entities::Vec3 at = gs::chara_position(slot);
        Thing t;
        t.kind = Kind::Character;
        t.key = "chara:" + std::to_string(slot);
        t.index = slot;
        t.label = chara_folder(id) + " #" + std::to_string(slot);
        const sh::world::ScriptSpawn* spawn = live_spawn(slot);
        if (spawn != nullptr) t.owner = spawn->owner;
        t.does = spawn != nullptr ? "character spawned by a script" : "character spawned by the map";
        t.position = {at.x, at.y, at.z};
        t.rotation = {0, gs::chara_rotation(slot).y, 0};
        t.caps = kMove | kHeight | kYaw;
        out.push_back(std::move(t));
    }
    for (const sh::world::ScriptMark& m : sh::world::script_marks()) {
        Thing t;
        t.kind = Kind::Zone;
        // The id of a mark already carries its mod's prefix (the script API adds it to a short name).
        t.key = "zone:" + (m.owner.empty() || m.id.rfind(m.owner + ":", 0) == 0 ? m.id : m.owner + ":" + m.id);
        t.id = m.id;
        t.owner = m.owner;
        t.label = m.label + (m.kind.empty() ? std::string() : " [" + m.kind + "]");
        t.does = "script zone" + (m.kind.empty() ? std::string() : ", " + m.kind) + ": " + m.label;
        t.position = {m.x, ground, m.z};
        t.radius = m.radius;
        t.caps = kMove | kRadius;
        t.on_ground = true;
        out.push_back(std::move(t));
    }
    const sh::maps::MapPointRecord* points = rt().points();
    std::vector<std::string> users(sh::maps::kMapPoints);
    const int events = map_event_count();
    for (int i = 0; i < events; ++i) {
        const EventInfo e = map_event_info(i);
        if (e.point < 0 || e.point >= sh::maps::kMapPoints) continue;
        const sh::maps::MapPointRecord& p = points[e.point];
        users[static_cast<std::size_t>(e.point)] +=
            (users[static_cast<std::size_t>(e.point)].empty() ? "" : ", ") + std::to_string(i);
        Thing t;
        t.kind = Kind::Trigger;
        t.key = "event:" + std::to_string(i);
        t.index = i;
        t.id = std::to_string(e.point);
        t.label = e.label;
        t.does = std::string(e.door ? "door or map transition" : "map event") + " at map point " + t.id +
                 " (moving it moves the point)";
        t.position = {p.x, ground, p.z};
        t.caps = kMove;
        t.on_ground = true;
        t.shown = e.open;
        if (e.trigger_type == 1) {
            t.size = {p.param0() * kBoxStep, p.param1() * kBoxStep};
            t.size_step = kBoxStep;
            t.caps |= kSize;
        } else if (e.trigger_type == 3 || e.trigger_type == 4) {
            t.radius = static_cast<std::int32_t>(p.param1()) * 256;
            t.size_step = kWidthStep;
            t.caps |= kRadius;
        } else if (e.trigger_type == 2) {
            t.radius = q12(kFacingReach);
        }
        out.push_back(std::move(t));
    }
    for (int i = 0; i < sh::maps::kMapPoints; ++i) {
        const sh::maps::MapPointRecord& p = points[i];
        if (p.x == 0 && p.z == 0) continue;
        Thing t;
        t.kind = Kind::Point;
        t.key = "point:" + std::to_string(i);
        t.index = i;
        t.id = std::to_string(i);
        t.label = "point " + t.id;
        const std::string& by = users[static_cast<std::size_t>(i)];
        t.does = by.empty() ? "map point (a landing place, or used by a script)" : "map point of the events " + by;
        t.position = {p.x, ground, p.z};
        t.caps = kMove;
        t.on_ground = true;
        out.push_back(std::move(t));
    }
    for (int row = 0; row < sh::maps::kSpawnRowCount; ++row) {
        const sh::maps::SpawnPoint& s = rt().spawn(row);
        if (s.flags == 0) continue;
        Thing t;
        t.kind = Kind::Spawn;
        t.key = "spawn:" + std::to_string(row);
        t.index = row;
        t.label = map_spawn_label(row);
        t.does = "spawn row of the map's table (group " + std::to_string(row / sh::maps::kSpawnSlots) + ", slot " +
                 std::to_string(row % sh::maps::kSpawnSlots) + ")";
        t.position = {s.x, ground, s.z};
        t.rotation = {0, static_cast<std::int16_t>(s.rotation_y * 16), 0};
        t.caps = kMove | kYaw;
        t.on_ground = true;
        out.push_back(std::move(t));
    }
    return out;
}

dt::EditValue value_of(const Thing& t) {
    dt::EditValue v;
    for (std::size_t i = 0; i < 3; ++i) {
        v.position[i] = number_of(dt::metres_text(t.position[i]));
        v.rotation[i] = number_of(dt::degrees_text(t.rotation[i]));
    }
    v.radius = number_of(dt::metres_text(t.radius));
    v.size = t.size;
    return v;
}

Vec3 place_of(const Thing& t) { return {metres(t.position[0]), metres(t.position[1]), metres(t.position[2])}; }

void apply(const Thing& t, const dt::EditValue& v) {
    const std::array<std::int32_t, 3> position{q12(v.position[0]), q12(v.position[1]), q12(v.position[2])};
    const std::array<std::int16_t, 3> rotation{sh::world::world_degrees_to_angle(v.rotation[0]),
                                               sh::world::world_degrees_to_angle(v.rotation[1]),
                                               sh::world::world_degrees_to_angle(v.rotation[2])};
    switch (t.kind) {
        case Kind::Object:
            (void)sh::world::world_object_runtime().tool_set_pose(t.id, {position, rotation});
            break;
        case Kind::Pickup:
            (void)sh::world::pickups().set_pose(t.id, position, rotation);
            break;
        case Kind::Character:
            if (gs::chara_id(t.index) > 0) {
                gs::set_chara_position(t.index, {position[0], position[1], position[2]});
                gs::set_chara_rotation_y(t.index, rotation[1]);
            }
            break;
        case Kind::Zone:
            if (const sh::world::ScriptMark* m = mark_of(t)) {
                sh::world::ScriptMark moved = *m;
                moved.x = position[0];
                moved.z = position[2];
                moved.radius = q12(std::max(0.0, v.radius));
                sh::world::set_script_mark(std::move(moved));
            }
            break;
        case Kind::Trigger:
        case Kind::Point: {
            const int index = std::atoi(t.id.c_str());
            if (!rt().loaded() || index < 0 || index >= sh::maps::kMapPoints) break;
            sh::maps::MapPointRecord& p = rt().points()[index];
            p.x = position[0];
            p.z = position[2];
            std::uint32_t a = p.param0(), b = p.param1();
            if ((t.caps & kSize) != 0) {
                a = static_cast<std::uint32_t>(point_param(v.size[0], kBoxStep));
                b = static_cast<std::uint32_t>(point_param(v.size[1], kBoxStep));
            } else if ((t.caps & kRadius) != 0) {
                b = static_cast<std::uint32_t>(point_param(v.radius, kWidthStep));
            }
            p.packed = (p.packed & 0x0000FFFFu) | (a << 16) | (b << 24);
            break;
        }
        case Kind::Spawn:
            if (rt().loaded() && t.index >= 0 && t.index < sh::maps::kSpawnRowCount) {
                sh::maps::SpawnPoint& s = rt().spawn(t.index);
                s.x = position[0];
                s.z = position[2];
                s.rotation_y =
                    static_cast<std::uint8_t>(static_cast<int>(std::lround(v.rotation[1] * 256.0 / 360.0)) & 0xFF);
            }
            break;
        case Kind::Count:
            break;
    }
}

Origin load_origin(const Thing& t, const dt::EditValue& file) {
    Origin o;
    o.key = t.key;
    const std::string map_dir = "maps/" + rt().name() + "/";
    const std::array<double, 3>& at = file.position;
    switch (t.kind) {
        case Kind::Object: {
            const std::string relative = map_dir + "world_objects.json";
            for (const std::string& file : layers_of(relative)) {
                Origin::Layer l;
                l.file = file;
                l.text = read_file(file);
                const auto record = dt::json_record(l.text, "name", t.id);
                if (!record) continue;
                l.record = *record;
                l.line = dt::line_of(l.text, record->begin);
                l.where = dt::file_owner_text(owner_of(file));
                l.position = dt::json_member(l.text, *record, "position").has_value();
                l.rotation = dt::json_member(l.text, *record, "rotation").has_value();
                l.pose = dt::json_member(l.text, *record, "pose").has_value();
                o.layers.push_back(std::move(l));
            }
            for (std::size_t i = 0; i < o.layers.size(); ++i)
                if (o.layers[i].position || o.layers[i].pose) o.target = static_cast<int>(i);
            if (o.target < 0 && !o.layers.empty()) o.target = static_cast<int>(o.layers.size()) - 1;
            for (std::size_t i = 0; i < o.layers.size(); ++i) {
                const Origin::Layer& l = o.layers[i];
                std::string row = l.where + ":" + std::to_string(l.line);
                if (static_cast<int>(i) == o.target) {
                    row += l.position ? "  <- its place (\"position\")"
                           : l.pose   ? "  <- its place (\"pose\": a disc table)"
                                      : "  <- its record";
                } else {
                    row += "  (patches other keys)";
                }
                o.rows.push_back(std::move(row));
            }
            if (o.layers.empty()) o.rows.push_back(data_source(relative) + "  (no record named " + t.id + " found)");
            break;
        }
        case Kind::Pickup: {
            const sh::world::Pickup* p = sh::world::pickups().find(t.id);
            if (p == nullptr) break;
            script_rows(o, "sh.world.pickup_create", p->spec.origin);
            if (o.lua_text.empty()) break;
            o.place_lines = dt::lines_with_values(o.lua_text, {wanted(at[0]), wanted(at[2])});
            if (o.place_lines.size() > 1 && !p->spec.ground) {
                // Two pickups side by side on one line of numbers each: the height tells them apart when it is there.
                const auto exact = dt::lines_with_values(o.lua_text, {wanted(at[0]), wanted(at[2]), wanted(at[1])});
                if (!exact.empty()) o.place_lines = exact;
            }
            line_rows(o, "its place", o.place_lines);
            break;
        }
        case Kind::Character: {
            const sh::world::ScriptSpawn* s = live_spawn(t.index);
            if (s == nullptr) {
                o.rows.push_back("spawned by the map: " + data_source(map_dir + "spawns.json"));
                o.rows.push_back("or by an event script: " + data_source(map_dir + "events.lua"));
                break;
            }
            script_rows(o, "sh.entity.spawn", s->origin);
            o.spawned = true;
            o.spawn = {s->x, s->z};
            if (o.lua_text.empty()) break;
            o.place_lines = dt::lines_with_values(o.lua_text, {wanted(metres(s->x)), wanted(metres(s->z))});
            line_rows(o, "where it is spawned", o.place_lines);
            break;
        }
        case Kind::Zone: {
            const sh::world::ScriptMark* m = mark_of(t);
            if (m == nullptr) break;
            script_rows(o, "sh.world.mark", m->origin);
            if (o.lua_text.empty()) break;
            const dt::Wanted r = wanted(file.radius);
            o.place_lines = dt::lines_with_values(o.lua_text, {wanted(at[0]), wanted(at[2])});
            // The radius: on the line that names the zone by its label, else on a line with the place.
            for (const dt::SourceLine& l : dt::lines_with_string(o.lua_text, m->label)) {
                if (!dt::lines_with_values(l.text, {r}).empty()) o.size_lines.push_back(l);
            }
            if (o.size_lines.empty())
                o.size_lines = dt::lines_with_values(o.lua_text, {wanted(at[0]), wanted(at[2]), r});
            line_rows(o, "its place", o.place_lines);
            line_rows(o, "its radius", o.size_lines);
            break;
        }
        case Kind::Trigger:
        case Kind::Point: {
            const std::string relative = map_dir + "points.json";
            const std::vector<std::string> files = layers_of(relative);
            if (!files.empty()) {
                const std::string text = read_file(files.back());
                if (const auto record = dt::json_record(text, "index", t.id)) {
                    o.plain_where = dt::file_owner_text(owner_of(files.back())) + ":" +
                                    std::to_string(dt::line_of(text, record->begin));
                    o.plain_before = text.substr(record->begin, record->end - record->begin);
                }
            }
            if (o.plain_where.empty()) o.plain_where = data_source(relative);
            o.rows.push_back("map point " + t.id + ": " + o.plain_where);
            o.rows.push_back("a mod changes it in " + data_source(map_dir + "map.json"));
            if (t.kind == Kind::Trigger) o.rows.push_back("the event: " + data_source(map_dir + "events.json"));
            break;
        }
        case Kind::Spawn:
            o.plain_where = data_source(map_dir + "spawns.json");
            o.rows.push_back(o.plain_where);
            o.rows.push_back(data_source(map_dir + "spawn_overrides.json"));
            break;
        case Kind::Count:
            break;
    }
    return o;
}

std::vector<Correction> corrections(const Thing& t, const Origin& o, const dt::EditValue& original,
                                    const dt::EditValue& now) {
    std::vector<Correction> out;
    if (o.key != t.key || original == now) return out;
    std::array<bool, 3> moved{}, turned{};
    for (std::size_t i = 0; i < 3; ++i) {
        moved[i] = !same_q12(original.position[i], now.position[i]);
        turned[i] = !same_angle(original.rotation[i], now.rotation[i]);
    }
    const bool any_moved = moved[0] || moved[1] || moved[2], any_turned = turned[0] || turned[1] || turned[2];
    const bool resized = !same_q12(original.radius, now.radius) || !same_q12(original.size[0], now.size[0]) ||
                         !same_q12(original.size[1], now.size[1]);
    const std::string map_dir = "maps/" + rt().name() + "/";

    if (t.kind == Kind::Object) {
        if (!any_moved && !any_turned) return out;
        const std::string patch = "{\"name\": " + dt::json_string(t.id) + ", \"position\": " + triple(now.position, 4) +
                                  ", \"rotation\": " + triple(now.rotation, 3) + "}";
        if (o.target < 0) {
            out.push_back({data_source(map_dir + "world_objects.json"),
                           {},
                           patch,
                           {},
                           "a record that patches the object by its name"});
            return out;
        }
        const Origin::Layer& l = o.layers[static_cast<std::size_t>(o.target)];
        std::vector<dt::MemberEdit> edits;
        // A record placed by a disc pose has neither member: both are written whole then.
        if (any_moved || !l.position) {
            edits.push_back({"position",
                             {now.position[0], now.position[1], now.position[2]},
                             4,
                             {!moved[0], !moved[1], !moved[2]}});
        }
        if (any_turned || (!l.position && !l.rotation)) {
            edits.push_back({"rotation",
                             {now.rotation[0], now.rotation[1], now.rotation[2]},
                             3,
                             {!turned[0], !turned[1], !turned[2]}});
        }
        const dt::PatchedRecord p = dt::patch_json_record(l.text, l.record, edits);
        for (std::size_t i = 0; i < p.before.size(); ++i) {
            out.push_back({l.where + ":" + std::to_string(p.before[i].line), p.before[i].text, p.after[i], {}, {}});
        }
        for (const std::string& a : p.added) {
            out.push_back(
                {l.where + ":" + std::to_string(p.line),
                 {},
                 a,
                 {},
                 l.pose ? "add it to the record and take \"pose\" out: the place no longer comes from the disc table"
                        : "add it to the record"});
        }
        if (!out.empty()) out.front().record = p.record;
        if (owner_of(l.file).mod.empty()) {
            out.push_back({"a mod's " + map_dir + "world_objects.json",
                           {},
                           patch,
                           {},
                           "to change the base object from a mod: a record with its name and the new keys"});
        }
        return out;
    }

    if (t.kind == Kind::Trigger || t.kind == Kind::Point) {
        if (!moved[0] && !moved[2] && !resized) return out;
        const int index = std::atoi(t.id.c_str());
        const sh::maps::MapPointRecord& p = rt().points()[index];
        int a = p.param0(), b = p.param1();
        if ((t.caps & kSize) != 0) {
            a = point_param(now.size[0], kBoxStep);
            b = point_param(now.size[1], kBoxStep);
        } else if ((t.caps & kRadius) != 0) {
            b = point_param(now.radius, kWidthStep);
        }
        const dt::PointEntry entry{
            index, q12(now.position[0]), q12(now.position[2]), p.paper_map(), p.loading_screen(), a,
            b,     p.weather_on_enter()};
        out.push_back({o.plain_where,
                       o.plain_before,
                       dt::point_json(entry),
                       {},
                       "the point's entry of points.json; a mod puts it under \"points\" of its map.json"});
        return out;
    }

    if (t.kind == Kind::Spawn) {
        if (!moved[0] && !moved[2] && !turned[1]) return out;
        const sh::maps::SpawnPoint& s = rt().spawn(t.index);
        const char* own = sh::convert::mapdata::chara_name_of(s.chara);
        const dt::SpawnEntry entry{q12(now.position[0]),
                                   q12(now.position[2]),
                                   static_cast<int>(std::lround(now.rotation[1] * 256.0 / 360.0)) & 0xFF,
                                   own != nullptr ? own : std::to_string(s.chara),
                                   s.flags,
                                   s.difficulty_min};
        out.push_back({o.plain_where,
                       {},
                       dt::spawn_json(entry),
                       {},
                       "slot " + std::to_string(t.index % sh::maps::kSpawnSlots) + " of group " +
                           std::to_string(t.index / sh::maps::kSpawnSlots)});
        return out;
    }

    // A thing a script made: the numbers stand in a table or a call of the script.
    if (o.lua_where.empty()) {
        if (any_moved || resized)
            out.push_back({"(not made from a script file)", {}, {}, {}, "nothing to correct in a file"});
        return out;
    }
    const std::string raw = dt::raw_point_row(now.position[0], now.position[1], now.position[2]);
    struct LineEdit {
        dt::SourceLine line;
        std::vector<dt::Change> changes;
    };
    std::vector<LineEdit> edits;
    const auto on_line = [&edits](const dt::SourceLine& line) -> LineEdit& {
        for (LineEdit& e : edits)
            if (e.line.line == line.line) return e;
        edits.push_back({line, {}});
        return edits.back();
    };
    // A character is written where it is spawned; it has walked from there since.
    const std::array<double, 3> from =
        o.spawned ? std::array<double, 3>{metres(o.spawn[0]), original.position[1], metres(o.spawn[1])}
                  : original.position;
    const bool place_changed = moved[0] || moved[2] || (t.kind == Kind::Pickup && moved[1]);
    if (place_changed) {
        if (o.place_lines.empty()) {
            out.push_back({o.rows.empty() ? o.lua_where : o.rows.front(),
                           {},
                           {},
                           {},
                           "its place is not written as two numbers on one line of that file (it is computed there): "
                           "put it by hand, " +
                               raw});
        } else {
            LineEdit& e = on_line(o.place_lines[static_cast<std::size_t>(
                std::clamp(o.place_pick, 0, static_cast<int>(o.place_lines.size()) - 1))]);
            static const char* kKeys[] = {"x", "y", "z"};
            for (const std::size_t i : {std::size_t{0}, std::size_t{2}, std::size_t{1}}) {
                if (i == 1 && !(t.kind == Kind::Pickup && moved[1])) continue;
                if (i != 1 && !o.spawned && !moved[i]) continue;
                e.changes.push_back({wanted(from[i]), now.position[i], kKeys[i]});
            }
        }
    }
    if (t.kind == Kind::Zone && resized) {
        if (o.size_lines.empty()) {
            out.push_back(
                {o.rows.empty() ? o.lua_where : o.rows.front(),
                 {},
                 {},
                 {},
                 "its radius is not written beside its label or its place in that file: put it by hand, r = " +
                     dt::tidy_number(now.radius)});
        } else {
            LineEdit& e = on_line(o.size_lines[static_cast<std::size_t>(
                std::clamp(o.size_pick, 0, static_cast<int>(o.size_lines.size()) - 1))]);
            e.changes.push_back({wanted(original.radius), now.radius, "r"});
        }
    }
    for (const LineEdit& e : edits) {
        int replaced = 0;
        const std::string after = dt::patch_line(e.line.text, e.changes, &replaced);
        Correction c{o.lua_where + ":" + std::to_string(e.line.line), trimmed(e.line.text), trimmed(after), {}, {}};
        if (replaced < static_cast<int>(e.changes.size())) {
            c.note = std::to_string(replaced) + " of " + std::to_string(e.changes.size()) +
                     " numbers stand on this line; the whole place: " + raw;
        }
        out.push_back(std::move(c));
    }
    if ((t.kind == Kind::Pickup && any_turned) || (t.kind == Kind::Character && turned[1])) {
        const auto angle = [&now](std::size_t i) {
            return std::to_string(sh::world::world_degrees_to_angle(now.rotation[i]) & 0xFFF);
        };
        out.push_back({o.rows.empty() ? o.lua_where : o.rows.front(),
                       {},
                       {},
                       {},
                       t.kind == Kind::Pickup ? "its rotation in the call: rot = { " + angle(0) + ", " + angle(1) +
                                                    ", " + angle(2) + " }  (Q12 turns)"
                                              : "its facing in the call: rot = " + angle(1) + "  (Q12 turns)"});
    }
    return out;
}

}  // namespace sh::tools::edit
