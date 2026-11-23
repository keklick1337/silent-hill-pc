// SPDX-License-Identifier: GPL-3.0-or-later
// Acid spit and gamedata/effects/acid.json.

#include "engine/effects/acid.h"

#include <algorithm>
#include <cstdlib>
#include <stdexcept>

#include "engine/effects/effects_file.h"
#include "engine/effects/weather.h"  // q12_from_metres
#include "formats/common/json.h"

namespace sh::effects {

namespace {

using json::Value;

constexpr std::int32_t kQ12 = 4096;

std::int32_t q12_mult(std::int32_t a, std::int32_t b) { return (a * b) >> 12; }
// 64-bit intermediate, truncated to 32 bits like the original's precise multiply.
std::int32_t q12_mult_precise(std::int32_t a, std::int32_t b) {
    return static_cast<std::int32_t>((static_cast<std::int64_t>(a) * static_cast<std::int64_t>(b)) >> 12);
}

// One of the four 64x64 cells of the 128x128 glow texture.
struct Cell {
    std::uint8_t u0, u1, v0, v1;
};
Cell cell(bool right, bool lower) {
    return Cell{static_cast<std::uint8_t>(right ? 64 : 0), static_cast<std::uint8_t>(right ? 127 : 63),
                static_cast<std::uint8_t>(lower ? 64 : 0), static_cast<std::uint8_t>(lower ? 127 : 63)};
}

// The halo (a quarter of the brightness) and the core over it, both semi-transparent, the same
// quad and depth slot.
void emit_glow(AcidHost& host, const AcidLook& look, const ScreenPoint& at, std::int32_t size, Cell c, std::int32_t halo,
               std::int32_t core, std::int32_t slot) {
    EffectPrim p;
    p.kind = EffectPrim::Kind::Quad;
    p.semi_transparent = true;
    const auto x = [&](int dx) { return static_cast<std::int16_t>(at.x + dx); };
    const auto y = [&](int dy) { return static_cast<std::int16_t>(at.y + dy); };
    p.x = {x(-size), x(size), x(-size), x(size)};
    p.y = {y(size), y(size), y(-size), y(-size)};
    p.u = {c.u0, c.u1, c.u0, c.u1};
    p.v = {c.v0, c.v0, c.v1, c.v1};
    p.slot = slot;
    p.texture = look.glow;
    const auto grey = [](std::int32_t v) {
        const auto b = static_cast<std::uint8_t>(v);
        return Rgb{b, b, b};
    };
    p.rgb[0] = grey(halo);
    host.emit(p);
    p.texture = look.core;
    p.rgb[0] = grey(core);
    host.emit(p);
}

Vec3s relative_to(const Vec3s& origin, std::int32_t x12, std::int32_t y12, std::int32_t z12) {
    return Vec3s{static_cast<std::int16_t>((x12 >> 4) - static_cast<std::uint16_t>(origin.x)),
                 static_cast<std::int16_t>((y12 >> 4) - origin.y), static_cast<std::int16_t>((z12 >> 4) - origin.z)};
}

}  // namespace

const AcidMapParams* AcidConfig::find(std::string_view map) const {
    for (const Entry& e : maps) {
        if (e.map == map) {
            return &e.params;
        }
    }
    return nullptr;
}

void AcidEffects::spawn_spit(AcidHost& host, const AcidParams& params, const Vec3& from, const Vec3& to) const {
    AcidParticle p;
    const int slot = host.alloc(kAcidSpitState, &p);
    if (slot < 0) {
        return;
    }
    p.x = from.x;
    p.y = static_cast<std::int16_t>(from.y);
    p.z = from.z;
    p.c0 = static_cast<std::int16_t>(host.atan2(to.x - from.x, to.z - from.z));
    p.c1 = static_cast<std::int16_t>(host.atan2(to.y - from.y, host.length2_q6(to.x - from.x, to.z - from.z)));
    p.t0 = static_cast<std::int16_t>(params.life);
    p.flag = 0;
    p.t1 = static_cast<std::int16_t>(kQ12);
    host.store(slot, p);
}

bool AcidEffects::update_spit(AcidHost& host, const AcidParams& params, int slot, AcidParticle& p) const {
    const AcidLook& look = config_.look;
    const auto life = static_cast<std::uint16_t>(p.t0);
    const auto fade = static_cast<std::uint16_t>(p.t1);

    // The speed and trail are map-set; a spit alive without them would divide by zero.
    if (life == 0 || fade == 0 || params.length == 0 || params.segments == 0) {
        p.state = 0;
        host.store(slot, p);
        return false;
    }

    const Vec3s origin = host.view_origin();
    const std::int32_t geom = host.geometry_screen();
    const std::int32_t length = params.length;
    const std::int32_t count = params.segments;

    const auto sin_pitch = static_cast<std::int16_t>(host.sin(-p.c1));
    const auto cos_pitch = static_cast<std::int16_t>(host.cos(-p.c1));
    const auto sin_back = static_cast<std::int16_t>(host.sin(p.c0 + 2048));  // yaw + 180 degrees
    const auto cos_back = static_cast<std::int16_t>(host.cos(p.c0 + 2048));

    const std::int32_t elapsed = q12_mult_precise(params.speed, params.life - life);
    const std::int32_t drawn = (count - 1) * std::min(elapsed, length) / length + 1;

    Vec3 tail;  // the last trail point, the ray's other end
    for (std::int32_t i = 0; i < drawn; i++) {
        const std::int32_t along = (i * length) / count;

        tail.y = p.y + q12_mult(along, sin_pitch);
        const std::int32_t flat = q12_mult(along, cos_pitch);
        tail.x = p.x + q12_mult(flat, sin_back);
        tail.z = p.z + q12_mult(flat, cos_back);

        const ScreenPoint pt = host.project(relative_to(origin, tail.x, tail.y, tail.z));
        const std::int32_t sz = pt.depth;
        if (sz <= 0 || (sz >> 3) >= look.ordering_table_size) {
            continue;
        }
        if (std::abs(static_cast<int>(pt.x)) > look.max_x || std::abs(static_cast<int>(pt.y)) > look.max_y) {
            continue;
        }

        const std::int32_t age = (i + count) - drawn;
        const std::int32_t size =
            (((params.size_start * (count - age) + params.size_end * age) / count) * geom / sz) >> 4;

        std::int32_t c = 128 - ((age << 7) / (count + 1));
        c = (c * host.darkness(tail)) >> 8;
        if (life < (params.life >> 1)) {
            c = (c * life) / (params.life >> 1);
        }
        c = q12_mult_precise(c, fade);
        if (c == 0) {
            p.state = 0;
        }

        emit_glow(host, look, pt, size, cell((i & 1) != 0, (i & 2) != 0), c >> 2, c, (sz - look.slot_near_spit) >> 3);
    }

    const std::int32_t dt = host.delta_time();
    const auto step =
        static_cast<std::int16_t>(q12_mult_precise(dt, p.flag != 0 ? (params.speed >> 2) : params.speed));

    const std::int32_t dy = q12_mult(step, host.sin(p.c1));
    const std::int32_t flat = q12_mult(step, host.cos(p.c1));
    p.x += q12_mult(flat, host.sin(p.c0));
    p.y = static_cast<std::int16_t>(p.y + dy);
    p.z += q12_mult(flat, host.cos(p.c0));

    const Vec3 from{p.x, p.y, p.z};
    const Vec3 offset{tail.x - p.x, tail.y - p.y, tail.z - p.z};
    const AcidProbe probe = host.probe(from, offset);

    p.t0 = static_cast<std::int16_t>(std::max<std::int32_t>(life - dt, 0));

    bool burst = false;
    if (p.flag != 0) {
        p.t1 = static_cast<std::int16_t>(std::max<std::int32_t>(fade - (dt << 2), 0));
    } else if (probe.hit || probe.ground_height < p.y) {
        p.flag++;
        burst = true;
    }
    // The burst takes slots; this one must read as it is now.
    host.store(slot, p);

    if (burst) {
        if (probe.hit && probe.hit_player) {
            spawn_drops(host, params, probe.target, true);
            spawn_drops(host, params, from, true);
            host.strike_player();
        } else {
            spawn_drops(host, params, from, false);
        }
    }
    return true;
}

void AcidEffects::spawn_drops(AcidHost& host, const AcidParams& params, const Vec3& at, bool on_player) const {
    const AcidLook& look = config_.look;
    for (std::int32_t i = 0; i < look.splash_count; i++) {
        AcidParticle p;
        const int slot = host.alloc(kAcidDropState, &p);
        if (slot < 0) {
            break;
        }
        // The draws below are in the order the original made them: x, y, z, age, scale, look.
        const std::int32_t spread = look.drop_spread;
        p.x = at.x + static_cast<std::int32_t>(static_cast<std::uint32_t>(host.rand16()) % (2 * spread)) - spread;
        p.y = static_cast<std::int16_t>(at.y + static_cast<std::int32_t>(static_cast<std::uint32_t>(host.rand16()) %
                                                                         (2 * spread)) -
                                        spread);
        p.z = at.z + static_cast<std::int32_t>(static_cast<std::uint32_t>(host.rand16()) % (2 * spread)) - spread;
        p.flag = on_player ? 1 : 0;
        p.c0 = static_cast<std::int16_t>(host.rand16() % look.drop_age_jitter);
        const std::int32_t scale = static_cast<std::int32_t>(static_cast<std::uint32_t>(host.rand16()) %
                                                             (look.drop_scale_max - look.drop_scale_min + 1)) +
                                   look.drop_scale_min;
        p.c1 = static_cast<std::int16_t>(q12_mult_precise(params.pitch_scale, scale));
        const std::int32_t variant =
            static_cast<std::int32_t>(static_cast<std::uint32_t>(host.rand16()) %
                                      (look.drop_variant_max - look.drop_variant_min + 1)) +
            look.drop_variant_min;
        p.t0 = static_cast<std::int16_t>((p.t0 & 0xFF00) | (variant & 0xFF));  // only the low byte is set
        host.store(slot, p);
    }
}

bool AcidEffects::update_drop(AcidHost& host, int slot, AcidParticle& p) const {
    const AcidLook& look = config_.look;

    p.c0 = static_cast<std::int16_t>(p.c0 + host.delta_time());
    if (p.c0 > look.drop_life) {
        p.state = 0;
        host.store(slot, p);
        return false;
    }
    host.store(slot, p);

    const Vec3s origin = host.view_origin();
    const ScreenPoint pt = host.project(relative_to(origin, p.x, p.y, p.z));
    std::int32_t sz = pt.depth;
    if (sz <= 0 || (sz >> 3) >= look.ordering_table_size) {
        return false;
    }
    if (std::abs(static_cast<int>(pt.x)) > look.max_x || std::abs(static_cast<int>(pt.y)) > look.max_y) {
        return false;
    }

    const std::int32_t grow = ((p.c0 >> 1) * kQ12) / look.drop_life + look.drop_size_base;
    const std::int32_t size =
        (host.geometry_screen() * q12_mult_precise(static_cast<std::uint16_t>(p.c1), grow) / sz) >> 4;

    const auto variant = static_cast<std::int32_t>(static_cast<std::uint8_t>(p.t0 & 0xFF));
    std::int32_t c;
    if (p.c0 > look.drop_fade_in) {
        c = variant - (variant * (p.c0 - look.drop_fade_in)) / kQ12;
    } else {
        c = (variant * p.c0) / look.drop_fade_in;
    }
    c = (c * host.darkness(Vec3{p.x, p.y, p.z})) >> 8;

    sz -= p.flag != 0 ? look.slot_near_drop_hit : look.slot_near_drop;
    emit_glow(host, look, pt, size, cell((variant & 1) == 0, (variant & 2) == 0), c >> 1, c, sz >> 3);
    return true;
}

namespace {

TextureRef texture_of(const Value& parent, std::string_view key) {
    const Value& t = parse::member(parent, key);
    parse::reject_unknown(t, {"page", "palette"});
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

std::int32_t metres_of(const Value& parent, std::string_view key) {
    const Value& v = parse::member(parent, key);
    if (!v.is_number()) {
        parse::fail(v, "a number is expected");
    }
    return q12_from_metres(v.number());
}

AcidParams params_of(const Value& v, const AcidParams& base, bool speed_only) {
    AcidParams out = base;
    if (speed_only) {
        parse::reject_unknown(v, {"speed"});
        out.speed = metres_of(v, "speed");
        return out;
    }
    parse::reject_unknown(v, {"speed", "size_start", "size_end", "pitch_scale", "life", "length", "segments"});
    out.speed = metres_of(v, "speed");
    out.size_start = metres_of(v, "size_start");
    out.size_end = metres_of(v, "size_end");
    out.pitch_scale = metres_of(v, "pitch_scale");
    out.life = metres_of(v, "life");
    out.length = metres_of(v, "length");
    out.segments = parse::integer_of(v, "segments", 1, 255);
    return out;
}

std::pair<std::int32_t, std::int32_t> int_pair(const Value& parent, std::string_view key, std::int64_t lo,
                                               std::int64_t hi) {
    const Value& v = parse::member(parent, key);
    if (!v.is_array() || v.array().size() != 2) {
        parse::fail(v, "[low, high] is expected");
    }
    const std::int32_t a = parse::integer(v.array()[0], lo, hi);
    const std::int32_t b = parse::integer(v.array()[1], lo, hi);
    return {a, b};
}

}  // namespace

Result<AcidConfig> parse_acid_config(std::string_view json_text) try {
    const Result<Value> doc = json::parse(json_text);
    if (!doc) {
        return Result<AcidConfig>::fail(doc.error());
    }
    const Value& root = doc.value();
    parse::reject_unknown(root, {"look", "drop", "maps"});
    AcidConfig out;
    AcidLook& look = out.look;

    const Value& lv = parse::member(root, "look");
    parse::reject_unknown(lv, {"halo", "core", "slot_offsets", "max_screen", "ordering_table_size"});
    look.glow = texture_of(lv, "halo");
    look.core = texture_of(lv, "core");
    const Value& so = parse::member(lv, "slot_offsets");
    parse::reject_unknown(so, {"spit", "drop", "drop_on_player"});
    look.slot_near_spit = parse::integer_of(so, "spit", 0, 4095);
    look.slot_near_drop = parse::integer_of(so, "drop", 0, 4095);
    look.slot_near_drop_hit = parse::integer_of(so, "drop_on_player", 0, 4095);
    const auto screen = int_pair(lv, "max_screen", 0, 4096);
    look.max_x = screen.first;
    look.max_y = screen.second;
    look.ordering_table_size = parse::integer_of(lv, "ordering_table_size", 1, 1 << 16);

    const Value& dv = parse::member(root, "drop");
    parse::reject_unknown(dv, {"count", "life", "fade_in", "size_base", "spread", "age_jitter", "scale", "variant"});
    look.splash_count = parse::integer_of(dv, "count", 0, 64);
    look.drop_life = metres_of(dv, "life");
    look.drop_fade_in = metres_of(dv, "fade_in");
    look.drop_size_base = parse::integer_of(dv, "size_base", 0, 65535);
    look.drop_spread = metres_of(dv, "spread");
    look.drop_age_jitter = metres_of(dv, "age_jitter");
    const auto scale = int_pair(dv, "scale", 0, 4095);
    if (scale.second < scale.first) {
        parse::fail(parse::member(dv, "scale"), "low is above high");
    }
    look.drop_scale_min = scale.first;
    look.drop_scale_max = scale.second;
    const auto variant = int_pair(dv, "variant", 0, 255);
    if (variant.second < variant.first) {
        parse::fail(parse::member(dv, "variant"), "low is above high");
    }
    look.drop_variant_min = variant.first;
    look.drop_variant_max = variant.second;
    if (look.drop_life <= 0 || look.drop_fade_in <= 0 || look.drop_spread <= 0 || look.drop_age_jitter <= 0) {
        parse::fail(dv, "life, fade_in, spread and age_jitter are above zero");
    }

    const Value& maps = parse::member(root, "maps");
    if (!maps.is_object()) {
        parse::fail(maps, "\"maps\" is an object of map names");
    }
    for (const auto& [name, mv] : maps.members()) {
        if (!name.empty() && name[0] == '_') {
            continue;
        }
        parse::reject_unknown(mv, {"normal", "hard"});
        AcidConfig::Entry e;
        e.map = name;
        e.params.normal = params_of(parse::member(mv, "normal"), AcidParams{}, false);
        // The hard difficulty differs only in the speed.
        const Value& hard = parse::member(mv, "hard");
        e.params.hard = params_of(hard, e.params.normal, true);
        out.maps.push_back(std::move(e));
    }
    return Result<AcidConfig>::ok(std::move(out));
} catch (const std::exception& e) {
    return Result<AcidConfig>::fail(e.what());
}

Result<AcidConfig> load_acid_config(const std::filesystem::path& gamedata_dir, const std::filesystem::path& mods_dir,
                                    std::string* source) {
    std::string where;
    Result<std::string> text =
        read_effect_file(gamedata_dir, mods_dir, std::filesystem::path("effects") / "acid.json", &where);
    if (source != nullptr) {
        *source = where;
    }
    if (!text) {
        return Result<AcidConfig>::fail(text.error());
    }
    Result<AcidConfig> parsed = parse_acid_config(text.value());
    if (!parsed) {
        return Result<AcidConfig>::fail(where + ": " + parsed.error());
    }
    return parsed;
}

}  // namespace sh::effects
