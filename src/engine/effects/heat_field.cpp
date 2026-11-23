// SPDX-License-Identifier: GPL-3.0-or-later
// Heat fields, written from the original's behaviour. Widths follow the original: the projected
// centre is kept as the raw words, the stamp's column and row are 16-bit, throw speeds of the
// Burst, Shell and Fan emitters are stored as 16-bit, products are 64-bit, and positions are
// narrowed to 16 bits for the projection.

#include "engine/effects/heat_field.h"

#include <algorithm>
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

std::int32_t mul(std::int32_t a, std::int32_t b) {
    return static_cast<std::int32_t>((static_cast<std::int64_t>(a) * b) >> 12);
}

std::int16_t narrow(std::int32_t v) {
    return static_cast<std::int16_t>(v);
}

// The stamp's cell: rounds away from zero by half a cell, then divides towards zero.
std::int16_t to_cell(std::int16_t v, int cell) {
    const int half = cell / 2;
    v = static_cast<std::int16_t>(v + (v > 0 ? half : -half));
    return static_cast<std::int16_t>(v / cell);
}

}  // namespace

HeatField::HeatField(HeatFieldParams params) : params_(std::move(params)) {
    state_.cols = params_.cols;
    state_.grid.assign(static_cast<std::size_t>(params_.rows + 2) * params_.cols, 0);
    state_.particles.assign(static_cast<std::size_t>(params_.particles), HeatParticle{});
}

Vec3 HeatField::place(const Vec3& position) const {
    return Vec3{(position.x >> 4) + params_.origin_offset_q8.x, (position.y >> 4) + params_.origin_offset_q8.y,
                (position.z >> 4) + params_.origin_offset_q8.z};
}

void HeatField::clear() {
    std::fill(state_.grid.begin(), state_.grid.end(), std::uint8_t{0});
    if (params_.emitter == HeatEmitter::Cloud || params_.emitter == HeatEmitter::Gather) {
        for (HeatParticle& p : state_.particles) {
            p.active = false;
        }
    }
}

void HeatField::start(HeatFieldHost& host, const Vec3& position, std::int32_t yaw) {
    state_.yaw = yaw;
    state_.origin_q8 = place(position);
    state_.delay = params_.start_delay;
    clear();
    if (params_.emitter == HeatEmitter::Burst || params_.emitter == HeatEmitter::Shell) {
        throw_all(host);
    }
}

void HeatField::start_between(HeatFieldHost& host, const Vec3& from, const Vec3& to) {
    const std::int32_t dx = to.x - from.x;
    const std::int32_t dz = to.z - from.z;
    const std::int32_t yaw = host.atan2(dx, dz);
    const int s = params_.between_shift;
    const std::int32_t round = (1 << s) - 1;
    const std::int32_t x = from.x + ((dx < 0 ? dx + round : dx) >> s);
    const std::int32_t z = from.z + ((dz < 0 ? dz + round : dz) >> s);
    state_.yaw = yaw;
    state_.origin_q8 = Vec3{(x >> 4) + params_.origin_offset_q8.x, params_.between_height + params_.origin_offset_q8.y,
                            (z >> 4) + params_.origin_offset_q8.z};
    state_.delay = params_.start_delay;
    clear();
    throw_all(host);
}

void HeatField::arm(const Vec3& position, std::int32_t yaw) {
    state_.yaw = yaw;
    state_.timer = 1;
    state_.origin_q8 = place(position);
}

