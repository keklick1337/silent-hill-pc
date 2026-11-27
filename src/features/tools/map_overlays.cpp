// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The map data overlays and the map inspector of the editor: event triggers and
// pickups, doors, spawns, world objects, camera zones, collision triggers and rooms of the loaded map, drawn from
// the structures the engine runs on (MapRuntime, the world object runtime, the camera rig), so a mod's data shows
// as the game uses it. A click near a marker selects it; the inspector shows its fields, the gamedata file it comes
// from (with the mods that patch it) and copies the entry in its file's form (map_snippets.h). Nothing here writes
// game state: file reads (handlers.json, world_objects.json, for names and conditions) and the room sampling run
// in the deferred update, the draws only read.

#include "features/tools/tools.h"
#include "features/tools/tools_internal.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <numbers>
#include <optional>
#include <string>
#include <vector>

#include "engine/camera/game_camera.h"
#include "engine/console/instance.h"
#include "engine/console/registry.h"
#include "engine/devtools/map_snippets.h"
#include "engine/devtools/position_snippets.h"
#include "engine/devtools/tool_mode.h"
#include "engine/game_state/game_state.h"
#include "engine/game_state/game_state_world.h"
#include "engine/ids/game_numbers.h"
#include "engine/log/sh_log.h"
#include "engine/chara/chara_library.h"
#include "engine/chara/runtime/chara_data.h"
#include "engine/maps/map_runtime.h"
#include "engine/mods/load_order.h"
#include "engine/mods/registry.h"
#include "engine/runtime/world_object_runtime.h"
#include "engine/script/sh_script.h"
#include "engine/vfs/vfs_data_json.h"
#include "engine/world/collision_triggers.h"
#include "engine/world/runtime_pickups.h"
#include "engine/world/world_objects.h"
#include "formats/common/json.h"
#include "formats/convert/mapdata/mapdata.h"

