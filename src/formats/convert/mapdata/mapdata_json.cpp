// JSON of gamedata/maps/<map>/*.json.

#include "formats/common/json.h"
#include "formats/convert/mapdata/mapdata.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

namespace sh::convert::mapdata {

namespace {

using json::Value;
using json::Writer;

constexpr int kVersion = 1;

constexpr const char* kCharaNames[] = {
    "None",          "Harry",          "AirScreamer",      "NightFlutter", "Groaner",        "Wormhead",
    "LarvalStalker", "Stalker",        "GreyChild",        "Mumbler",      "HangedScratcher", "Creeper",
    "Romper",        "Chicken",        "SplitHead",        "Floatstinger", "PuppetNurse",    "DummyNurse",
    "PuppetDoctor",  "DummyDoctor",    "Twinfeeler",       "Bloodsucker",  "Incubus",        "Unknown23",
    "MonsterCybil",  "LockerDeadBody", "Cybil",            "EndingCybil",  "Cheryl",         "Cat",
    "Dahlia",        "EndingDahlia",   "Lisa",             "BloodyLisa",   "Alessa",         "GhostChildAlessa",
    "Incubator",     "BloodyIncubator", "Kaufmann",        "EndingKaufmann", "Flauros",      "LittleIncubus",
    "GhostDoctor",   "Parasite",       "Padlock",
};

// e_Chara of a character name ("GreyChild"); -1 when unknown. The name is the
// identifier of a spawn entry; the number survives in
// the files only for ids the table does not know.
int chara_number(const char* name) {
    if (name != nullptr) {
        for (std::size_t i = 0; i < std::size(kCharaNames); ++i) {
            if (std::strcmp(name, kCharaNames[i]) == 0) {
                return static_cast<int>(i);
            }
        }
    }
    return -1;
}

// The character of a spawn entry: the name where the table knows one, the raw
// id otherwise (a mod's own numbers must round-trip exactly).
void member_chara(Writer& w, std::string_view key, int id) {
    if (id >= 0 && id < static_cast<int>(std::size(kCharaNames))) {
        w.member(key, kCharaNames[id]);
    } else {
        w.member(key, id);
    }
}

const char* format_name(Kind kind) {
    switch (kind) {
        case Kind::Header: return "sh-map-header";
        case Kind::Spawns: return "sh-map-spawns";
        case Kind::Points: return "sh-map-points";
        case Kind::Events: return "sh-map-events";
        case Kind::Cameras: return "sh-map-cameras";
        case Kind::Collision: return "sh-map-collision";
    }
    return "";
}

std::string hex(std::uint32_t v) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "0x%X", v);
    return buf;
}

double q12_m(std::int32_t q) { return q / 4096.0; }
double q4_m(std::int32_t q) { return q / 16.0; }
double q8_deg(std::int32_t q) { return q * 360.0 / 256.0; }

void begin_file(Writer& w, const MapData& d, Kind kind, std::uint32_t offset, std::uint32_t va) {
    w.begin_object();
    w.member("format", format_name(kind));
    w.member("version", kVersion);
    w.member("map", d.map);
    w.key("source").inline_next().begin_object();
    w.member("file", overlay_path(d.map));
    w.member("offset", hex(offset));
    w.member("va", hex(va));
    w.end_object();
}

void member_m(Writer& w, std::string_view key, double v) {
    w.key(key).value_shortest(v);
}

IdNaming& naming() {
    static IdNaming n;
    return n;
}

// "silenthill:M1S03_ShotgunShells0" -> "M1S03_ShotgunShells0": the base game's names are written short.
std::string_view short_id(std::string_view id) {
    constexpr std::string_view kBase = "silenthill:";
    return id.starts_with(kBase) ? id.substr(kBase.size()) : id;
}

// An event flag (0 = none, -1 = no flag) or an item (0 = none) by name where it has one.
void member_named(Writer& w, std::string_view key, int number, const char* (*name_of)(int)) {
    const char* name = number > 0 && name_of != nullptr ? name_of(number) : nullptr;
    if (name != nullptr) {
        w.member(key, short_id(name));
    } else {
        w.member(key, number);
    }
}

void member_flag(Writer& w, std::string_view key, int bit) { member_named(w, key, bit, naming().flag_name); }
void member_item(Writer& w, std::string_view key, int item) { member_named(w, key, item, naming().item_name); }

std::string map_name_by_index(int idx);