void HeatField::throw_all(HeatFieldHost& host) {
    for (HeatParticle& p : state_.particles) {
        p.pos = Vec3{};
        p.brightness = params_.start_brightness;
        switch (params_.emitter) {
        case HeatEmitter::Burst: {
            p.delay = 0;
            const std::int32_t angle = host.rand16();
            const std::int32_t s = host.sin(angle);
            const std::int32_t c = host.cos(angle);
            const std::int32_t speed = mul((host.rand16() % params_.speed_range) + params_.speed_min, params_.speed_scale);
            p.vel[0] = narrow(mul(c, speed));
            p.vel[1] = narrow(mul(s, speed));
            p.vel[2] = narrow(params_.fall_start);
            break;
        }
        case HeatEmitter::Shell: {
            const std::int32_t up_angle = host.rand16();
            const std::int32_t up = mul(host.sin(up_angle), params_.speed);
            const std::int32_t flat = mul(host.cos(up_angle), params_.speed);
            const std::int32_t yaw = host.rand16();
            p.vel[0] = narrow(mul(host.sin(yaw), flat));
            p.vel[1] = narrow(up);
            p.vel[2] = narrow(mul(host.cos(yaw), flat));
            break;
        }
        case HeatEmitter::Fan: {
            const std::int32_t angle = host.rand16();
            const std::int32_t s = host.sin(angle);
            const std::int32_t c = host.cos(angle);
            p.vel[0] = narrow(c);
            p.vel[1] = narrow(s);
            p.vel[2] = 0;
            break;
        }
        case HeatEmitter::Cloud:
        case HeatEmitter::Gather:
            break;
        }
    }
}

void HeatField::gather_one(HeatFieldHost& host, const Vec3& around) {
    const std::int32_t up_angle = host.rand16();
    const std::int32_t up = host.sin(up_angle);
    const std::int32_t flat = host.cos(up_angle);
    const std::int32_t yaw = host.rand16();
    const std::int32_t dx = mul(flat, host.sin(yaw));
    const std::int32_t dz = mul(flat, host.cos(yaw));
    const Vec3 from{around.x + mul(dx, params_.radius), around.y + mul(up, params_.radius),
                    around.z + mul(dz, params_.radius)};

    for (HeatParticle& p : state_.particles) {
        if (p.active) {
            continue;
        }
        p.active = true;
        p.landed = false;
        p.pos = from;
        const Vec3 way{around.x - from.x, around.y - from.y, around.z - from.z};
        const Vec3 dir = host.normalize(way);
        p.vel[0] = mul(dir.x, params_.speed);
        p.vel[1] = mul(dir.y, params_.speed);
        p.vel[2] = mul(dir.z, params_.speed);
        // The flight time: distance / speed, both Q12 (the shift wraps as the original's did).
        const std::int32_t dist = host.sqrt12(mul(way.x, way.x) + mul(way.y, way.y) + mul(way.z, way.z));
        p.life = params_.speed != 0
                     ? static_cast<std::int32_t>(static_cast<std::uint32_t>(dist) << 12) / params_.speed
                     : 0;
        p.brightness = params_.start_brightness;
        p.delay = 0;
        return;
    }
}

void HeatField::spawn(HeatFieldHost& host, const Vec3& around, int count) {
    if (params_.emitter == HeatEmitter::Gather) {
        for (int i = 0; i < count; i++) {
            gather_one(host, around);
        }
        return;
    }
    int made = 0;
    for (HeatParticle& p : state_.particles) {
        if (p.active) {
            continue;
        }
        if (made >= count) {
            break;
        }
        made++;

        std::int32_t a = host.rand16();
        std::int32_t s = host.sin(a);
        std::int32_t c = host.cos(a);
        std::int32_t yaw = host.rand16();
        std::int32_t dx = mul(c, host.sin(yaw));
        std::int32_t dz = mul(c, host.cos(yaw));
        p.pos.x = around.x + mul(dx, params_.spread);
        p.pos.y = around.y + mul(s, params_.spread);
        p.pos.z = around.z + mul(dz, params_.spread);
        p.brightness = params_.start_brightness;

        a = host.rand16();
        s = host.sin(a);
        c = host.cos(a);
        yaw = host.rand16();
        dx = mul(host.sin(yaw), c);
        dz = mul(host.cos(yaw), c);
        p.vel[0] = (p.pos.x - around.x) * params_.spread_push + mul(dx, params_.speed);
        p.vel[1] = (p.pos.y - around.y) * params_.spread_push + mul(s, params_.speed);
        p.vel[2] = (p.pos.z - around.z) * params_.spread_push + mul(dz, params_.speed);

        // The original rolled twice more for values it never kept.
        host.rand16();
        p.delay = 0;
        host.rand16();
        p.life = params_.life;
        p.active = true;
    }
}