namespace sh::tools {

namespace {

namespace gs = sh::game_state;
namespace dt = sh::devtools;
namespace cam = sh::camera;

enum class Kind : std::uint8_t { Event, Door, Spawn, Object, Camera, Collision, Room, Live, Count };

constexpr const char* kKindNames[] = {"event", "door", "spawn", "object", "camera", "collision", "room", "live"};
constexpr const char* kOverlayIds[] = {"map.events", "map.doors", "map.spawns", "map.objects",
                                       "map.cameras", "map.collision", "map.rooms", "map.live"};
constexpr double kPi = std::numbers::pi;
constexpr double kFacingReach = 0.8;  // FacingPoint reach outside first person (event_triggers.h)
constexpr double kOrientedBoxDepth = 4.0;

// --- data the runtime does not keep: handler names and shapes, world object conditions ---

struct HandlerInfo {
    std::string symbol, name, shape;
};

struct MapExtras {
    std::string map;
    std::map<int, HandlerInfo> handlers;
    std::vector<sh::world::WorldObjectDef> objects;
};
MapExtras s_extras;
bool s_extras_pending = false;

std::string short_id(const char* full) {
    if (full == nullptr) return {};
    std::string s = full;
    if (s.rfind("silenthill:", 0) == 0) s = s.substr(11);
    return s;
}

std::string flag_text(int bit) {
    if (bit == 0) return {};
    const char* n = sh::ids::flag_name(bit);
    return n ? short_id(n) : std::to_string(bit);
}

std::string item_text(int number) {
    if (number == 0) return {};
    const char* n = sh::ids::item_name(number);
    return n ? short_id(n) : std::to_string(number);
}

void load_extras(const std::string& map) {
    MapExtras e;
    e.map = map;
    const std::string handlers = "maps/" + map + "/handlers.json";
    if (auto text = ShVfs_ReadDataJsonText(handlers.c_str())) {
        if (auto v = sh::json::parse(*text)) {
            if (const sh::json::Value* list = v.value().find("handlers"); list && list->is_array()) {
                for (const sh::json::Value& h : list->array()) {
                    const sh::json::Value* idx = h.find("index");
                    if (!idx || !idx->is_integer()) continue;
                    HandlerInfo info;
                    if (const auto* s = h.find("symbol"); s && s->is_string()) info.symbol = s->string();
                    if (const auto* s = h.find("name"); s && s->is_string()) info.name = s->string();
                    if (const auto* s = h.find("shape"); s && s->is_string()) info.shape = s->string();
                    e.handlers[static_cast<int>(idx->integer())] = info;
                }
            }
        }
    }
    const std::string objects = "maps/" + map + "/world_objects.json";
    if (auto text = ShVfs_ReadDataJsonText(objects.c_str())) {
        if (auto table = sh::world::parse_world_objects(*text)) e.objects = table.value().objects;
    }
    s_extras = std::move(e);
}

// The gamedata file of a kind and the active mods that have a copy of it.
std::string source_of(const std::string& relative) {
    std::string s = "gamedata/" + relative;
    std::vector<std::string> mods;
    const auto files = sh::mods::registry().find_all(relative);
    const auto active = sh::mods::registry().active();
    for (const auto& f : files) {
        const std::string fs = f.generic_string();
        for (const sh::mods::ResolvedMod* m : active) {
            const std::string root = m->candidate.root.generic_string();
            if (fs.rfind(root, 0) == 0) {
                mods.push_back(m->candidate.manifest.id);
                break;
            }
        }
    }
    if (!mods.empty()) {
        s += "  + mods:";
        for (const auto& m : mods) s += " " + m;
    }
    return s;
}

// --- markers ---

struct Marker {
    Kind kind = Kind::Event;
    int index = 0;
    Vec3 pos;
    std::string label;
    std::string search;  // lower-case text the search box matches
    // Area kinds (cameras, collision, rooms): the XZ rectangle a click inside selects.
    bool area = false;
    double min_x = 0, max_x = 0, min_z = 0, max_z = 0;
};

struct Settings {
    float distance = 40.0f;      // markers farther from the camera are not drawn
    float label_distance = 20.0f;
    bool labels = true;
    // What runs now rather than what the files hold: events whose flags shut them, world objects not drawn now are
    // left out; the scripts' pickups, marks and live characters are shown (the "live" kind).
    bool live = true;
};

// The scripts' things of the "live" kind, rebuilt with the markers: pickups, marks, characters.
struct LiveThing {
    std::string label;
    std::string detail;
    Vec3 pos;
    double radius = 0;
    int type = 0;  // 0 pickup, 1 mark, 2 character
};
std::vector<LiveThing> s_live;
Settings s_settings;

struct Selection {
    bool valid = false;
    std::string map;
    Kind kind = Kind::Event;
    int index = 0;
};
Selection s_selection;

double ground_y() { return metres(gs::chara_position(0).y); }

sh::maps::MapRuntime& rt() { return sh::maps::map_runtime(); }

int event_count() {
    if (!rt().loaded()) return 0;
    int n = 0;
    const sh::maps::MapEventRecord* ev = rt().events();
    while (n < sh::maps::kMapEvents && ev[n].trigger_type() != -1) ++n;
    return n;
}

int ev_sys_state(const sh::maps::MapEventRecord& e) { return static_cast<int>(e.action & 0x1F); }
int ev_param(const sh::maps::MapEventRecord& e) { return static_cast<int>((e.action >> 5) & 0xFF); }
int ev_map_idx(const sh::maps::MapEventRecord& e) { return static_cast<int>((e.action >> 25) & 0x3F); }

bool is_door(const sh::maps::MapEventRecord& e) {
    const int s = ev_sys_state(e);
    return s == 5 || s == 6;
}

const char* sys_state_name(int s) {
    switch (s) {
        case 0: return "flag_gate";
        case 4: return "fmv";
        case 5: return "door";
        case 6: return "room_warp";
        case 7: return "message";
        case 10: return "callback";
        case 11: return "set_flag";
        case 12: return "sound";
        default: return "other";
    }
}

const char* shape_name(int t) {
    switch (t) {
        case 0: return "always";
        case 1: return "box";
        case 2: return "facing_point";
        case 3: return "facing_segment";
        case 4: return "oriented_box";
        default: return "shape?";
    }
}

const char* activation_name(int a) {
    switch (a) {
        case 0: return "touch";
        case 1: return "exclusive";
        case 2: return "button";
        case 3: return "item";
        default: return "?";
    }
}

const HandlerInfo* handler_of(const sh::maps::MapEventRecord& e) {
    if (ev_sys_state(e) != 10) return nullptr;
    const auto it = s_extras.handlers.find(ev_param(e));
    return it == s_extras.handlers.end() ? nullptr : &it->second;
}

std::string handler_name(const HandlerInfo& h) { return h.name.empty() ? h.symbol : h.name; }

std::string door_target(const sh::maps::MapEventRecord& e) {
    if (ev_sys_state(e) == 6) return "room warp " + std::to_string(ev_param(e));
    const char* m = gs::map_name(ev_map_idx(e));
    return std::string(m ? m : "?") + " point " + std::to_string(ev_param(e));
}

std::string event_label(int i, const sh::maps::MapEventRecord& e) {
    std::string s = "#" + std::to_string(i) + " ";
    if (is_door(e)) return s + "-> " + door_target(e);
    if (const HandlerInfo* h = handler_of(e)) return s + handler_name(*h) + (h->shape.empty() ? "" : " [" + h->shape + "]");
    s += sys_state_name(ev_sys_state(e));
    if (ev_sys_state(e) == 7) s += " " + std::to_string(ev_param(e));
    return s;
}

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

Vec3 point_pos(int index) {
    const sh::maps::MapPointRecord& p = rt().points()[index];
    return {metres(p.x), ground_y(), metres(p.z)};
}

std::string spawn_chara_name(int row) {
    const sh::maps::SpawnPoint& s = rt().spawn(row);
    const int chara = s.chara > 0 ? s.chara : rt().group_chara(row / sh::maps::kSpawnSlots);
    const char* n = gs::chara_name(chara);
    return n ? short_id(n) : "chara " + std::to_string(chara);
}

const char* difficulty_name(int d) { return d < 0 ? "easy+" : d == 0 ? "normal+" : "hard"; }

// The camera zone the game camera runs on (-1: the engine's fallback zone).
int active_camera_zone() { return cam::game_camera().state().current_road.road; }

const char* camera_move_name(int m) {
    switch (m) {
        case 0: return "chase";
        case 1: return "settle";
        case 2: return "fixed_angle";
        case 3: return "self_view";
        case 4: return "through_door";
        default: return "move?";
    }
}

const char* road_type_name(int t) {
    switch (t) {
        case 0: return "road";
        case 1: return "effect";
        case 2: return "event";
        case 3: return "low_priority";
        case 4: return "high_priority";
        case 5: return "self_view_only";
        default: return "?";
    }
}

std::vector<sh::world::TriggerBox> collision_boxes() {
    std::vector<sh::world::TriggerBox> out;
    if (!rt().loaded()) return out;
    const sh::maps::CollisionTriggerRecord* list = rt().collision_triggers();
    for (int i = 0; i < sh::maps::kCollisionTriggers; ++i) {
        const sh::world::PackedTrigger t{list[i].packed};
        if (sh::world::is_list_end(t)) break;
        out.push_back(sh::world::unpack_trigger(t));
    }
    return out;
}

// --- rooms: sampled around the camera in the deferred update (the rule may run Lua) ---

struct RoomSample {
    std::string map;
    int origin_x = 0, origin_z = 0;  // metres of cell (0, 0)
    int size = 0;                    // cells per side, 1 m each
    std::vector<std::uint8_t> rooms;
};
RoomSample s_rooms;
bool s_rooms_pending = false;

void sample_rooms(const std::string& map, int cx, int cz, int radius) {
    RoomSample r;
    r.map = map;
    r.size = radius * 2;
    r.origin_x = cx - radius;
    r.origin_z = cz - radius;
    r.rooms.resize(static_cast<std::size_t>(r.size * r.size));
    for (int z = 0; z < r.size; ++z)
        for (int x = 0; x < r.size; ++x)
            r.rooms[static_cast<std::size_t>(z * r.size + x)] =
                rt().room_at(q12(r.origin_x + x + 0.5), q12(r.origin_z + z + 0.5));
    s_rooms = std::move(r);
}

// --- marker list of this frame ---

std::vector<Marker> collect_markers() {
    std::vector<Marker> out;
    if (!rt().loaded()) return out;
    if (s_extras.map != rt().name() && !s_extras_pending) {
        s_extras_pending = true;
        const std::string map = rt().name();
        defer([map] {
            s_extras_pending = false;
            load_extras(map);
        });
    }
    const double y = ground_y();
    const int events = event_count();
    for (int i = 0; i < events; ++i) {
        const sh::maps::MapEventRecord& e = rt().events()[i];
        // Live: an event a flag shuts now (its condition unmet, or done) cannot run.
        if (s_settings.live && ((e.required_flag > 0 && !gs::event_flag(e.required_flag)) ||
                                (e.done_flag > 0 && gs::event_flag(e.done_flag)))) {
            continue;
        }
        Marker m;
        m.kind = is_door(e) ? Kind::Door : Kind::Event;
        m.index = i;
        m.pos = point_pos(e.point);
        m.label = event_label(i, e);
        m.search = lower(m.label + " " + flag_text(e.required_flag) + " " + flag_text(e.done_flag) + " " +
                         item_text(e.required_item) + " " + sys_state_name(ev_sys_state(e)) + " " + kKindNames[static_cast<int>(m.kind)]);
        if (const HandlerInfo* h = handler_of(e)) m.search += " " + lower(h->symbol);
        out.push_back(std::move(m));
    }
    for (int row = 0; row < sh::maps::kSpawnRowCount; ++row) {
        const sh::maps::SpawnPoint& s = rt().spawn(row);
        if (s.flags == 0) continue;
        Marker m;
        m.kind = Kind::Spawn;
        m.index = row;
        m.pos = {metres(s.x), y, metres(s.z)};
        m.label = spawn_chara_name(row) + " [" + std::to_string(row / sh::maps::kSpawnSlots) + ":" +
                  std::to_string(row % sh::maps::kSpawnSlots) + "] " + difficulty_name(s.difficulty_min);
        m.search = lower(m.label + " spawn");
        out.push_back(std::move(m));
    }
    sh::world::WorldObjectRuntime& wo = sh::world::world_object_runtime();
    if (wo.map() == rt().name()) {
        const auto objects = wo.describe();
        for (std::size_t i = 0; i < objects.size(); ++i) {
            const auto& o = objects[i];
            if (s_settings.live && !o.queued) continue;  // not drawn now (its cells or flags)
            Marker m;
            m.kind = Kind::Object;
            m.index = static_cast<int>(i);
            m.pos = {metres(o.position[0]), metres(o.position[1]), metres(o.position[2])};
            m.label = o.name + " (" + o.model + ")";
            m.search = lower(m.label + " object");
            if (i < s_extras.objects.size())
                for (const auto& f : s_extras.objects[i].flags_set) m.search += " " + lower(f);
            if (i < s_extras.objects.size())
                for (const auto& f : s_extras.objects[i].flags_clear) m.search += " " + lower(f);
            out.push_back(std::move(m));
        }
    }
    const auto& zones = rt().camera_zones();
    for (std::size_t i = 0; i < zones.size(); ++i) {
        const cam::CameraRoad& c = zones[i];
        Marker m;
        m.kind = Kind::Camera;
        m.index = static_cast<int>(i);
        m.area = true;
        m.min_x = c.switch_area.min_x / 16.0;
        m.max_x = c.switch_area.max_x / 16.0;
        m.min_z = c.switch_area.min_z / 16.0;
        m.max_z = c.switch_area.max_z / 16.0;
        m.pos = {(m.min_x + m.max_x) * 0.5, y, (m.min_z + m.max_z) * 0.5};
        m.label = "cam " + std::to_string(i) + " " + camera_move_name(static_cast<int>(c.camera_move)) + "/" +
                  road_type_name(static_cast<int>(c.road_type));
        m.search = lower(m.label + " camera");
        out.push_back(std::move(m));
    }
    const auto boxes = collision_boxes();
    for (std::size_t i = 0; i < boxes.size(); ++i) {
        const auto& b = boxes[i];
        Marker m;
        m.kind = Kind::Collision;
        m.index = static_cast<int>(i);
        m.area = true;
        m.min_x = b.x;
        m.max_x = b.x + static_cast<double>(b.size_x);
        m.min_z = b.z;
        m.max_z = b.z + static_cast<double>(b.size_z);
        m.pos = {(m.min_x + m.max_x) * 0.5, y, (m.min_z + m.max_z) * 0.5};
        char text[64];
        std::snprintf(text, sizeof(text), "trigger %zu h %.1f m", i, b.height_steps * 0.5);
        m.label = text;
        m.search = lower(m.label + " collision");
        out.push_back(std::move(m));
    }
    // The scripts' live things: their pickups (not taken), their marks, the characters in the world now.
    s_live.clear();
    for (const sh::world::Pickup& p : sh::world::pickups().all()) {
        if (p.resolved.flag > 0 && gs::event_flag(p.resolved.flag)) continue;
        LiveThing t;
        t.type = 0;
        t.pos = {metres(p.spec.position[0]), p.spec.ground ? y : metres(p.spec.position[1]),
                 metres(p.spec.position[2])};
        t.label = "pickup " + p.spec.id + " (" + p.spec.item + ")";
        t.detail = "flag " + p.spec.flag + (p.spec.unseen ? ", drawn by a world object" : ", model " + p.spec.model);
        s_live.push_back(std::move(t));
    }
    for (const sh::world::ScriptMark& k : sh::world::script_marks()) {
        LiveThing t;
        t.type = 1;
        t.pos = {metres(k.x), y, metres(k.z)};
        t.radius = metres(k.radius);
        t.label = k.label + (k.kind.empty() ? std::string() : " [" + k.kind + "]");
        // The id of a mark already carries its mod's prefix (the script API adds it to a short name).
        t.detail = k.owner.empty() || k.id.rfind(k.owner + ":", 0) == 0 ? k.id : k.owner + ":" + k.id;
        s_live.push_back(std::move(t));
    }
    for (int slot = 1; slot <= gs::kNpcSlots; ++slot) {
        const int id = gs::chara_id(slot);
        if (id <= 0) continue;
        const sh::entities::Vec3 q = gs::chara_position(slot);
        LiveThing t;
        t.type = 2;
        t.pos = {metres(q.x), metres(q.y), metres(q.z)};
        const sh::chara::CharaLibrary* lib = Pc_CharaData_Library();
        const sh::chara::CharacterDef* def = lib != nullptr ? lib->find(id) : nullptr;
        t.label = (def != nullptr ? def->folder : std::string("chara")) + " #" + std::to_string(slot);
        t.detail = "character slot " + std::to_string(slot);
        s_live.push_back(std::move(t));
    }
    for (std::size_t i = 0; i < s_live.size(); ++i) {
        Marker m;
        m.kind = Kind::Live;
        m.index = static_cast<int>(i);
        m.pos = s_live[i].pos;
        m.label = s_live[i].label;
        m.search = lower(m.label + " " + s_live[i].detail + " live");
        out.push_back(std::move(m));
    }
    if (s_rooms.map == rt().name() && s_rooms.size > 0) {
        // One marker per room seen: the centre of its sampled cells.
        std::map<int, std::array<double, 5>> acc;  // sum x, sum z, count, min/max not needed
        for (int z = 0; z < s_rooms.size; ++z)
            for (int x = 0; x < s_rooms.size; ++x) {
                const int room = s_rooms.rooms[static_cast<std::size_t>(z * s_rooms.size + x)];
                if (room == 0) continue;
                auto& a = acc[room];
                a[0] += s_rooms.origin_x + x + 0.5;
                a[1] += s_rooms.origin_z + z + 0.5;
                a[2] += 1;
            }
        for (const auto& [room, a] : acc) {
            Marker m;
            m.kind = Kind::Room;
            m.index = room;
            m.pos = {a[0] / a[2], y, a[1] / a[2]};
            m.label = "room " + std::to_string(room);
            m.search = m.label;
            out.push_back(std::move(m));
        }
    }
    return out;
}

bool kind_enabled(Kind k) { return overlay_enabled(kOverlayIds[static_cast<int>(k)]); }

double distance_xz(const Vec3& a, const Vec3& b) { return std::hypot(a.x - b.x, a.z - b.z); }

double distance_to_marker(const Marker& m, const Vec3& from) {
    if (!m.area) return distance_xz(m.pos, from);
    const double dx = std::max({m.min_x - from.x, 0.0, from.x - m.max_x});
    const double dz = std::max({m.min_z - from.z, 0.0, from.z - m.max_z});
    return std::hypot(dx, dz);
}

bool is_selected(Kind k, int index) {
    return s_selection.valid && s_selection.map == rt().name() && s_selection.kind == k && s_selection.index == index;
}

void select_marker(Kind k, int index) { s_selection = {true, rt().name(), k, index}; }

// The markers of this frame, built once for every overlay, the picking and the inspector.
const std::vector<Marker>& markers() {
    static std::vector<Marker> cache;
    static int frame = -1;
    static std::string map;
    const int now = ImGui::GetCurrentContext() ? ImGui::GetFrameCount() : -2;
    if (now != frame || now < 0 || map != rt().name()) {
        cache = collect_markers();
        frame = now;
        map = rt().name();
    }
    return cache;
}

void fly_to(const Vec3& p) {
    double yaw = 0;
    camera_angles(&yaw, nullptr);
    const double r = yaw * kPi / 180.0;
    place_camera({p.x - 6.0 * std::sin(r), p.y - 2.5, p.z - 6.0 * std::cos(r)}, yaw);
}

}  // namespace

std::vector<RoomStop> room_stops() {
    std::vector<RoomStop> out;
    if (!rt().loaded()) return out;
    const sh::maps::MapPointRecord* points = rt().points();
    for (int i = 0; i < sh::maps::kMapPoints; ++i) {
        const sh::maps::MapPointRecord& p = points[i];
        if (p.x == 0 && p.z == 0) continue;
        RoomStop s;
        s.point = i;
        s.room = rt().has_rooms() ? rt().room_at(p.x, p.z) : 0;
        s.x = metres(p.x);
        s.z = metres(p.z);
        // A room is entered through several doors: its first point stands for it.
        const bool seen = s.room != 0 && std::any_of(out.begin(), out.end(), [&s](const RoomStop& o) { return o.room == s.room; });
        if (!seen) out.push_back(s);
    }
    return out;
}

int current_room_stop(const std::vector<RoomStop>& stops) {
    const sh::entities::Vec3 p = gs::chara_position(0);
    const int room = rt().has_rooms() ? rt().room_at(p.x, p.z) : 0;
    int best = -1;
    double best_d = 0;
    for (std::size_t i = 0; i < stops.size(); ++i) {
        if (room != 0 && stops[i].room == room) return static_cast<int>(i);
        const double dx = stops[i].x - metres(p.x), dz = stops[i].z - metres(p.z);
        const double d = dx * dx + dz * dz;
        if (best < 0 || d < best_d) {
            best = static_cast<int>(i);
            best_d = d;
        }
    }
    return best;
}

namespace {

// The ground query's answer where no collision cell is loaded: 8 m below the origin.
constexpr std::int32_t kNoGround = 8 * 4096;
// Frames a stop waits for its floor before it counts as a place without one.
constexpr int kFloorWaitFrames = 60;

struct SettlingStop {
    bool active = false;
    bool on_floor = false;
    std::int32_t x = 0, z = 0;
    sh::entities::Vec3 from{};  // where the player stood before the stop
    int waited = 0;
    int search = -1;  // looking for a floor: the index of the stop tried, -1 for a single stop
};
SettlingStop s_stop;

void start_stop(std::int32_t x, std::int32_t z, const sh::entities::Vec3& from, int search) {
    s_stop = SettlingStop{};
    s_stop.active = true;
    s_stop.x = x;
    s_stop.z = z;
    s_stop.from = from;
    s_stop.search = search;
    // Half a second of game: the room around the new place loads and the player stands in it.
    sh::devtools::tool_mode().run_frames = 30;
    room_stop_tick();
}

}  // namespace

void go_to_room_stop(const RoomStop& stop) {
    const std::int32_t x = q12(stop.x), z = q12(stop.z);
    defer([x, z] { start_stop(x, z, gs::chara_position(0), -1); });
}

void teleport_player(double x, double z) {
    const std::int32_t qx = q12(x), qz = q12(z);
    defer([qx, qz] { start_stop(qx, qz, gs::chara_position(0), -1); });
}

int map_event_count() { return event_count(); }

EventInfo map_event_info(int index) {
    EventInfo info;
    if (index < 0 || index >= event_count()) return info;
    const sh::maps::MapEventRecord& e = rt().events()[index];
    info.label = event_label(index, e);
    info.point = e.point;
    info.trigger_type = e.trigger_type();
    info.door = is_door(e);
    info.open = !((e.required_flag > 0 && !gs::event_flag(e.required_flag)) ||
                  (e.done_flag > 0 && gs::event_flag(e.done_flag)));
    return info;
}

std::string map_spawn_label(int row) {
    const sh::maps::SpawnPoint& s = rt().spawn(row);
    return spawn_chara_name(row) + " [" + std::to_string(row / sh::maps::kSpawnSlots) + ":" +
           std::to_string(row % sh::maps::kSpawnSlots) + "] " + difficulty_name(s.difficulty_min);
}

std::string data_source(const std::string& relative) { return source_of(relative); }

void stand_on_floor() {
    const sh::entities::Vec3 p = gs::chara_position(0);
    if (s_stop.active || gs::ground_height(p.x, p.z) != kNoGround) return;
    const std::vector<RoomStop> stops = room_stops();
    if (stops.empty()) return;
    start_stop(q12(stops[0].x), q12(stops[0].z), p, 0);
}

void room_stop_tick() {
    if (!s_stop.active) return;
    sh::devtools::ToolMode& mode = sh::devtools::tool_mode();
    if (!s_stop.on_floor) {
        // A place far from the player has no collision cells yet, and the ground query answers "8 m down" there: the
        // player is held at the stop at his old height while the frames load its cells, and set onto the floor once
        // it is there.
        const bool known = gs::ground_height(s_stop.x, s_stop.z) != kNoGround;
        gs::place_player(s_stop.x, s_stop.z, known ? nullptr : &s_stop.from.y);
        if (known) {
            s_stop.on_floor = true;
        } else if (++s_stop.waited > kFloorWaitFrames) {
            const std::vector<RoomStop> stops = s_stop.search >= 0 ? room_stops() : std::vector<RoomStop>{};
            const int next = s_stop.search + 1;
            if (next > 0 && next < static_cast<int>(stops.size())) {
                start_stop(q12(stops[next].x), q12(stops[next].z), s_stop.from, next);
                return;
            }
            const std::int32_t y = s_stop.from.y;
            gs::place_player(s_stop.from.x, s_stop.from.z, &y);
            SH_LOG("[EDITOR] no floor at (%.2f, %.2f)%s", metres(s_stop.x), metres(s_stop.z),
                   s_stop.search >= 0 ? " nor at any room stop of the map" : "");
            s_stop.active = false;
            return;
        } else {
            mode.run_frames = std::max(mode.run_frames, 2);
        }
    }
    if (mode.run_frames <= 1) {
        const sh::entities::Vec3 p = gs::chara_position(0);
        fly_to({metres(p.x), metres(p.y), metres(p.z)});
        s_stop.active = false;
    }
}

namespace {

// --- drawing ---

constexpr Color kSelected{1.0f, 1.0f, 1.0f, 1.0f};

Color colour_of(const sh::maps::MapEventRecord& e) {
    if (const HandlerInfo* h = handler_of(e)) {
        if (h->shape == "item") return {1.0f, 0.85f, 0.2f, 1.0f};
        if (h->shape == "cutscene") return {1.0f, 0.4f, 0.9f, 1.0f};
        return {0.7f, 0.5f, 1.0f, 1.0f};
    }
    switch (ev_sys_state(e)) {
        case 7: return {0.4f, 0.9f, 1.0f, 1.0f};
        case 11: return {0.6f, 1.0f, 0.6f, 1.0f};
        case 12: return {0.5f, 0.7f, 1.0f, 1.0f};
        case 4: return {1.0f, 0.5f, 0.5f, 1.0f};
        default: return {0.85f, 0.85f, 0.85f, 1.0f};
    }
}

void rect_xz(Draw3D& d, double x0, double x1, double z0, double z1, double y, const Color& c, float w) {
    d.line({x0, y, z0}, {x1, y, z0}, c, w);
    d.line({x1, y, z0}, {x1, y, z1}, c, w);
    d.line({x1, y, z1}, {x0, y, z1}, c, w);
    d.line({x0, y, z1}, {x0, y, z0}, c, w);
}

// The trigger shape of an event around its point (event_triggers.cpp geometry, in metres).
void draw_trigger_shape(Draw3D& d, const sh::maps::MapEventRecord& e, const Color& c, float w) {
    const sh::maps::MapPointRecord& p = rt().points()[e.point];
    const Vec3 at = point_pos(e.point);
    const double a = p.param0() * 2.0 * kPi / 256.0;
    switch (e.trigger_type()) {
        case 1: {
            const double hx = p.param0() * 0.25, hz = p.param1() * 0.25;
            d.box({at.x - hx, at.y, at.z - hz}, {at.x + hx, at.y - 1.0, at.z + hz}, c, w);
            break;
        }
        case 2:
            d.circle_xz(at, kFacingReach, c, 24, w);
            break;
        case 3: {
            const double h = p.param1() / 16.0;
            const Vec3 a0{at.x + std::cos(a) * h, at.y, at.z - std::sin(a) * h};
            const Vec3 a1{at.x - std::cos(a) * h, at.y, at.z + std::sin(a) * h};
            d.line(a0, a1, c, w + 1.0f);
            d.line({a0.x, at.y - 1.0, a0.z}, {a1.x, at.y - 1.0, a1.z}, c, w);
            d.line(a0, {a0.x, at.y - 1.0, a0.z}, c, w);
            d.line(a1, {a1.x, at.y - 1.0, a1.z}, c, w);
            break;
        }
        case 4: {
            const double hw = p.param1() / 16.0;
            const double fx = std::sin(a), fz = std::cos(a), sx = std::cos(a), sz = -std::sin(a);
            const Vec3 c0{at.x + sx * hw, at.y, at.z + sz * hw}, c1{at.x - sx * hw, at.y, at.z - sz * hw};
            const Vec3 c2{c1.x + fx * kOrientedBoxDepth, at.y, c1.z + fz * kOrientedBoxDepth};
            const Vec3 c3{c0.x + fx * kOrientedBoxDepth, at.y, c0.z + fz * kOrientedBoxDepth};
            d.line(c0, c1, c, w + 1.0f);
            d.line(c1, c2, c, w);
            d.line(c2, c3, c, w);
            d.line(c3, c0, c, w);
            break;
        }
        default:
            d.cross(at, 0.3, c, w);
            break;
    }
    d.line(at, {at.x, at.y - 1.2, at.z}, c, w);
}

template <typename Fn>
void each_visible(Kind k, Fn&& fn) {
    const Vec3 eye = camera_position();
    for (const Marker& m : markers()) {
        if (m.kind != k) continue;
        const double dist = distance_to_marker(m, eye);
        if (dist > s_settings.distance && !is_selected(m.kind, m.index)) continue;
        fn(m, dist);
    }
}

void maybe_label(Draw3D& d, const Marker& m, double dist, const Vec3& at, const Color& c) {
    if (is_selected(m.kind, m.index) || (s_settings.labels && dist <= s_settings.label_distance)) d.label(at, m.label, c);
}

void draw_events_common(Draw3D& d, Kind k) {
    each_visible(k, [&](const Marker& m, double dist) {
        const sh::maps::MapEventRecord& e = rt().events()[m.index];
        const bool sel = is_selected(m.kind, m.index);
        Color c = k == Kind::Door ? Color{1.0f, 0.55f, 0.1f, 1.0f} : colour_of(e);
        if (sel) c = kSelected;
        draw_trigger_shape(d, e, c, sel ? 3.0f : 1.5f);
        if (k == Kind::Door && ev_sys_state(e) == 5 && gs::map_name(ev_map_idx(e)) != nullptr &&
            rt().name() == gs::map_name(ev_map_idx(e))) {
            // A door within the map: the line to where it puts the player.
            d.line(m.pos, point_pos(ev_param(e)), c, 1.0f);
        }
        maybe_label(d, m, dist, {m.pos.x, m.pos.y - 1.35, m.pos.z}, c);
    });
}

void draw_events(Draw3D& d) { draw_events_common(d, Kind::Event); }
void draw_doors(Draw3D& d) { draw_events_common(d, Kind::Door); }

void draw_spawns(Draw3D& d) {
    each_visible(Kind::Spawn, [&](const Marker& m, double dist) {
        const sh::maps::SpawnPoint& s = rt().spawn(m.index);
        const bool sel = is_selected(m.kind, m.index);
        const Color c = sel ? kSelected : (m.index < sh::maps::kSpawnSlots ? Color{1.0f, 0.3f, 0.3f, 1.0f}
                                                                           : Color{1.0f, 0.5f, 0.75f, 1.0f});
        const double a = s.rotation_y * 2.0 * kPi / 256.0;
        d.circle_xz(m.pos, 0.5, c, 20, sel ? 3.0f : 2.0f);
        d.line(m.pos, {m.pos.x + std::sin(a) * 0.9, m.pos.y, m.pos.z + std::cos(a) * 0.9}, c, 2.0f);
        d.line(m.pos, {m.pos.x, m.pos.y - 1.6, m.pos.z}, c, 1.5f);
        maybe_label(d, m, dist, {m.pos.x, m.pos.y - 1.75, m.pos.z}, c);
    });
}

void draw_objects(Draw3D& d) {
    each_visible(Kind::Object, [&](const Marker& m, double dist) {
        const bool sel = is_selected(m.kind, m.index);
        const Color c = sel ? kSelected : Color{0.3f, 1.0f, 0.8f, 1.0f};
        const double h = 0.2;
        d.box({m.pos.x - h, m.pos.y - h, m.pos.z - h}, {m.pos.x + h, m.pos.y + h, m.pos.z + h}, c, sel ? 3.0f : 1.5f);
        maybe_label(d, m, dist, {m.pos.x, m.pos.y - 0.35, m.pos.z}, c);
    });
}

void draw_cameras(Draw3D& d) {
    const int active = active_camera_zone();
    const auto& zones = rt().camera_zones();
    each_visible(Kind::Camera, [&](const Marker& m, double dist) {
        const cam::CameraRoad& z = zones[static_cast<std::size_t>(m.index)];
        const bool sel = is_selected(m.kind, m.index), on = m.index == active;
        const Color c = sel ? kSelected : on ? Color{0.2f, 1.0f, 0.2f, 1.0f} : Color{0.45f, 0.6f, 1.0f, 0.7f};
        const double y = m.pos.y;
        rect_xz(d, m.min_x, m.max_x, m.min_z, m.max_z, y, c, sel || on ? 3.0f : 1.2f);
        // The road area: where the camera itself may travel, at its height limits.
        const Color road{c.r, c.g, c.b, c.a * 0.6f};
        rect_xz(d, z.road_area.min_x / 16.0, z.road_area.max_x / 16.0, z.road_area.min_z / 16.0,
                z.road_area.max_z / 16.0, z.road_min_y / 16.0, road, 1.0f);
        if (on || sel || (s_settings.labels && dist <= s_settings.label_distance))
            d.label(m.pos, m.label + (on ? "  (active)" : ""), c);
    });
    // The game camera itself and where it looks (Q12 state of the rig, frozen while the editor is open).
    const cam::CameraState& st = cam::game_camera().state();
    const Vec3 eye{metres(st.cam_pos.x), metres(st.cam_pos.y), metres(st.cam_pos.z)};
    const Vec3 look{metres(st.watch_target_pos.x), metres(st.watch_target_pos.y), metres(st.watch_target_pos.z)};
    const Color gc{0.2f, 1.0f, 0.2f, 1.0f};
    d.cross(eye, 0.3, gc, 2.0f);
    d.line(eye, look, gc, 1.5f);
    d.cross(look, 0.15, gc, 1.5f);
    d.label(eye, "game camera", gc);
}

void draw_collision(Draw3D& d) {
    const auto boxes = collision_boxes();
    const sh::world::ActiveTriggers& near = sh::world::active_triggers();
    const sh::maps::CollisionTriggerRecord* list = rt().collision_triggers();
    each_visible(Kind::Collision, [&](const Marker& m, double dist) {
        const auto& b = boxes[static_cast<std::size_t>(m.index)];
        bool active = false;
        for (int i = 0; i < near.count; ++i)
            if (reinterpret_cast<const void*>(near.near[i]) == static_cast<const void*>(&list[m.index])) active = true;
        const bool sel = is_selected(m.kind, m.index);
        const Color c = sel ? kSelected : active ? Color{1.0f, 0.9f, 0.2f, 1.0f} : Color{1.0f, 0.6f, 0.2f, 0.75f};
        const double y = m.pos.y;
        d.box({m.min_x, y, m.min_z}, {m.max_x, y - b.height_steps * 0.5, m.max_z}, c, sel ? 3.0f : 1.2f);
        maybe_label(d, m, dist, m.pos, c);
    });
}

void draw_rooms(Draw3D& d) {
    if (!rt().has_rooms()) return;
    // Resample when the camera has moved a few metres or the map changed.
    const Vec3 eye = camera_position();
    const int radius = std::clamp(static_cast<int>(s_settings.distance), 8, 60);
    const int cx = static_cast<int>(std::floor(eye.x / 4.0)) * 4, cz = static_cast<int>(std::floor(eye.z / 4.0)) * 4;
    if (!s_rooms_pending && (s_rooms.map != rt().name() || s_rooms.size != radius * 2 ||
                             s_rooms.origin_x != cx - radius || s_rooms.origin_z != cz - radius)) {
        s_rooms_pending = true;
        const std::string map = rt().name();
        defer([map, cx, cz, radius] {
            s_rooms_pending = false;
            if (rt().name() == map) sample_rooms(map, cx, cz, radius);
        });
    }
    if (s_rooms.map != rt().name() || s_rooms.size == 0) return;
    const double y = ground_y();
    const Color c{1.0f, 1.0f, 0.4f, 0.8f};
    const int n = s_rooms.size;
    auto at = [&](int x, int z) { return s_rooms.rooms[static_cast<std::size_t>(z * n + x)]; };
    for (int z = 0; z < n; ++z)
        for (int x = 0; x < n; ++x) {
            const double x0 = s_rooms.origin_x + x, z0 = s_rooms.origin_z + z;
            if (x + 1 < n && at(x, z) != at(x + 1, z)) d.line({x0 + 1, y, z0}, {x0 + 1, y, z0 + 1}, c, 2.0f);
            if (z + 1 < n && at(x, z) != at(x, z + 1)) d.line({x0, y, z0 + 1}, {x0 + 1, y, z0 + 1}, c, 2.0f);
        }
    each_visible(Kind::Room, [&](const Marker& m, double) {
        d.label(m.pos, m.label, is_selected(m.kind, m.index) ? kSelected : c);
    });
}

void draw_live(Draw3D& d) {
    each_visible(Kind::Live, [&](const Marker& m, double dist) {
        if (m.index < 0 || static_cast<std::size_t>(m.index) >= s_live.size()) return;
        const LiveThing& t = s_live[static_cast<std::size_t>(m.index)];
        const bool sel = is_selected(m.kind, m.index);
        static const Color kTypes[] = {
            {1.0f, 0.95f, 0.3f, 1.0f}, {0.85f, 0.4f, 1.0f, 1.0f}, {1.0f, 0.25f, 0.25f, 1.0f}};
        const Color c = sel ? kSelected : kTypes[std::clamp(t.type, 0, 2)];
        if (t.type == 1) {
            d.circle_xz(m.pos, t.radius > 0.05 ? t.radius : 0.4, c, 28, sel ? 3.0f : 1.5f);
            d.line(m.pos, {m.pos.x, m.pos.y - 1.0, m.pos.z}, c, 1.5f);
        } else if (t.type == 0) {
            d.cross(m.pos, 0.25, c, sel ? 3.0f : 2.0f);
        } else {
            d.circle_xz(m.pos, 0.45, c, 16, 2.0f);
            d.line(m.pos, {m.pos.x, m.pos.y - 1.7, m.pos.z}, c, 1.5f);
        }
        maybe_label(d, m, dist, {m.pos.x, m.pos.y - 1.2, m.pos.z}, c);
    });
}

// --- selection ---

std::optional<Marker> find_marker(Kind k, int index) {
    for (const Marker& m : markers())
        if (m.kind == k && m.index == index) return m;
    return std::nullopt;
}

bool on_pick(const Pick& pick) {
    if (pick.button != 0 || !rt().loaded()) return false;
    const Vec3 eye = camera_position();
    const double hit_t = pick.hit ? std::hypot(pick.point.x - pick.ray_origin.x, pick.point.y - pick.ray_origin.y,
                                               pick.point.z - pick.ray_origin.z)
                                  : 1e9;
    const Marker* best = nullptr;
    double best_d = 1e9;
    const std::vector<Marker>& all = markers();
    // Point markers near the ray first.
    for (const Marker& m : all) {
        if (m.area || !kind_enabled(m.kind) || distance_to_marker(m, eye) > s_settings.distance) continue;
        const double vx = m.pos.x - pick.ray_origin.x, vy = m.pos.y - pick.ray_origin.y, vz = m.pos.z - pick.ray_origin.z;
        const double t = vx * pick.ray_dir.x + vy * pick.ray_dir.y + vz * pick.ray_dir.z;
        if (t <= 0 || t > hit_t + 3.0) continue;
        const double px = vx - t * pick.ray_dir.x, py = vy - t * pick.ray_dir.y, pz = vz - t * pick.ray_dir.z;
        const double off = std::sqrt(px * px + py * py + pz * pz);
        if (off < std::max(0.6, 0.04 * t) && off < best_d) {
            best_d = off;
            best = &m;
        }
    }
    // Else the smallest enabled area under the picked point.
    if (best == nullptr && pick.hit) {
        double best_area = 1e18;
        for (const Marker& m : all) {
            if (!m.area || !kind_enabled(m.kind)) continue;
            if (pick.point.x < m.min_x || pick.point.x > m.max_x || pick.point.z < m.min_z || pick.point.z > m.max_z)
                continue;
            const double a = (m.max_x - m.min_x) * (m.max_z - m.min_z);
            if (a < best_area) {
                best_area = a;
                best = &m;
            }
        }
    }
    if (best == nullptr) return false;
    select_marker(best->kind, best->index);
    return true;
}

// --- inspector ---

char s_search[96] = "";

void copy_row(const char* id, const char* label, const std::string& text) {
    ImGui::PushID(id);
    if (ImGui::SmallButton("Copy")) copy_to_clipboard(text);
    ImGui::PopID();
    ImGui::SameLine();
    ImGui::TextUnformatted(label);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", text.c_str());
    ImGui::PopStyleColor();
}

void field(const char* name, const std::string& value) {
    ImGui::TextDisabled("%-16s", name);
    ImGui::SameLine();
    ImGui::TextUnformatted(value.empty() ? "-" : value.c_str());
}

void field(const char* name, long long value) { field(name, std::to_string(value)); }

std::string kind_source(const char* kind_file) {
    const std::string base = "maps/" + rt().name() + "/";
    return source_of(base + kind_file) + "\n(or " + source_of(base + "map.json") + ")";
}

dt::EventEntry event_entry(int i) {
    const sh::maps::MapEventRecord& e = rt().events()[i];
    dt::EventEntry x;
    x.index = i;
    x.required_flag = flag_text(e.required_flag);
    x.complete_flag = flag_text(e.done_flag);
    x.trigger_type = e.trigger_type();
    x.activation_type = e.trigger >> 4;
    x.map_point = e.point;
    x.required_item = item_text(e.required_item);
    x.sys_state = ev_sys_state(e);
    x.param = ev_param(e);
    x.transition_flags = static_cast<int>((e.action >> 13) & 0x3F);
    x.sfx_pair = static_cast<int>((e.action >> 19) & 0x1F);
    x.camera_rail = static_cast<int>((e.action >> 24) & 1);
    x.map_idx = ev_map_idx(e);
    if (const char* m = gs::map_name(x.map_idx)) x.map = m;
    return x;
}

dt::PointEntry point_entry(int i) {
    const sh::maps::MapPointRecord& p = rt().points()[i];
    return {i, p.x, p.z, p.paper_map(), p.loading_screen(), p.param0(), p.param1(), p.weather_on_enter()};
}

void inspect_event(int i) {
    const sh::maps::MapEventRecord& e = rt().events()[i];
    const dt::EventEntry x = event_entry(i);
    field("kind", sys_state_name(x.sys_state));
    field("trigger", std::string(shape_name(x.trigger_type)) + " / " + activation_name(x.activation_type));
    field("map point", x.map_point);
    field("required flag", x.required_flag);
    field("complete flag", x.complete_flag);
    field("required item", x.required_item);
    field("sys_state / param", std::to_string(x.sys_state) + " / " + std::to_string(x.param));
    field("transition", std::to_string(x.transition_flags) + "  sfx pair " + std::to_string(x.sfx_pair) +
                            "  rail " + std::to_string(x.camera_rail));
    if (is_door(e)) field("target", door_target(e));
    const HandlerInfo* h = handler_of(e);
    if (h) {
        field("handler", handler_name(*h));
        field("handler symbol", h->symbol);
        field("handler shape", h->shape);
    }
    const Vec3 p = point_pos(e.point);
    ImGui::Text("point %.3f, %.3f m", p.x, p.z);
    ImGui::SeparatorText("Source");
    ImGui::TextWrapped("%s", kind_source("events.json").c_str());
    if (h) ImGui::TextWrapped("%s", (source_of("maps/" + rt().name() + "/handlers.json") + ", events.lua").c_str());
    ImGui::SeparatorText("Copy as");
    copy_row("ev", "events.json entry", dt::event_json(x));
    copy_row("pt", "points.json entry (its point)", dt::point_json(point_entry(e.point)));
    if (h) {
        copy_row("hn", "handler name", handler_name(*h));
        copy_row("hl", "events.lua function", dt::handler_lua(handler_name(*h), ev_param(e), h->shape));
    }
    if (!x.complete_flag.empty()) copy_row("fl", "complete flag", x.complete_flag);
}

void inspect_spawn(int row) {
    const sh::maps::SpawnPoint& s = rt().spawn(row);
    const char* own = sh::convert::mapdata::chara_name_of(s.chara);
    dt::SpawnEntry x{s.x, s.z, s.rotation_y, own ? own : std::to_string(s.chara), s.flags, s.difficulty_min};
    field("character", spawn_chara_name(row));
    field("group / slot", std::to_string(row / sh::maps::kSpawnSlots) + " / " + std::to_string(row % sh::maps::kSpawnSlots));
    field("slot chara", x.chara);
    field("entry state", s.flags);
    field("difficulty", std::string(difficulty_name(s.difficulty_min)) + " (difficulty_min " +
                            std::to_string(s.difficulty_min) + ")");
    ImGui::Text("%.3f, %.3f m   rot %.1f deg", metres(s.x), metres(s.z), s.rotation_y * 360.0 / 256.0);
    ImGui::TextDisabled("As loaded: spawn_overrides.json and spawn_variants are applied.");
    ImGui::SeparatorText("Source");
    ImGui::TextWrapped("%s", kind_source("spawns.json").c_str());
    ImGui::TextWrapped("%s", source_of("maps/" + rt().name() + "/spawn_overrides.json").c_str());
    ImGui::SeparatorText("Copy as");
    copy_row("sj", "spawns.json slot", dt::spawn_json(x));
    copy_row("sl", "event script spawn (Lua)", dt::spawn_lua(x, spawn_chara_name(row)));
}

void inspect_object(int i) {
    const auto objects = sh::world::world_object_runtime().describe();
    if (i >= static_cast<int>(objects.size())) return;
    const auto& o = objects[static_cast<std::size_t>(i)];
    const sh::world::WorldObjectDef* def =
        i < static_cast<int>(s_extras.objects.size()) && s_extras.objects[static_cast<std::size_t>(i)].name == o.name
            ? &s_extras.objects[static_cast<std::size_t>(i)]
            : nullptr;
    field("name", o.name);
    field("model", o.model);
    if (!o.item_model.empty()) field("item model", o.item_model);
    ImGui::Text("%.4f, %.4f, %.4f m", metres(o.position[0]), metres(o.position[1]), metres(o.position[2]));
    ImGui::Text("rot %.2f, %.2f, %.2f deg", o.rotation[0] * 360.0 / 4096.0, o.rotation[1] * 360.0 / 4096.0,
                o.rotation[2] * 360.0 / 4096.0);
    field("drawn now", o.queued ? "yes" : "no");
    if (!o.pose_resolved) field("pose", "not resolved (never drawn)");
    dt::WorldObjectEntry x;
    x.name = o.name;
    x.model = def ? def->model : o.model;
    x.position = o.position;
    x.rotation = o.rotation;
    if (def) {
        for (const auto& z : def->zones) x.zones.push_back({z.x_cells, z.z_cells});
        x.flags_set = def->flags_set;
        x.flags_clear = def->flags_clear;
        x.script = def->script;
        x.pc_near_pickup = def->pc_near_pickup;
        x.item_model = def->item_model;
        x.item_model_scale = def->item_model_scale;
        if (def->pose) {
            x.pose_table = def->pose->disc_table;
            x.pose_index = def->pose->index;
        }
        std::string flags;
        for (const auto& f : def->flags_set) flags += "+" + f + " ";
        for (const auto& f : def->flags_clear) flags += "-" + f + " ";
        field("flags", flags);
        field("zones", std::to_string(def->zones.size()) + (def->zones.empty() ? " (everywhere)" : ""));
        field("script", def->script ? "yes" : "no");
    }
    ImGui::SeparatorText("Source");
    ImGui::TextWrapped("%s", source_of("maps/" + rt().name() + "/world_objects.json").c_str());
    ImGui::SeparatorText("Copy as");
    copy_row("wo", "world_objects.json entry", dt::world_object_json(x));
    if (x.pose_table.empty()) {
        // The same object at its live place (a pose object's file entry keeps its disc reference).
    } else {
        dt::WorldObjectEntry placed = x;
        placed.pose_table.clear();
        copy_row("wp", "world_objects.json entry, placed here", dt::world_object_json(placed));
    }
    char lua[256];
    std::snprintf(lua, sizeof(lua), "sh.world_object.position_set(\"%s\", %d, %d, %d)", o.name.c_str(), o.position[0],
                  o.position[1], o.position[2]);
    copy_row("wl", "script move (Lua, \"script\": true objects)", lua);
}

void inspect_camera(int i) {
    const auto& zones = rt().camera_zones();
    if (i >= static_cast<int>(zones.size())) return;
    const cam::CameraRoad& z = zones[static_cast<std::size_t>(i)];
    dt::CameraEntry x;
    x.index = i;
    x.switch_area_q4 = {z.switch_area.min_x, z.switch_area.max_x, z.switch_area.min_z, z.switch_area.max_z};
    x.road_area_q4 = {z.road_area.min_x, z.road_area.max_x, z.road_area.min_z, z.road_area.max_z};
    x.flags = z.flags;
    x.area_size = static_cast<int>(z.area_size);
    x.road_type = static_cast<int>(z.road_type);
    x.move_y = z.move_y_type;
    x.camera_move = static_cast<int>(z.camera_move);
    x.road_max_y_q4 = z.road_max_y;
    x.road_min_y_q4 = z.road_min_y;
    x.watch_offset_y_q4 = z.watch_offset_y;
    x.lens_flare = z.lens_flare;
    x.fix_angle_x = z.fix_angle_x;
    x.fix_angle_y = z.fix_angle_y;
    field("active", i == active_camera_zone() ? "yes (the game camera runs on it)" : "no");
    field("type", std::string(camera_move_name(x.camera_move)) + " / " + road_type_name(x.road_type));
    char buf[160];
    std::snprintf(buf, sizeof(buf), "x %.2f..%.2f  z %.2f..%.2f", x.switch_area_q4[0] / 16.0, x.switch_area_q4[1] / 16.0,
                  x.switch_area_q4[2] / 16.0, x.switch_area_q4[3] / 16.0);
    field("switch area", buf);
    std::snprintf(buf, sizeof(buf), "x %.2f..%.2f  z %.2f..%.2f  y %.2f..%.2f", x.road_area_q4[0] / 16.0,
                  x.road_area_q4[1] / 16.0, x.road_area_q4[2] / 16.0, x.road_area_q4[3] / 16.0, x.road_max_y_q4 / 16.0,
                  x.road_min_y_q4 / 16.0);
    field("road area", buf);
    std::snprintf(buf, sizeof(buf), "%.2f m", x.watch_offset_y_q4 / 16.0);
    field("look-at offset y", buf);
    field("fixed angles", std::to_string(x.fix_angle_x) + ", " + std::to_string(x.fix_angle_y) + " (Q8)");
    field("flags", x.flags);
    ImGui::SeparatorText("Source");
    ImGui::TextWrapped("%s", kind_source("cameras.json").c_str());
    ImGui::SeparatorText("Copy as");
    copy_row("cj", "cameras.json entry", dt::camera_json(x));
}

void inspect_collision(int i) {
    const auto boxes = collision_boxes();
    if (i >= static_cast<int>(boxes.size())) return;
    const auto& b = boxes[static_cast<std::size_t>(i)];
    dt::CollisionEntry x{i, b.x, b.z, static_cast<int>(b.size_x), static_cast<int>(b.size_z),
                         static_cast<int>(b.height_steps)};
    field("corner", std::to_string(b.x) + ", " + std::to_string(b.z) + " m");
    field("size", std::to_string(b.size_x) + " x " + std::to_string(b.size_z) + " m");
    char h[32];
    std::snprintf(h, sizeof(h), "%.1f m", b.height_steps * 0.5);
    field("height", h);
    char g[32];
    std::snprintf(g, sizeof(g), "0x%04X", sh::world::active_triggers().groups);
    field("groups in force", g);
    ImGui::SeparatorText("Source");
    ImGui::TextWrapped("%s", kind_source("collision.json").c_str());
    ImGui::SeparatorText("Copy as");
    copy_row("tj", "collision.json trigger", dt::collision_json(x));
}

void inspect_room(int room) {
    field("room", room);
    ImGui::TextWrapped("Rooms come from rooms.json (zones by hook rule, else the disc's 40 m grid).");
    ImGui::SeparatorText("Source");
    ImGui::TextWrapped("%s", source_of("maps/" + rt().name() + "/rooms.json").c_str());
    ImGui::SeparatorText("Copy as");
    copy_row("rn", "room number", std::to_string(room));
}

void draw_inspector() {
    // The host opens panels with a zero height; a list and a field table need room the first time.
    static bool sized = false;
    if (!sized) {
        ImGui::SetWindowSize(ImVec2(ImGui::GetFontSize() * 34.0f, ImGui::GetFontSize() * 46.0f), ImGuiCond_Always);
        sized = true;
    }
    if (!rt().loaded()) {
        ImGui::TextDisabled("No map loaded.");
        return;
    }
    ImGui::SeparatorText("Overlays");
    for (int k = 0; k < static_cast<int>(Kind::Count); ++k) {
        bool on = overlay_enabled(kOverlayIds[k]);
        if (k % 4) ImGui::SameLine();
        if (ImGui::Checkbox(kKindNames[k], &on)) set_overlay_enabled(kOverlayIds[k], on);
    }
    ImGui::SliderFloat("distance (m)", &s_settings.distance, 5.0f, 200.0f, "%.0f");
    ImGui::SliderFloat("labels within (m)", &s_settings.label_distance, 0.0f, 100.0f, "%.0f");
    ImGui::Checkbox("labels", &s_settings.labels);
    ImGui::SameLine();
    ImGui::Checkbox("live", &s_settings.live);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("What runs now: events and world objects the flags shut are left out;\n"
                          "the scripts' pickups, marks (sh.world.mark) and the characters in the world are shown.");
    ImGui::SameLine();
    if (ImGui::SmallButton("Reload names")) {
        const std::string map = rt().name();
        defer([map] { load_extras(map); });
    }

    ImGui::SeparatorText("Find");
    ImGui::InputTextWithHint("##find", "name, handler, flag, chara, model", s_search, sizeof(s_search));
    if (s_search[0] != '\0') {
        const std::string q = lower(s_search);
        if (ImGui::BeginChild("found", ImVec2(0, ImGui::GetTextLineHeightWithSpacing() * 8), ImGuiChildFlags_Borders)) {
            int shown = 0;
            for (const Marker& m : markers()) {
                if (m.search.find(q) == std::string::npos) continue;
                if (++shown > 200) break;
                ImGui::PushID(shown);
                if (ImGui::SmallButton("Go")) {
                    select_marker(m.kind, m.index);
                    fly_to(m.pos);
                    set_overlay_enabled(kOverlayIds[static_cast<int>(m.kind)], true);
                }
                ImGui::SameLine();
                if (ImGui::Selectable((std::string(kKindNames[static_cast<int>(m.kind)]) + "  " + m.label).c_str(),
                                      is_selected(m.kind, m.index)))
                    select_marker(m.kind, m.index);
                ImGui::PopID();
            }
            if (shown == 0) ImGui::TextDisabled("nothing matches");
        }
        ImGui::EndChild();
    }

    ImGui::SeparatorText("Selection");
    if (!s_selection.valid || s_selection.map != rt().name()) {
        ImGui::TextWrapped("Click near a marker of an enabled overlay (or inside a camera zone / collision box), or "
                           "find one above.");
        return;
    }
    const std::optional<Marker> m = find_marker(s_selection.kind, s_selection.index);
    if (!m) {
        ImGui::TextDisabled("The selected marker is gone.");
        return;
    }
    ImGui::Text("%s  %s", kKindNames[static_cast<int>(m->kind)], m->label.c_str());
    if (ImGui::SmallButton("Fly to")) fly_to(m->pos);
    ImGui::SameLine();
    if (ImGui::SmallButton("Deselect")) s_selection.valid = false;
    switch (m->kind) {
        case Kind::Event:
        case Kind::Door: inspect_event(m->index); break;
        case Kind::Spawn: inspect_spawn(m->index); break;
        case Kind::Object: inspect_object(m->index); break;
        case Kind::Camera: inspect_camera(m->index); break;
        case Kind::Collision: inspect_collision(m->index); break;
        case Kind::Room: inspect_room(m->index); break;
        case Kind::Live:
            if (m->index >= 0 && static_cast<std::size_t>(m->index) < s_live.size()) {
                const LiveThing& t = s_live[static_cast<std::size_t>(m->index)];
                ImGui::TextUnformatted(t.label.c_str());
                ImGui::TextDisabled("%s", t.detail.c_str());
                ImGui::Text("at %.2f, %.2f, %.2f", t.pos.x, t.pos.y, t.pos.z);
            }
            break;
        case Kind::Count: break;
    }
}

int kind_of(const std::string& name) {
    for (int k = 0; k < static_cast<int>(Kind::Count); ++k)
        if (name == kKindNames[k]) return k;
    return -1;
}

// editormap: the overlays, the selection and the search by name, for scripted runs (-consolecmd).
void register_command() {
    using sh::console::ArgSpec;
    using sh::console::ArgType;
    sh::console::CommandSpec spec;
    spec.name = "editormap";
    spec.group = "debug";
    spec.help = "map overlays of the editor: overlay <kind|all> <on|off>, select <kind> <index>, find <text>, "
                "distance <m>";
    spec.details = "kinds: event door spawn object camera collision room. docs/modding/tools.md.";
    spec.args = {ArgSpec{"action", ArgType::Enum, false, "what to do", {"overlay", "select", "find", "distance"}, {}},
                 ArgSpec{"a", ArgType::String, false, "kind, text or metres", {}, {}},
                 ArgSpec{"b", ArgType::String, true, "on/off or index", {}, {}}};
    spec.run = [](sh::console::Context& ctx, const sh::console::Args& args) -> Result<void> {
        const std::string& action = args.str(0);
        const std::string a = args.str(1);
        const std::string b = args.has(2) ? args.str(2) : std::string();
        if (action == "overlay") {
            const bool on = b != "off" && b != "0";
            for (int k = 0; k < static_cast<int>(Kind::Count); ++k)
                if (a == "all" || a == kKindNames[k]) set_overlay_enabled(kOverlayIds[k], on);
        } else if (action == "select") {
            const int k = kind_of(a);
            if (k < 0) {
                ctx.out.error("editormap: unknown kind " + a);
                return Result<void>::ok();
            }
            const int index = std::atoi(b.c_str());
            defer([k, index] {
                if (const auto m = find_marker(static_cast<Kind>(k), index)) select_marker(m->kind, m->index);
            });
        } else if (action == "find") {
            const std::string q = lower(a);
            defer([q] {
                for (const Marker& m : collect_markers()) {
                    if (m.search.find(q) == std::string::npos) continue;
                    select_marker(m.kind, m.index);
                    set_overlay_enabled(kOverlayIds[static_cast<int>(m.kind)], true);
                    fly_to(m.pos);
                    break;
                }
            });
        } else if (action == "distance") {
            s_settings.distance = static_cast<float>(std::atof(a.c_str()));
        }
        return Result<void>::ok();
    };
    (void)sh::console::global_registry().add(std::move(spec));
}

struct Install {
    Install() {
        register_panel({"map.inspector", "Map Inspector", "Data", &draw_inspector, true, false});
        register_overlay({kOverlayIds[0], "Map events and pickups", &draw_events, true});
        register_overlay({kOverlayIds[1], "Doors and map transitions", &draw_doors, true});
        register_overlay({kOverlayIds[2], "Spawn points", &draw_spawns, true});
        register_overlay({kOverlayIds[3], "World objects", &draw_objects, false});
        register_overlay({kOverlayIds[4], "Camera zones", &draw_cameras, false});
        register_overlay({kOverlayIds[5], "Collision triggers", &draw_collision, false});
        register_overlay({kOverlayIds[6], "Rooms", &draw_rooms, false});
        register_overlay({kOverlayIds[7], "Scripts: pickups, marks, characters", &draw_live, true});
        register_selection_handler("map.inspector", 100, &on_pick);
        register_command();
    }
};

const Install s_install;

}  // namespace

}  // namespace sh::tools
