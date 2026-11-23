// SPDX-License-Identifier: GPL-3.0-or-later
// gamedata/effects/combat_effects.json (combat_effects.h).

#include <stdexcept>
#include <utility>

#include "engine/effects/combat_effects.h"
#include "engine/effects/effects_file.h"
#include "engine/mods/data_merge.h"
#include "formats/common/json.h"

namespace sh::effects {

namespace {

using parse::boolean_of;
using parse::fail;
using parse::integer;
using parse::integer_of;
using parse::member;
using parse::reject_unknown;

Vec3s vec3s(const json::Value& v) {
    if (!v.is_array() || v.array().size() != 3) fail(v, "[x, y, z] is expected");
    return Vec3s{static_cast<std::int16_t>(integer(v.array()[0], -32768, 32767)),
                 static_cast<std::int16_t>(integer(v.array()[1], -32768, 32767)),
                 static_cast<std::int16_t>(integer(v.array()[2], -32768, 32767))};
}

MuzzleBones bones(const json::Value& v) {
    if (v.is_string() && v.string() == "weapon") return MuzzleBones::Weapon;
    if (v.is_string() && v.string() == "second") return MuzzleBones::Second;
    fail(v, "\"weapon\" or \"second\" is expected");
}

const std::vector<json::Value>& list(const json::Value& root, std::string_view key) {
    const json::Value& v = member(root, key);
    if (!v.is_array()) fail(v, "an array is expected");
    return v.array();
}

BloodHit blood_hit(const json::Value& v) {
    reject_unknown(v, {"sprays", "splats", "size"});
    BloodHit h;
    h.sprays = integer_of(v, "sprays", 0, 64);
    const json::Value& splats = member(v, "splats");
    if (splats.is_array()) {
        if (splats.array().size() != 2) fail(splats, "[min, max] is expected");
        h.splats_random = true;
        h.splats_min = integer(splats.array()[0], 0, 64);
        h.splats_max = integer(splats.array()[1], h.splats_min, 64);
    } else {
        h.splats_min = h.splats_max = integer(splats, 0, 64);
    }
    h.size = integer_of(v, "size", 0, 255);
    return h;
}

BodyPool body_pool(const json::Value& v) {
    reject_unknown(v, {"size", "patches"});
    BodyPool p;
    // A size below 4 would divide by zero in its random growth.
    p.size = static_cast<std::uint8_t>(integer_of(v, "size", 4, 255));
    for (const json::Value& e : list(v, "patches")) {
        if (!e.is_array() || e.array().size() != 4) fail(e, "[forward_range, forward_base, side_range, side_base]");
        PoolPatch patch;
        patch.forward_range = static_cast<std::int16_t>(integer(e.array()[0], 1, 32767));
        patch.forward_base = static_cast<std::int16_t>(integer(e.array()[1], -32768, 32767));
        patch.side_range = static_cast<std::int16_t>(integer(e.array()[2], 1, 32767));
        patch.side_base = static_cast<std::int16_t>(integer(e.array()[3], -32768, 32767));
        p.patches.push_back(patch);
    }
    return p;
}

MuzzleFlash muzzle_flash(const json::Value& v) {
    reject_unknown(v, {"life", "length", "half_width", "swirl", "muzzle", "bones"});
    MuzzleFlash f;
    f.life = integer_of(v, "life", 0, 32767);
    f.length = static_cast<std::int16_t>(integer_of(v, "length", 0, 32767));
    f.half_width = static_cast<std::int16_t>(integer_of(v, "half_width", 0, 32767));
    f.swirl = integer_of(v, "swirl", 0, 32767);
    f.muzzle = vec3s(member(v, "muzzle"));
    f.bones = bones(member(v, "bones"));
    return f;
}

GunSmoke gun_smoke(const json::Value& v) {
    reject_unknown(v, {"drift_after", "life"});
    GunSmoke s;
    s.drift_after = integer_of(v, "drift_after", 0, 65535);
    // The fade divides by the life.
    s.life = integer_of(v, "life", 1, 65535);
    return s;
}

float number_in(const json::Value& parent, std::string_view key, float fallback, double lo, double hi) {
    const json::Value* v = parent.find(key);
    if (v == nullptr) return fallback;
    if (!v->is_number() || v->number() < lo || v->number() > hi)
        fail(*v, "a number in " + std::to_string(lo) + " .. " + std::to_string(hi) + " is expected");
    return static_cast<float>(v->number());
}

GunLight gun_light(const json::Value& v) {
    if (!v.is_object()) fail(v, "\"muzzle_light\" is an object or null");
    reject_unknown(v, {"description", "color", "intensity", "range", "radius", "fade", "casts_shadows", "gi"});
    GunLight l;
    if (const json::Value* c = v.find("color")) {
        if (!c->is_array() || c->array().size() != 3) fail(*c, "[r, g, b] in 0 .. 1 is expected");
        for (std::size_t i = 0; i < 3; ++i) {
            const json::Value& e = c->array()[i];
            if (!e.is_number() || e.number() < 0.0 || e.number() > 1.0) fail(e, "[r, g, b] in 0 .. 1 is expected");
            l.color[i] = static_cast<float>(e.number());
        }
    }
    l.intensity = number_in(v, "intensity", l.intensity, 0.0, 64.0);
    l.range = number_in(v, "range", l.range, 0.1, 64.0);
    l.radius = number_in(v, "radius", l.radius, 0.0, 2.0);
    l.fade = number_in(v, "fade", l.fade, 0.0, 1.0);
    if (v.find("casts_shadows") != nullptr) l.casts_shadows = boolean_of(v, "casts_shadows");
    if (v.find("gi") != nullptr) l.gi = boolean_of(v, "gi");
    return l;
}

Gun gun(const json::Value& v, const CombatEffectParams& p) {
    reject_unknown(v, {"bones", "muzzle", "flash", "smoke", "puffs", "flash_angles", "muzzle_light"});
    Gun g;
    g.bones = bones(member(v, "bones"));
    g.muzzle = vec3s(member(v, "muzzle"));
    const json::Value& flash = member(v, "flash");
    if (!flash.is_null()) g.flash = integer(flash, 0, static_cast<std::int64_t>(p.flashes.size()) - 1);
    g.smoke = integer_of(v, "smoke", 0, static_cast<std::int64_t>(p.smoke.size()) - 1);
    g.puffs = integer_of(v, "puffs", 0, 5);
    if (const json::Value* a = v.find("flash_angles"); a != nullptr && !a->is_null()) {
        if (!a->is_array() || a->array().size() != 2) fail(*a, "[yaw, pitch] is expected");
        g.flash_angles = std::array<std::int16_t, 2>{static_cast<std::int16_t>(integer(a->array()[0], -32768, 32767)),
                                                     static_cast<std::int16_t>(integer(a->array()[1], -32768, 32767))};
    }
    if (const json::Value* l = v.find("muzzle_light"); l != nullptr && !l->is_null()) g.light = gun_light(*l);
    return g;
}

}  // namespace

Result<CombatEffectParams> parse_combat_effect_params(std::string_view json_text) try {
    const Result<json::Value> doc = json::parse(json_text);
    if (!doc) return Result<CombatEffectParams>::fail(doc.error());
    const json::Value& root = doc.value();
    reject_unknown(root, {"blood_hits", "body_pools", "muzzle_flashes", "gun_smoke", "guns"});

    CombatEffectParams p;
    for (const json::Value& v : list(root, "blood_hits")) p.blood_hits.push_back(blood_hit(v));
    for (const json::Value& v : list(root, "body_pools")) p.body_pools.push_back(body_pool(v));
    // The flash and smoke rows are slot kinds 15..19 and 20..22.
    for (const json::Value& v : list(root, "muzzle_flashes")) p.flashes.push_back(muzzle_flash(v));
    if (p.flashes.size() > 5) fail(member(root, "muzzle_flashes"), "at most 5 flashes (slot kinds 15..19)");
    for (const json::Value& v : list(root, "gun_smoke")) p.smoke.push_back(gun_smoke(v));
    if (p.smoke.size() > 3) fail(member(root, "gun_smoke"), "at most 3 smokes (slot kinds 20..22)");
    const json::Value& guns = member(root, "guns");
    if (!guns.is_object()) fail(guns, "an object is expected");
    for (const auto& [name, v] : guns.members()) {
        if (name.rfind('_', 0) == 0) continue;
        p.guns[name] = gun(v, p);
    }
    return Result<CombatEffectParams>::ok(std::move(p));
} catch (const std::exception& e) {
    return Result<CombatEffectParams>::fail(e.what());
}

Result<CombatEffectParams> load_combat_effect_params(const std::filesystem::path& gamedata_dir,
                                                     const std::filesystem::path& mods_dir, std::string* source) {
    const std::filesystem::path relative = std::filesystem::path("effects") / "combat_effects.json";
    std::string where;
    const Result<std::string> read = mods::read_data_text(gamedata_dir, mods_dir, relative, &where);
    if (source != nullptr) *source = where;
    if (!read) return Result<CombatEffectParams>::fail(read.error());
    Result<CombatEffectParams> parsed = parse_combat_effect_params(read.value());
    if (!parsed) return Result<CombatEffectParams>::fail(where + ": " + parsed.error());
    return parsed;
}

}  // namespace sh::effects
