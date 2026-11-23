// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/gconfig/map_info.h"

#include "engine/mods/data_merge.h"
#include "formats/common/json.h"

#include <fstream>
#include <iterator>
#include <stdexcept>

namespace sh::gconfig {

namespace {

using json::Value;

[[noreturn]] void fail(const Value& at, const std::string& what) {
    throw std::runtime_error("map_info.json line " + std::to_string(at.line()) + ": " + what);
}

const Value& need(const Value& obj, std::string_view key) {
    const Value* v = obj.is_object() ? obj.find(key) : nullptr;
    if (!v) {
        fail(obj, "missing \"" + std::string(key) + "\"");
    }
    return *v;
}

std::int64_t int_in(const Value& v, std::int64_t lo, std::int64_t hi, std::string_view what) {
    if (!v.is_integer() || v.integer() < lo || v.integer() > hi) {
        fail(v, std::string(what) + " must be an integer in " + std::to_string(lo) + ".." + std::to_string(hi));
    }
    return v.integer();
}

template <class T, std::size_t N>
std::array<T, N> triple(const Value& v, std::string_view what, std::int64_t lo, std::int64_t hi) {
    if (!v.is_array() || v.array().size() != N) {
        fail(v, std::string(what) + " must be an array of " + std::to_string(N) + " numbers");
    }
    std::array<T, N> out{};
    for (std::size_t i = 0; i < N; ++i) {
        out[i] = static_cast<T>(int_in(v.array()[i], lo, hi, what));
    }
    return out;
}

std::uint8_t flag_bit(const Value& name) {
    if (!name.is_string()) {
        fail(name, "flags are strings");
    }
    const std::string& s = name.string();
    if (s == "OneActiveChunk") return kMapOneActiveChunk;
    if (s == "TwoActiveChunks") return kMapTwoActiveChunks;
    if (s == "Interior") return kMapInterior;
    if (s == "Unused3") return kMapUnused3;
    fail(name, "unknown map flag \"" + s + "\"");
}

std::int16_t q4(const Value& obj, std::string_view key) {
    return static_cast<std::int16_t>(int_in(need(obj, key), -32768, 32767, key));
}

}  // namespace

const std::vector<SpeedZoneData>* MapInfoLibrary::speed_zones(const std::string& name) const {
    const auto it = speed_.find(name);
    return it == speed_.end() ? nullptr : &it->second;
}

const std::vector<WaterZoneData>* MapInfoLibrary::water_zones(const std::string& name) const {
    const auto it = water_.find(name);
    return it == water_.end() ? nullptr : &it->second;
}

Result<MapInfoLibrary> MapInfoLibrary::parse(std::string_view text) try {
    const Result<Value> doc = json::parse(text);
    if (!doc) {
        return Result<MapInfoLibrary>::fail("map_info.json: " + doc.error());
    }
    const Value& root = doc.value();
    MapInfoLibrary lib;

    if (const Value* speed = root.find("speed_zones"); speed && speed->is_object()) {
        for (const auto& [name, list] : speed->members()) {
            if (!list.is_array()) fail(list, "zone set \"" + name + "\" must be an array");
            std::vector<SpeedZoneData>& out = lib.speed_[name];
            for (const Value& z : list.array()) {
                SpeedZoneData d;
                const Value& type = need(z, "type");
                if (!type.is_string()) fail(type, "\"type\" must be Slow, Normal or Fast");
                if (type.string() == "Slow") d.kind = SpeedZoneKind::Slow;
                else if (type.string() == "Normal") d.kind = SpeedZoneKind::Normal;
                else if (type.string() == "Fast") d.kind = SpeedZoneKind::Fast;
                else fail(type, "unknown speed zone type \"" + type.string() + "\"");
                d.min_x = q4(z, "min_x"); d.max_x = q4(z, "max_x");
                d.min_z = q4(z, "min_z"); d.max_z = q4(z, "max_z");
                out.push_back(d);
            }
        }
    } else {
        fail(root, "\"speed_zones\" must be an object");
    }

    if (const Value* water = root.find("water_zones"); water && water->is_object()) {
        for (const auto& [name, list] : water->members()) {
            if (!list.is_array()) fail(list, "zone set \"" + name + "\" must be an array");
            std::vector<WaterZoneData>& out = lib.water_[name];
            for (const Value& z : list.array()) {
                WaterZoneData d;
                const Value& en = need(z, "enabled");
                if (!en.is_bool()) fail(en, "\"enabled\" must be true or false");
                d.enabled = en.boolean();
                d.illumination = static_cast<std::int16_t>(int_in(need(z, "illumination"), -32768, 32767, "illumination"));
                d.min_x = q4(z, "min_x"); d.max_x = q4(z, "max_x");
                d.min_z = q4(z, "min_z"); d.max_z = q4(z, "max_z");
                out.push_back(d);
            }
        }
    } else {
        fail(root, "\"water_zones\" must be an object");
    }

    const Value& maps = need(root, "maps");
    if (!maps.is_array() || maps.array().empty()) fail(maps, "\"maps\" must be a non-empty array");
    for (const Value& m : maps.array()) {
        MapTypeData d;
        const Value& tag = need(m, "tag");
        if (!tag.is_string() || tag.string().empty() || tag.string().size() > 3) fail(tag, "\"tag\" must be 1-3 characters");
        d.tag = tag.string();
        if (const Value* plm = m.find("plm"); plm && !plm->is_null()) {
            if (!plm->is_string()) fail(*plm, "\"plm\" must be a string or null");
            d.plm = plm->string();
        }
        const Value& flags = need(m, "flags");
        if (!flags.is_array()) fail(flags, "\"flags\" must be an array");
        for (const Value& f : flags.array()) d.flags |= flag_bit(f);
        const Value& sz = need(m, "speed_zones");
        if (!sz.is_string() || !lib.speed_.count(sz.string())) fail(sz, "unknown speed zone set");
        d.speed_zones = sz.string();
        if (const Value* wz = m.find("water_zones"); wz && !wz->is_null()) {
            if (!wz->is_string() || !lib.water_.count(wz->string())) fail(*wz, "unknown water zone set");
            d.water_zones = wz->string();
        }
        lib.maps_.push_back(std::move(d));
    }

    const Value& envs = need(root, "environments");
    if (!envs.is_array() || envs.array().size() != kEnvironmentCount) {
        fail(envs, "\"environments\" must have exactly " + std::to_string(kEnvironmentCount) + " entries");
    }
    for (const Value& e : envs.array()) {
        EnvPresetData d;
        d.flags = static_cast<std::uint8_t>(int_in(need(e, "flags"), 0, 255, "flags"));
        d.light_level = static_cast<std::uint8_t>(int_in(need(e, "light_level"), 0, 255, "light_level"));
        d.light_mode = static_cast<std::uint8_t>(int_in(need(e, "light_mode"), 0, 255, "light_mode"));
        d.unused_byte = static_cast<std::uint8_t>(int_in(need(e, "unused_byte"), 0, 255, "unused_byte"));
        d.alpha = static_cast<std::int16_t>(int_in(need(e, "alpha"), -32768, 32767, "alpha"));
        d.tint_intensity = static_cast<std::int16_t>(int_in(need(e, "tint_intensity"), -32768, 32767, "tint_intensity"));
        d.tint = triple<std::int16_t, 3>(need(e, "tint"), "tint", -32768, 32767);
        d.fog_mode = static_cast<std::uint8_t>(int_in(need(e, "fog_mode"), 0, 255, "fog_mode"));
        d.fog_distance = static_cast<std::int32_t>(int_in(need(e, "fog_distance"), INT32_MIN, INT32_MAX, "fog_distance"));
        d.fog_color = triple<std::uint8_t, 3>(need(e, "fog_color"), "fog_color", 0, 255);
        d.screen_tint_on = static_cast<std::uint8_t>(int_in(need(e, "screen_tint_on"), 0, 255, "screen_tint_on"));
        d.screen_tint_add = triple<std::uint8_t, 3>(need(e, "screen_tint_add"), "screen_tint_add", 0, 255);
        d.screen_tint_sub = triple<std::uint8_t, 3>(need(e, "screen_tint_sub"), "screen_tint_sub", 0, 255);
        d.particle_color_a = triple<std::uint8_t, 3>(need(e, "particle_color_a"), "particle_color_a", 0, 255);
        d.particle_color_b = triple<std::uint8_t, 3>(need(e, "particle_color_b"), "particle_color_b", 0, 255);
        lib.environments_.push_back(d);
    }
    return Result<MapInfoLibrary>::ok(std::move(lib));
} catch (const std::exception& e) {
    return Result<MapInfoLibrary>::fail(e.what());
}

Result<MapInfoLibrary> MapInfoLibrary::load(const std::filesystem::path& gamedata_dir, const std::filesystem::path& mods_dir,
                                            std::vector<std::string>& warnings, MapInfoLoadSource* source) {
    namespace fs = std::filesystem;
    const fs::path relative = fs::path("config") / "map_info.json";
    // Base + every active mod's patch, in load order.
    Result<mods::MergedFile> read = mods::read_data_file(gamedata_dir, mods_dir, relative, warnings);
    if (!read) {
        return Result<MapInfoLibrary>::fail(read.error());
    }
    if (source) source->mod_id = read.value().last_mod();
    return parse(read.value().text);
}

}  // namespace sh::gconfig
