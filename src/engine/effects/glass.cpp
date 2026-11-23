// SPDX-License-Identifier: GPL-3.0-or-later
// The glass shards' flight and draw, written from the original's behaviour. The loop
// structure below (a `continue` that re-tests the condition without stepping the cell)
// mirrors the ported code exactly: the hole re-roll consumes a new random number each
// time, and the ring mask (only reachable with a zero second half-size) would spin, as
// the ported code does - no shipped pane calls it that way.

#include "engine/effects/glass.h"

#include <fstream>
#include <iterator>
#include <stdexcept>

#include "engine/mods/data_merge.h"
#include "formats/common/json.h"

namespace sh::effects {
namespace {

[[noreturn]] void fail(const std::string& what) {
    throw std::runtime_error(what);
}

// The game's macros multiply in 32 bits and shift arithmetically; keep the wrap.
std::int32_t wrap(std::int64_t v) {
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(v));
}

std::int32_t mul_shift(std::int32_t a, std::int32_t b, int shift) {
    return wrap(static_cast<std::int64_t>(a) * b) >> shift;
}

std::int32_t mul_precise(std::int32_t a, std::int32_t b, int shift) {
    return static_cast<std::int32_t>((static_cast<std::int64_t>(a) * b) >> shift);
}

std::int32_t square(std::int32_t v) {
    return wrap(static_cast<std::int64_t>(v) * v);
}

// The game's Q25.6 "safe" magnitudes: shift each component to Q6, square, sum, take the
// integer root, shift back to Q12.
std::int32_t sqr_mag_q6(std::int32_t v) {
    return square(v >> 6);
}

std::int32_t mag2_q6(std::int32_t a, std::int32_t b, GlassHost& host) {
    return (host.sqrt(sqr_mag_q6(a) + sqr_mag_q6(b))) << 6;
}

std::int32_t mag3_q6(std::int32_t x, std::int32_t y, std::int32_t z, GlassHost& host) {
    return (host.sqrt(sqr_mag_q6(x) + sqr_mag_q6(y) + sqr_mag_q6(z))) << 6;
}

}  // namespace

