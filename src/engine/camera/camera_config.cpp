#include "engine/camera/camera_config.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "engine/gameflow/flow_data.h"
#include "formats/common/json.h"
#include "engine/mods/data_reload.h"

namespace sh::camera {

namespace {

// The game's float-to-fixed rule: multiply in single precision, truncate toward zero.
std::int32_t to_q(double value, float scale) { return static_cast<std::int32_t>(static_cast<float>(value) * scale); }

// Degrees to a Q12 angle, divided in single precision first like the game's angle constants.
std::int32_t to_angle(double degrees) { return static_cast<std::int32_t>(static_cast<float>(degrees) / 360.0f * 4096.0f); }

class Reader {
public:
    explicit Reader(std::string* error) : error_(error) {}

    // A length in metres (or a plain rate) as Q12 (scale 4096) or Q8 (256).
    template <typename T>
    void metres(const json::Value* obj, std::string_view section, std::string_view key, T& out, float scale = 4096.0f) {
        if (const json::Value* v = number(obj, section, key)) {
            out = static_cast<T>(to_q(v->number(), scale));
        }
    }

    template <typename T>
    void degrees(const json::Value* obj, std::string_view section, std::string_view key, T& out) {
        if (const json::Value* v = number(obj, section, key)) {
            out = static_cast<T>(to_angle(v->number()));
        }
    }

    template <typename T>
    void integer(const json::Value* obj, std::string_view section, std::string_view key, T& out, std::int64_t lo,
                 std::int64_t hi) {
        const json::Value* v = obj != nullptr ? obj->find(key) : nullptr;
        if (v == nullptr || !error_->empty()) {
            return;
        }
        if (!v->is_integer() || v->integer() < lo || v->integer() > hi) {
            *error_ = std::string(section) + "." + std::string(key) + " must be an integer " + std::to_string(lo) +
                      ".." + std::to_string(hi);
            return;
        }
        out = static_cast<T>(v->integer());
    }

    // A map type selector: a map_info.json tag, "TAG#n" for the n-th entry with that tag.
    bool map_type(const json::Value* v, std::string_view where, MapTypeSelector& out) {
        if (!error_->empty()) {
            return false;
        }
        if (v == nullptr || !v->is_string() || v->string().empty()) {
            *error_ = std::string(where) + " must be a map type tag (\"THR\", \"ER#2\")";
            return false;
        }
        const std::string& text = v->string();
        const std::size_t hash = text.find('#');
        out.tag = text.substr(0, hash);
        out.occurrence = 0;
        if (hash != std::string::npos) {
            const std::string n = text.substr(hash + 1);
            if (out.tag.empty() || n.empty() || n.size() > 2 || n.find_first_not_of("0123456789") != std::string::npos ||
                std::stoi(n) < 1) {
                *error_ = std::string(where) + " must be a map type tag (\"THR\", \"ER#2\")";
                return false;
            }
            out.occurrence = std::stoi(n);
        }
        return true;
    }

    bool name(const json::Value* obj, std::string_view where, std::string_view key, std::string& out) {
        const json::Value* v = obj->find(key);
        if (!error_->empty()) {
            return false;
        }
        if (v == nullptr || !v->is_string() || v->string().empty()) {
            *error_ = std::string(where) + "." + std::string(key) + " must be a name";
            return false;
        }
        out = v->string();
        return true;
    }

    // A key the entry of a list must have.
    template <typename T>
    void required_metres(const json::Value* obj, std::string_view where, std::string_view key, T& out,
                         float scale = 4096.0f) {
        if (error_->empty() && obj->find(key) == nullptr) {
            *error_ = std::string(where) + "." + std::string(key) + " is missing";
            return;
        }
        metres(obj, where, key, out, scale);
    }

    template <typename T>
    void required_integer(const json::Value* obj, std::string_view where, std::string_view key, T& out,
                          std::int64_t lo, std::int64_t hi) {
        if (error_->empty() && obj->find(key) == nullptr) {
            *error_ = std::string(where) + "." + std::string(key) + " is missing";
            return;
        }
        integer(obj, where, key, out, lo, hi);
    }

