// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/gameflow/collision_zones.h"

#include <array>
#include <limits>
#include <utility>

#include "formats/common/json.h"

namespace sh::gameflow {

namespace {

constexpr std::array<std::pair<std::string_view, std::uint16_t>, 4> kFlagNames = {{
    {"Map", 1u << 0},
    {"Objects", 1u << 1},
    {"AltObjects", 1u << 2},
    {"Unused3", 1u << 3},
}};

Result<std::uint16_t> flag_set(const json::Value* list, std::string_view where) {
    std::uint16_t bits = 0;
    if (list == nullptr) {
        return Result<std::uint16_t>::ok(bits);
    }
    if (!list->is_array()) {
        return Result<std::uint16_t>::fail(std::string(where) + " must be an array of flag names");
    }
    for (const json::Value& v : list->array()) {
        bool known = false;
        if (v.is_string()) {
            for (const auto& [name, bit] : kFlagNames) {
                if (v.string() == name) {
                    bits = static_cast<std::uint16_t>(bits | bit);
                    known = true;
                }
            }
        }
        if (!known) {
            return Result<std::uint16_t>::fail(std::string(where) +
                                               ": unknown flag (Map, Objects, AltObjects, Unused3)");
        }
    }
    return Result<std::uint16_t>::ok(bits);
}

Result<std::int32_t> coord(const json::Value& zone, std::string_view key, std::string_view where) {
    const json::Value* v = zone.find(key);
    if (v == nullptr || !v->is_integer() || v->integer() < std::numeric_limits<std::int32_t>::min() ||
        v->integer() > std::numeric_limits<std::int32_t>::max()) {
        return Result<std::int32_t>::fail(std::string(where) + ": \"" + std::string(key) +
                                          "\" must be a 32-bit integer (Q12 metres)");
    }
    return Result<std::int32_t>::ok(static_cast<std::int32_t>(v->integer()));
}

}  // namespace

CollisionZones CollisionZones::builtin() {
    CollisionZones out;
    out.zones_.push_back({{"THR"}, 784793, 814284, -393216, -369868, 1u << 1, 1u << 2});
    out.zones_.push_back({{"SPR", "SPU"}, -409600, -387072, -287948, -253952, 1u << 1, 1u << 2});
    return out;
}

Result<CollisionZones> CollisionZones::parse(std::string_view text) {
    const Result<json::Value> doc = json::parse(text);
    if (!doc) {
        return Result<CollisionZones>::fail("collision_zones.json: " + doc.error());
    }
    const json::Value* zones = doc.value().find("zones");
    if (zones == nullptr || !zones->is_array()) {
        return Result<CollisionZones>::fail("collision_zones.json: \"zones\" must be an array");
    }
    CollisionZones out;
    for (std::size_t i = 0; i < zones->array().size(); ++i) {
        const json::Value& z = zones->array()[i];
        const std::string where = "collision_zones.json: zones[" + std::to_string(i) + "]";
        CollisionZone zone;
        const json::Value* maps = z.find("maps");
        if (maps == nullptr || !maps->is_array() || maps->array().empty()) {
            return Result<CollisionZones>::fail(where + ": \"maps\" must be a non-empty array of map tags");
        }
        for (const json::Value& m : maps->array()) {
            if (!m.is_string() || m.string().empty() || m.string().size() > 3) {
                return Result<CollisionZones>::fail(where + ": a map tag is a string of 1 to 3 characters");
            }
            zone.maps.push_back(m.string());
        }
        const Result<std::int32_t> min_x = coord(z, "min_x", where), max_x = coord(z, "max_x", where);
        const Result<std::int32_t> min_z = coord(z, "min_z", where), max_z = coord(z, "max_z", where);
        for (const Result<std::int32_t>* c : {&min_x, &max_x, &min_z, &max_z}) {
            if (!*c) {
                return Result<CollisionZones>::fail(c->error());
            }
        }
        zone.min_x = min_x.value();
        zone.max_x = max_x.value();
        zone.min_z = min_z.value();
        zone.max_z = max_z.value();
        const Result<std::uint16_t> clear = flag_set(z.find("clear"), where + ".clear");
        if (!clear) {
            return Result<CollisionZones>::fail(clear.error());
        }
        const Result<std::uint16_t> set = flag_set(z.find("set"), where + ".set");
        if (!set) {
            return Result<CollisionZones>::fail(set.error());
        }
        zone.clear = clear.value();
        zone.set = set.value();
        out.zones_.push_back(std::move(zone));
    }
    return Result<CollisionZones>::ok(std::move(out));
}

std::uint16_t CollisionZones::apply(std::string_view map, std::int32_t x, std::int32_t z, std::uint16_t flags) const {
    for (const CollisionZone& zone : zones_) {
        bool here = false;
        for (const std::string& m : zone.maps) {
            here = here || m == map;
        }
        if (here && x >= zone.min_x && x <= zone.max_x && z >= zone.min_z && z <= zone.max_z) {
            flags = static_cast<std::uint16_t>((flags & ~zone.clear) | zone.set);
        }
    }
    return flags;
}

}  // namespace sh::gameflow