void HeatField::stamp(int x, int y, std::int32_t brightness) {
    std::int32_t ax = 0;
    std::int32_t ay = 0;
    if (params_.follow_origin) {
        ax = state_.centre_x & 0xFFFF;
        ay = state_.centre_y;
    }
    const std::int16_t col = to_cell(static_cast<std::int16_t>((x + params_.stamp_x) - ax), params_.cell);
    const std::int16_t row = to_cell(static_cast<std::int16_t>((y + params_.stamp_y) - ay), params_.cell);
    if (col > 0 && col < params_.cols && row > 0 && row < params_.rows) {
        std::int32_t v = brightness;
        if (params_.stamp_shift > 0) {
            v >>= params_.stamp_shift;
            v = v < 0x100 ? v : 0xFF;
        }
        state_.row(0)[col + params_.cols * row] = static_cast<std::uint8_t>(v);
    }
}

void HeatField::fly(HeatFieldHost& host) {
    const int sh = params_.project_shift;
    for (HeatParticle& p : state_.particles) {
        if ((params_.emitter == HeatEmitter::Cloud || params_.emitter == HeatEmitter::Gather) && !p.active) {
            continue;
        }
        if (params_.emitter == HeatEmitter::Gather) {
            // Arrived: stops and rests; after the rest it goes, but is still stamped this frame.
            if (p.life < 0) {
                if (!p.landed) {
                    p.life = 0;
                    p.vel[0] = p.vel[1] = p.vel[2] = 0;
                    p.landed = true;
                } else {
                    p.active = false;
                }
            }
            const Vec3s at{narrow(p.pos.x >> sh), narrow(p.pos.y >> sh), narrow(p.pos.z >> sh)};
            const ScreenPoint s = host.project(at);
            stamp(s.x, s.y, p.brightness);
            const std::int32_t dt = host.delta_time();
            p.pos.x += mul(p.vel[0], dt);
            p.pos.y += mul(p.vel[1], dt);
            p.pos.z += mul(p.vel[2], dt);
            p.brightness = std::max(p.brightness - mul(dt, params_.brightness_decay), 0);
            p.life -= dt;
            continue;
        }
        const bool waits = params_.emitter == HeatEmitter::Burst || params_.emitter == HeatEmitter::Cloud;
        if (waits && p.delay >= 0) {
            p.delay -= host.delta_time();
            continue;
        }
        const Vec3s at{narrow(p.pos.x >> sh), narrow(p.pos.y >> sh),
                       params_.emitter == HeatEmitter::Fan ? std::int16_t{0} : narrow(p.pos.z >> sh)};
        const ScreenPoint s = host.project(at);
        stamp(s.x, s.y, p.brightness);

        if (params_.emitter == HeatEmitter::Cloud) {
            const std::int32_t dt = host.delta_time();
            p.pos.x += mul(p.vel[0], dt);
            p.pos.y += mul(p.vel[1], dt);
            p.pos.z += mul(p.vel[2], dt);
            p.brightness = std::max(p.brightness - mul(dt, params_.brightness_decay), 0);
            p.life -= dt;
            if (p.life < 0) {
                p.active = false;
            }
            continue;
        }
        p.pos.x += p.vel[0];
        p.pos.y += p.vel[1];
        p.pos.z += p.vel[2];
        if (params_.emitter == HeatEmitter::Burst) {
            p.vel[2] = std::max<std::int32_t>(narrow(p.vel[2] - params_.gravity), params_.fall_max);
        }
        p.brightness = std::max(p.brightness - params_.brightness_decay, 0);
    }
}

