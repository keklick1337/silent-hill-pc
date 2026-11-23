// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/devtools/map_snippets.h"

#include "engine/devtools/position_snippets.h"
#include "formats/common/json.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace sh::devtools {

namespace {

// A name field of events.json: the name as a string, none as 0.
std::string name_or_zero(const std::string& name) {
    if (name.empty() || name == "0") return "0";
    return json_string(name);
}

std::string q4_array(const std::array<int, 4>& a) {
    std::string s = "[";
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (i) s += ", ";
        s += json_number(a[i] / 16.0);
    }
    return s + "]";
}

std::string int_array(const std::array<int, 4>& a) {
    std::string s = "[";
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (i) s += ", ";
        s += std::to_string(a[i]);
    }
    return s + "]";
}

std::string string_array(const std::vector<std::string>& v) {
    std::string s = "[";
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (i) s += ", ";
        s += json_string(v[i]);
    }
    return s + "]";
}

// One axis of a zone: a single cell as a number, several as an array.
std::string cells(const std::vector<int>& v) {
    if (v.size() == 1) return std::to_string(v[0]);
    std::string s = "[";
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (i) s += ", ";
        s += std::to_string(v[i]);
    }
    return s + "]";
}

std::int16_t angle_of_degrees(float degrees) {
    const float turns = degrees / 360.0f;
    return static_cast<std::int16_t>(static_cast<std::int32_t>(turns * 4096.0f));
}

}  // namespace

std::string json_number(double value) { return sh::json::format_number(value); }

std::string json_string(const std::string& text) { return sh::json::quote(text); }

std::string degrees_text(std::int16_t angle) {
    const double exact = angle * 360.0 / 4096.0;
    char buf[48];
    for (int decimals = 0; decimals <= 6; ++decimals) {
        std::snprintf(buf, sizeof(buf), "%.*f", decimals, exact);
        // A candidate a hair below the exact value truncates one step short; the one just above reads back.
        for (const double step : {0.0, 1.0, -1.0}) {
            const double candidate = std::strtod(buf, nullptr) + step * std::pow(10.0, -decimals);
            if (angle_of_degrees(static_cast<float>(candidate)) == angle) {
                std::snprintf(buf, sizeof(buf), "%.*f", decimals, candidate);
                std::string s = buf;
                if (s == "-0") s = "0";
                return s;
            }
        }
    }
    std::snprintf(buf, sizeof(buf), "%.6f", exact);
    return buf;
}

std::string event_json(const EventEntry& e) {
    std::string s = "{\"index\": " + std::to_string(e.index);
    s += ", \"required_flag\": " + name_or_zero(e.required_flag);
    s += ", \"complete_flag\": " + name_or_zero(e.complete_flag);
    s += ", \"trigger_type\": " + std::to_string(e.trigger_type);
    s += ", \"activation_type\": " + std::to_string(e.activation_type);
    s += ", \"map_point\": " + std::to_string(e.map_point);
    s += ", \"required_item\": " + name_or_zero(e.required_item);
    s += ", \"sys_state\": " + std::to_string(e.sys_state);
    s += ", \"param\": " + std::to_string(e.param);
    s += ", \"transition_flags\": " + std::to_string(e.transition_flags);
    s += ", \"sfx_pair\": " + std::to_string(e.sfx_pair);
    s += ", \"camera_rail\": " + std::to_string(e.camera_rail);
    if (e.sys_state == 5 && !e.map.empty())
        s += ", \"map\": " + json_string(e.map);
    else
        s += ", \"map_idx\": " + std::to_string(e.map_idx);
    return s + "}";
}

std::string point_json(const PointEntry& p) {
    std::string s = "{\"index\": " + std::to_string(p.index);
    s += ", \"x\": " + json_number(p.x_q12 / 4096.0);
    s += ", \"z\": " + json_number(p.z_q12 / 4096.0);
    s += ", \"paper_map\": " + std::to_string(p.paper_map);
    s += ", \"loading_screen\": " + std::to_string(p.loading_screen);
    s += ", \"heading_q8\": " + std::to_string(p.heading_q8);
    s += ", \"half_width_q4\": " + std::to_string(p.half_width_q4);
    s += ", \"weather_on_enter\": " + std::to_string(p.weather_on_enter);
    s += ", \"x_q12\": " + std::to_string(p.x_q12);
    s += ", \"z_q12\": " + std::to_string(p.z_q12);
    return s + "}";
}

std::string spawn_json(const SpawnEntry& sp) {
    std::string s = "{\"x\": " + json_number(sp.x_q12 / 4096.0);
    s += ", \"z\": " + json_number(sp.z_q12 / 4096.0);
    s += ", \"rotation_deg\": " + json_number(sp.rotation_q8 * 360.0 / 256.0);
    s += ", \"chara\": " + json_string(sp.chara);
    s += ", \"flags\": " + std::to_string(sp.flags);
    s += ", \"difficulty_min\": " + std::to_string(sp.difficulty_min);
    s += ", \"x_q12\": " + std::to_string(sp.x_q12);
    s += ", \"z_q12\": " + std::to_string(sp.z_q12);
    s += ", \"rotation_q8\": " + std::to_string(sp.rotation_q8);
    return s + "}";
}

