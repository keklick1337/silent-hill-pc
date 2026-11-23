// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/devtools/state_dump.h"

#include "formats/common/json.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace sh::devtools {

namespace {

using json::Writer;

double metres(std::int32_t q12) { return static_cast<double>(q12) / 4096.0; }
double degrees(int q12_turn) { return static_cast<double>(q12_turn & 0xFFF) * 360.0 / 4096.0; }

// Three decimals are below a Q12 step's size in print and keep the file readable.
double rounded(double v) { return std::round(v * 1000.0) / 1000.0; }

void point(Writer& w, std::string_view name, const DumpPoint& p) {
    w.key(name).inline_next().begin_object();
    w.key("metres").begin_array();
    w.value_shortest(rounded(metres(p.x))).value_shortest(rounded(metres(p.y))).value_shortest(rounded(metres(p.z)));
    w.end_array();
    w.key("q12").begin_array().value(p.x).value(p.y).value(p.z).end_array();
    w.end_object();
}

void angle(Writer& w, std::string_view name, int q12_turn) {
    w.key(name).inline_next().begin_object();
    w.key("degrees").value_shortest(rounded(degrees(q12_turn)));
    w.member("q12", q12_turn);
    w.end_object();
}

double distance(const DumpPoint& a, const DumpPoint& b) {
    const double dx = metres(a.x) - metres(b.x), dy = metres(a.y) - metres(b.y), dz = metres(a.z) - metres(b.z);
    return rounded(std::sqrt(dx * dx + dy * dy + dz * dz));
}

void strings(Writer& w, std::string_view name, const std::vector<std::string>& list, bool one_line) {
    w.key(name);
    if (one_line) {
        w.inline_next();
    }
    w.begin_array();
    for (const std::string& s : list) {
        w.value(s);
    }
    w.end_array();
}

void write_player(Writer& w, const StateDump& d) {
    w.key("player").begin_object();
    point(w, "position", d.position);
    angle(w, "yaw", d.yaw);
    w.key("health").value_shortest(rounded(metres(d.health)));
    w.member("health_q12", d.health);
    w.member("aiming", d.aiming);
    w.member("weapon", d.weapon);
    w.member("weapon_attack", d.weapon_attack);
    w.member("ammo", d.ammo);
    w.key("animation").inline_next().begin_object();
    w.member("status", d.anim_status).member("keyframe", d.anim_keyframe).member("flags", d.anim_flags);
    w.end_object();
    w.key("state").inline_next().begin_object();
    w.member("control", d.control_state).member("step", d.control_step).member("upper_body", d.upper_body_state);
    w.end_object();
    w.end_object();
}

void write_camera(Writer& w, const StateDump& d) {
    w.key("camera").begin_object();
    point(w, "position", d.camera_position);
    point(w, "look_at", d.camera_look_at);
    angle(w, "pitch", d.camera_pitch);
    angle(w, "yaw", d.camera_yaw);
    angle(w, "roll", d.camera_roll);
    w.key("projection").inline_next().begin_object();
    w.member("distance", d.projection_distance);
    w.member("offset_x", d.projection_offset_x).member("offset_y", d.projection_offset_y);
    w.key("fov_x_degrees").value_shortest(rounded(degrees(d.half_angle_x) * 2.0));
    w.key("fov_y_degrees").value_shortest(rounded(degrees(d.half_angle_y) * 2.0));
    w.end_object();
    w.member("script_camera", d.script_camera);
    w.member("script_look_at", d.script_look_at);
    w.member("free_camera", d.free_camera);
    w.member("zone", d.camera_zone);
    w.member("flags", d.camera_flags);
    // World -> view: the rotation rows (Q12) and the camera's world position (Q12) it is taken about.
    w.key("view_matrix").begin_object();
    w.key("rotation_q12").begin_array();
    for (int row = 0; row < 3; ++row) {
        w.inline_next().begin_array();
        for (int col = 0; col < 3; ++col) {
            w.value(d.view_rotation[static_cast<std::size_t>(row * 3 + col)]);
        }
        w.end_array();
    }
    w.end_array();
    point(w, "origin", d.view_origin);
    w.end_object();
    w.end_object();
}

void write_world(Writer& w, const StateDump& d) {
    w.key("environment").begin_object();
    w.member("flashlight_on", d.flashlight_on);
    w.member("flashlight_unavailable", d.flashlight_unavailable);
    w.member("fog_mode", d.fog_mode);
    w.key("fog_distance_metres").value_shortest(rounded(metres(d.fog_distance)));
    w.key("fog_color").inline_next().begin_array();
    w.value(d.fog_color[0]).value(d.fog_color[1]).value(d.fog_color[2]).end_array();
    w.member("light_mode", d.light_mode);
    w.member("light_level_max", d.light_level_max);
    w.member("spot_light_intensity", d.spot_light_intensity);
    w.member("world_light_intensity", d.world_light_intensity);
    w.end_object();

    strings(w, "flags", d.flags, false);

    w.key("inventory").begin_array();
    for (const DumpItem& item : d.inventory) {
        w.inline_next().begin_object().member("name", item.name).member("count", item.count).end_object();
    }
    w.end_array();

    w.key("characters").begin_array();
    for (const DumpCharacter& c : d.characters) {
        w.begin_object();
        w.member("slot", c.slot);
        w.member("name", c.name);
        point(w, "position", c.position);
        angle(w, "yaw", c.yaw);
        w.key("health").value_shortest(rounded(metres(c.health)));
        w.key("state").inline_next().begin_object();
        w.member("control", c.control_state).member("step", c.control_step).end_object();
        strings(w, "flags", c.flags, true);
        w.member("model_loaded", c.model_loaded);
        w.member("in_camera_frustum", c.in_camera_frustum);
        w.member("visible", c.model_loaded && c.in_camera_frustum);
        w.key("distance").value_shortest(distance(c.position, d.position));
        w.end_object();
    }
    w.end_array();

    std::vector<const DumpObject*> objects;
    objects.reserve(d.objects.size());
    for (const DumpObject& o : d.objects) {
        objects.push_back(&o);
    }
    std::stable_sort(objects.begin(), objects.end(), [&](const DumpObject* a, const DumpObject* b) {
        return distance(a->position, d.position) < distance(b->position, d.position);
    });
    w.key("objects").begin_array();
    for (const DumpObject* o : objects) {
        w.begin_object();
        w.member("name", o->name);
        w.member("model", o->model);
        if (!o->item_model.empty()) {
            w.member("item_model", o->item_model);
        }
        point(w, "position", o->position);
        w.key("rotation").inline_next().begin_object();
        w.key("degrees").begin_array();
        for (const int r : o->rotation) {
            w.value_shortest(rounded(degrees(r)));
        }
        w.end_array();
        w.key("q12").begin_array().value(o->rotation[0]).value(o->rotation[1]).value(o->rotation[2]).end_array();
        w.end_object();
        w.member("pose_resolved", o->pose_resolved);
        w.member("queued", o->queued);
        w.member("library", o->library);
        if (o->library_slot >= 0) {
            w.member("library_slot", o->library_slot);
        }
        w.key("distance").value_shortest(distance(o->position, d.position));
        w.end_object();
    }
    w.end_array();

    w.key("cells").begin_object();
    w.member("exterior", d.exterior);
    w.member("library_file", d.library_file);
    w.key("resident").begin_array();
    for (const DumpCell& c : d.cells) {
        w.inline_next().begin_object();
        w.member("slot", c.slot).member("x", c.x).member("z", c.z).member("file", c.file).member("ready", c.ready);
        w.end_object();
    }
    w.end_array();
    w.end_object();

    w.key("effects").begin_array();
    for (const DumpEffect& e : d.effects) {
        w.inline_next().begin_object();
        w.member("slot", e.slot).member("kind", e.kind);
        w.key("metres").begin_array();
        w.value_shortest(rounded(metres(e.position.x))).value_shortest(rounded(metres(e.position.y)));
        w.value_shortest(rounded(metres(e.position.z))).end_array();
        w.end_object();
    }
    w.end_array();
}

// Hundredths: a frame section is tens of microseconds at its shortest.
double hundredths(double v) { return std::round(v * 100.0) / 100.0; }

// A number the platform may not report: null instead of the negative mark.
void reported(Writer& w, std::string_view name, double v) {
    w.key(name);
    if (v < 0.0) {
        w.null();
    } else {
        w.value_shortest(hundredths(v));
    }
}

void write_series(Writer& w, std::string_view name, const std::vector<PerfSeries>& list, bool sections) {
    w.key(name).begin_array();
    for (const PerfSeries& s : list) {
        // A section that never reached a hundredth of a millisecond says nothing: dozens of frame hooks are such.
        if (sections && s.max < 0.005) {
            continue;
        }
        w.inline_next().begin_object();
        w.member("name", s.name);
        if (!s.inside.empty()) {
            w.member("inside", s.inside);
        }
        w.key("average").value_shortest(hundredths(s.average));
        w.key("max").value_shortest(hundredths(s.max));
        w.key("last").value_shortest(hundredths(s.last));
        w.key("in_slowest_frame").value_shortest(hundredths(s.in_slowest_frame));
        if (sections) {
            w.key("calls_per_frame").value_shortest(hundredths(s.calls_per_frame));
        }
        w.end_object();
    }
    w.end_array();
}

void write_performance(Writer& w, const DumpPerformance& p) {
    const PerfReport& h = p.history;
    const auto fps = [](double ms) { return ms > 0.0 ? std::round(10000.0 / ms) / 10.0 : 0.0; };
    w.key("performance").begin_object();
    w.key("fps").value_shortest(fps(h.average_ms));
    w.key("low_1pct_fps").value_shortest(fps(h.low_1pct_ms));
    w.key("frame_ms").inline_next().begin_object();
    w.key("average").value_shortest(hundredths(h.average_ms));
    w.key("min").value_shortest(hundredths(h.min_ms));
    w.key("max").value_shortest(hundredths(h.max_ms));
    w.key("p99").value_shortest(hundredths(h.p99_ms));
    w.key("low_1pct").value_shortest(hundredths(h.low_1pct_ms));
    w.key("last").value_shortest(hundredths(h.frame_ms.empty() ? 0.0 : h.frame_ms.back()));
    w.end_object();
    w.member("history_frames", static_cast<std::int64_t>(h.frame_ms.size()));
    w.member("stutter_frames", h.stutter_frames);
    w.key("held_frames_excluded").inline_next().begin_object();
    w.member("before_dump", h.held_frames_before_report).member("total", h.held_frames_total);
    w.end_object();
    reported(w, "cpu_percent", p.cpu_percent);
    reported(w, "main_thread_percent", p.main_thread_percent);
    reported(w, "gpu_percent", p.gpu_percent);
    reported(w, "ram_mb", p.ram_mb);
    reported(w, "vram_game_mb", p.vram_game_mb);
    reported(w, "vram_used_mb", p.vram_used_mb);
    reported(w, "vram_total_mb", p.vram_total_mb);
    w.key("settings").inline_next().begin_object();
    w.member("vsync", p.vsync).member("fps_cap", p.fps_cap).member("refresh_rate", p.refresh_rate);
    w.member("fps_unlocked", p.fps_unlocked);
    w.end_object();
    w.key("counts").inline_next().begin_object();
    w.member("characters", p.characters);
    w.member("remains_bodies", p.remains_bodies).member("remains_stains", p.remains_stains);
    w.member("remains_bodies_all_maps", p.remains_bodies_all_maps);
    w.member("effect_slots", p.effect_slots);
    reported(w, "textures_loaded", p.textures_loaded);
    reported(w, "texture_uploads_pending", p.texture_uploads_pending);
    w.end_object();
    write_series(w, "per_frame", h.counters, false);
    write_series(w, "sections_ms", h.sections, true);
    w.key("frame_ms_history").inline_next().begin_array();
    for (const double ms : h.frame_ms) {
        w.value_shortest(hundredths(ms));
    }
    w.end_array();
    w.end_object();
}

std::int32_t integer_at(const json::Value& array, std::size_t i) {
    return i < array.array().size() && array.array()[i].is_number() ? static_cast<std::int32_t>(array.array()[i].number())
                                                                    : 0;
}

// The "q12" triple of a point member written by point(); false when `owner` has no such member.
bool read_point(const json::Value* owner, std::string_view name, DumpPoint& out) {
    const json::Value* member = owner != nullptr && owner->is_object() ? owner->find(name) : nullptr;
    const json::Value* q12 = member != nullptr && member->is_object() ? member->find("q12") : nullptr;
    if (q12 == nullptr || !q12->is_array() || q12->array().size() != 3) {
        return false;
    }
    out = {integer_at(*q12, 0), integer_at(*q12, 1), integer_at(*q12, 2)};
    return true;
}

}  // namespace