void HeatField::smooth(HeatFieldHost& host) {
    const int cols = params_.cols;
    for (int i = 0; i < params_.rows; i++) {
        std::uint8_t* tab = state_.row(i);
        for (int j = 0; j < cols; j++) {
            // As the original: the cell above is dropped on the first column, everything so far
            // on the last.
            std::int32_t val = tab[j - cols];
            if (j == 0) {
                val = 0;
            } else {
                val += tab[j - 1];
            }
            if (j == cols - 1) {
                val = 0;
            } else {
                val += tab[j + 1];
            }
            val += tab[j + cols];
            val >>= 2;
            val -= params_.smooth_loss;
            tab[j] = val <= 0 ? 0 : static_cast<std::uint8_t>(val);

            if (params_.clear_range >= 0 &&
                static_cast<std::uint32_t>(host.rand16()) % static_cast<std::uint32_t>(params_.clear_range + 1) == 0) {
                tab[j] = 0;
            }
        }
    }
}

std::uint32_t HeatField::shade_at(int col, int row) const {
    return params_.shade[state_.row(0)[row * params_.cols + col]];
}

void HeatField::draw(HeatFieldHost& host) {
    std::int32_t x = params_.draw_x;
    std::int32_t y = params_.draw_y;
    if (params_.follow_origin) {
        x += state_.centre_x & 0xFFFF;
        y += static_cast<std::int32_t>(static_cast<std::uint32_t>(state_.centre_y) & 0xFFFF);
    }
    const std::int32_t near = state_.depth >> 1;
    std::int32_t slot = near < params_.slot_limit ? near + params_.slot_add : params_.slot_far;
    const int cell = params_.cell;

    for (int i = 1; i < params_.rows; i++) {
        std::uint32_t c1 = shade_at(0, i - 1);
        std::uint32_t c3 = shade_at(0, i);
        for (int j = 1; j < params_.cols; j++) {
            const std::uint32_t c0 = c1;
            const std::uint32_t c2 = c3;
            c1 = shade_at(j, i - 1);
            c3 = shade_at(j, i);
            if (params_.quiet_color == c0 && c0 == c1 && c0 == c2 && c2 == c3) {
                continue;
            }
            const std::int32_t px = x + j * cell;
            const std::int32_t py = y + i * cell;
            SplashQuad q;
            if ((j + i) & 1) {
                const std::int32_t xs[4] = {px, px + cell, px, px + cell};
                const std::uint32_t cs[4] = {c0, c1, c2, c3};
                std::copy(xs, xs + 4, q.x);
                std::copy(cs, cs + 4, q.color);
            } else {
                const std::int32_t xs[4] = {px + cell, px, px + cell, px};
                const std::uint32_t cs[4] = {c1, c0, c3, c2};
                std::copy(xs, xs + 4, q.x);
                std::copy(cs, cs + 4, q.color);
            }
            const std::int32_t ys[4] = {py, py, py + cell, py + cell};
            std::copy(ys, ys + 4, q.y);
            host.emit_quad(q, slot);
        }
        slot -= params_.row_slot_step;
    }
    host.emit_mode(slot, params_.mode_tpage, params_.mode_dfe, params_.mode_dtd);
}

void HeatField::update(HeatFieldHost& host, bool advance) {
    if (params_.emitter == HeatEmitter::Fan) {
        if (state_.timer == 0) {
            return;
        }
        if (state_.timer == 1) {
            std::fill(state_.grid.begin(), state_.grid.end(), std::uint8_t{0});
            throw_all(host);
            state_.timer++;
        }
    } else if (state_.delay > 0) {
        state_.delay -= host.delta_time();
        return;
    }
    host.begin_view(state_.origin_q8, state_.yaw);
    const ScreenPoint c = host.project_origin();
    state_.centre_x = static_cast<std::uint16_t>(c.x);
    state_.centre_y = c.y;
    state_.depth = c.depth + params_.depth_add;

    const bool still = params_.hold_on_still_frame && host.delta_time() == 0;
    if (advance && !still) {
        fly(host);
    }
    if (!still) {
        smooth(host);
    }
    draw(host);

    if (params_.emitter == HeatEmitter::Fan) {
        state_.timer += host.delta_time();
        if (state_.timer > params_.lifetime) {
            state_.timer = 0;
        }
    }
}