void GlassEffects::spawn(GlassHost& host, const Vec3& from, const Vec3& to, std::int16_t speed, std::int32_t size,
                         std::int16_t half_a, std::int16_t half_b, std::int32_t spread, int group) const {
    GlassPane pane;
    pane.x = from.x;
    pane.z = from.z;
    pane.size = size;
    host.set_pane(group, pane);

    std::int32_t rows = half_b;
    bool ring = false;
    if (rows == 0) {
        ring = true;
        rows = half_a;
    }

    const Vec3 dir{from.x - to.x, from.y - to.y, from.z - to.z};
    const std::int16_t pitch = static_cast<std::int16_t>(host.atan2(dir.y, dir.z));
    const std::int16_t yaw = static_cast<std::int16_t>(host.atan2(dir.x, dir.z));

    host.rotate_setup(pitch, yaw);

    // The ring's normalising length; +1 keeps the divisions below away from zero.
    const std::int32_t radius = mag2_q6(rows, half_a, host) + 1;

    std::int32_t step_sum = spread * 3;

    for (std::int32_t u = -half_a; half_a >= u;) {
        const std::int32_t u_q6 = square(u >> 6);

        for (std::int32_t v = -rows; rows >= v;) {
            if (ring && square(half_a >> 6) < u_q6 + square(v >> 6)) {
                continue;
            }

            /* The engine's zero-divisor guard: the modulo operand is zero exactly on the
             * spawn radius, and that cell spawns instead of faulting. */
            {
                const std::int32_t denom = 0x20 - ((host.sqrt(u_q6 + square(v >> 6)) << 6) * 24) / radius;
                if (denom != 0 && (host.rand16() % denom) == 0) {
                    continue;
                }
            }

            const int slot = host.alloc(static_cast<std::uint8_t>(group + 0x17));
            if (slot < 0) {
                return;
            }

            Vec3s cell;
            cell.x = static_cast<std::int16_t>((u + host.rand16() % (spread >> 1)) - (spread >> 2));
            cell.y = static_cast<std::int16_t>((v + host.rand16() % (spread >> 1)) - (spread >> 2));
            cell.z = 0;

            const Vec3 offset = host.rotate(cell);

            GlassShard shard;
            shard.state = group + 0x17;  // the group the alloc marked the slot with
            shard.x = offset.x;
            shard.y = offset.y + from.y;
            shard.z = offset.z;

            std::int32_t mag = mag3_q6(offset.x, offset.y, offset.z, host);
            shard.spin = (speed >> 10) != 0 ? (mag / (speed >> 10) < 4096 ? mag / (speed >> 10) : 4096) : 4096;

            const std::int32_t look = (mag * 6) / radius + 3;
            shard.variant = static_cast<std::uint8_t>((host.rand16() % (look < 8 ? look : 8)) * 16 - 0x80 +
                                                      host.rand_uint(0, 15));

            // The launch velocity points away from the far corner; each component draws
            // its own random factor, as the original's three calls did.
            const Vec3 away{offset.x + dir.x, offset.y + dir.y, offset.z + dir.z};
            mag = mag3_q6(away.x, away.y, away.z, host);

            if (mag == 0) {
                shard.vel_x = 0;
                shard.vel_y = 0;
                shard.vel_z = 0;
            } else {
                // Each component draws its own random factor, as the original's three
                // separate calls did: (speed * factor) >> 7, scaled by the direction.
                shard.vel_x = wrap(mul_shift(speed, host.rand_uint(64, 191), 7) * away.x) / mag;
                shard.vel_y = wrap(mul_shift(speed, host.rand_uint(64, 191), 7) * away.y) / mag;
                shard.vel_z = wrap(mul_shift(speed, host.rand_uint(64, 191), 7) * away.z) / mag;
            }

            shard.yaw = host.rand_add(pitch, -255, 256);
            shard.pitch = host.rand_add(yaw, -255, 256);

            host.store(slot, shard);

            const std::int32_t row_step = rows != 0 ? (v * spread) / rows : 0;
            v += ((row_step < 0 ? -row_step : row_step) + step_sum) >> 2;
        }

        const std::int32_t col_step = half_a != 0 ? (u * spread) / half_a : 0;
        u += ((col_step < 0 ? -col_step : col_step) + step_sum) >> 2;
    }
}