// e_MapIdx of a map name ("map1_s03"), -1 when unknown.
int map_index_by_name(std::string_view map) {
    const std::span<const MapLayout> layouts = map_layouts();
    for (std::size_t i = 0; i < layouts.size(); ++i) {
        if (layouts[i].map == map) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

// Index of the last entry that is not all zero, + 1.
template <typename T, std::size_t N>
std::size_t used_count(const std::array<T, N>& a) {
    std::size_t n = N;
    const T zero{};
    while (n > 0 && std::memcmp(&a[n - 1], &zero, sizeof(T)) == 0) {
        --n;
    }
    return n;
}

void write_header(Writer& w, const MapData& d) {
    begin_file(w, d, Kind::Header, kHeaderOffset, kOverlayBase + kHeaderOffset);
    w.member("bgm", d.header.bgmIdx);
    w.member("ambient_audio", d.header.ambientAudioIdx);
    w.member("ambient_tint", d.header.field16);
    w.member("weather", d.header.field17);
    w.member("unused_8", d.header.unused8);
    if (d.header.pad9[0] || d.header.pad9[1] || d.header.pad9[2]) {
        w.key("pad_9").inline_next().begin_array();
        for (std::uint8_t b : d.header.pad9) {
            w.value(b);
        }
        w.end_array();
    }
    w.end_object();
}

void write_spawn(Writer& w, const ShMapSpawn& s) {
    const ShMapSpawn zero{};
    if (std::memcmp(&s, &zero, sizeof s) == 0) {
        w.null();
        return;
    }
    w.inline_next().begin_object();
    member_m(w, "x", q12_m(s.positionX));
    member_m(w, "z", q12_m(s.positionZ));
    w.key("rotation_deg").value_shortest(q8_deg(s.rotationY));
    member_chara(w, "chara", s.charaId);
    w.member("flags", s.flags);
    w.member("difficulty_min", s.gameDifficultyMin);
    w.member("x_q12", s.positionX);
    w.member("z_q12", s.positionZ);
    w.member("rotation_q8", s.rotationY);
    if (s.pad7) {
        w.member("pad_7", s.pad7);
    }
    w.end_object();
}

void write_spawns(Writer& w, const MapData& d) {
    begin_file(w, d, Kind::Spawns, kHeaderOffset + 0x248, kOverlayBase + kHeaderOffset + 0x248);
    w.key("groups").begin_array();
    for (int g = 0; g < SH_MAP_SPAWN_GROUPS; ++g) {
        w.begin_object();
        member_chara(w, "chara", d.spawns.charaGroupIds[g]);
        int last = SH_MAP_SPAWN_SLOTS;
        const ShMapSpawn zero{};
        while (last > 0 && std::memcmp(&d.spawns.slots[g][last - 1], &zero, sizeof zero) == 0) {
            --last;
        }
        w.key("slots").begin_array();
        for (int s = 0; s < last; ++s) {
            write_spawn(w, d.spawns.slots[g][s]);
        }
        w.end_array();
        w.end_object();
    }
    w.end_array();
    if (d.spawns.charaGroupIds[2] || d.spawns.charaGroupIds[3]) {
        w.key("extra_group_ids").inline_next().begin_array();
        w.value(d.spawns.charaGroupIds[2]).value(d.spawns.charaGroupIds[3]);
        w.end_array();
    }
    w.end_object();
}

// The two shape bytes of a map point (the original's triggerParam0/1) mean different things per use; points.json names
// them by the use the map's events make of the point (kPointsDescription).
constexpr const char* kPointsDescription =
    "Map points: positions that events use as trigger shapes and as landing points. x, z in metres (x_q12, z_q12 "
    "the same in Q12, 4096 = 1 m); paper_map, loading_screen and weather_on_enter (0 keeps the weather, n sets "
    "weather n - 1) apply when the player lands on the point. The two shape bytes are named by the point's use: "
    "most points carry heading_q8 (a direction, 256 per turn: 0 faces +Z, 64 faces +X) and half_width_q4 (a half "
    "width in 1/16 m). A facing-segment trigger (trigger_type 3, doors) is a segment through the point across "
    "heading_q8, half_width_q4 long on each side; an oriented-box trigger (trigger_type 4) is a box in front of the "
    "point up to 4 m deep along heading_q8 and half_width_q4 wide on each side; a facing-point trigger (trigger_type "
    "2) uses neither. Landing on a point faces heading_q8 and half_width_q4 picks the placement: 0 on the point, 1 "
    "keeps the player's offset from the door used, 2 or more 0.4 m in front of the point. A point used by an "
    "axis-aligned box trigger (trigger_type 1) carries half_x_q2 and half_z_q2 instead: the box's half extents "
    "along X and Z in quarter metres. Either pair reads for any point (the old keys param0 / param1 too).";

// True when an axis-aligned box trigger uses point `index`; its bytes are then the box's half extents.
bool box_point(const MapData& d, std::size_t index) {
    return std::any_of(d.events.begin(), d.events.end(),
                       [&](const ShMapEvent& e) { return e.triggerType == 1 && e.mapPointIdx == index; });
}

void write_point_shape(Writer& w, const MapData& d, std::size_t index, const ShMapPoint& p) {
    const bool box = box_point(d, index);
    w.member(box ? "half_x_q2" : "heading_q8", p.triggerParam0);
    w.member(box ? "half_z_q2" : "half_width_q4", p.triggerParam1);
}

void write_points(Writer& w, const MapData& d, const MapLayout& layout) {
    begin_file(w, d, Kind::Points, layout.points_va - kOverlayBase, layout.points_va);
    w.member("description", kPointsDescription);
    w.key("points").begin_array();
    for (std::size_t i = 0; i < d.points.size(); ++i) {
        const ShMapPoint& p = d.points[i];
        w.inline_next().begin_object();
        w.member("index", i);
        member_m(w, "x", q12_m(p.positionX));
        member_m(w, "z", q12_m(p.positionZ));
        w.member("paper_map", p.paperMapIdx);
        w.member("loading_screen", p.loadingScreenId);
        write_point_shape(w, d, i, p);
        w.member("weather_on_enter", p.weatherOnEnter);
        w.member("x_q12", p.positionX);
        w.member("z_q12", p.positionZ);
        if (p.unused4_12) {
            w.member("unused_4_12", p.unused4_12);
        }
        w.end_object();
    }
    w.end_array();
    w.end_object();
}

void write_events(Writer& w, const MapData& d, const MapLayout& layout) {
    begin_file(w, d, Kind::Events, layout.events_va - kOverlayBase, layout.events_va);
    w.key("events").begin_array();
    for (std::size_t i = 0; i < d.events.size(); ++i) {
        const ShMapEvent& e = d.events[i];
        w.inline_next().begin_object();
        w.member("index", i);
        member_flag(w, "required_flag", e.requiredFlag);
        member_flag(w, "complete_flag", e.completeFlag);
        w.member("trigger_type", e.triggerType);
        w.member("activation_type", e.activationType);
        w.member("map_point", e.mapPointIdx);
        member_item(w, "required_item", e.requiredItemId);
        w.member("sys_state", e.sysState);
        w.member("param", e.eventParam);
        w.member("transition_flags", e.transitionFlags);
        w.member("sfx_pair", e.sfxPairIdx);
        w.member("camera_rail", e.setCameraRail);
        if (e.sysState == 5 && map_name_by_index(e.mapIdx) != "?") {
            w.member("map", map_name_by_index(e.mapIdx));  // a door: the target map by name
        } else {
            w.member("map_idx", e.mapIdx);
        }
        if (e.pad7) {
            w.member("pad_7", e.pad7);
        }
        if (e.bit31) {
            w.member("bit_31", e.bit31);
        }
        w.end_object();
    }
    w.end_array();
    w.end_object();
}

void write_area(Writer& w, std::string_view key, const std::int16_t (&a)[4]) {
    w.key(key).inline_next().begin_array();
    for (std::int16_t v : a) {
        w.value_shortest(q4_m(v));
    }
    w.end_array();
    w.key(std::string(key) + "_q4").inline_next().begin_array();
    for (std::int16_t v : a) {
        w.value(v);
    }
    w.end_array();
}

void write_cameras(Writer& w, const MapData& d) {
    begin_file(w, d, Kind::Cameras, kHeaderOffset + 0x3CC, kOverlayBase + kHeaderOffset + 0x3CC);
    w.member("capacity", static_cast<int>(SH_MAP_CAMERA_PATHS));
    w.key("cameras").begin_array();
    const std::size_t n = used_count(d.cameras);
    for (std::size_t i = 0; i < n; ++i) {
        const ShMapCamera& c = d.cameras[i];
        w.inline_next().begin_object();
        w.member("index", i);
        write_area(w, "switch_area", c.switchArea);
        write_area(w, "road_area", c.roadArea);
        w.member("flags", c.flags);
        w.member("area_size", c.areaSizeType);
        w.member("road_type", c.roadType);
        w.member("move_y", c.moveYType);
        w.member("camera_move", c.cameraMoveType);
        member_m(w, "road_max_y", q4_m(c.roadMaxY));
        member_m(w, "road_min_y", q4_m(c.roadMinY));
        member_m(w, "watch_offset_y", q4_m(c.watchOffsetY));
        w.member("road_max_y_q4", c.roadMaxY);
        w.member("road_min_y_q4", c.roadMinY);
        w.member("watch_offset_y_q4", c.watchOffsetY);
        w.member("lens_flare", c.lensFlare);
        w.member("fix_angle_x", c.fixAngleX);
        w.member("fix_angle_y", c.fixAngleY);
        w.end_object();
    }
    w.end_array();
    w.end_object();
}

void write_collision(Writer& w, const MapData& d) {
    begin_file(w, d, Kind::Collision, kHeaderOffset + 0xD2C, kOverlayBase + kHeaderOffset + 0xD2C);
    w.member("capacity", static_cast<int>(SH_MAP_COLLISION_COUNT));
    w.key("triggers").begin_array();
    const std::size_t n = used_count(d.collision);
    for (std::size_t i = 0; i < n; ++i) {
        const ShMapCollision& t = d.collision[i];
        w.inline_next().begin_object();
        w.member("index", i);
        w.member("x", t.positionX);
        w.member("z", t.positionZ);
        w.member("size_x", t.sizeX);
        w.member("size_z", t.sizeZ);
        w.member("height_half_m", t.height);
        w.member("end", t.isEndOfArray != 0);
        w.end_object();
    }
    w.end_array();
    w.end_object();
}

// --- reading ---

class Reader {
public:
    Reader(std::vector<std::string>& warnings) : warnings_(warnings) {}

    std::string error;

    bool fail(const Value& at, const std::string& path, const std::string& message) {
        if (error.empty()) {
            error = "line " + std::to_string(at.line()) + ", column " + std::to_string(at.column()) + ": " + path +
                    ": " + message;
        }
        return false;
    }

    const Value* member(const Value& obj, const std::string& path, std::string_view key, bool required = true) {
        const Value* v = obj.find(key);
        if (!v && required) {
            fail(obj, path, "missing \"" + std::string(key) + "\"");
        }
        return v;
    }

    bool integer(const Value& obj, const std::string& path, std::string_view key, std::int64_t lo, std::int64_t hi,
                 std::int64_t& out, bool required = true) {
        const Value* v = member(obj, path, key, required);
        if (!v) {
            if (!required) {
                out = 0;
                return true;
            }
            return false;
        }
        return check_int(*v, path + "." + std::string(key), lo, hi, out);
    }

    // A number or the name of one (an event flag, an item: IdNaming).
    bool named(const Value& obj, const std::string& path, std::string_view key, std::int64_t lo, std::int64_t hi,
               std::int64_t& out, bool required, int (*number_of)(const char*), const char* what) {
        const Value* v = member(obj, path, key, required);
        if (!v || !v->is_string()) {
            return integer(obj, path, key, lo, hi, out, required);
        }
        const int number = number_of != nullptr ? number_of(v->string().c_str()) : -1;
        if (number < 0) {
            return fail(*v, path + "." + std::string(key), std::string("unknown ") + what + " \"" + v->string() + "\"");
        }
        out = number;
        if (out < lo || out > hi) {
            return fail(*v, path + "." + std::string(key), v->string() + " is outside " + std::to_string(lo) + ".." +
                                                            std::to_string(hi));
        }
        return true;
    }

    bool check_int(const Value& v, const std::string& path, std::int64_t lo, std::int64_t hi, std::int64_t& out) {
        if (v.is_bool()) {
            out = v.boolean() ? 1 : 0;
        } else if (!v.is_integer()) {
            return fail(v, path, std::string("expected an integer, found ") + Value::type_name(v.type()));
        } else {
            out = v.integer();
        }
        if (out < lo || out > hi) {
            return fail(v, path, std::to_string(out) + " is outside " + std::to_string(lo) + ".." + std::to_string(hi));
        }
        return true;
    }

    // A value in a human unit (`human`, times `scale` gives the game's integer) or the game's integer (`raw`).
    bool fixed(const Value& obj, const std::string& path, std::string_view human, std::string_view raw, double scale,
               std::int64_t lo, std::int64_t hi, std::int64_t& out) {
        const Value* h = obj.find(human);
        const Value* r = obj.find(raw);
        if (!h && !r) {
            return fail(obj, path, "missing \"" + std::string(human) + "\"");
        }
        std::int64_t raw_value = 0;
        if (r && !check_int(*r, path + "." + std::string(raw), lo, hi, raw_value)) {
            return false;
        }
        if (!h) {
            out = raw_value;
            return true;
        }
        if (!h->is_number()) {
            return fail(*h, path + "." + std::string(human),
                        std::string("expected a number, found ") + Value::type_name(h->type()));
        }
        const double scaled = h->number() * scale;
        if (!std::isfinite(scaled) || scaled < static_cast<double>(lo) - 0.5 || scaled > static_cast<double>(hi) + 0.5) {
            return fail(*h, path + "." + std::string(human),
                        json::format_number(h->number()) + " is outside " + json::format_number(lo / scale) + ".." +
                            json::format_number(hi / scale));
        }
        out = std::llround(scaled);
        if (out < lo) out = lo;
        if (out > hi) out = hi;
        if (r && raw_value != out) {
            warnings_.push_back("line " + std::to_string(h->line()) + ": " + path + ": \"" + std::string(human) +
                                "\" gives " + std::to_string(out) + ", \"" + std::string(raw) + "\" is " +
                                std::to_string(raw_value) + "; using \"" + std::string(human) + "\"");
        }
        return true;
    }

    const Value* array(const Value& obj, const std::string& path, std::string_view key, std::size_t max_size) {
        const Value* v = member(obj, path, key);
        if (!v) {
            return nullptr;
        }
        if (!v->is_array()) {
            fail(*v, path + "." + std::string(key), std::string("expected an array, found ") + Value::type_name(v->type()));
            return nullptr;
        }
        if (v->array().size() > max_size) {
            fail(*v, path + "." + std::string(key),
                 std::to_string(v->array().size()) + " entries, at most " + std::to_string(max_size));
            return nullptr;
        }
        return v;
    }

    void warn(std::string message) { warnings_.push_back(std::move(message)); }

    // A key renamed after files were written: the old name still reads, with one warning per file.
    std::string_view renamed(const Value& obj, std::string_view key, std::string_view old_key) {
        if (obj.find(key) != nullptr || obj.find(old_key) == nullptr) {
            return key;
        }
        if (!old_key_warned_) {
            old_key_warned_ = true;
            warnings_.push_back("\"" + std::string(old_key) + "\" is the old name of \"" + std::string(key) +
                                "\"; it still reads");
        }
        return old_key;
    }

    // Warnings around "chara" (the numeric "chara_id" is its older form) fire once per file.
    void warn_chara(const Value& at, const std::string& path, std::string message) {
        if (!chara_warned_) {
            chara_warned_ = true;
            warn("line " + std::to_string(at.line()) + ": " + path + ": " + std::move(message));
        }
    }

    bool object(const Value& v, const std::string& path) {
        return v.is_object() ? true : fail(v, path, std::string("expected an object, found ") + Value::type_name(v.type()));
    }

private:
    std::vector<std::string>& warnings_;
    bool old_key_warned_ = false;
    bool chara_warned_ = false;
};

template <typename T>
bool set(Reader& r, const Value& obj, const std::string& path, std::string_view key, T& field,
         std::int64_t lo, std::int64_t hi, bool required = true) {
    std::int64_t v = 0;
    if (!r.integer(obj, path, key, lo, hi, v, required)) {
        return false;
    }
    field = static_cast<T>(v);
    return true;
}

// A point's shape bytes under either name pair of kPointsDescription, or the old "param0" / "param1".
bool read_point_shape(Reader& r, const Value& v, const std::string& p, ShMapPoint& m) {
    const bool box = v.find("half_x_q2") != nullptr || v.find("half_z_q2") != nullptr;
    if (box && (v.find("heading_q8") != nullptr || v.find("half_width_q4") != nullptr)) {
        return r.fail(v, p, "half_x_q2 / half_z_q2 and heading_q8 / half_width_q4 name the same bytes");
    }
    return set(r, v, p, r.renamed(v, box ? "half_x_q2" : "heading_q8", "param0"), m.triggerParam0, 0, 255) &&
           set(r, v, p, r.renamed(v, box ? "half_z_q2" : "half_width_q4", "param1"), m.triggerParam1, 0, 255);
}

template <typename T>
bool set_flag(Reader& r, const Value& obj, const std::string& path, std::string_view key, T& field, bool required = true) {
    std::int64_t v = 0;
    if (!r.named(obj, path, key, -32768, 32767, v, required, naming().flag_number, "event flag")) {
        return false;
    }
    field = static_cast<T>(v);
    return true;
}

// The character of a spawn entry: "chara" — the name ("GreyChild"), or a number
// for ids the name table does not know — is the source of truth. The old numeric
// "chara_id" key still reads alone (one warning per file); when both are present
// the name wins and a mismatch warns.
template <typename T>
bool set_chara(Reader& r, const Value& obj, const std::string& path, T& field) {
    std::int64_t v = 0;
    if (!r.named(obj, path, "chara", -128, 127, v, false, chara_number, "character")) {
        return false;
    }
    const Value* old = obj.find("chara_id");
    if (obj.find("chara") == nullptr) {
        if (old == nullptr) {
            return r.fail(obj, path, "missing \"chara\"");
        }
        if (!r.check_int(*old, path + ".chara_id", -128, 127, v)) {
            return false;
        }
        r.warn_chara(*old, path, "\"chara_id\" is the old form of \"chara\"; it still reads");
    } else if (old != nullptr) {
        std::int64_t old_v = 0;
        if (r.check_int(*old, path + ".chara_id", -128, 127, old_v) && old_v != v) {
            r.warn_chara(*old, path, "\"chara\" gives " + std::to_string(v) + ", \"chara_id\" is " +
                                           std::to_string(old_v) + "; using \"chara\"");
        }
    }
    field = static_cast<T>(v);
    return true;
}

template <typename T>
bool set_item(Reader& r, const Value& obj, const std::string& path, std::string_view key, T& field, bool required = true) {
    std::int64_t v = 0;
    if (!r.named(obj, path, key, 0, 255, v, required, naming().item_number, "item")) {
        return false;
    }
    field = static_cast<T>(v);
    return true;
}

// "map": "map1_s03" (a door's target) or a raw "map_idx": 3. The raw number wins: a room warp
// keeps its side-table reference there beside the name of its own map.
bool read_map_idx(Reader& r, const Value& obj, const std::string& path, std::int64_t& out, bool required) {
    if (const Value* m = obj.find("map"); m != nullptr && m->is_string() && obj.find("map_idx") == nullptr) {
        const int idx = map_index_by_name(m->string());
        if (idx < 0 || idx > 63) {
            return r.fail(*m, path + ".map", "unknown map \"" + m->string() + "\"");
        }
        out = idx;
        return true;
    }
    return r.integer(obj, path, "map_idx", 0, 63, out, required);
}

template <typename T>
bool set_fixed(Reader& r, const Value& obj, const std::string& path, std::string_view human, std::string_view raw,
               double scale, T& field) {
    std::int64_t v = 0;
    if (!r.fixed(obj, path, human, raw, scale, std::numeric_limits<T>::min(), std::numeric_limits<T>::max(), v)) {
        return false;
    }
    field = static_cast<T>(v);
    return true;
}

bool read_header(Reader& r, const Value& root, MapData& d) {
    ShMapHeaderInfo h{};
    const std::string p = "header";
    bool ok = set(r, root, p, "bgm", h.bgmIdx, -128, 127) && set(r, root, p, "ambient_audio", h.ambientAudioIdx, 0, 255) &&
              set(r, root, p, "ambient_tint", h.field16, -128, 127) && set(r, root, p, "weather", h.field17, -128, 127) &&
              set(r, root, p, "unused_8", h.unused8, -128, 127, false);
    if (ok) {
        if (const Value* pad = root.find("pad_9")) {
            if (!pad->is_array() || pad->array().size() != 3) {
                return r.fail(*pad, p + ".pad_9", "expected 3 integers");
            }
            for (int i = 0; i < 3; ++i) {
                std::int64_t v = 0;
                if (!r.check_int(pad->array()[i], p + ".pad_9", 0, 255, v)) {
                    return false;
                }
                h.pad9[i] = static_cast<std::uint8_t>(v);
            }
        }
        d.header = h;
    }
    return ok;
}

bool read_spawn(Reader& r, const Value& v, const std::string& p, ShMapSpawn& s) {
    s = ShMapSpawn{};
    if (v.is_null()) {
        return true;
    }
    if (!r.object(v, p)) {
        return false;
    }
    return set_fixed(r, v, p, "x", "x_q12", 4096.0, s.positionX) && set_fixed(r, v, p, "z", "z_q12", 4096.0, s.positionZ) &&
           set_fixed(r, v, p, "rotation_deg", "rotation_q8", 256.0 / 360.0, s.rotationY) &&
           set_chara(r, v, p, s.charaId) && set(r, v, p, "flags", s.flags, -128, 127) &&
           set(r, v, p, "difficulty_min", s.gameDifficultyMin, -8, 7) && set(r, v, p, "pad_7", s.pad7, 0, 15, false);
}

bool read_spawns(Reader& r, const Value& root, MapData& d) {
    ShMapSpawns out{};
    const Value* groups = r.array(root, "spawns", "groups", SH_MAP_SPAWN_GROUPS);
    if (!groups) {
        return false;
    }
    if (groups->array().size() != SH_MAP_SPAWN_GROUPS) {
        return r.fail(*groups, "spawns.groups", "expected 2 groups");
    }
    for (int g = 0; g < SH_MAP_SPAWN_GROUPS; ++g) {
        const Value& group = groups->array()[g];
        const std::string p = "groups[" + std::to_string(g) + "]";
        if (!r.object(group, p) || !set_chara(r, group, p, out.charaGroupIds[g])) {
            return false;
        }
        const Value* slots = r.array(group, p, "slots", SH_MAP_SPAWN_SLOTS);
        if (!slots) {
            return false;
        }
        for (std::size_t s = 0; s < slots->array().size(); ++s) {
            if (!read_spawn(r, slots->array()[s], p + ".slots[" + std::to_string(s) + "]", out.slots[g][s])) {
                return false;
            }
        }
    }
    if (const Value* extra = root.find("extra_group_ids")) {
        if (!extra->is_array() || extra->array().size() != 2) {
            return r.fail(*extra, "spawns.extra_group_ids", "expected 2 integers");
        }
        for (int i = 0; i < 2; ++i) {
            std::int64_t v = 0;
            if (!r.check_int(extra->array()[i], "spawns.extra_group_ids", -128, 127, v)) {
                return false;
            }
            out.charaGroupIds[2 + i] = static_cast<std::int8_t>(v);
        }
    }
    d.spawns = out;
    return true;
}

bool read_points(Reader& r, const Value& root, const MapLayout& layout, MapData& d) {
    const Value* list = r.array(root, "points", "points", SH_MAP_POINTS_MAX);
    if (!list) {
        return false;
    }
    if (list->array().size() != layout.point_count) {
        return r.fail(*list, "points", std::to_string(list->array().size()) + " entries, the map has " +
                                           std::to_string(layout.point_count) + " (the count cannot change)");
    }
    std::vector<ShMapPoint> out(list->array().size());
    for (std::size_t i = 0; i < out.size(); ++i) {
        const Value& v = list->array()[i];
        const std::string p = "points[" + std::to_string(i) + "]";
        ShMapPoint& m = out[i];
        if (!r.object(v, p) || !set_fixed(r, v, p, "x", "x_q12", 4096.0, m.positionX) ||
            !set_fixed(r, v, p, "z", "z_q12", 4096.0, m.positionZ) || !set(r, v, p, "paper_map", m.paperMapIdx, 0, 31) ||
            !set(r, v, p, "loading_screen", m.loadingScreenId, 0, 7) || !read_point_shape(r, v, p, m) ||
            !set(r, v, p, r.renamed(v, "weather_on_enter", "field_4_5"), m.weatherOnEnter, 0, 15) ||
            !set(r, v, p, "unused_4_12", m.unused4_12, 0, 15, false)) {
            return false;
        }
    }
    d.points = std::move(out);
    return true;
}

bool read_events(Reader& r, const Value& root, const MapLayout& layout, MapData& d) {
    const Value* list = r.array(root, "events", "events", SH_MAP_EVENTS_MAX);
    if (!list) {
        return false;
    }
    if (list->array().size() != layout.event_count) {
        return r.fail(*list, "events", std::to_string(list->array().size()) + " entries, the map has " +
                                           std::to_string(layout.event_count) + " (the count cannot change)");
    }
    std::vector<ShMapEvent> out(list->array().size());
    for (std::size_t i = 0; i < out.size(); ++i) {
        const Value& v = list->array()[i];
        const std::string p = "events[" + std::to_string(i) + "]";
        ShMapEvent& e = out[i];
        std::int64_t map_idx = 0;
        if (!r.object(v, p) || !set_flag(r, v, p, "required_flag", e.requiredFlag) ||
            !set_flag(r, v, p, "complete_flag", e.completeFlag) ||
            !set(r, v, p, "trigger_type", e.triggerType, -8, 7) || !set(r, v, p, "activation_type", e.activationType, 0, 15) ||
            !set(r, v, p, "map_point", e.mapPointIdx, 0, 255) || !set_item(r, v, p, "required_item", e.requiredItemId) ||
            !set(r, v, p, "sys_state", e.sysState, 0, 31) || !set(r, v, p, "param", e.eventParam, 0, 255) ||
            !set(r, v, p, "transition_flags", e.transitionFlags, 0, 63) || !set(r, v, p, "sfx_pair", e.sfxPairIdx, 0, 31) ||
            !set(r, v, p, "camera_rail", e.setCameraRail, 0, 1) || !read_map_idx(r, v, p, map_idx, true) ||
            !set(r, v, p, "pad_7", e.pad7, 0, 255, false) || !set(r, v, p, "bit_31", e.bit31, 0, 1, false)) {
            return false;
        }
        e.mapIdx = static_cast<decltype(e.mapIdx)>(map_idx);
    }
    d.events = std::move(out);
    return true;
}

bool read_area(Reader& r, const Value& obj, const std::string& p, const char* key, std::int16_t (&out)[4]) {
    const std::string raw = std::string(key) + "_q4";
    const Value* h = obj.find(key);
    const Value* q = obj.find(raw);
    if (!h && !q) {
        return r.fail(obj, p, "missing \"" + std::string(key) + "\"");
    }
    for (const Value* a : {h, q}) {
        if (a && (!a->is_array() || a->array().size() != 4)) {
            return r.fail(*a, p, "expected 4 numbers: min x, max x, min z, max z");
        }
    }
    for (int i = 0; i < 4; ++i) {
        std::int64_t raw_value = 0;
        if (q && !r.check_int(q->array()[i], p + "." + raw, -32768, 32767, raw_value)) {
            return false;
        }
        std::int64_t v = raw_value;
        if (h) {
            const Value& e = h->array()[i];
            if (!e.is_number()) {
                return r.fail(e, p + "." + key, "expected a number");
            }
            const double scaled = e.number() * 16.0;
            if (!std::isfinite(scaled) || scaled < -32768.5 || scaled > 32767.5) {
                return r.fail(e, p + "." + key, json::format_number(e.number()) + " is outside -2048..2047.9375");
            }
            v = std::llround(scaled);
            if (q && v != raw_value) {
                r.warn("line " + std::to_string(e.line()) + ": " + p + ": \"" + key + "\" gives " + std::to_string(v) +
                       ", \"" + raw + "\" is " + std::to_string(raw_value) + "; using \"" + key + "\"");
            }
        }
        out[i] = static_cast<std::int16_t>(v);
    }
    return true;
}

bool read_cameras(Reader& r, const Value& root, MapData& d) {
    const Value* list = r.array(root, "cameras", "cameras", SH_MAP_CAMERA_PATHS);
    if (!list) {
        return false;
    }
    std::array<ShMapCamera, SH_MAP_CAMERA_PATHS> out{};
    for (std::size_t i = 0; i < list->array().size(); ++i) {
        const Value& v = list->array()[i];
        const std::string p = "cameras[" + std::to_string(i) + "]";
        ShMapCamera& c = out[i];
        if (!r.object(v, p) || !read_area(r, v, p, "switch_area", c.switchArea) ||
            !read_area(r, v, p, "road_area", c.roadArea) || !set(r, v, p, "flags", c.flags, 0, 255) ||
            !set(r, v, p, "area_size", c.areaSizeType, 0, 3) || !set(r, v, p, "road_type", c.roadType, 0, 7) ||
            !set(r, v, p, "move_y", c.moveYType, 0, 7) || !set(r, v, p, "camera_move", c.cameraMoveType, -8, 7) ||
            !set_fixed(r, v, p, "road_max_y", "road_max_y_q4", 16.0, c.roadMaxY) ||
            !set_fixed(r, v, p, "road_min_y", "road_min_y_q4", 16.0, c.roadMinY) ||
            !set_fixed(r, v, p, "watch_offset_y", "watch_offset_y_q4", 16.0, c.watchOffsetY) ||
            !set(r, v, p, "lens_flare", c.lensFlare, 0, 15) || !set(r, v, p, "fix_angle_x", c.fixAngleX, -128, 127) ||
            !set(r, v, p, "fix_angle_y", c.fixAngleY, -128, 127)) {
            return false;
        }
    }
    d.cameras = out;
    return true;
}

bool read_collision(Reader& r, const Value& root, MapData& d) {
    const Value* list = r.array(root, "collision", "triggers", SH_MAP_COLLISION_COUNT);
    if (!list) {
        return false;
    }
    std::array<ShMapCollision, SH_MAP_COLLISION_COUNT> out{};
    for (std::size_t i = 0; i < list->array().size(); ++i) {
        const Value& v = list->array()[i];
        const std::string p = "triggers[" + std::to_string(i) + "]";
        ShMapCollision& t = out[i];
        if (!r.object(v, p) || !set(r, v, p, "x", t.positionX, -512, 511) || !set(r, v, p, "z", t.positionZ, -512, 511) ||
            !set(r, v, p, "size_x", t.sizeX, 0, 15) || !set(r, v, p, "size_z", t.sizeZ, 0, 15) ||
            !set(r, v, p, "height_half_m", t.height, 0, 7) || !set(r, v, p, "end", t.isEndOfArray, 0, 1)) {
            return false;
        }
    }
    d.collision = out;
    return true;
}

// ---- unified map.json ----

// e_MapIdx order is the map_layouts() order.
std::string map_name_by_index(int idx) {
    const std::span<const MapLayout> layouts = map_layouts();
    if (idx >= 0 && static_cast<std::size_t>(idx) < layouts.size()) {
        return std::string(layouts[static_cast<std::size_t>(idx)].map);
    }
    return "?";
}

const char* event_kind(const ShMapEvent& e) {
    switch (e.sysState) {
        case 0: return "flag_gate";  // no state change: required_flag gates it, fires immediately
        case 4: return "fmv";
        case 5: return "door";       // LoadOverlay: param = arrival point, map_idx = target map
        case 6: return "room_warp";  // LoadRoom: same-room warp
        case 7: return "message";    // param = MapMsg index
        case 10: return "callback";  // param = index into g_MapEventFuncs
        case 11: return "set_flag";
        case 12: return "sound";
        default: return "other";     // menus, game over, gameplay: not scene entities
    }
}

void write_entity(Writer& w, const MapData& d, std::size_t i, std::span<const HandlerInfo> handlers) {
    const ShMapEvent& e = d.events[i];
    const char* kind = event_kind(e);
    w.inline_next().begin_object();
    w.member("id", "event_" + std::to_string(i));
    w.member("kind", kind);
    w.member("event_index", i);
    // The raw state beside the label: "other" entities (menus, game over) keep theirs
    // and a reader rebuilds the event exactly from this field.
    w.member("sys_state", e.sysState);
    w.key("trigger").inline_next().begin_object();
    w.member("type", e.triggerType);
    w.member("activation", e.activationType);
    w.member("point", e.mapPointIdx);
    w.end_object();
    member_flag(w, "required_flag", e.requiredFlag);
    member_flag(w, "complete_flag", e.completeFlag);
    if (e.requiredItemId) {
        member_item(w, "required_item", e.requiredItemId);
    }
    if (e.sysState == 5 || e.sysState == 6) {
        w.key("door").inline_next().begin_object();
        if (e.sysState == 5) {
            w.member("map", map_name_by_index(e.mapIdx));
            if (map_name_by_index(e.mapIdx) == "?") {
                w.member("map_idx", e.mapIdx);
            }
        } else {
            // LoadRoom warps inside the same map through a side table; keep the raw refs.
            w.member("map", d.map);
            w.member("map_idx", e.mapIdx);
        }
        w.member("arrival_point", e.eventParam);
        w.end_object();
    }
    w.member("param", e.eventParam);
    if (e.sysState == 7 && naming().message_key != nullptr) {
        if (const char* key = naming().message_key(d.map, e.eventParam)) {
            w.member("message", key);
        }
    }
    w.member("transition_flags", e.transitionFlags);
    w.member("sfx_pair", e.sfxPairIdx);
    w.member("camera_rail", e.setCameraRail);
    if (e.mapIdx && e.sysState != 5) {
        w.member("map_idx", e.mapIdx);
    }
    if (e.pad7) {
        w.member("pad_7", e.pad7);
    }
    if (e.bit31) {
        w.member("bit_31", e.bit31);
    }
    if (e.sysState == 10) {
        const HandlerInfo* info = nullptr;
        for (const HandlerInfo& h : handlers) {
            if (h.index == e.eventParam) {
                info = &h;
                break;
            }
        }
        if (info != nullptr) {
            w.member("handler", info->symbol);
            if (!info->name.empty() && info->name != info->symbol) {
                w.member("handler_name", info->name);
            }
            w.member("handler_shape", info->shape);
        } else {
            w.key("handler").null();
            w.key("handler_shape").null();
        }
    }
    w.end_object();
}

}  // namespace

const char* chara_name_of(int id) {
    return id >= 0 && id < static_cast<int>(std::size(kCharaNames)) ? kCharaNames[id] : nullptr;
}

int chara_number_of(const char* name) {
    return chara_number(name);
}

std::string write_json(const MapData& d, Kind kind) {
    Writer w(true);
    const MapLayout* layout = find_layout(d.map);
    const MapLayout fallback{d.map, kOverlayBase, 0, kOverlayBase, 0};
    const MapLayout& l = layout ? *layout : fallback;
    switch (kind) {
        case Kind::Header: write_header(w, d); break;
        case Kind::Spawns: write_spawns(w, d); break;
        case Kind::Points: write_points(w, d, l); break;
        case Kind::Events: write_events(w, d, l); break;
        case Kind::Cameras: write_cameras(w, d); break;
        case Kind::Collision: write_collision(w, d); break;
    }
    std::string text = w.take();
    text += '\n';
    return text;
}

Result<std::vector<HandlerInfo>> read_handlers_json(std::string_view text) {
    const Result<Value> parsed = json::parse(text);
    if (!parsed) {
        return Result<std::vector<HandlerInfo>>::fail(parsed.error());
    }
    const Value& root = parsed.value();
    const Value* list = root.find("handlers");
    if (list == nullptr || !list->is_array()) {
        return Result<std::vector<HandlerInfo>>::fail("line " + std::to_string(root.line()) +
                                                      ": handlers.json: no \"handlers\" array");
    }
    std::vector<HandlerInfo> out;
    for (std::size_t i = 0; i < list->array().size(); ++i) {
        const Value& v = list->array()[i];
        const std::string where = "handlers[" + std::to_string(i) + "]";
        if (!v.is_object()) {
            return Result<std::vector<HandlerInfo>>::fail("line " + std::to_string(v.line()) + ": " +
                                                          where + ": expected an object");
        }
        const Value* index = v.find("index");
        const Value* symbol = v.find("symbol");
        const Value* shape = v.find("shape");
        if (index == nullptr || !index->is_integer()) {
            return Result<std::vector<HandlerInfo>>::fail("line " + std::to_string(v.line()) + ": " +
                                                          where + ": bad \"index\"");
        }
        if (symbol == nullptr || symbol->is_null()) {
            continue;  // NULL slot of the function table
        }
        if (!symbol->is_string() || shape == nullptr || !shape->is_string()) {
            return Result<std::vector<HandlerInfo>>::fail("line " + std::to_string(v.line()) + ": " +
                                                          where + ": \"symbol\"/\"shape\" must be strings");
        }
        HandlerInfo info;
        info.index = static_cast<int>(index->integer());
        info.symbol = symbol->string();
        info.shape = shape->string();
        if (const Value* name = v.find("name"); name != nullptr && name->is_string()) {
            info.name = name->string();
        }
        out.push_back(std::move(info));
    }
    return Result<std::vector<HandlerInfo>>::ok(std::move(out));
}

std::string write_unified_json(const MapData& d, std::span<const HandlerInfo> handlers) {
    Writer w(true);
    w.begin_object();
    w.member("format", "sh-map-scene");
    w.member("version", kVersion);
    w.member("map", d.map);
    w.key("info").inline_next().begin_object();
    w.member("bgm", d.header.bgmIdx);
    w.member("ambient_audio", d.header.ambientAudioIdx);
    w.member("ambient_tint", d.header.field16);
    w.member("weather", d.header.field17);
    if (d.header.unused8) {
        w.member("unused_8", d.header.unused8);
    }
    if (d.header.pad9[0] || d.header.pad9[1] || d.header.pad9[2]) {
        w.key("pad_9").inline_next().begin_array();
        for (std::uint8_t b : d.header.pad9) {
            w.value(b);
        }
        w.end_array();
    }
    w.end_object();
    // The four group characters of the spawn table (names; a number is an id the
    // name table does not know); slots below carry only their own delta.
    w.key("spawn_groups").inline_next().begin_array();
    for (int g = 0; g < SH_MAP_CHARA_GROUP_IDS; ++g) {
        const int id = d.spawns.charaGroupIds[g];
        if (id >= 0 && id < static_cast<int>(std::size(kCharaNames))) {
            w.value(kCharaNames[id]);
        } else {
            w.value(id);
        }
    }
    w.end_array();
    // Points: the compact view (metres); the full record stays in points.json.
    w.key("points").begin_array();
    for (std::size_t i = 0; i < d.points.size(); ++i) {
        const ShMapPoint& p = d.points[i];
        w.inline_next().begin_object();
        w.member("index", i);
        member_m(w, "x", q12_m(p.positionX));
        member_m(w, "z", q12_m(p.positionZ));
        w.member("paper_map", p.paperMapIdx);
        w.member("loading_screen", p.loadingScreenId);
        write_point_shape(w, d, i, p);
        w.member("weather_on_enter", p.weatherOnEnter);
        if (p.unused4_12) {
            w.member("unused_4_12", p.unused4_12);
        }
        w.end_object();
    }
    w.end_array();
    w.key("cameras").begin_array();
    const std::size_t cams = used_count(d.cameras);
    for (std::size_t i = 0; i < cams; ++i) {
        const ShMapCamera& c = d.cameras[i];
        w.inline_next().begin_object();
        w.member("index", i);
        write_area(w, "switch", c.switchArea);
        write_area(w, "road", c.roadArea);
        w.member("flags", c.flags);
        w.member("area_size", c.areaSizeType);
        w.member("road_type", c.roadType);
        w.member("move_y", c.moveYType);
        w.member("camera_move", c.cameraMoveType);
        member_m(w, "road_max_y", q4_m(c.roadMaxY));
        member_m(w, "road_min_y", q4_m(c.roadMinY));
        member_m(w, "watch_offset_y", q4_m(c.watchOffsetY));
        w.member("lens_flare", c.lensFlare);
        w.member("fix_angle_x", c.fixAngleX);
        w.member("fix_angle_y", c.fixAngleY);
        w.end_object();
    }
    w.end_array();
    w.key("collision_triggers").begin_array();
    const std::size_t trigs = used_count(d.collision);
    for (std::size_t i = 0; i < trigs; ++i) {
        const ShMapCollision& t = d.collision[i];
        w.inline_next().begin_object();
        w.member("index", i);
        w.member("x", t.positionX);
        w.member("z", t.positionZ);
        w.member("size_x", t.sizeX);
        w.member("size_z", t.sizeZ);
        w.member("height_half_m", t.height);
        if (t.isEndOfArray) {
            w.member("end", true);
        }
        w.end_object();
    }
    w.end_array();
    w.key("spawns").begin_array();
    for (int g = 0; g < SH_MAP_SPAWN_GROUPS; ++g) {
        int last = SH_MAP_SPAWN_SLOTS;
        const ShMapSpawn zero{};
        while (last > 0 && std::memcmp(&d.spawns.slots[g][last - 1], &zero, sizeof zero) == 0) {
            --last;  // trailing empty slots are simply absent
        }
        for (int s = 0; s < last; ++s) {
            const ShMapSpawn& sp = d.spawns.slots[g][s];
            if (std::memcmp(&sp, &zero, sizeof sp) == 0) {
                continue;  // empty slots are absent; "slot" below keeps the positions honest
            }
            w.inline_next().begin_object();
            w.member("group", g);
            w.member("slot", s);
            // The slot's own id: 0 means "the group's character" (spawn_groups);
            // writing the resolved id here would not round-trip back to 0.
            member_chara(w, "chara", sp.charaId);
            member_m(w, "x", q12_m(sp.positionX));
            member_m(w, "z", q12_m(sp.positionZ));
            w.key("rotation_deg").value_shortest(q8_deg(sp.rotationY));
            w.member("flags", sp.flags);
            w.member("difficulty_min", sp.gameDifficultyMin);
            if (sp.pad7) {
                w.member("pad_7", sp.pad7);
            }
            w.end_object();
        }
    }
    w.end_array();
    // The overlay's alternative whole spawn tables, by the importer's names; the hand file's
    // spawn_variants refer to them. Every row is written (null = empty) so the index is the slot.
    if (!d.spawn_variant_rows.empty()) {
        w.key("spawn_variant_rows").begin_object();
        for (const SpawnVariantRows& t : d.spawn_variant_rows) {
            w.key(t.name).begin_array();
            for (const ShMapSpawn& row : t.rows) {
                write_spawn(w, row);
            }
            w.end_array();
        }
        w.end_object();
    }
    w.key("entities").begin_array();
    for (std::size_t i = 0; i < d.events.size(); ++i) {
        write_entity(w, d, i, handlers);
    }
    w.end_array();
    w.end_object();
    std::string text = w.take();
    text += '\n';
    return text;
}

Result<void> read_json(std::string_view text, Kind kind, const MapLayout& layout, MapData& d,
                       std::vector<std::string>& warnings) {
    const Result<Value> parsed = json::parse(text);
    if (!parsed) {
        return Result<void>::fail(parsed.error());
    }
    const Value& root = parsed.value();
    Reader r(warnings);
    if (!r.object(root, "file")) {
        return Result<void>::fail(r.error);
    }
    const Value* format = root.find("format");
    if (!format || !format->is_string() || format->string() != format_name(kind)) {
        return Result<void>::fail(std::string("\"format\" must be \"") + format_name(kind) + "\"");
    }
    std::int64_t version = 0;
    if (!r.integer(root, "file", "version", 1, kVersion, version)) {
        return Result<void>::fail(r.error + " (this game reads version " + std::to_string(kVersion) + ")");
    }
    if (const Value* map = root.find("map"); map && (!map->is_string() || map->string() != layout.map)) {
        return Result<void>::fail("\"map\" names another map than " + std::string(layout.map));
    }
    bool ok = false;
    switch (kind) {
        case Kind::Header: ok = read_header(r, root, d); break;
        case Kind::Spawns: ok = read_spawns(r, root, d); break;
        case Kind::Points: ok = read_points(r, root, layout, d); break;
        case Kind::Events: ok = read_events(r, root, layout, d); break;
        case Kind::Cameras: ok = read_cameras(r, root, d); break;
        case Kind::Collision: ok = read_collision(r, root, d); break;
    }
    if (!ok) {
        return Result<void>::fail(r.error);
    }
    return Result<void>::ok();
}

// ---- reading the unified map.json ----

// Rebuilds the six kinds' records from the unified scene view. The writer is lossless
// (every bit of the disc records has a member), so this gives the same MapData as the
// six separate files; a mod can therefore ship a map.json alone.
bool read_spawn_condition(Reader& r, const Value& v, const std::string& p, SpawnCondition& c, int depth);
bool read_spawn_overrides(const Value& section, std::vector<SpawnOverride>& overrides,
                          std::vector<std::string>& warnings);
bool read_spawn_variants(Reader& r, const Value& v, SpawnVariants& out);

Result<void> read_unified_json(std::string_view text, const MapLayout& layout, MapData& d,
                               std::vector<std::string>& warnings) {
    const Result<Value> parsed = json::parse(text);
    if (!parsed) {
        return Result<void>::fail(parsed.error());
    }
    const Value& root = parsed.value();
    Reader r(warnings);
    if (!r.object(root, "map.json")) {
        return Result<void>::fail(r.error);
    }
    if (const Value* format = root.find("format"); !format || !format->is_string() || format->string() != "sh-map-scene") {
        return Result<void>::fail("\"format\" must be \"sh-map-scene\"");
    }
    std::int64_t version = 0;
    if (!r.integer(root, "map.json", "version", 1, kVersion, version)) {
        return Result<void>::fail(r.error);
    }
    if (const Value* map = root.find("map"); map && (!map->is_string() || map->string() != layout.map)) {
        return Result<void>::fail("\"map\" names another map than " + std::string(layout.map));
    }

    // info -> header
    const Value* info = r.member(root, "map.json", "info");
    if (!info || !r.object(*info, "info")) {
        return Result<void>::fail(r.error);
    }
    ShMapHeaderInfo h{};
    if (!set(r, *info, "info", "bgm", h.bgmIdx, -128, 127) ||
        !set(r, *info, "info", "ambient_audio", h.ambientAudioIdx, 0, 255) ||
        !set(r, *info, "info", "ambient_tint", h.field16, -128, 127) ||
        !set(r, *info, "info", "weather", h.field17, -128, 127) ||
        !set(r, *info, "info", "unused_8", h.unused8, -128, 127, false)) {
        return Result<void>::fail(r.error);
    }
    if (const Value* pad = info->find("pad_9")) {
        if (!pad->is_array() || pad->array().size() != 3) {
            r.fail(*pad, "info.pad_9", "expected 3 integers");
            return Result<void>::fail(r.error);
        }
        for (int i = 0; i < 3; ++i) {
            std::int64_t v = 0;
            if (!r.check_int(pad->array()[i], "info.pad_9", 0, 255, v)) {
                return Result<void>::fail(r.error);
            }
            h.pad9[i] = static_cast<std::uint8_t>(v);
        }
    }
    d.header = h;

    // spawn_groups (characters, names or numbers; older files say spawn_group_ids) -> charaGroupIds
    // (optional; zero groups with no slots)
    const std::string groups_key(r.renamed(root, "spawn_groups", "spawn_group_ids"));
    if (const Value* groups = root.find(groups_key)) {
        if (!groups->is_array() || groups->array().size() != SH_MAP_CHARA_GROUP_IDS) {
            r.fail(*groups, groups_key, "expected 4 characters");
            return Result<void>::fail(r.error);
        }
        for (int i = 0; i < SH_MAP_CHARA_GROUP_IDS; ++i) {
            std::int64_t v = 0;
            const Value& e = groups->array()[i];
            if (e.is_string()) {
                const int number = chara_number(e.string().c_str());
                if (number < 0) {
                    r.fail(e, groups_key, "unknown character \"" + e.string() + "\"");
                    return Result<void>::fail(r.error);
                }
                v = number;
            } else if (!r.check_int(e, groups_key, -128, 127, v)) {
                return Result<void>::fail(r.error);
            }
            d.spawns.charaGroupIds[i] = static_cast<std::int8_t>(v);
        }
    }

    // points
    if (const Value* list = r.array(root, "map.json", "points", SH_MAP_POINTS_MAX)) {
        if (list->array().size() != layout.point_count) {
            r.fail(*list, "points", std::to_string(list->array().size()) + " entries, the map has " +
                                       std::to_string(layout.point_count));
            return Result<void>::fail(r.error);
        }
        std::vector<ShMapPoint> points(list->array().size());
        for (std::size_t i = 0; i < points.size(); ++i) {
            const Value& v = list->array()[i];
            const std::string p = "points[" + std::to_string(i) + "]";
            ShMapPoint& m = points[i];
            if (!r.object(v, p) || !set_fixed(r, v, p, "x", "x_q12", 4096.0, m.positionX) ||
                !set_fixed(r, v, p, "z", "z_q12", 4096.0, m.positionZ) ||
                !set(r, v, p, "paper_map", m.paperMapIdx, 0, 31) ||
                !set(r, v, p, "loading_screen", m.loadingScreenId, 0, 7) ||
                !read_point_shape(r, v, p, m) ||
                !set(r, v, p, r.renamed(v, "weather_on_enter", "field_4_5"), m.weatherOnEnter, 0, 15) ||
                !set(r, v, p, "unused_4_12", m.unused4_12, 0, 15, false)) {
                return Result<void>::fail(r.error);
            }
        }
        d.points = std::move(points);
    }

    // cameras
    if (const Value* list = r.array(root, "map.json", "cameras", SH_MAP_CAMERA_PATHS)) {
        std::array<ShMapCamera, SH_MAP_CAMERA_PATHS> cameras{};
        for (std::size_t i = 0; i < list->array().size(); ++i) {
            const Value& v = list->array()[i];
            const std::string p = "cameras[" + std::to_string(i) + "]";
            ShMapCamera& c = cameras[i];
            if (!r.object(v, p) || !read_area(r, v, p, "switch", c.switchArea) ||
                !read_area(r, v, p, "road", c.roadArea) || !set(r, v, p, "flags", c.flags, 0, 255) ||
                !set(r, v, p, "area_size", c.areaSizeType, 0, 3) || !set(r, v, p, "road_type", c.roadType, 0, 7) ||
                !set(r, v, p, "move_y", c.moveYType, 0, 7) || !set(r, v, p, "camera_move", c.cameraMoveType, -8, 7) ||
                !set_fixed(r, v, p, "road_max_y", "road_max_y_q4", 16.0, c.roadMaxY) ||
                !set_fixed(r, v, p, "road_min_y", "road_min_y_q4", 16.0, c.roadMinY) ||
                !set_fixed(r, v, p, "watch_offset_y", "watch_offset_y_q4", 16.0, c.watchOffsetY) ||
                !set(r, v, p, "lens_flare", c.lensFlare, 0, 15) || !set(r, v, p, "fix_angle_x", c.fixAngleX, -128, 127) ||
                !set(r, v, p, "fix_angle_y", c.fixAngleY, -128, 127)) {
                return Result<void>::fail(r.error);
            }
        }
        d.cameras = cameras;
    }

    // collision_triggers
    if (const Value* list = r.array(root, "map.json", "collision_triggers", SH_MAP_COLLISION_COUNT)) {
        std::array<ShMapCollision, SH_MAP_COLLISION_COUNT> collision{};
        for (std::size_t i = 0; i < list->array().size(); ++i) {
            const Value& v = list->array()[i];
            const std::string p = "collision_triggers[" + std::to_string(i) + "]";
            ShMapCollision& t = collision[i];
            if (!r.object(v, p) || !set(r, v, p, "x", t.positionX, -512, 511) ||
                !set(r, v, p, "z", t.positionZ, -512, 511) || !set(r, v, p, "size_x", t.sizeX, 0, 15) ||
                !set(r, v, p, "size_z", t.sizeZ, 0, 15) ||
                !set(r, v, p, "height_half_m", t.height, 0, 7) ||
                !set(r, v, p, "end", t.isEndOfArray, 0, 1, false)) {
                return Result<void>::fail(r.error);
            }
        }
        d.collision = collision;
    }

    // spawns: each entry carries its group and slot, so empty slots can be absent
    if (const Value* list = r.array(root, "map.json", "spawns", SH_MAP_SPAWN_GROUPS * SH_MAP_SPAWN_SLOTS)) {
        ShMapSpawns spawns{};
        spawns.charaGroupIds[0] = d.spawns.charaGroupIds[0];
        spawns.charaGroupIds[1] = d.spawns.charaGroupIds[1];
        spawns.charaGroupIds[2] = d.spawns.charaGroupIds[2];
        spawns.charaGroupIds[3] = d.spawns.charaGroupIds[3];
        int next_slot[SH_MAP_SPAWN_GROUPS] = {0, 0};
        for (std::size_t i = 0; i < list->array().size(); ++i) {
            const Value& v = list->array()[i];
            const std::string p = "spawns[" + std::to_string(i) + "]";
            std::int64_t group = 0;
            std::int64_t slot = -1;
            if (!r.object(v, p) || !r.integer(v, p, "group", 0, SH_MAP_SPAWN_GROUPS - 1, group) ||
                !r.integer(v, p, "slot", 0, SH_MAP_SPAWN_SLOTS - 1, slot, false)) {
                return Result<void>::fail(r.error);
            }
            if (slot < 0) {
                slot = next_slot[group];
            }
            if (slot >= SH_MAP_SPAWN_SLOTS) {
                r.fail(v, p, "group " + std::to_string(group) + " has more than " +
                                 std::to_string(SH_MAP_SPAWN_SLOTS) + " slots");
                return Result<void>::fail(r.error);
            }
            ShMapSpawn& s = spawns.slots[group][slot];
            if (!set_fixed(r, v, p, "x", "x_q12", 4096.0, s.positionX) ||
                !set_fixed(r, v, p, "z", "z_q12", 4096.0, s.positionZ) ||
                !set_fixed(r, v, p, "rotation_deg", "rotation_q8", 256.0 / 360.0, s.rotationY) ||
                !set_chara(r, v, p, s.charaId) ||
                !set(r, v, p, "flags", s.flags, -128, 127) ||
                !set(r, v, p, "difficulty_min", s.gameDifficultyMin, -8, 7) ||
                !set(r, v, p, "pad_7", s.pad7, 0, 15, false)) {
                return Result<void>::fail(r.error);
            }
            next_slot[group] = static_cast<int>(slot) + 1;
        }
        d.spawns = spawns;
    }

    // entities -> events
    const Value* entities = r.array(root, "map.json", "entities", SH_MAP_EVENTS_MAX);
    if (!entities) {
        return Result<void>::fail(r.error);
    }
    if (entities->array().size() != layout.event_count) {
        r.fail(*entities, "entities", std::to_string(entities->array().size()) + " entries, the map has " +
                                    std::to_string(layout.event_count));
        return Result<void>::fail(r.error);
    }
    std::vector<ShMapEvent> events(entities->array().size());
    for (std::size_t i = 0; i < events.size(); ++i) {
        const Value& v = entities->array()[i];
        const std::string p = "entities[" + std::to_string(i) + "]";
        ShMapEvent& e = events[i];
        if (!r.object(v, p)) {
            return Result<void>::fail(r.error);
        }
        const Value* trigger = r.member(v, p, "trigger");
        if (!trigger || !r.object(*trigger, p + ".trigger")) {
            return Result<void>::fail(r.error);
        }
        const Value* door = v.find("door");
        const Value* message = v.find("message");
        std::int64_t map_idx = 0;
        if (!r.integer(v, p, "map_idx", 0, 63, map_idx, false)) {
            return Result<void>::fail(r.error);
        }
        if (door) {
            if (!r.object(*door, p + ".door") || !read_map_idx(r, *door, p + ".door", map_idx, true)) {
                return Result<void>::fail(r.error);
            }
        }
        if (!set(r, *trigger, p + ".trigger", "type", e.triggerType, -8, 7) ||
            !set(r, *trigger, p + ".trigger", "activation", e.activationType, 0, 15) ||
            !set(r, *trigger, p + ".trigger", "point", e.mapPointIdx, 0, 255) ||
            !set_flag(r, v, p, "required_flag", e.requiredFlag) ||
            !set_flag(r, v, p, "complete_flag", e.completeFlag) ||
            !set_item(r, v, p, "required_item", e.requiredItemId, false) ||
            !set(r, v, p, "sys_state", e.sysState, 0, 31) ||
            !set(r, v, p, "param", e.eventParam, 0, 255, message == nullptr) ||
            !set(r, v, p, "transition_flags", e.transitionFlags, 0, 63) ||
            !set(r, v, p, "sfx_pair", e.sfxPairIdx, 0, 31) ||
            !set(r, v, p, "camera_rail", e.setCameraRail, 0, 1) ||
            !set(r, v, p, "pad_7", e.pad7, 0, 255, false) ||
            !set(r, v, p, "bit_31", e.bit31, 0, 1, false)) {
            return Result<void>::fail(r.error);
        }
        // A message event names its message by key; the number of "param" is the fallback.
        if (message != nullptr) {
            const int number = message->is_string() && naming().message_number != nullptr
                                   ? naming().message_number(layout.map, message->string())
                                   : -1;
            if (number >= 0 && number <= 255) {
                e.eventParam = static_cast<std::uint8_t>(number);
            } else if (v.find("param") == nullptr) {
                r.fail(*message, p + ".message", "not a message key of " + std::string(layout.map));
                return Result<void>::fail(r.error);
            } else {
                r.warn(p + ".message: not a message key of " + std::string(layout.map) + "; \"param\" is used");
            }
        }
        e.mapIdx = static_cast<std::uint8_t>(map_idx);
    }
    d.events = std::move(events);

    // spawn_variant_rows: optional; present, it replaces the tables (a mod's own set), absent
    // keeps what `d` had (the disc's).
    if (const Value* tables = root.find("spawn_variant_rows"); tables && !tables->is_null()) {
        if (!r.object(*tables, "spawn_variant_rows")) {
            return Result<void>::fail(r.error);
        }
        std::vector<SpawnVariantRows> out;
        for (const auto& [name, rows] : tables->members()) {
            const std::string p = "spawn_variant_rows." + name;
            if (name.empty() || !rows.is_array() || rows.array().size() != static_cast<std::size_t>(kSpawnTableRows)) {
                r.fail(rows, p, "expected " + std::to_string(kSpawnTableRows) + " rows under a table name");
                return Result<void>::fail(r.error);
            }
            if (std::any_of(out.begin(), out.end(), [&](const SpawnVariantRows& o) { return o.name == name; })) {
                r.fail(rows, p, "duplicate table");
                return Result<void>::fail(r.error);
            }
            SpawnVariantRows& t = out.emplace_back();
            t.name = name;
            for (int k = 0; k < kSpawnTableRows; ++k) {
                if (!read_spawn(r, rows.array()[k], p + "[" + std::to_string(k) + "]", t.rows[k])) {
                    return Result<void>::fail(r.error);
                }
            }
        }
        d.spawn_variant_rows = std::move(out);
    }

    // spawn_overrides: optional; absent means "no conditional spawns" and changes nothing.
    // The entries parse through the shared reader of the hand file (same shape).
    if (const Value* section = root.find("spawn_overrides")) {
        std::vector<SpawnOverride> overrides;
        if (!read_spawn_overrides(*section, overrides, warnings)) {
            return Result<void>::fail(warnings.empty() ? "spawn_overrides: malformed" : warnings.back());
        }
        d.spawn_overrides = std::move(overrides);
    }
    return Result<void>::ok();
}

/* The entries of one "spawn_overrides" array: the map.json section and the hand file
 * share the shape. Every problem lands in `warnings` with a
 * path; the caller decides whether that fails the whole read. */
bool read_spawn_overrides(const Value& section, std::vector<SpawnOverride>& overrides,
                          std::vector<std::string>& warnings) {
    Reader r(warnings);
    if (!section.is_array()) {
        r.fail(section, "spawn_overrides", "expected an array");
        return false;
    }
    overrides.resize(section.array().size());
    for (std::size_t i = 0; i < overrides.size(); ++i) {
        const Value& v = section.array()[i];
        const std::string p = "spawn_overrides[" + std::to_string(i) + "]";
        if (!r.object(v, p)) {
            return false;
        }
        SpawnOverride& o = overrides[i];
        const Value* when = r.member(v, p, "when");
        if (!when || !r.object(*when, p + ".when")) {
            return false;
        }
        if (!read_spawn_condition(r, *when, p + ".when", o.when, 0)) {
            return false;
        }
        const Value* sets = r.member(v, p, "set");
        if (!sets || !sets->is_array()) {
            r.fail(sets ? *sets : v, p + ".set", "expected an array of flag writes");
            return false;
        }
        o.set.resize(sets->array().size());
        for (std::size_t s = 0; s < o.set.size(); ++s) {
            const Value& sv = sets->array()[s];
            const std::string sp = p + ".set[" + std::to_string(s) + "]";
            SpawnFlagSet& fs = o.set[s];
            if (!r.object(sv, sp)) {
                return false;
            }
            const Value* chara = r.member(sv, sp, "chara");
            if (!chara || !chara->is_string() || chara->string().empty()) {
                r.fail(chara ? *chara : sv, sp + ".chara", "expected a character name");
                return false;
            }
            fs.chara = chara->string();
            std::int64_t slot = 0;
            if (!r.integer(sv, sp, "slot", 0, 255, slot)) {
                return false;
            }
            fs.slot = static_cast<int>(slot);
            const Value* flags = r.member(sv, sp, "flags");
            if (!flags || !flags->is_array()) {
                r.fail(flags ? *flags : sv, sp + ".flags", "expected an array of flag names");
                return false;
            }
            for (const Value& f : flags->array()) {
                if (!f.is_string() || f.string().empty()) {
                    r.fail(f, sp + ".flags", "expected a flag name");
                    return false;
                }
                fs.flags.push_back(f.string());
            }
        }
    }
    return true;
}

/* The hand/mod file maps/<map>/spawn_overrides.json: the
 * same entries as the map.json section in their own small file, so hand conditions never live
 * in disc-derived data. The store applies them IN ADDITION to any map.json section (appended
 * after it, so the hand entries win). */
Result<void> read_spawn_overrides_file(std::string_view text, MapData& d,
                                       std::vector<std::string>& warnings) {
    const Result<Value> parsed = json::parse(text);
    if (!parsed) {
        return Result<void>::fail(parsed.error());
    }
    const Value& root = parsed.value();
    Reader r(warnings);
    if (!r.object(root, "spawn_overrides.json")) {
        return Result<void>::fail(r.error);
    }
    if (const Value* format = root.find("format");
        !format || !format->is_string() || format->string() != "sh-map-spawn-overrides") {
        return Result<void>::fail("\"format\" must be \"sh-map-spawn-overrides\"");
    }
    std::int64_t version = 0;
    if (!r.integer(root, "spawn_overrides.json", "version", 1, 1, version)) {
        return Result<void>::fail(r.error);
    }
    const Value* section = r.member(root, "spawn_overrides.json", "spawn_overrides");
    if (!section || !section->is_array()) {
        r.fail(section ? *section : root, "spawn_overrides", "expected an array");
        return Result<void>::fail(r.error);
    }
    std::vector<SpawnOverride> overrides;
    if (!read_spawn_overrides(*section, overrides, warnings)) {
        return Result<void>::fail(warnings.empty() ? "spawn_overrides: malformed" : warnings.back());
    }
    SpawnVariants variants;
    if (const Value* v = root.find("spawn_variants"); v && !v->is_null()) {
        if (!read_spawn_variants(r, *v, variants)) {
            return Result<void>::fail(r.error.empty() ? "spawn_variants: malformed" : r.error);
        }
    }
    d.spawn_overrides.insert(d.spawn_overrides.end(), overrides.begin(), overrides.end());
    if (variants.present()) {
        d.spawn_variants = std::move(variants);
    }
    return Result<void>::ok();
}

Result<void> resolve_spawn_variant_rows(const std::vector<SpawnVariantRows>& tables, SpawnVariants& variants) {
    for (SpawnVariant& v : variants.variants) {
        if (v.rows_resolved) {
            continue;
        }
        const auto it = std::find_if(tables.begin(), tables.end(),
                                     [&](const SpawnVariantRows& t) { return t.name == v.name; });
        if (it == tables.end()) {
            return Result<void>::fail("spawn variant \"" + v.name +
                                      "\": the map has no spawn table of that name (map.json spawn_variant_rows)");
        }
        v.rows = it->rows;
        v.rows_resolved = true;
    }
    return Result<void>::ok();
}

// One condition node: leaf bounds plus nested all/any groups. depth guards against cyclic hand
// edits of the json (a normal section is one or two levels).
bool read_spawn_condition(Reader& r, const Value& v, const std::string& p, SpawnCondition& c, int depth)
{
    if (depth > 8) {
        r.fail(v, p, "conditions nested too deep");
        return false;
    }
    for (const char* key : {"difficulty", "difficulty_min", "difficulty_max"}) {
        if (const Value* d = v.find(key); d && !d->is_null()) {
            /* Names only ("easy"/"normal"/"hard"): e_GameDifficulty is -1..1 and the numbers
             * differ from every intuition, so the data never carries them. */
            static const struct { const char* name; int value; } kDifficulties[3] = {
                {"easy", -1}, {"normal", 0}, {"hard", 1},
            };
            if (!d->is_string()) {
                r.fail(*d, std::string(p) + "." + key, "expected \"easy\", \"normal\" or \"hard\"");
                return false;
            }
            const std::string& name = d->string();
            int value = SpawnCondition::kDifficultyUnset;
            for (const auto& entry : kDifficulties) {
                if (name == entry.name) {
                    value = entry.value;
                    break;
                }
            }
            if (value == SpawnCondition::kDifficultyUnset) {
                r.fail(*d, std::string(p) + "." + key, "unknown difficulty \"" + name + "\"");
                return false;
            }
            (key == std::string_view("difficulty") ? c.difficulty
             : key == std::string_view("difficulty_min") ? c.difficulty_min
                                                         : c.difficulty_max) = value;
        }
    }
    for (const char* key : {"flags_set", "flags_clear"}) {
        if (const Value* list = v.find(key); list && !list->is_null()) {
            if (!list->is_array()) {
                r.fail(*list, std::string(p) + "." + key, "expected an array of flag names");
                return false;
            }
            for (const Value& f : list->array()) {
                if (!f.is_string() || f.string().empty()) {
                    r.fail(f, std::string(p) + "." + key, "expected a flag name");
                    return false;
                }
                (key == std::string_view("flags_set") ? c.flags_set : c.flags_clear).push_back(f.string());
            }
        }
    }
    for (const char* key : {"all", "any"}) {
        if (const Value* list = v.find(key); list && !list->is_null()) {
            if (!list->is_array() || (!list->array().empty() && !list->array()[0].is_object())) {
                r.fail(*list, std::string(p) + "." + key, "expected an array of conditions");
                return false;
            }
            auto& into = key == std::string_view("all") ? c.all : c.any;
            into.resize(list->array().size());
            for (std::size_t i = 0; i < into.size(); ++i) {
                const std::string ip = std::string(p) + "." + key + "[" + std::to_string(i) + "]";
                if (!r.object(list->array()[i], ip) ||
                    !read_spawn_condition(r, list->array()[i], ip, into[i], depth + 1)) {
                    return false;
                }
            }
        }
    }
    return true;
}

/* "spawn_variants" of the hand file. A variant's rows are its own
 * "rows" (32 entries in the spawns-kind slot shape, null = empty; row = group * 16 + slot) or, without
 * them, the map's imported table of the variant's name (resolve_spawn_variant_rows). A choice must
 * name a declared variant. */
bool read_spawn_variants(Reader& r, const Value& v, SpawnVariants& out) {
    const std::string p = "spawn_variants";
    if (!r.object(v, p)) {
        return false;
    }
    std::int64_t active = 0;
    if (!r.integer(v, p, "enemy_state_active", 0, 44, active)) {
        return false;
    }
    out.enemy_state_active = static_cast<int>(active);
    const Value* variants = r.member(v, p, "variants");
    if (!variants || !variants->is_array() || variants->array().empty()) {
        return r.fail(variants ? *variants : v, p + ".variants", "expected a non-empty array of variants");
    }
    out.variants.resize(variants->array().size());
    for (std::size_t i = 0; i < out.variants.size(); ++i) {
        const Value& e = variants->array()[i];
        const std::string ep = p + ".variants[" + std::to_string(i) + "]";
        SpawnVariant& sv = out.variants[i];
        if (!r.object(e, ep)) {
            return false;
        }
        const Value* name = r.member(e, ep, "name");
        if (!name || !name->is_string() || name->string().empty()) {
            return r.fail(name ? *name : e, ep + ".name", "expected a variant name");
        }
        sv.name = name->string();
        for (std::size_t j = 0; j < i; ++j) {
            if (out.variants[j].name == sv.name) {
                return r.fail(*name, ep + ".name", "duplicate variant \"" + sv.name + "\"");
            }
        }
        if (const Value* rows = e.find("rows"); rows && !rows->is_null()) {
            if (!rows->is_array() || rows->array().size() != static_cast<std::size_t>(kSpawnTableRows)) {
                return r.fail(*rows, ep + ".rows", "expected " + std::to_string(kSpawnTableRows) + " rows");
            }
            for (int k = 0; k < kSpawnTableRows; ++k) {
                if (!read_spawn(r, rows->array()[k], ep + ".rows[" + std::to_string(k) + "]", sv.rows[k])) {
                    return false;
                }
            }
            sv.rows_inline = true;
            sv.rows_resolved = true;
        }
        std::int64_t load = 0;
        std::int64_t save = 0;
        if (!r.integer(e, ep, "enemy_state_load", 0, 44, load) || !r.integer(e, ep, "enemy_state_save", 0, 44, save)) {
            return false;
        }
        sv.enemy_state_load = static_cast<int>(load);
        sv.enemy_state_save = static_cast<int>(save);
    }
    const Value* choices = r.member(v, p, "choices");
    if (!choices || !choices->is_array()) {
        return r.fail(choices ? *choices : v, p + ".choices", "expected an array of choices");
    }
    out.choices.resize(choices->array().size());
    for (std::size_t i = 0; i < out.choices.size(); ++i) {
        const Value& e = choices->array()[i];
        const std::string ep = p + ".choices[" + std::to_string(i) + "]";
        SpawnVariantChoice& c = out.choices[i];
        if (!r.object(e, ep)) {
            return false;
        }
        const Value* when = r.member(e, ep, "when");
        if (!when || !r.object(*when, ep + ".when") || !read_spawn_condition(r, *when, ep + ".when", c.when, 0)) {
            return false;
        }
        const Value* variant = r.member(e, ep, "variant");
        if (!variant || !variant->is_string()) {
            return r.fail(variant ? *variant : e, ep + ".variant", "expected a variant name");
        }
        c.variant = variant->string();
        bool known = false;
        for (const SpawnVariant& sv : out.variants) {
            known = known || sv.name == c.variant;
        }
        if (!known) {
            return r.fail(*variant, ep + ".variant", "unknown variant \"" + c.variant + "\"");
        }
        if (const Value* flags = e.find("set_flags"); flags && !flags->is_null()) {
            if (!flags->is_array()) {
                return r.fail(*flags, ep + ".set_flags", "expected an array of flag names");
            }
            for (const Value& f : flags->array()) {
                if (!f.is_string() || f.string().empty()) {
                    return r.fail(f, ep + ".set_flags", "expected a flag name");
                }
                c.set_flags.push_back(f.string());
            }
        }
    }
    return true;
}

void set_id_naming(const IdNaming& n) { naming() = n; }

}  // namespace sh::convert::mapdata