namespace {

const char* emitter_name(HeatEmitter e) {
    switch (e) {
    case HeatEmitter::Burst: return "burst";
    case HeatEmitter::Shell: return "shell";
    case HeatEmitter::Cloud: return "cloud";
    case HeatEmitter::Fan: return "fan";
    case HeatEmitter::Gather: return "gather";
    }
    return "?";
}

HeatFieldParams parse_one(const std::string& name, const json::Value& v) {
    if (!v.is_object()) {
        fail("\"" + name + "\" must be an object");
    }
    const auto where = [&](const char* key) { return "\"" + name + "." + key + "\""; };
    const auto int_of = [&](const char* key, std::int64_t lo, std::int64_t hi, std::int64_t fallback,
                            bool required) -> std::int64_t {
        const json::Value* e = v.find(key);
        if (e == nullptr) {
            if (required) {
                fail("missing key " + where(key));
            }
            return fallback;
        }
        if (!e->is_integer() || e->integer() < lo || e->integer() > hi) {
            fail(where(key) + " must be an integer in " + std::to_string(lo) + ".." + std::to_string(hi));
        }
        return e->integer();
    };
    const auto pair_of = [&](const char* key, std::int32_t* a, std::int32_t* b) {
        const json::Value* e = v.find(key);
        if (e == nullptr || !e->is_array() || e->array().size() != 2 || !e->array()[0].is_integer() ||
            !e->array()[1].is_integer()) {
            fail(where(key) + " must be [x, y]");
        }
        *a = static_cast<std::int32_t>(e->array()[0].integer());
        *b = static_cast<std::int32_t>(e->array()[1].integer());
    };

    HeatFieldParams p;
    const json::Value* em = v.find("emitter");
    if (em == nullptr || !em->is_string()) {
        fail(where("emitter") + " must be one of burst, shell, cloud, fan, gather");
    }
    bool known = false;
    for (HeatEmitter e :
         {HeatEmitter::Burst, HeatEmitter::Shell, HeatEmitter::Cloud, HeatEmitter::Fan, HeatEmitter::Gather}) {
        if (em->string() == emitter_name(e)) {
            p.emitter = e;
            known = true;
        }
    }
    if (!known) {
        fail(where("emitter") + " must be one of burst, shell, cloud, fan, gather");
    }

    std::vector<std::string> keys = {"emitter", "cols", "rows", "cell", "follow_origin", "stamp", "draw",
                                     "origin_offset", "depth_add", "slot_add", "slot_limit", "slot_far",
                                     "row_slot_step", "particles", "start_brightness", "brightness_decay",
                                     "stamp_shift", "project_shift", "smooth_loss", "clear_range", "quiet_color",
                                     "prim_code", "mode_tpage", "mode_dfe", "mode_dtd", "shade"};
    switch (p.emitter) {
    case HeatEmitter::Burst:
        keys.insert(keys.end(), {"start_delay", "speed_min", "speed_range", "speed_scale", "fall_start", "gravity",
                                 "fall_max", "between_shift", "between_height"});
        break;
    case HeatEmitter::Shell: keys.insert(keys.end(), {"start_delay", "speed"}); break;
    case HeatEmitter::Cloud: keys.insert(keys.end(), {"speed", "spread", "spread_push", "life"}); break;
    case HeatEmitter::Fan: keys.insert(keys.end(), {"lifetime", "hold_on_still_frame"}); break;
    case HeatEmitter::Gather: keys.insert(keys.end(), {"speed", "radius"}); break;
    }
    for (const auto& [key, value] : v.members()) {
        (void)value;
        if (key.rfind("_", 0) == 0 || key == "description") {
            continue;
        }
        if (std::find(keys.begin(), keys.end(), key) == keys.end()) {
            fail("unknown key " + where(key.c_str()) + " for a " + emitter_name(p.emitter) + " field");
        }
    }

    constexpr std::int64_t big = 1 << 24;
    p.cols = static_cast<int>(int_of("cols", 3, 256, 0, true));
    p.rows = static_cast<int>(int_of("rows", 2, 256, 0, true));
    p.cell = static_cast<int>(int_of("cell", 1, 64, 0, true));
    const json::Value* follow = v.find("follow_origin");
    if (follow == nullptr || !follow->is_bool()) {
        fail(where("follow_origin") + " must be true or false");
    }
    p.follow_origin = follow->boolean();
    pair_of("stamp", &p.stamp_x, &p.stamp_y);
    pair_of("draw", &p.draw_x, &p.draw_y);
    if (const json::Value* off = v.find("origin_offset"); off != nullptr) {
        if (!off->is_array() || off->array().size() != 3) {
            fail(where("origin_offset") + " must be [x, y, z] (Q8)");
        }
        std::int32_t c[3];
        for (int i = 0; i < 3; i++) {
            if (!off->array()[i].is_integer()) {
                fail(where("origin_offset") + " must be [x, y, z] (Q8)");
            }
            c[i] = static_cast<std::int32_t>(off->array()[i].integer());
        }
        p.origin_offset_q8 = Vec3{c[0], c[1], c[2]};
    }
    p.depth_add = static_cast<std::int32_t>(int_of("depth_add", -big, big, 0, false));
    p.slot_add = static_cast<std::int32_t>(int_of("slot_add", -big, big, 0, false));
    p.slot_limit = static_cast<std::int32_t>(int_of("slot_limit", -big, INT_MAX, INT_MAX, false));
    p.slot_far = static_cast<std::int32_t>(int_of("slot_far", 0, big, 0, false));
    p.row_slot_step = static_cast<std::int32_t>(int_of("row_slot_step", -256, 256, 0, false));
    p.particles = static_cast<int>(int_of("particles", 1, 4096, 0, true));
    p.start_brightness = static_cast<std::int32_t>(int_of("start_brightness", 0, big, 0, true));
    p.brightness_decay = static_cast<std::int32_t>(int_of("brightness_decay", 0, big, 0, true));
    p.stamp_shift = static_cast<int>(int_of("stamp_shift", 0, 16, 0, false));
    p.project_shift = static_cast<int>(int_of("project_shift", 0, 16, 0, true));
    p.smooth_loss = static_cast<std::int32_t>(int_of("smooth_loss", 0, 255, 0, true));
    p.clear_range = static_cast<std::int32_t>(int_of("clear_range", -1, big, -1, false));
    p.quiet_color = static_cast<std::uint32_t>(int_of("quiet_color", 0, 0xFFFFFFFFll, 0, true));
    p.prim_code = static_cast<std::int32_t>(int_of("prim_code", 0, 255, 0, true));
    p.mode_tpage = static_cast<std::int32_t>(int_of("mode_tpage", 0, 65535, 0, true));
    p.mode_dfe = static_cast<std::int32_t>(int_of("mode_dfe", 0, 1, 0, true));
    p.mode_dtd = static_cast<std::int32_t>(int_of("mode_dtd", 0, 1, 0, true));

    switch (p.emitter) {
    case HeatEmitter::Burst:
        p.start_delay = static_cast<std::int32_t>(int_of("start_delay", 0, big, 0, false));
        p.speed_min = static_cast<std::int32_t>(int_of("speed_min", -big, big, 0, true));
        p.speed_range = static_cast<std::int32_t>(int_of("speed_range", 1, big, 1, true));
        p.speed_scale = static_cast<std::int32_t>(int_of("speed_scale", -big, big, 0, true));
        p.fall_start = static_cast<std::int32_t>(int_of("fall_start", -32768, 32767, 0, true));
        p.gravity = static_cast<std::int32_t>(int_of("gravity", -32768, 32767, 0, true));
        p.fall_max = static_cast<std::int32_t>(int_of("fall_max", -32768, 32767, 0, true));
        p.between_shift = static_cast<int>(int_of("between_shift", 0, 16, 0, false));
        p.between_height = static_cast<std::int32_t>(int_of("between_height", -big, big, 0, false));
        break;
    case HeatEmitter::Shell:
        p.start_delay = static_cast<std::int32_t>(int_of("start_delay", 0, big, 0, false));
        p.speed = static_cast<std::int32_t>(int_of("speed", -big, big, 0, true));
        break;
    case HeatEmitter::Cloud:
        p.speed = static_cast<std::int32_t>(int_of("speed", -big, big, 0, true));
        p.spread = static_cast<std::int32_t>(int_of("spread", -big, big, 0, true));
        p.spread_push = static_cast<std::int32_t>(int_of("spread_push", -256, 256, 0, true));
        p.life = static_cast<std::int32_t>(int_of("life", 0, big, 0, true));
        break;
    case HeatEmitter::Gather:
        p.speed = static_cast<std::int32_t>(int_of("speed", 1, big, 0, true));
        p.radius = static_cast<std::int32_t>(int_of("radius", 0, big, 0, true));
        break;
    case HeatEmitter::Fan: {
        p.lifetime = static_cast<std::int32_t>(int_of("lifetime", 1, big, 0, true));
        const json::Value* hold = v.find("hold_on_still_frame");
        if (hold != nullptr) {
            if (!hold->is_bool()) {
                fail(where("hold_on_still_frame") + " must be true or false");
            }
            p.hold_on_still_frame = hold->boolean();
        }
        break;
    }
    }

    const json::Value* shade = v.find("shade");
    if (shade == nullptr || !shade->is_array() || shade->array().empty() || shade->array().size() > 256) {
        fail(where("shade") + " must be an array of 1..256 colour words (missing entries are 0)");
    }
    for (std::size_t i = 0; i < shade->array().size(); i++) {
        const json::Value& e = shade->array()[i];
        if (!e.is_integer() || e.integer() < 0 || e.integer() > 0xFFFFFFFFll) {
            fail(where("shade") + " entries must be unsigned 32-bit integers");
        }
        p.shade[i] = static_cast<std::uint32_t>(e.integer());
    }
    return p;
}

}  // namespace