std::string spawn_lua(const SpawnEntry& sp, const std::string& chara) {
    char buf[256];
    std::snprintf(buf, sizeof(buf), "sh.chara_spawn(%s, 0, %d, %d, %d, %d) -- (%s, %s)", json_string(chara).c_str(),
                  sp.x_q12, sp.z_q12, sp.rotation_q8 * 16, sp.flags, metres_text(sp.x_q12).c_str(),
                  metres_text(sp.z_q12).c_str());
    return buf;
}

std::string camera_json(const CameraEntry& c) {
    std::string s = "{\"index\": " + std::to_string(c.index);
    s += ", \"switch_area\": " + q4_array(c.switch_area_q4);
    s += ", \"switch_area_q4\": " + int_array(c.switch_area_q4);
    s += ", \"road_area\": " + q4_array(c.road_area_q4);
    s += ", \"road_area_q4\": " + int_array(c.road_area_q4);
    s += ", \"flags\": " + std::to_string(c.flags);
    s += ", \"area_size\": " + std::to_string(c.area_size);
    s += ", \"road_type\": " + std::to_string(c.road_type);
    s += ", \"move_y\": " + std::to_string(c.move_y);
    s += ", \"camera_move\": " + std::to_string(c.camera_move);
    s += ", \"road_max_y\": " + json_number(c.road_max_y_q4 / 16.0);
    s += ", \"road_min_y\": " + json_number(c.road_min_y_q4 / 16.0);
    s += ", \"watch_offset_y\": " + json_number(c.watch_offset_y_q4 / 16.0);
    s += ", \"road_max_y_q4\": " + std::to_string(c.road_max_y_q4);
    s += ", \"road_min_y_q4\": " + std::to_string(c.road_min_y_q4);
    s += ", \"watch_offset_y_q4\": " + std::to_string(c.watch_offset_y_q4);
    s += ", \"lens_flare\": " + std::to_string(c.lens_flare);
    s += ", \"fix_angle_x\": " + std::to_string(c.fix_angle_x);
    s += ", \"fix_angle_y\": " + std::to_string(c.fix_angle_y);
    return s + "}";
}

std::string collision_json(const CollisionEntry& c) {
    return "{\"index\": " + std::to_string(c.index) + ", \"x\": " + std::to_string(c.x) + ", \"z\": " +
           std::to_string(c.z) + ", \"size_x\": " + std::to_string(c.size_x) + ", \"size_z\": " +
           std::to_string(c.size_z) + ", \"height_half_m\": " + std::to_string(c.height_half_m) +
           ", \"end\": false}";
}

std::string world_object_json(const WorldObjectEntry& o) {
    std::string s = "{\"name\": " + json_string(o.name) + ", \"model\": " + json_string(o.model);
    if (!o.item_model.empty()) {
        s += ", \"item_model\": {\"file\": " + json_string(o.item_model);
        if (o.item_model_scale != 1.0) s += ", \"scale\": " + json_number(o.item_model_scale);
        s += "}";
    }
    if (!o.pose_table.empty()) {
        s += ", \"pose\": {\"disc_table\": " + json_string(o.pose_table) + ", \"index\": " +
             std::to_string(o.pose_index) + "}";
    } else {
        s += ", \"position\": [" + metres_text(o.position[0]) + ", " + metres_text(o.position[1]) + ", " +
             metres_text(o.position[2]) + "]";
        if (o.rotation[0] != 0 || o.rotation[1] != 0 || o.rotation[2] != 0)
            s += ", \"rotation\": [" + degrees_text(o.rotation[0]) + ", " + degrees_text(o.rotation[1]) + ", " +
                 degrees_text(o.rotation[2]) + "]";
    }
    if (!o.zones.empty()) {
        s += ", \"zones\": [";
        for (std::size_t i = 0; i < o.zones.size(); ++i) {
            if (i) s += ", ";
            s += "{";
            bool first = true;
            if (!o.zones[i].first.empty()) {
                s += "\"x\": " + cells(o.zones[i].first);
                first = false;
            }
            if (!o.zones[i].second.empty()) s += std::string(first ? "" : ", ") + "\"z\": " + cells(o.zones[i].second);
            s += "}";
        }
        s += "]";
    }
    if (!o.flags_set.empty()) s += ", \"flags_set\": " + string_array(o.flags_set);
    if (!o.flags_clear.empty()) s += ", \"flags_clear\": " + string_array(o.flags_clear);
    if (o.script) s += ", \"script\": true";
    if (o.pc_near_pickup) s += ", \"pc_near_pickup\": true";
    return s + "}";
}

std::string handler_lua(const std::string& name, int slot, const std::string& shape) {
    std::string s = "-- handler slot " + std::to_string(slot);
    if (!shape.empty()) s += " (" + shape + ")";
    return s + "\nfunction " + name + "()\nend\n";
}

}  // namespace sh::devtools
