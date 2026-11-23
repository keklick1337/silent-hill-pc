// SPDX-License-Identifier: GPL-3.0-or-later
// gamedata/effects/map0_s00_effect.json.

#include <stdexcept>

#include "engine/effects/effects_file.h"
#include "engine/effects/insects.h"
#include "engine/effects/weather.h"  // q12_from_metres
#include "formats/common/json.h"

namespace sh::effects {

namespace {

using json::Value;

std::int32_t metres_of(const Value& parent, std::string_view key) {
    const Value& v = parse::member(parent, key);
    if (!v.is_number()) {
        parse::fail(v, "a number is expected");
    }
    return q12_from_metres(v.number());
}

std::int32_t metres_value(const Value& v) {
    if (!v.is_number()) {
        parse::fail(v, "a number is expected");
    }
    return q12_from_metres(v.number());
}

TextureRef texture_of(const Value& t) {
    TextureRef out;
    out.page = static_cast<std::uint16_t>(parse::integer_of(t, "page", 0, 0xFFFF));
    const Value& pal = parse::member(t, "palette");
    if (!pal.is_array() || pal.array().size() != 2) {
        parse::fail(pal, "\"palette\" is [x, y] in video memory");
    }
    const std::int32_t x = parse::integer(pal.array()[0], 0, 1008);
    if (x % 16 != 0) {
        parse::fail(pal.array()[0], "a palette x is a multiple of 16");
    }
    out.palette_x = static_cast<std::uint16_t>(x);
    out.palette_y = static_cast<std::uint16_t>(parse::integer(pal.array()[1], 0, 511));
    return out;
}

InsectSheet sheet_of(const Value& v) {
    parse::reject_unknown(v, {"page", "palette", "u_base", "cell_size", "cells", "v", "cell_frames", "min_slot"});
    InsectSheet s;
    s.texture = texture_of(v);
    s.u_base = parse::integer_of(v, "u_base", 0, 255);
    s.cell_size = parse::integer_of(v, "cell_size", 1, 255);
    s.cells = parse::integer_of(v, "cells", 1, 16);
    const Value& rows = parse::member(v, "v");
    if (!rows.is_array() || rows.array().size() != 2) {
        parse::fail(rows, "\"v\" is [top, bottom] in texels");
    }
    s.v_top = parse::integer(rows.array()[0], 0, 255);
    s.v_bottom = parse::integer(rows.array()[1], 0, 255);
    s.cell_frames = parse::integer_of(v, "cell_frames", 1, 255);
    s.min_slot = parse::integer_of(v, "min_slot", 0, 4095);
    if (s.u_base + s.cells * s.cell_size > 256) {
        parse::fail(v, "the cells run past the right edge of the texture page");
    }
    return s;
}

std::vector<std::int32_t> int_list(const Value& parent, std::string_view key) {
    const Value& v = parse::member(parent, key);
    if (!v.is_array()) {
        parse::fail(v, "a list of numbers is expected");
    }
    std::vector<std::int32_t> out;
    for (const Value& e : v.array()) {
        out.push_back(parse::integer(e, -128, 127));
    }
    return out;
}

}  // namespace

Result<InsectConfig> parse_insect_config(std::string_view json_text) try {
    const Result<Value> doc = json::parse(json_text);
    if (!doc) {
        return Result<InsectConfig>::fail(doc.error());
    }
    const Value& root = doc.value();
    parse::reject_unknown(root, {"anchors", "activate_within", "flies", "crawlers", "look"});
    InsectConfig out;

    const Value& anchors = parse::member(root, "anchors");
    if (!anchors.is_array() || anchors.array().size() != 4) {
        parse::fail(anchors, "\"anchors\" is four [x, y, z] points in metres");
    }
    for (std::size_t i = 0; i < 4; i++) {
        const Value& a = anchors.array()[i];
        if (!a.is_array() || a.array().size() != 3) {
            parse::fail(a, "[x, y, z] in metres is expected");
        }
        out.anchors[i] = Vec3{metres_value(a.array()[0]), metres_value(a.array()[1]), metres_value(a.array()[2])};
    }
    out.active_distance = metres_of(root, "activate_within");

    const Value& fv = parse::member(root, "flies");
    parse::reject_unknown(fv, {"count", "spread", "height", "dart_chance", "sheet"});
    out.fly_count = parse::integer_of(fv, "count", 0, 255);
    out.fly_spread = metres_of(fv, "spread");
    out.fly_height = metres_of(fv, "height");
    out.dart_chance = parse::integer_of(fv, "dart_chance", 1, 65536);
    out.look.flies = sheet_of(parse::member(fv, "sheet"));
    if (out.fly_spread <= 0 || out.fly_height <= 0) {
        parse::fail(fv, "spread and height are above zero");
    }

    const Value& cv = parse::member(root, "crawlers");
    parse::reject_unknown(cv, {"count", "per_group", "spread", "startle_radius", "brightness", "sheet"});
    out.crawler_count = parse::integer_of(cv, "count", 0, 255);
    out.crawlers_per_group = parse::integer_of(cv, "per_group", 1, 255);
    out.crawler_spread = metres_of(cv, "spread");
    out.startle_radius_q8 = metres_of(cv, "startle_radius") >> 4;
    out.look.crawler_brightness = static_cast<std::uint8_t>(parse::integer_of(cv, "brightness", 0, 255));
    out.look.crawlers = sheet_of(parse::member(cv, "sheet"));
    if (out.crawler_spread <= 0) {
        parse::fail(cv, "spread is above zero");
    }

    const Value& lv = parse::member(root, "look");
    parse::reject_unknown(lv, {"size", "ordering_table_size", "darkness_cap", "full_darkness_modes", "plain_modes"});
    const Value& size = parse::member(lv, "size");
    parse::reject_unknown(size, {"min", "max", "far_steps"});
    out.look.size_min = parse::integer_of(size, "min", 1, 255);
    out.look.size_max = parse::integer_of(size, "max", 1, 255);
    out.look.size_far_steps = parse::integer_of(size, "far_steps", 1, 255);
    if (out.look.size_max < out.look.size_min) {
        parse::fail(size, "max is below min");
    }
    out.look.ordering_table_size = parse::integer_of(lv, "ordering_table_size", 4, 1 << 16);
    out.look.darkness_cap = parse::integer_of(lv, "darkness_cap", 1, 255);
    out.look.full_darkness_modes = int_list(lv, "full_darkness_modes");
    out.look.plain_modes = int_list(lv, "plain_modes");
    return Result<InsectConfig>::ok(std::move(out));
} catch (const std::exception& e) {
    return Result<InsectConfig>::fail(e.what());
}

Result<InsectConfig> load_insect_config(const std::filesystem::path& gamedata_dir,
                                        const std::filesystem::path& mods_dir, std::string* source) {
    std::string where;
    Result<std::string> text =
        read_effect_file(gamedata_dir, mods_dir, std::filesystem::path("effects") / "map0_s00_effect.json", &where);
    if (source != nullptr) {
        *source = where;
    }
    if (!text) {
        return Result<InsectConfig>::fail(text.error());
    }
    Result<InsectConfig> parsed = parse_insect_config(text.value());
    if (!parsed) {
        return Result<InsectConfig>::fail(where + ": " + parsed.error());
    }
    return parsed;
}

}  // namespace sh::effects