Result<std::map<std::string, HeatFieldParams>> parse_heat_fields(std::string_view json_text) try {
    const Result<json::Value> doc = json::parse(json_text);
    if (!doc) {
        return Result<std::map<std::string, HeatFieldParams>>::fail(doc.error());
    }
    const json::Value& root = doc.value();
    if (!root.is_object()) {
        fail("the file must be an object");
    }
    std::map<std::string, HeatFieldParams> out;
    for (const auto& [key, value] : root.members()) {
        if (key.rfind("_", 0) == 0 || key == "description") {
            continue;
        }
        if (key != "fields") {
            fail("unknown key \"" + key + "\"");
        }
    }
    const json::Value* fields = root.find("fields");
    if (fields == nullptr || !fields->is_object()) {
        fail("\"fields\" must be an object of named fields");
    }
    for (const auto& [name, value] : fields->members()) {
        if (name.rfind("_", 0) == 0) {
            continue;
        }
        out.emplace(name, parse_one(name, value));
    }
    return Result<std::map<std::string, HeatFieldParams>>::ok(std::move(out));
} catch (const std::exception& e) {
    return Result<std::map<std::string, HeatFieldParams>>::fail(e.what());
}

Result<std::map<std::string, HeatFieldParams>> load_heat_fields(const std::filesystem::path& gamedata_dir,
                                                               const std::filesystem::path& mods_dir,
                                                               std::string* source) {
    namespace fs = std::filesystem;
    using R = Result<std::map<std::string, HeatFieldParams>>;
    const fs::path relative = fs::path("effects") / "heat_fields.json";
    // Base + every active mod's patch, in load order.
    std::string where;
    const Result<std::string> read = mods::read_data_text(gamedata_dir, mods_dir, relative, &where);
    if (source != nullptr) {
        *source = where;
    }
    if (!read) {
        return R::fail(read.error());
    }
    const std::string& text = read.value();
    R parsed = parse_heat_fields(text);
    if (!parsed) {
        return R::fail(where + ": " + parsed.error());
    }
    return parsed;
}

}  // namespace sh::effects