bool GlassEffects::update(GlassHost& host, int slot, GlassShard& shard) const {
    const std::int32_t dt = host.delta_time();

    if ((shard.variant & 0x80) != 0) {
        // At rest: count down, then relaunch with fresh spin derived from the velocity.
        shard.spin = shard.spin - dt < 0 ? 0 : shard.spin - dt;

        if (shard.spin == 0) {
            shard.variant = static_cast<std::uint8_t>(shard.variant + 0x80);  // clears the resting bit

            const std::int32_t flags =
                ((host.atan2(shard.vel_y, shard.vel_z) - shard.yaw) > 0) << 7;
            shard.spin = flags |
                         (((host.atan2(shard.vel_x, shard.vel_z) - shard.pitch) > 0) ? 0x8000 : 0);

            // Each half draws its random number inside a clamp that names the expression
            // twice, so the generator advances twice per half unless the first value
            // already exceeds the limit (the original's compiled code does the same).
            const auto jitter = [&](std::int32_t mag) {
                const std::int32_t span = mag >> 2;
                const std::int32_t base = mag >> 3;
                const auto value = [&] { return mag + (span != 0 ? host.rand16() % span : 0) - base; };
                if (value() > 0x7FFF) {
                    return static_cast<std::int32_t>(0x7FFF / 256);
                }
                return value() / 256;
            };

            shard.spin = (shard.spin & 0xFFFF) + jitter(mag2_q6(shard.vel_y, shard.vel_z, host));
            shard.spin = (shard.spin & 0xFFFF) + (jitter(mag2_q6(shard.vel_x, shard.vel_z, host)) << 8);
            shard.spin &= 0xFFFF;
        }
        host.store(slot, shard);
    } else {
        // Flying: air drag against the drift, gravity, then the spin.
        const std::int32_t angle = host.atan2(shard.x, shard.z);
        const std::int32_t size2 = square(shard.variant >> 4);

        const std::int32_t drag = ((size2 + 7) * 6144) / 28;

        const std::int32_t drag_x = mul_precise(dt, mul_shift(drag, host.sin(angle), 12), 12);
        if (shard.vel_x > 0) {
            shard.vel_x = shard.vel_x - drag_x < 0 ? 0 : shard.vel_x - drag_x;
        } else if (shard.vel_x < 0) {
            shard.vel_x = shard.vel_x - drag_x > 0 ? 0 : shard.vel_x - drag_x;
        }

        const std::int32_t drag_z = mul_precise(dt, mul_shift(drag, host.cos(angle), 12), 12);
        if (shard.vel_z > 0) {
            shard.vel_z = shard.vel_z - drag_z < 0 ? 0 : shard.vel_z - drag_z;
        } else if (shard.vel_z < 0) {
            shard.vel_z = shard.vel_z - drag_z > 0 ? 0 : shard.vel_z - drag_z;
        }

        const std::int32_t fall = (host.gravity() * ((shard.variant >> 4) + 9)) >> 4;
        shard.vel_y = shard.vel_y + fall > 0x7FFF ? 0x7FFF : shard.vel_y + fall;

        shard.x = wrap(shard.x + mul_precise(dt, shard.vel_x, 12));
        shard.y = wrap(shard.y + mul_precise(dt, shard.vel_y, 12));
        shard.z = wrap(shard.z + mul_precise(dt, shard.vel_z, 12));

        if ((shard.spin & 0x80) != 0) {
            shard.yaw = wrap(shard.yaw + mul_precise(dt, (shard.spin & 0x7F) << 7, 12));
        } else {
            shard.yaw = wrap(shard.yaw - mul_precise(dt, (shard.spin & 0x7F) << 7, 12));
        }

        if ((shard.spin & 0x8000) != 0) {
            shard.pitch = wrap(shard.pitch + mul_precise(dt, (shard.spin >> 1) & 0x3F80, 12));
        } else {
            shard.pitch = wrap(shard.pitch - mul_precise(dt, (shard.spin >> 1) & 0x3F80, 12));
        }

        // A shard below its ground, or still falling over default ground, is gone.
        const GlassHost::Ground ground = host.ground(shard.x, shard.z);
        if ((ground.type == 0 && shard.y > 0) || ground.height < shard.y) {
            shard.state = 0;
        }
        // The slot keeps 16-bit words; the quad below must see what the slot holds.
        shard.x = static_cast<std::int16_t>(shard.x);
        shard.y = static_cast<std::int16_t>(shard.y);
        shard.z = static_cast<std::int16_t>(shard.z);
        shard.yaw = static_cast<std::int16_t>(shard.yaw);
        shard.pitch = static_cast<std::int16_t>(shard.pitch);
        host.store(slot, shard);
    }

    // ---- the quad ----

    const GlassPane pane = host.pane(shard.state - 23);

    const std::int32_t size2 = (shard.variant & 0x80) != 0 ? square((shard.variant - 0x80) >> 4)
                                                           : square(shard.variant >> 4);

    host.frame_setup(static_cast<std::int16_t>(shard.yaw), static_cast<std::int16_t>(shard.pitch), pane.x, pane.z);

    const std::int32_t half = ((static_cast<std::int16_t>(pane.size) * (size2 + 4)) >> 4) & 0xFFFF;

    Vec3 corners[4];
    for (int i = 0; i < 4; i++) {
        // The four corners: (+-half, +-half) on the shard's plane.
        Vec3s c;
        c.x = static_cast<std::int16_t>((i & 1) != 0 ? half : -half & 0xFFFF);
        c.y = static_cast<std::int16_t>(i >= 2 ? -half & 0xFFFF : half);
        c.z = 0;
        corners[i] = host.rotate(c);
    }

    host.use_view();
    const Vec3s origin = host.view_origin();

    Vec3s rel[4];
    for (int i = 0; i < 4; i++) {
        rel[i].x = static_cast<std::int16_t>(
            (((shard.x + corners[i].x) >> 4) - (origin.x & 0xFFFF)) & 0xFFFF);
        rel[i].y = static_cast<std::int16_t>(((shard.y + corners[i].y) >> 4) - origin.y);
        rel[i].z = static_cast<std::int16_t>(((shard.z + corners[i].z) >> 4) - origin.z);
    }

    GlassHost::Quad quad;
    quad.variant = shard.variant;
    host.project3(rel[0], rel[1], rel[2], quad.corners);
    quad.corners[3] = host.project(rel[3]);

    const std::int32_t depth = (quad.corners[0].depth + quad.corners[1].depth + quad.corners[2].depth +
                                quad.corners[3].depth) >> 2;

    if (depth <= 0 || (depth >> 3) >= params_.ordering_table_size || quad.corners[3].x > params_.max_x ||
        quad.corners[3].x < -params_.max_x || quad.corners[3].y > params_.max_y ||
        quad.corners[3].y < -params_.max_y) {
        return false;
    }

    quad.color = host.fog(depth * 0x10);

    const std::int32_t near = depth - params_.slot_depth_offset;
    quad.slot = near >= 0 ? near >> 3 : 0;

    host.emit(quad);
    return true;
}

