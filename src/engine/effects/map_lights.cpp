// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// gamedata/maps/<map>/lights.json (map_lights.h).

#include "engine/effects/map_lights.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "engine/effects/effects_file.h"
#include "engine/effects/map_lighting_config.h"
#include "engine/mods/data_merge.h"
#include "formats/common/json.h"

namespace sh::effects {

namespace {

double number_of(const json::Value& v, double lo, double hi) {
    if (!v.is_number()) parse::fail(v, "a number is expected");
    const double n = v.number();
    if (!std::isfinite(n) || n < lo || n > hi)
        parse::fail(v, "the number must lie in " + std::to_string(lo) + " .. " + std::to_string(hi));
    return n;
}

float number_or(const json::Value& parent, std::string_view key, float fallback, double lo, double hi) {
    const json::Value* v = parent.find(key);
    return v != nullptr ? static_cast<float>(number_of(*v, lo, hi)) : fallback;
}

std::array<float, 3> vec3_of(const json::Value& v, double lo, double hi) {
    if (!v.is_array() || v.array().size() != 3) parse::fail(v, "[x, y, z] is expected");
    std::array<float, 3> out{};
    for (std::size_t i = 0; i < 3; ++i) out[i] = static_cast<float>(number_of(v.array()[i], lo, hi));
    return out;
}

std::array<float, 3> unit_of(const json::Value& v) {
    std::array<float, 3> d = vec3_of(v, -1e6, 1e6);
    const float l = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (l < 1e-6f) parse::fail(v, "a direction cannot be zero");
    for (float& c : d) c /= l;
    return d;
}

bool bool_or(const json::Value& parent, std::string_view key, bool fallback) {
    const json::Value* v = parent.find(key);
    if (v == nullptr) return fallback;
    if (!v->is_bool()) parse::fail(*v, "true or false is expected");
    return v->boolean();
}

// "when": one condition or a list of them, all of which must hold: "flag:<name>" (the event flag is set) or
// "!flag:<name>" (it is clear). The same lists as flags_set and flags_clear, which stay for older files.
void when_of(const json::Value& parent, std::vector<std::string>* set, std::vector<std::string>* clear) {
    const json::Value* v = parent.find("when");
    if (v == nullptr) return;
    auto term = [&](const json::Value& t) {
        if (!t.is_string()) parse::fail(t, "a condition is \"flag:<name>\" or \"!flag:<name>\"");
        std::string_view text = t.string();
        const bool negated = !text.empty() && text.front() == '!';
        if (negated) text.remove_prefix(1);
        constexpr std::string_view kFlag = "flag:";
        if (text.substr(0, kFlag.size()) != kFlag || text.size() == kFlag.size())
            parse::fail(t, "a condition is \"flag:<name>\" or \"!flag:<name>\"");
        (negated ? clear : set)->emplace_back(text.substr(kFlag.size()));
    };
    if (v->is_array()) {
        for (const json::Value& t : v->array()) term(t);
    } else {
        term(*v);
    }
}

std::vector<std::string> names_of(const json::Value& parent, std::string_view key) {
    std::vector<std::string> out;
    const json::Value* v = parent.find(key);
    if (v == nullptr) return out;
    if (!v->is_array()) parse::fail(*v, "a list of event flag names is expected");
    for (const json::Value& n : v->array()) {
        if (!n.is_string() || n.string().empty()) parse::fail(n, "an event flag name is expected");
        out.push_back(n.string());
    }
    return out;
}

std::vector<MapCeiling> ceilings_of(const json::Value& list) {
    if (!list.is_array()) parse::fail(list, "\"ceilings\" is a list of {\"rect\", \"height\"}");
    if (list.array().size() > kMaxMapCeilings)
        parse::fail(list, "at most " + std::to_string(kMaxMapCeilings) + " ceilings");
    std::vector<MapCeiling> out;
    for (const json::Value& v : list.array()) {
        if (!v.is_object()) parse::fail(v, "a ceiling is {\"rect\": [x0, z0, x1, z1], \"height\": y}");
        parse::reject_unknown(v, {"description", "rect", "height"});
        const json::Value& r = parse::member(v, "rect");
        if (!r.is_array() || r.array().size() != 4) parse::fail(r, "\"rect\" is [x0, z0, x1, z1] in metres");
        MapCeiling c;
        for (std::size_t i = 0; i < 4; ++i) c.rect[i] = static_cast<float>(number_of(r.array()[i], -1e5, 1e5));
        if (c.rect[2] <= c.rect[0] || c.rect[3] <= c.rect[1]) parse::fail(r, "x1 > x0 and z1 > z0 are expected");
        c.height = static_cast<float>(number_of(parse::member(v, "height"), -1e4, 1e4));
        out.push_back(c);
    }
    return out;
}

MapLight light_of(const json::Value& v) {
    if (!v.is_object()) parse::fail(v, "a light is an object");
    parse::reject_unknown(
        v, {"name", "description", "type", "position", "direction", "cone", "color", "intensity", "range", "radius",
            "casts_shadows", "gi", "flags_set", "flags_clear", "when", "extra", "bone", "while_effect", "flicker"});
    MapLight l;
    const json::Value& name = parse::member(v, "name");
    if (!name.is_string() || name.string().empty()) parse::fail(name, "\"name\" is the light's name");
    l.name = name.string();
    if (const json::Value* t = v.find("type")) {
        if (!t->is_string() || (t->string() != "point" && t->string() != "spot"))
            parse::fail(*t, "\"type\" is \"point\" or \"spot\"");
        l.spot = t->string() == "spot";
    }
    l.position = vec3_of(parse::member(v, "position"), -100000.0, 100000.0);
    if (l.spot) {
        l.direction = unit_of(parse::member(v, "direction"));
        const json::Value& cone = parse::member(v, "cone");
        if (!cone.is_array() || cone.array().size() != 2) parse::fail(cone, "\"cone\" is [inner, outer] in degrees");
        l.cone_inner = static_cast<float>(number_of(cone.array()[0], 0.0, 89.0));
        l.cone_outer = static_cast<float>(number_of(cone.array()[1], 1.0, 90.0));
        if (l.cone_outer <= l.cone_inner) parse::fail(cone, "the outer cone must be wider than the inner");
    }
    if (const json::Value* c = v.find("color")) l.color = vec3_of(*c, 0.0, 1.0);
    l.intensity = number_or(v, "intensity", 1.0f, 0.0, 16.0);
    l.range = number_or(v, "range", 6.0f, 0.1, 64.0);
    l.radius = number_or(v, "radius", 0.05f, 0.0, 2.0);
    l.casts_shadows = bool_or(v, "casts_shadows", true);
    l.gi = bool_or(v, "gi", true);
    l.flags_set = names_of(v, "flags_set");
    l.flags_clear = names_of(v, "flags_clear");
    when_of(v, &l.flags_set, &l.flags_clear);
    l.extra = bool_or(v, "extra", false);
    if (const json::Value* b = v.find("bone")) {
        if (!b->is_string() || (l.bone = harry_bone_index(b->string())) < 0)
            parse::fail(*b, "\"bone\" names a bone of Harry (\"RightHand\", ...)");
    }
    if (const json::Value* e = v.find("while_effect")) {
        if (!e->is_string() ||
            std::find(std::begin(kLightEffects), std::end(kLightEffects), e->string()) == std::end(kLightEffects))
            parse::fail(*e, "\"while_effect\" names an effect a light can follow (\"HandFlame\")");
        l.while_effect = e->string();
    }
    l.flicker = number_or(v, "flicker", 0.0f, 0.0, 1.0);
    return l;
}

std::array<float, std::size(kLightEffects)> s_shown = [] {
    std::array<float, std::size(kLightEffects)> a{};
    a.fill(-1.0f);
    return a;
}();

int effect_index(std::string_view effect) {
    for (std::size_t i = 0; i < std::size(kLightEffects); ++i)
        if (kLightEffects[i] == effect) return static_cast<int>(i);
    return -1;
}

GlowLights glows_of(const json::Value& v) {
    GlowLights g;
    if (v.is_null()) return g;
    if (!v.is_object()) parse::fail(v, "\"glows\" is an object or null");
    parse::reject_unknown(v, {"description", "color", "intensity", "range", "radius", "drop", "casts_shadows", "gi"});
    g.on = true;
    if (const json::Value* c = v.find("color")) g.color = vec3_of(*c, 0.0, 1.0);
    g.intensity = number_or(v, "intensity", g.intensity, 0.0, 16.0);
    g.range = number_or(v, "range", g.range, 0.1, 64.0);
    g.radius = number_or(v, "radius", g.radius, 0.0, 2.0);
    g.drop = number_or(v, "drop", g.drop, -4.0, 4.0);
    g.casts_shadows = bool_or(v, "casts_shadows", g.casts_shadows);
    g.gi = bool_or(v, "gi", g.gi);
    return g;
}

}  // namespace

Result<GlowLights> parse_lamp_lights(std::string_view json_text) try {
    const Result<json::Value> doc = json::parse(json_text);
    if (!doc) return Result<GlowLights>::fail(doc.error());
    const json::Value& root = doc.value();
    if (!root.is_object()) throw std::runtime_error("the file must be an object");
    parse::reject_unknown(root, {"format", "version", "glows"});
    return Result<GlowLights>::ok(glows_of(parse::member(root, "glows")));
} catch (const std::exception& e) {
    return Result<GlowLights>::fail(e.what());
}

Result<GlowLights> load_lamp_lights(const std::filesystem::path& gamedata_dir, const std::filesystem::path& mods_dir,
                                    std::string* source) {
    const std::filesystem::path relative = std::filesystem::path("effects") / "lamp_lights.json";
    std::string where;
    const Result<std::string> read = mods::read_data_text(gamedata_dir, mods_dir, relative, &where);
    if (source != nullptr) *source = where;
    if (!read) return Result<GlowLights>::fail(read.error());
    Result<GlowLights> parsed = parse_lamp_lights(read.value());
    if (!parsed) return Result<GlowLights>::fail(where + ": " + parsed.error());
    return parsed;
}

namespace {

int fire_index(std::string_view fire) {
    for (std::size_t i = 0; i < std::size(kFireEffects); ++i)
        if (kFireEffects[i] == fire) return static_cast<int>(i);
    return -1;
}

FireLight fire_light_of(const json::Value& v) {
    if (!v.is_object()) parse::fail(v, "a fire light is an object");
    parse::reject_unknown(
        v, {"description", "color", "intensity", "full", "range", "radius", "lift", "flicker", "casts_shadows", "gi"});
    FireLight l;
    if (const json::Value* c = v.find("color")) l.color = vec3_of(*c, 0.0, 1.0);
    l.intensity = number_or(v, "intensity", l.intensity, 0.0, 16.0);
    l.full = number_or(v, "full", l.full, 0.01, 10000.0);
    l.range = number_or(v, "range", l.range, 0.1, 64.0);
    l.radius = number_or(v, "radius", l.radius, 0.0, 2.0);
    l.lift = number_or(v, "lift", l.lift, -8.0, 8.0);
    l.flicker = number_or(v, "flicker", l.flicker, 0.0, 1.0);
    l.casts_shadows = bool_or(v, "casts_shadows", l.casts_shadows);
    l.gi = bool_or(v, "gi", l.gi);
    return l;
}

struct FireAccumulator {
    float amount = 0.0f;
    std::array<double, 3> sum{0, 0, 0};
};
std::array<FireAccumulator, std::size(kFireEffects)> s_fires{};

}  // namespace

Result<FireLights> parse_fire_lights(std::string_view json_text) try {
    const Result<json::Value> doc = json::parse(json_text);
    if (!doc) return Result<FireLights>::fail(doc.error());
    const json::Value& root = doc.value();
    if (!root.is_object()) throw std::runtime_error("the file must be an object");
    parse::reject_unknown(root, {"format", "version", "description", "fires"});
    FireLights out;
    const json::Value& fires = parse::member(root, "fires");
    if (!fires.is_object()) parse::fail(fires, "\"fires\" is an object of fire effects");
    for (const json::Value::Member& m : fires.members()) {
        const std::string& name = m.first;
        const json::Value& v = m.second;
        if (fire_index(name) < 0) parse::fail(v, "\"" + name + "\" is not a fire effect that reports its particles");
        if (v.is_null()) continue;
        out.emplace_back(name, fire_light_of(v));
    }
    return Result<FireLights>::ok(std::move(out));
} catch (const std::exception& e) {
    return Result<FireLights>::fail(e.what());
}

Result<FireLights> load_fire_lights(const std::filesystem::path& gamedata_dir, const std::filesystem::path& mods_dir,
                                    std::string* source) {
    const std::filesystem::path relative = std::filesystem::path("effects") / "fire_lights.json";
    std::string where;
    const Result<std::string> read = mods::read_data_text(gamedata_dir, mods_dir, relative, &where);
    if (source != nullptr) *source = where;
    if (!read) return Result<FireLights>::fail(read.error());
    Result<FireLights> parsed = parse_fire_lights(read.value());
    if (!parsed) return Result<FireLights>::fail(where + ": " + parsed.error());
    return parsed;
}

void fire_particle_shown(std::string_view fire, const std::array<float, 3>& position, float amount) {
    const int i = fire_index(fire);
    if (i < 0 || !(amount > 0.0f)) return;
    FireAccumulator& a = s_fires[static_cast<std::size_t>(i)];
    a.amount += amount;
    for (std::size_t k = 0; k < 3; ++k) a.sum[k] += static_cast<double>(position[k]) * amount;
}

FireSample take_fire_particles(std::string_view fire) {
    FireSample out;
    const int i = fire_index(fire);
    if (i < 0) return out;
    FireAccumulator& a = s_fires[static_cast<std::size_t>(i)];
    out.amount = a.amount;
    if (a.amount > 0.0f)
        for (std::size_t k = 0; k < 3; ++k) out.position[k] = static_cast<float>(a.sum[k] / a.amount);
    a = {};
    return out;
}

void effect_shown(std::string_view effect, float brightness) {
    if (const int i = effect_index(effect); i >= 0)
        s_shown[static_cast<std::size_t>(i)] = std::clamp(brightness, 0.0f, 1.0f);
}

std::optional<float> effect_brightness(std::string_view effect) {
    const int i = effect_index(effect);
    if (i < 0 || s_shown[static_cast<std::size_t>(i)] < 0.0f) return std::nullopt;
    return s_shown[static_cast<std::size_t>(i)];
}

namespace {
std::vector<std::pair<int, EffectLight>> s_effectLights;
}  // namespace

void effect_light_shown(int key, const EffectLight& light) {
    for (auto& [k, l] : s_effectLights) {
        if (k == key) {
            l = light;
            return;
        }
    }
    s_effectLights.emplace_back(key, light);
}

const std::vector<std::pair<int, EffectLight>>& effect_lights_shown() { return s_effectLights; }

void clear_effects_shown() {
    s_shown.fill(-1.0f);
    s_effectLights.clear();
}

Result<MapLights> parse_map_lights(std::string_view json_text) try {
    const Result<json::Value> doc = json::parse(json_text);
    if (!doc) return Result<MapLights>::fail(doc.error());
    const json::Value& root = doc.value();
    if (!root.is_object()) throw std::runtime_error("the file must be an object");
    parse::reject_unknown(root, {"format", "version", "map", "lights", "sun", "sky", "glows"});
    MapLights out;
    if (const json::Value* g = root.find("glows")) out.glows = glows_of(*g);
    if (const json::Value* list = root.find("lights")) {
        if (!list->is_array()) parse::fail(*list, "\"lights\" is a list");
        for (const json::Value& v : list->array()) {
            MapLight l = light_of(v);
            for (const MapLight& other : out.lights)
                if (other.name == l.name) parse::fail(v, "two lights are named \"" + l.name + "\"");
            out.lights.push_back(std::move(l));
        }
    }
    const json::Value* sun = root.find("sun");
    const json::Value* sky = root.find("sky");
    if ((sun != nullptr && !sun->is_null()) || (sky != nullptr && !sky->is_null())) {
        MapSun s;
        if (sun != nullptr && !sun->is_null()) {
            if (!sun->is_object()) parse::fail(*sun, "\"sun\" is an object or null");
            parse::reject_unknown(*sun, {"description", "direction", "color", "intensity", "angular_radius", "ceilings",
                                         "flags_set", "flags_clear", "when"});
            s.direction = unit_of(parse::member(*sun, "direction"));
            if (const json::Value* c = sun->find("color")) s.color = vec3_of(*c, 0.0, 1.0);
            s.intensity = number_or(*sun, "intensity", 1.0f, 0.0, 16.0);
            s.angular_radius = number_or(*sun, "angular_radius", 0.5f, 0.0, 10.0);
            s.flags_set = names_of(*sun, "flags_set");
            s.flags_clear = names_of(*sun, "flags_clear");
            when_of(*sun, &s.flags_set, &s.flags_clear);
            if (const json::Value* list = sun->find("ceilings")) s.ceilings = ceilings_of(*list);
        }
        if (sky != nullptr && !sky->is_null()) {
            if (!sky->is_object()) parse::fail(*sky, "\"sky\" is an object or null");
            parse::reject_unknown(*sky, {"description", "color", "intensity"});
            if (const json::Value* c = sky->find("color")) s.sky_color = vec3_of(*c, 0.0, 1.0);
            s.sky_intensity = number_or(*sky, "intensity", 1.0f, 0.0, 16.0);
        }
        out.sun = s;
    }
    return Result<MapLights>::ok(std::move(out));
} catch (const std::exception& e) {
    return Result<MapLights>::fail(e.what());
}

Result<MapLights> load_map_lights(const std::filesystem::path& gamedata_dir, const std::filesystem::path& mods_dir,
                                  std::string_view map, std::string* source) {
    const std::filesystem::path relative = std::filesystem::path("maps") / std::string(map) / "lights.json";
    std::vector<std::string> warnings;
    const Result<mods::MergedFile> read = mods::read_data_file(gamedata_dir, mods_dir, relative, warnings);
    mods::emit_merge_warnings(warnings);
    // Most maps have no lights of their own.
    if (!read) return Result<MapLights>::ok(MapLights{});
    mods::emit_merge_trace(read.value());
    if (source != nullptr) *source = read.value().source();
    Result<MapLights> parsed = parse_map_lights(read.value().text);
    if (!parsed) return Result<MapLights>::fail(read.value().source() + ": " + parsed.error());
    return parsed;
}

}  // namespace sh::effects