    // The entries of a list key; null when the key is absent (the shipped list stays).
    const std::vector<json::Value>* list(const json::Value* obj, std::string_view section, std::string_view key) {
        const json::Value* v = obj->find(key);
        if (v == nullptr || !error_->empty()) {
            return nullptr;
        }
        if (!v->is_array()) {
            *error_ = std::string(section) + "." + std::string(key) + " must be a list";
            return nullptr;
        }
        return &v->array();
    }

    void fail(const std::string& message) {
        if (error_->empty()) {
            *error_ = message;
        }
    }

    bool object(const json::Value& v, const std::string& where) {
        if (error_->empty() && !v.is_object()) {
            *error_ = where + " must be an object";
        }
        return error_->empty();
    }

private:
    const json::Value* number(const json::Value* obj, std::string_view section, std::string_view key) {
        const json::Value* v = obj != nullptr ? obj->find(key) : nullptr;
        if (v == nullptr || !error_->empty()) {
            return nullptr;
        }
        // Every length of the camera stays well inside +-500 km; the bound keeps the Q12 product in range.
        if (!v->is_number() || !std::isfinite(v->number()) || std::fabs(v->number()) > 500000.0) {
            *error_ = std::string(section) + "." + std::string(key) + " must be a number";
            return nullptr;
        }
        return v;
    }