Result<GlassParams> parse_glass_params(std::string_view json_text) try {
    const Result<json::Value> doc = json::parse(json_text);
    if (!doc) {
        return Result<GlassParams>::fail(doc.error());
    }
    const json::Value& root = doc.value();
    if (!root.is_object()) {
        fail("the file must be an object");
    }
    for (const auto& [key, value] : root.members()) {
        (void)value;
        if (key.rfind("_", 0) == 0 || key == "description") {
            continue;  // "_comment" and friends, the file's "description"
        }
        if (key != "max_x" && key != "max_y" && key != "ordering_table_size" && key != "slot_depth_offset" &&
            key != "fog_mix") {
            fail("unknown key \"" + key + "\"");
        }
    }

    GlassParams p;
    const auto int_of = [&](const char* key, std::int32_t lo, std::int32_t hi) -> std::int32_t {
        const json::Value* v = root.find(key);
        if (v == nullptr) {
            fail(std::string("missing key \"") + key + "\"");
        }
        if (!v->is_integer() || v->integer() < lo || v->integer() > hi) {
            fail(std::string("\"") + key + "\" must be an integer in " + std::to_string(lo) + ".." +
                 std::to_string(hi));
        }
        return static_cast<std::int32_t>(v->integer());
    };
    p.max_x = int_of("max_x", 0, 4096);
    p.max_y = int_of("max_y", 0, 4096);
    p.ordering_table_size = int_of("ordering_table_size", 1, 65536);
    p.slot_depth_offset = int_of("slot_depth_offset", 0, 1 << 20);
    p.fog_mix = int_of("fog_mix", 0, 255);
    return Result<GlassParams>::ok(p);
} catch (const std::exception& e) {
    return Result<GlassParams>::fail(e.what());
}

Result<GlassParams> load_glass_params(const std::filesystem::path& gamedata_dir,
                                      const std::filesystem::path& mods_dir, std::string* source) {
    namespace fs = std::filesystem;
    const fs::path relative = fs::path("effects") / "glass.json";
    // Base + every active mod's patch, in load order.
    std::string where;
    const Result<std::string> read = mods::read_data_text(gamedata_dir, mods_dir, relative, &where);
    if (source != nullptr) {
        *source = where;
    }
    if (!read) {
        return Result<GlassParams>::fail(read.error());
    }
    const std::string& text = read.value();
    Result<GlassParams> parsed = parse_glass_params(text);
    if (!parsed) {
        return Result<GlassParams>::fail(where + ": " + parsed.error());
    }
    return parsed;
}

}  // namespace sh::effects
