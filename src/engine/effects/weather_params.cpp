// SPDX-License-Identifier: GPL-3.0-or-later
// gamedata/maps/<map>/particles.json.

#include <stdexcept>

#include "engine/effects/effects_file.h"
#include "engine/effects/weather.h"
#include "formats/common/json.h"

namespace sh::effects {

namespace {

using json::Value;

double number_of(const Value& v) {
    if (!v.is_number()) {
        parse::fail(v, "a number is expected");
    }
    return v.number();
}

std::int32_t metres(const Value& v) { return q12_from_metres(number_of(v)); }

std::optional<std::int32_t> opt_metres(const Value& parent, std::string_view key) {
    const Value* v = parent.find(key);
    if (v == nullptr) {
        return std::nullopt;
    }
    return metres(*v);
}

std::vector<int> int_list(const Value& v, int lo, int hi) {
    if (!v.is_array()) {
        parse::fail(v, "a list of integers is expected");
    }
    std::vector<int> out;
    for (const Value& e : v.array()) {
        out.push_back(parse::integer(e, lo, hi));
    }
    return out;
}

ApertureLine line_of(const Value& v) {
    if (!v.is_array() || v.array().size() != 3) {
        parse::fail(v, "a line is [corner a, corner b, flag]");
    }
    ApertureLine l;
    l.a = parse::integer(v.array()[0], 0, 7);
    l.b = parse::integer(v.array()[1], 0, 7);
    l.flag = parse::integer(v.array()[2], 0, 1);
    return l;
}

std::vector<ApertureLine> lines_of(const Value& v) {
    if (!v.is_array()) {
        parse::fail(v, "a list of lines is expected");
    }
    std::vector<ApertureLine> out;
    for (const Value& e : v.array()) {
        out.push_back(line_of(e));
    }
    return out;
}

ApertureKind kind_of(const Value& v) {
    parse::reject_unknown(v, {"type", "lines", "extra", "x_below", "override_lines"});
    ApertureKind k;
    const Value* type = v.find("type");
    if (type != nullptr) {
        if (!type->is_string() || (type->string() != "chain" && type->string() != "roof")) {
            parse::fail(*type, "\"type\" is \"chain\" or \"roof\"");
        }
        k.type = type->string() == "roof" ? ApertureKind::Type::Roof : ApertureKind::Type::Chain;
    }
    if (k.type == ApertureKind::Type::Chain) {
        k.lines = lines_of(parse::member(v, "lines"));
    }
    if (const Value* extra = v.find("extra")) {
        if (!extra->is_array()) {
            parse::fail(*extra, "\"extra\" is a list of {room, line}");
        }
        for (const Value& e : extra->array()) {
            parse::reject_unknown(e, {"room", "line"});
            ApertureKind::Extra x;
            x.room = parse::integer_of(e, "room", 0, 255);
            x.line = line_of(parse::member(e, "line"));
            k.extras.push_back(x);
        }
    }
    k.x_below = opt_metres(v, "x_below");
    if (const Value* o = v.find("override_lines")) {
        k.override_lines = lines_of(*o);
    }
    if (k.x_below.has_value() != !k.override_lines.empty()) {
        parse::fail(v, "\"x_below\" and \"override_lines\" go together");
    }
    return k;
}

Aperture aperture_of(const Value& v) {
    parse::reject_unknown(v, {"modes", "fallback"});
    Aperture a;
    if (const Value* modes = v.find("modes")) {
        if (!modes->is_object()) {
            parse::fail(*modes, "\"modes\" maps a mode number to its kind");
        }
        for (const auto& [key, value] : modes->members()) {
            if (!key.empty() && key[0] == '_') {
                continue;
            }
            int mode = 0;
            try {
                mode = std::stoi(key);
            } catch (const std::exception&) {
                parse::fail(value, "mode keys are numbers");
            }
            a.by_mode.emplace_back(mode, kind_of(value));
        }
    }
    if (const Value* f = v.find("fallback")) {
        a.fallback = kind_of(*f);
    }
    return a;
}

CornerSet corner_of(const Value& v) {
    if (!v.is_array() || (v.array().size() != 2 && v.array().size() != 3)) {
        parse::fail(v, "a corner is [x, z] or [x, y, z] in metres");
    }
    CornerSet c;
    c.x = metres(v.array()[0]);
    if (v.array().size() == 3) {
        c.has_y = true;
        c.y = metres(v.array()[1]);
        c.z = metres(v.array()[2]);
    } else {
        c.z = metres(v.array()[1]);
    }
    return c;
}

RoomRule rule_of(const Value& v) {
    parse::reject_unknown(v, {"rooms", "when", "mode", "corners", "skip"});
    RoomRule r;
    if (const Value* rooms = v.find("rooms")) {
        r.rooms = int_list(*rooms, 0, 255);
    }
    if (const Value* when = v.find("when")) {
        parse::reject_unknown(*when, {"x_gt", "x_lt", "x_le", "z_gt", "z_lt"});
        r.when.x_gt = opt_metres(*when, "x_gt");
        r.when.x_lt = opt_metres(*when, "x_lt");
        r.when.x_le = opt_metres(*when, "x_le");
        r.when.z_gt = opt_metres(*when, "z_gt");
        r.when.z_lt = opt_metres(*when, "z_lt");
    }
    if (const Value* mode = v.find("mode")) {
        r.mode = parse::integer(*mode, 0, 15);
    }
    if (const Value* corners = v.find("corners")) {
        if (!corners->is_array() || corners->array().size() > 8) {
            parse::fail(*corners, "\"corners\" is a list of at most 8 corners");
        }
        for (const Value& c : corners->array()) {
            r.corners.push_back(corner_of(c));
        }
    }
    if (const Value* skip = v.find("skip")) {
        if (!skip->is_bool()) {
            parse::fail(*skip, "\"skip\" is true or false");
        }
        r.skip = skip->boolean();
    }
    return r;
}

std::vector<RoomRule> rules_of(const Value& v) {
    if (!v.is_array()) {
        parse::fail(v, "a list of rules is expected");
    }
    std::vector<RoomRule> out;
    for (const Value& e : v.array()) {
        out.push_back(rule_of(e));
    }
    return out;
}

bool bool_or(const Value& parent, std::string_view key, bool fallback) {
    const Value* v = parent.find(key);
    if (v == nullptr) {
        return fallback;
    }
    if (!v->is_bool()) {
        parse::fail(*v, "\"" + std::string(key) + "\" must be true or false");
    }
    return v->boolean();
}

SnowParams snow_of(const Value& v) {
    parse::reject_unknown(v, {"range", "start_height", "landing", "drifting", "hidden_rooms", "only_rooms",
                              "far_slot_mode", "aperture"});
    SnowParams s;
    if (const Value* r = v.find("range")) {
        s.range = parse::integer(*r, 1, 12);
    }
    if (const Value* h = v.find("start_height")) {
        s.start_height = metres(*h);
    }
    if (const Value* l = v.find("landing")) {
        if (!l->is_string()) {
            parse::fail(*l, "\"landing\" is \"settle\", \"recycle\" or \"flat\"");
        }
        if (l->string() == "settle") {
            s.landing = SnowParams::Landing::Settle;
        } else if (l->string() == "recycle") {
            s.landing = SnowParams::Landing::Recycle;
        } else if (l->string() == "flat") {
            s.landing = SnowParams::Landing::Flat;
        } else {
            parse::fail(*l, "\"landing\" is \"settle\", \"recycle\" or \"flat\"");
        }
    }
    s.drifting = bool_or(v, "drifting", false);
    if (const Value* h = v.find("hidden_rooms")) {
        s.hidden_rooms = int_list(*h, 0, 255);
    }
    if (const Value* o = v.find("only_rooms")) {
        s.only_rooms = int_list(*o, 0, 255);
    }
    if (const Value* f = v.find("far_slot_mode")) {
        s.far_slot_mode = parse::integer(*f, 0, 15);
    }
    if (const Value* a = v.find("aperture")) {
        s.aperture = aperture_of(*a);
    }
    return s;
}

RainParams rain_of(const Value& v) {
    parse::reject_unknown(v, {"light", "boundary", "match_anim", "pool_splash", "after_rest", "sound"});
    RainParams r;
    r.light = bool_or(v, "light", false);
    r.boundary = bool_or(v, "boundary", false);
    r.match_anim = bool_or(v, "match_anim", false);
    r.pool_splash = bool_or(v, "pool_splash", false);
    r.after_rest = bool_or(v, "after_rest", false);
    r.sound = bool_or(v, "sound", false);
    return r;
}

}  // namespace

Result<WeatherParams> parse_weather_params(std::string_view json_text) try {
    const Result<Value> doc = json::parse(json_text);
    if (!doc) {
        return Result<WeatherParams>::fail(doc.error());
    }
    const Value& root = doc.value();
    parse::reject_unknown(root, {"particle_max", "snow", "rain", "rest_gate", "wind_x_off", "insects", "suspend", "init", "update"});
    WeatherParams p;
    if (const Value* m = root.find("particle_max")) {
        p.particle_max = parse::integer(*m, 8, 450);
    }
    if (const Value* s = root.find("snow")) {
        p.snow_on = true;
        p.snow = snow_of(*s);
    }
    if (const Value* r = root.find("rain")) {
        p.rain_on = true;
        p.rain = rain_of(*r);
    }
    p.rest_gate = bool_or(root, "rest_gate", false);
    p.wind_x_off = bool_or(root, "wind_x_off", false);
    p.insects = bool_or(root, "insects", false);
    if (const Value* s = root.find("suspend")) {
        parse::reject_unknown(*s, {"event_param", "step"});
        WeatherParams::Suspend sp;
        sp.event_param = parse::integer_of(*s, "event_param", 0, 4095);
        sp.step = parse::integer_of(*s, "step", 0, 255);
        p.suspend = sp;
    }
    if (const Value* i = root.find("init")) {
        p.init_rules = rules_of(*i);
    }
    if (const Value* u = root.find("update")) {
        p.update_rules = rules_of(*u);
    }
    return Result<WeatherParams>::ok(std::move(p));
} catch (const std::exception& e) {
    return Result<WeatherParams>::fail(e.what());
}

Result<WeatherParams> load_weather_params(const std::filesystem::path& gamedata_dir,
                                          const std::filesystem::path& mods_dir, std::string_view map,
                                          std::string* source, bool* found) {
    namespace fs = std::filesystem;
    const fs::path relative = fs::path("maps") / std::string(map) / "particles.json";
    std::string where;
    Result<std::string> text = read_effect_file(gamedata_dir, mods_dir, relative, &where);
    if (source != nullptr) {
        *source = where;
    }
    std::error_code ec;
    const bool exists = fs::is_regular_file(where, ec);
    if (found != nullptr) {
        *found = exists;
    }
    if (!exists) {
        return Result<WeatherParams>::ok(WeatherParams{});
    }
    if (!text) {
        return Result<WeatherParams>::fail(text.error());
    }
    Result<WeatherParams> parsed = parse_weather_params(text.value());
    if (!parsed) {
        return Result<WeatherParams>::fail(where + ": " + parsed.error());
    }
    return parsed;
}

}  // namespace sh::effects