    std::string* error_;
};

void parse_map_rules(Reader& r, const json::Value& m, MapCameraRules& out) {
    constexpr std::string_view kSection = "map_rules";
    if (const auto* items = r.list(&m, kSection, "self_view_blocked")) {
        out.self_view_blocks.clear();
        for (std::size_t i = 0; i < items->size(); ++i) {
            const json::Value& e = (*items)[i];
            const std::string where = "map_rules.self_view_blocked[" + std::to_string(i) + "]";
            SelfViewBlock b;
            if (!r.object(e, where) || !r.map_type(e.find("map_type"), where + ".map_type", b.map_type)) {
                return;
            }
            r.required_metres(&e, where, "min_x", b.min_x);
            r.required_metres(&e, where, "max_x", b.max_x);
            r.required_metres(&e, where, "min_z", b.min_z);
            r.required_metres(&e, where, "max_z", b.max_z);
            out.self_view_blocks.push_back(b);
        }
    }
    if (const auto* items = r.list(&m, kSection, "tight_doorways")) {
        out.tight_doorway_types.clear();
        for (std::size_t i = 0; i < items->size(); ++i) {
            MapTypeSelector s;
            if (!r.map_type(&(*items)[i], "map_rules.tight_doorways[" + std::to_string(i) + "]", s)) {
                return;
            }
            out.tight_doorway_types.push_back(s);
        }
    }
    if (const auto* items = r.list(&m, kSection, "road_anchor_shifts")) {
        out.road_anchor_shifts.clear();
        for (std::size_t i = 0; i < items->size(); ++i) {
            const json::Value& e = (*items)[i];
            const std::string where = "map_rules.road_anchor_shifts[" + std::to_string(i) + "]";
            RoadAnchorRule a;
            if (!r.object(e, where) || !r.name(&e, where, "map", a.map)) {
                return;
            }
            r.required_integer(&e, where, "room", a.room, 0, 127);
            r.required_metres(&e, where, "road_x", a.road_x, 16.0f);
            r.required_metres(&e, where, "road_z", a.road_z, 16.0f);
            r.required_metres(&e, where, "shift_x", a.shift_x);
            out.road_anchor_shifts.push_back(a);
        }
    }
    if (const auto* items = r.list(&m, kSection, "wide_shots")) {
        out.wide_shots.clear();
        for (std::size_t i = 0; i < items->size(); ++i) {
            const json::Value& e = (*items)[i];
            const std::string where = "map_rules.wide_shots[" + std::to_string(i) + "]";
            WideShotRule w;
            if (!r.object(e, where) || !r.name(&e, where, "map", w.map)) {
                return;
            }
            r.required_integer(&e, where, "room", w.room, 0, 127);
            r.required_metres(&e, where, "anchor_x", w.anchor_x);
            r.required_metres(&e, where, "anchor_z", w.anchor_z);
            r.required_metres(&e, where, "tolerance", w.tolerance);
            out.wide_shots.push_back(w);
        }
    }
}

// The camera's enemy characters by name; every key is checked, both lists are required.
void parse_enemies(Reader& r, const json::Value& e, CameraConfig& out) {
    if (!r.object(e, "enemies")) {
        return;
    }
    for (const auto& [key, value] : e.members()) {
        if (key != "description" && key != "characters" && key != "always_active") {
            r.fail("enemies has an unknown key \"" + key + "\"");
            return;
        }
    }
    const auto names = [&](std::string_view key, std::vector<std::string>& list) {
        const std::string where = "enemies." + std::string(key);
        if (e.find(key) == nullptr) {
            r.fail(where + " is missing");
            return;
        }
        const auto* items = r.list(&e, "enemies", key);
        if (items == nullptr) {
            return;
        }
        list.clear();
        for (std::size_t i = 0; i < items->size(); ++i) {
            const json::Value& v = (*items)[i];
            if (!v.is_string() || v.string().empty()) {
                r.fail(where + "[" + std::to_string(i) + "] must be a character name");
                return;
            }
            list.push_back(v.string());
        }
    };
    names("characters", out.enemy_characters);
    names("always_active", out.always_active_characters);
    for (const std::string& name : out.always_active_characters) {
        if (std::find(out.enemy_characters.begin(), out.enemy_characters.end(), name) == out.enemy_characters.end()) {
            r.fail("enemies.always_active names \"" + name + "\", which is not in enemies.characters");
            return;
        }
    }
}

}  // namespace

Result<CameraConfig> parse_camera_config(std::string_view json_text) {
    const Result<json::Value> doc = json::parse(json_text);
    if (!doc) {
        return Result<CameraConfig>::fail("camera.json: " + doc.error());
    }
    CameraConfig out;
    std::string error;
    Reader r(&error);
    const json::Value& root = doc.value();
    for (const auto& [key, value] : root.members()) {
        static constexpr std::string_view kKnown[] = {"format", "version", "description", "view", "camera",
                                                      "subject", "debug_camera", "map_rules", "enemies"};
        if (std::find(std::begin(kKnown), std::end(kKnown), key) == std::end(kKnown)) {
            out.warnings.push_back("camera.json: unknown key \"" + key + "\" (ignored)");
        }
    }

    r.integer(root.find("view"), "view", "draw_cull_margin", out.draw_cull_margin, 0, 1024);

    if (const json::Value* api = root.find("camera")) {
        if (const json::Value* w = api->find("default_watch")) {
            r.metres(w, "camera.default_watch", "accel_x", out.default_watch.accel_x);
            r.metres(w, "camera.default_watch", "accel_y", out.default_watch.accel_y);
            r.metres(w, "camera.default_watch", "max_speed_x", out.default_watch.max_speed_x);
            r.metres(w, "camera.default_watch", "max_speed_y", out.default_watch.max_speed_y);
        }
        if (const json::Value* c = api->find("user_camera")) {
            r.metres(c, "camera.user_camera", "accel_xz", out.user_camera.accel_xz);
            r.metres(c, "camera.user_camera", "accel_y", out.user_camera.accel_y);
            r.metres(c, "camera.user_camera", "max_speed_xz", out.user_camera.max_speed_xz);
            r.metres(c, "camera.user_camera", "max_speed_y", out.user_camera.max_speed_y);
        }
        r.metres(api, "camera", "watch_max_y", out.watch_max_y);
        r.metres(api, "camera", "first_camera_distance", out.first_camera_distance);
        r.metres(api, "camera", "first_camera_height", out.first_camera_height);
    }

    if (const json::Value* s = root.find("subject")) {
        SubjectRules& sr = out.subject;
        r.metres(s, "subject", "top_offset_y", sr.top_offset_y);
        r.metres(s, "subject", "bottom_offset_y", sr.bottom_offset_y);
        r.metres(s, "subject", "connected_ground_y", sr.connected_ground_y);
        r.metres(s, "subject", "connected_head_y", sr.connected_head_y);
        r.metres(s, "subject", "ground_missing_y", sr.ground_missing_y);
        r.degrees(s, "subject", "eye_half_angle", sr.eye_half_angle);
        r.metres(s, "subject", "watch_radius", sr.watch_radius);
        r.metres(s, "subject", "head_bone_offset_y", sr.head_bone_offset_y, 256.0f);
        r.metres(s, "subject", "head_offset_y", sr.head_offset_y);
    }

    if (const json::Value* d = root.find("debug_camera")) {
        DebugCameraRules& dr = out.debug;
        r.metres(d, "debug_camera", "start_offset_x", dr.start_offset_x);
        r.metres(d, "debug_camera", "start_y", dr.start_y);
        r.metres(d, "debug_camera", "start_radius", dr.start_radius);
        r.metres(d, "debug_camera", "orbit_step", dr.orbit_step);
        r.metres(d, "debug_camera", "orbit_min_radius", dr.orbit_min_radius);
        r.integer(d, "debug_camera", "turn_per_vblank", dr.turn_per_vblank, 0, 4096);
        r.metres(d, "debug_camera", "reference_ahead", dr.reference_ahead);
        r.metres(d, "debug_camera", "reference_y", dr.reference_y);
        r.metres(d, "debug_camera", "fly_step", dr.fly_step, 256.0f);
        r.integer(d, "debug_camera", "fly_boost", dr.fly_boost, 1, 64);
        r.metres(d, "debug_camera", "fly_reference_distance", dr.fly_reference_distance, 256.0f);
        if (const json::Value* h = d->find("head_reference")) {
            r.metres(h, "debug_camera.head_reference", "x", dr.head_reference.x);
            r.metres(h, "debug_camera.head_reference", "y", dr.head_reference.y);
            r.metres(h, "debug_camera.head_reference", "z", dr.head_reference.z);
        }
        r.degrees(d, "debug_camera", "head_orbit_yaw", dr.head_orbit_yaw);
        r.degrees(d, "debug_camera", "head_orbit_roll", dr.head_orbit_roll);
        r.metres(d, "debug_camera", "head_orbit_height", dr.head_orbit_height);
        r.metres(d, "debug_camera", "head_orbit_radius", dr.head_orbit_radius);
    }

    if (const json::Value* m = root.find("map_rules")) {
        if (r.object(*m, "map_rules")) {
            parse_map_rules(r, *m, out.map_rules);
        }
    }

    if (const json::Value* e = root.find("enemies")) {
        parse_enemies(r, *e, out);
    } else {
        out.warnings.push_back("camera.json: no \"enemies\" section - the camera tracks no character as an enemy");
    }

    if (!error.empty()) {
        return Result<CameraConfig>::fail("camera.json: " + error);
    }
    return Result<CameraConfig>::ok(out);
}

const CameraConfig& shared_camera_config(const std::filesystem::path& gamedata_dir,
                                         const std::filesystem::path& mods_dir, std::string* error) {
    static const CameraConfig* s_config = nullptr;
    // After `mods reload` the file is read again; the old config stays alive for references still held.
    static std::uint32_t s_epoch = mods::kNeverRead;
    if (mods::data_stale(s_epoch)) {
        s_config = nullptr;
    }
    if (s_config == nullptr) {
        CameraConfig* config = new CameraConfig();
        std::vector<std::string> warnings;
        std::string source;
        const Result<std::string> text = gameflow::read_config_file(gamedata_dir, mods_dir, "camera.json", &source, warnings);
        if (text) {
            const Result<CameraConfig> parsed = parse_camera_config(text.value());
            if (parsed) {
                *config = parsed.value();
            } else if (error != nullptr) {
                *error = parsed.error();
            }
        }
        config->warnings.insert(config->warnings.end(), warnings.begin(), warnings.end());
        s_config = config;
    }
    return *s_config;
}

}  // namespace sh::camera