std::string state_dump_id(std::string_view content, std::uint64_t time) {
    std::uint64_t h = 14695981039346656037ull;
    const auto mix = [&h](unsigned char byte) {
        h ^= byte;
        h *= 1099511628211ull;
    };
    for (const char c : content) {
        mix(static_cast<unsigned char>(c));
    }
    for (int i = 0; i < 8; ++i) {
        mix(static_cast<unsigned char>(time >> (8 * i)));
    }
    char text[16];
    std::snprintf(text, sizeof(text), "%08x", static_cast<unsigned>((h ^ (h >> 32)) & 0xFFFFFFFFu));
    return text;
}

bool valid_dump_id(std::string_view id) {
    return id.size() == 8 && std::all_of(id.begin(), id.end(), [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    });
}

std::string state_dump_json(const StateDump& d, std::string_view id) {
    Writer w(true);
    w.begin_object();
    w.member("id", id);
    w.member("time", d.time);
    w.member("revision", d.revision);
    w.member("note", d.note);
    w.member("map", d.map);
    w.member("map_description", d.map_description);
    w.member("room", d.room);
    w.member("game_state", d.game_state);
    w.member("system_state", d.system_state);
    w.member("free_gameplay", d.free_gameplay);
    w.member("state_saved", d.state_saved);
    write_player(w, d);
    write_camera(w, d);
    write_world(w, d);
    w.key("config").begin_object();
    w.member("graphics_preset", d.graphics_preset);
    w.member("renderer", d.renderer);
    w.key("window").inline_next().begin_array().value(d.window_width).value(d.window_height).end_array();
    w.key("frame").inline_next().begin_array().value(d.frame_width).value(d.frame_height).end_array();
    w.member("widescreen_mode", d.widescreen_mode);
    w.key("render_scale").value_shortest(d.render_scale);
    w.member("native_resolution", d.native_resolution);
    w.end_object();
    write_performance(w, d.performance);
    strings(w, "log", d.log, false);
    w.end_object();
    std::string out = w.take();
    out += '\n';
    return out;
}

Result<DumpRestore> parse_state_dump(std::string_view json_text) {
    auto doc = json::parse(json_text);
    if (!doc) {
        return Result<DumpRestore>::fail(doc.error());
    }
    const json::Value& root = doc.value();
    const json::Value* map = root.is_object() ? root.find("map") : nullptr;
    const json::Value* player = root.is_object() ? root.find("player") : nullptr;
    if (map == nullptr || !map->is_string() || player == nullptr || !player->is_object()) {
        return Result<DumpRestore>::fail("not a state dump: no map or player");
    }
    DumpRestore out;
    const json::Value* yaw = player->find("yaw");
    const json::Value* yaw_q12 = yaw != nullptr && yaw->is_object() ? yaw->find("q12") : nullptr;
    if (!read_point(player, "position", out.position) || yaw_q12 == nullptr || !yaw_q12->is_number()) {
        return Result<DumpRestore>::fail("not a state dump: no player position or yaw");
    }
    out.map = map->string();
    out.yaw = static_cast<int>(yaw_q12->number());
    const json::Value* saved = root.find("state_saved");
    out.state_saved = saved != nullptr && saved->is_bool() && saved->boolean();
    return Result<DumpRestore>::ok(std::move(out));
}

}  // namespace sh::devtools
