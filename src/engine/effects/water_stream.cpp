// SPDX-License-Identifier: GPL-3.0-or-later
// The drain's water drops and ribbon, written from the original's behaviour. Arithmetic keeps
// the original's widths: products in 32 bits (wrapping), the "precise" products in 64 bits,
// partial table words truncated to 16 bits, and the scale words read back as unsigned.
// Divisions the original left unguarded (a zero length makes the x86 divide trap where the
// console returned garbage) skip the drop or the term instead.

#include "engine/effects/water_stream.h"
#include "engine/effects/effect_view.h"

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

std::int32_t wrap(std::int64_t v) {
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(v));
}
std::int32_t mul(std::int32_t a, std::int32_t b) {
    return wrap(static_cast<std::int64_t>(a) * b) >> 12;
}
std::int32_t mulp(std::int32_t a, std::int32_t b) {
    return static_cast<std::int32_t>((static_cast<std::int64_t>(a) * b) >> 12);
}
std::int32_t q12(std::int32_t v) {
    return wrap(static_cast<std::int64_t>(v) * 4096);
}
std::int16_t s16(std::int32_t v) {
    return static_cast<std::int16_t>(v);
}
std::int32_t absv(std::int32_t v) {
    return v < 0 ? -v : v;
}

Vec3s rel(std::int32_t x, std::int32_t y, std::int32_t z, const Vec3& o) {
    return Vec3s{s16((x >> 4) - o.x), s16((y >> 4) - o.y), s16((z >> 4) - o.z)};
}

}  // namespace

void WaterStream::spawn(WaterStreamHost& host, StreamTable& t, int slot, int mode, int segment) const {
    WaterDrop d = host.load(slot);
    if (t.count == 0) {
        d.state = 0;
        host.store(slot, d);
        return;
    }
    int v;
    if (mode == 2) {
        v = host.rand16() % t.count;
        d.elapsed = static_cast<std::uint16_t>(t.length[v] != 0 ? host.rand16() % t.length[v] : 0);
    } else {
        v = segment % t.count;
        d.elapsed = 0;
    }
    d.x = t.x[v];
    d.y = s16(t.y[v]);
    d.z = t.z[v];

    if (t.mode[v] == 0) {
        if (mode == 2) {
            d.x += mul(d.elapsed, host.sin(t.angle[v]));
            d.z += mul(d.elapsed, host.cos(t.angle[v]));
        }
        d.scale = static_cast<std::uint16_t>(
            (t.jitter[v] * host.rand_uint(params_.spawn_speed_low, params_.spawn_speed_high)) >>
            params_.spawn_speed_shift);
    } else {
        if (mode == 2) {
            d.y = s16(d.y + s16(d.elapsed));
        }
        d.scale = static_cast<std::uint16_t>(host.rand_uint(params_.fall_speed_low, params_.fall_speed_high));
        d.fall = 0;
    }

    const int pick = host.rand16() % 3;
    d.cell = static_cast<std::uint8_t>((pick ? pick + 1 : 0) + v * 4);
    host.store(slot, d);
}

void WaterStream::start(WaterStreamHost& host, StreamTable& t) const {
    for (int i = 0; i < t.drops; i++) {
        const int slot = host.alloc(static_cast<std::uint8_t>(params_.group));
        if (slot < 0) {
            break;
        }
        spawn(host, t, slot, 2, 0);
    }
    for (int i = 0; i < t.count; i++) {
        t.phase[i] = 0;
    }
    host.set_running(true);
}

void WaterStream::stop(WaterStreamHost& host) const {
    host.clear_groups(static_cast<std::uint8_t>(params_.group), static_cast<std::uint8_t>(params_.group + 1));
    host.set_running(false);
}

bool WaterStream::update_drop(WaterStreamHost& host, StreamTable& t, int slot) const {
    WaterDrop d = host.load(slot);
    const int idx0 = d.cell >> 2;
    const int count = std::min<int>(t.count, kWaterSegments);
    const std::int32_t tail = t.length[count];

    if (t.ramp[idx0] == 0 || t.length[idx0] == 0 || tail == 0) {
        return false;
    }

    const WaterSegmentTables& seg = host.segments();
    const Vec3 origin = host.view_origin();

    std::uint8_t v_off = 0;   // the quad's top texel row
    std::uint8_t v_len = 0;   // its height
    std::int16_t y_low = 0;
    std::int16_t y_high = 0;
    std::int32_t fade = 0;
    std::int32_t travel;
    std::int32_t ramp_frac;
    std::int32_t blend;
    Vec3s c[4];

    if (t.mode[idx0] == 0) {
        travel = mulp(host.delta_time(), d.scale);

        if (d.elapsed + travel < t.length[idx0]) {
            d.x += mul(travel, seg.sin[idx0]);
            d.z += mul(travel, seg.cos[idx0]);
        } else if (d.elapsed + travel >= t.length[idx0] + t.ramp[idx0]) {
            host.store(slot, d);
            spawn(host, t, slot, 3, idx0 + 1);
            return false;
        } else if (d.elapsed < t.length[idx0]) {
            d.x += wrap(static_cast<std::int64_t>(t.length[idx0] - d.elapsed) * seg.sin[idx0]) >> 12;
            d.z += wrap(static_cast<std::int64_t>(t.length[idx0] - d.elapsed) * seg.cos[idx0]) >> 12;
        }

        d.elapsed = static_cast<std::uint16_t>(d.elapsed + travel);
        travel = d.elapsed;

        if (travel < t.ramp[idx0]) {
            ramp_frac = q12(travel) / t.ramp[idx0];
            v_off = 0;
            v_len = static_cast<std::uint8_t>(ramp_frac >> 6);
            y_low = s16(t.y[idx0]);
        } else if (t.length[idx0] < travel) {
            ramp_frac = q12(t.length[idx0] + t.ramp[idx0] - travel) / t.ramp[idx0];
            v_off = static_cast<std::uint8_t>(63 - (ramp_frac >> 6));
            v_len = static_cast<std::uint8_t>(ramp_frac >> 6);
            y_high = s16(t.y[idx0 + 1]);
        } else {
            ramp_frac = 4096;
            v_off = 0;
            v_len = 63;
        }

        if (travel >= t.ramp[idx0]) {
            y_low = s16(t.y[idx0] + (wrap(static_cast<std::int64_t>(t.y[idx0 + 1] - t.y[idx0]) * (travel - t.ramp[idx0])) /
                                     t.length[idx0]));
        }
        if (t.length[idx0] >= travel) {
            y_high = s16(t.y[idx0] +
                         (wrap(static_cast<std::int64_t>(t.y[idx0 + 1] - t.y[idx0]) * travel) / t.length[idx0]));
        }

        std::int32_t side_z, side_x, reach_x, reach_z;
        if (idx0 == count - 1 && (t.length[idx0] - tail) < travel) {
            const std::int32_t base = t.length[idx0] - tail;
            blend = q12(std::min(t.length[idx0], travel) - base) / tail;
            fade = mulp(ramp_frac >> 5, host.cos(blend >> 2));
            const std::int16_t half_width = s16(mulp(4096 - blend, t.spread[idx0] >> 1) + mulp(blend, t.spread[idx0 + 1] >> 1));
            const std::int16_t reach = s16(mulp(4096 - blend, t.ramp[idx0]) + mulp(blend, t.ramp[idx0 + 1]));
            side_z = wrap(static_cast<std::int64_t>(half_width) * host.sin(t.angle[idx0]));
            side_x = wrap(static_cast<std::int64_t>(half_width) * host.cos(t.angle[idx0]));
            reach_x = mulp(ramp_frac, wrap(static_cast<std::int64_t>(reach) * host.sin(t.angle[idx0])));
            reach_z = mulp(ramp_frac, wrap(static_cast<std::int64_t>(reach) * host.cos(t.angle[idx0])));
        } else {
            fade = ramp_frac >> 5;
            side_z = seg.ec[idx0];
            side_x = seg.fc[idx0];
            reach_x = mulp(ramp_frac, seg.far_x[idx0]);
            reach_z = mulp(ramp_frac, seg.far_z[idx0]);
        }

        if (idx0 < count - 1) {
            if (t.mode[idx0 + 1] == 0) {
                blend = absv(mul(t.spread[idx0 + 1], host.sin(t.angle[idx0] - t.angle[idx0 + 1])));
                std::int32_t near = t.length[idx0] - travel;
                if (near < blend) {
                    near = near >= 0 ? q12(near) / blend : 0;
                    fade = mulp(near, fade);
                }
            }
        }

        if (idx0 > 0) {
            if (t.mode[idx0 - 1] == 0) {
                ramp_frac = t.ramp[idx0];
                const std::int32_t sw = host.sin(t.angle[idx0 - 1] - t.angle[idx0]);
                if ((t.spread[idx0 - 1] * sw) < 0) {
                    blend = ramp_frac - ((t.spread[idx0 - 1] * sw) >> 12);
                } else {
                    blend = ramp_frac + ((t.spread[idx0 - 1] * sw) >> 12);
                }
                if (travel < blend) {
                    fade = mulp(q12(travel) / blend, fade);
                }
            }
        }

        const auto word_y = [&](std::int32_t y) { return s16(((y) >> 4) - origin.y); };
        c[0] = Vec3s{s16(((d.x + (side_x >> 12)) >> 4) - origin.x), word_y(y_low),
                     s16(((d.z + ((-side_z) >> 12)) >> 4) - origin.z)};
        c[1] = Vec3s{s16(((d.x + ((-side_x) >> 12)) >> 4) - origin.x), word_y(y_low),
                     s16(((d.z + (side_z >> 12)) >> 4) - origin.z)};
        c[2] = Vec3s{s16(((d.x + ((side_x - reach_x) >> 12)) >> 4) - origin.x), word_y(y_high),
                     s16(((d.z + ((-side_z - reach_z) >> 12)) >> 4) - origin.z)};
        c[3] = Vec3s{s16(((d.x + ((-side_x - reach_x) >> 12)) >> 4) - origin.x), word_y(y_high),
                     s16(((d.z + ((side_z - reach_z) >> 12)) >> 4) - origin.z)};
    } else {
        d.fall = static_cast<std::uint16_t>(
            std::min<std::int32_t>(d.fall + ((host.gravity() * d.scale) >> 7), params_.gravity_cap));

        travel = mulp(host.delta_time(), d.fall);

        if (d.elapsed + travel >= t.length[idx0] + t.ramp[idx0]) {
            host.store(slot, d);
            spawn(host, t, slot, 3, idx0 + 1);
            return false;
        }

        d.elapsed = static_cast<std::uint16_t>(d.elapsed + travel);
        d.y = s16(d.y + travel);
        travel = d.elapsed;

        if (travel < t.ramp[idx0]) {
            ramp_frac = q12(travel) / t.ramp[idx0];
            v_off = 0;
            v_len = static_cast<std::uint8_t>(ramp_frac >> 6);
        } else if (t.length[idx0] < travel) {
            ramp_frac = q12((t.length[idx0] + t.ramp[idx0]) - travel) / t.ramp[idx0];
            v_off = static_cast<std::uint8_t>(63 - (ramp_frac >> 6));
            v_len = static_cast<std::uint8_t>(ramp_frac >> 6);
        } else {
            ramp_frac = 4096;
            v_off = 0;
            v_len = 63;
        }

        fade = ramp_frac >> 5;
        const std::int32_t drop_len = mulp(ramp_frac, t.ramp[idx0]);

        const auto lo = [](std::int32_t raw) { return s16(raw); };
        const auto hi = [](std::int32_t raw) { return s16(static_cast<std::int32_t>(static_cast<std::uint32_t>(raw) >> 16)); };
        const std::int32_t ex0 = lo(seg.ec[idx0]), ex1 = hi(seg.ec[idx0]);
        const std::int32_t fz0 = lo(seg.fc[idx0]), fz1 = hi(seg.fc[idx0]);
        c[0] = Vec3s{s16(((d.x + ex0) >> 4) - origin.x), s16((d.y >> 4) - origin.y), s16(((d.z + fz0) >> 4) - origin.z)};
        c[1] = Vec3s{s16(((d.x + ex1) >> 4) - origin.x), s16((d.y >> 4) - origin.y), s16(((d.z + fz1) >> 4) - origin.z)};
        c[2] = Vec3s{s16(((d.x + ex0) >> 4) - origin.x), s16(((d.y + drop_len) >> 4) - origin.y),
                     s16(((d.z + fz0) >> 4) - origin.z)};
        c[3] = Vec3s{s16(((d.x + ex1) >> 4) - origin.x), s16(((d.y + drop_len) >> 4) - origin.y),
                     s16(((d.z + fz1) >> 4) - origin.z)};
    }

    host.store(slot, d);

    ScreenPoint p3[3];
    host.project3(c[0], c[1], c[2], p3);
    const ScreenPoint p4 = host.project(c[3]);

    const std::int32_t depth = (p3[0].depth + p3[1].depth + p3[2].depth + p4.depth) >> 2;
    if (depth <= 0 || (depth >> 3) >= params_.ordering_table_size) {
        return false;
    }
    // The authored box answers the caller; the part a wider frame adds is drawn too (effect_view.h).
    const bool shown = absv(p4.x) <= params_.max_x && absv(p4.y) <= params_.max_y;
    if (!shown && (absv(p4.x) > screen_box_half_width(params_.max_x) || absv(p4.y) > params_.max_y)) {
        return false;
    }

    WaterStreamHost::DropQuad q;
    q.corners[0] = p3[0];
    q.corners[1] = p3[1];
    q.corners[2] = p3[2];
    q.corners[3] = p4;
    q.cell = d.cell;
    const std::int32_t top = v_off + ((d.cell & 1) << 6);
    q.v_top = top;
    q.v_bottom = v_len + top;
    q.intensity = (fade * host.darkness(d.x, d.y, d.z)) >> 7;
    q.slot = (depth + params_.slot_depth_offset) >> 3;
    host.emit_drop(q);
    return shown;
}

void WaterStream::draw_ribbon(WaterStreamHost& host, StreamTable& t) const {
    const Vec3 origin = host.view_origin();
    const int count = std::min<int>(t.count, kWaterSegments);
    const std::int32_t tail = t.length[count];

    std::int32_t last_step = 0;
    std::int16_t sin_a = 0, cos_a = 0;
    std::int16_t side[2][2] = {};
    std::int16_t row_y = 0;

    for (int i = 0; i < count; i++) {
        if (t.step[i] == 0) {
            continue;
        }

        std::int32_t half = t.width[i] >> 1;
        if (t.mode[i] == 0) {
            sin_a = s16(host.sin(t.angle[i]));
            cos_a = s16(host.cos(t.angle[i]));
            side[0][0] = s16(mul(half, cos_a));
            side[0][1] = s16(-side[0][0]);
            side[1][0] = s16(mul(-half, sin_a));
            side[1][1] = s16(-side[1][0]);
        } else {
            const std::int32_t yaw = host.view_yaw();
            side[0][0] = s16(mul(half, host.sin(yaw - 1024)));
            side[1][0] = s16(mul(half, host.cos(yaw - 1024)));
            side[0][1] = s16(mul(half, host.sin(yaw + 1024)));
            side[1][1] = s16(mul(half, host.cos(yaw + 1024)));
        }

        const std::uint16_t last_row = (i == count - 1) ? static_cast<std::uint16_t>(t.length[i] - tail) : 0xFFFF;

        ScreenPoint pts[kWaterRibbonRows][2] = {};
        std::int32_t grey[kWaterRibbonRows][2] = {};
        std::int32_t along = 0;
        std::int32_t blend = 0;
        int j = 0;
        for (;; j++) {
            blend = 0;

            if (t.mode[i] == 0) {
                if (along == 0) {
                    row_y = s16(t.y[i]);
                } else if (along == t.length[i]) {
                    row_y = s16(t.y[i + 1]);
                } else {
                    row_y = s16(t.y[i] + (wrap(static_cast<std::int64_t>(t.y[i + 1] - t.y[i]) * along) / t.length[i]));
                }

                if (last_row < along && tail != 0) {
                    blend = q12(along - last_row) / tail;
                    half = mulp(0x1000 - blend, t.width[i] >> 1) + mulp(blend, t.width[i + 1] >> 1);
                    side[0][0] = s16(mul(half, cos_a));
                    side[0][1] = s16(-side[0][0]);
                    side[1][0] = s16(mul(-half, sin_a));
                    side[1][1] = s16(-side[1][0]);
                }
            }

            for (int k = 0; k < 2; k++) {
                std::int32_t px, py, pz;
                if (t.mode[i] == 0) {
                    px = t.x[i] + side[0][k] + mul(along, sin_a);
                    py = row_y;
                    pz = t.z[i] + side[1][k] + mul(along, cos_a);
                } else {
                    px = t.x[i] + side[0][k];
                    py = t.y[i] + along;
                    pz = t.z[i] + side[1][k];
                }

                grey[j][k] = static_cast<std::uint8_t>(mulp(host.cos(blend >> 2), host.darkness(px, py, pz)));

                if (i < count - 1 && t.mode[i] == 0) {
                    if (t.mode[i + 1] == 0) {
                        blend = mul(t.spread[i + 1], host.sin(t.angle[i] - t.angle[i + 1]));
                        if (k == 0) {
                            blend = -blend;
                        }
                        const std::int32_t near = t.length[i] - along;
                        if (near < blend) {
                            const std::int32_t ratio = near >= 0 ? q12(near) / blend : 0;
                            grey[j][k] = static_cast<std::uint8_t>(mulp(ratio, grey[j][k]));
                        }
                    }
                }

                if (i > 0) {
                    if (t.mode[i] == 0 && t.mode[i - 1] == 0) {
                        blend = mul(t.spread[i - 1], host.sin(t.angle[i - 1] - t.angle[i]));
                        if (k == 0) {
                            blend = -blend;
                        }
                        if (along < blend) {
                            grey[j][k] = static_cast<std::uint8_t>(mulp(q12(along) / blend, grey[j][k]));
                        }
                    }
                }

                pts[j][k] = host.project(rel(px, py, pz, origin));
            }

            if (along != t.length[i]) {
                if (j != 0) {
                    if (t.length[i] < along + t.step[i]) {
                        last_step = t.length[i] - along;
                        along = t.length[i];
                    } else {
                        along += t.step[i];
                    }
                } else {
                    along += t.phase[i];
                }
            } else {
                break;
            }

            if (j >= params_.ribbon_max_rows - 1) {
                break;
            }
        }

        for (int k = 0; k < j; k++) {
            std::int32_t v_top, v_span;
            if (k == 0) {
                const std::int32_t lo = (t.phase[i] << 6) / t.step[i];
                v_top = 0x7F - lo;
                v_span = lo;
            } else {
                v_top = 0x40;
                v_span = (k + 1) == j ? (last_step << 6) / t.step[i] : 0x3F;
            }

            WaterStreamHost::RibbonQuad q;
            q.corners[0] = pts[k][0];
            q.corners[1] = pts[k][1];
            q.corners[2] = pts[k + 1][0];
            q.corners[3] = pts[k + 1][1];
            q.grey[0] = grey[k][0];
            q.grey[1] = grey[k][1];
            q.grey[2] = grey[k + 1][0];
            q.grey[3] = grey[k + 1][1];
            q.v_top = v_top;
            q.v_span = v_span;
            q.slot = (((pts[k][0].depth + pts[k][1].depth + pts[k + 1][0].depth + pts[k + 1][1].depth) >> 2) +
                      params_.slot_depth_offset) >> 3;
            host.emit_ribbon(q);
        }

        t.phase[i] = static_cast<std::uint16_t>(t.phase[i] + mulp(host.delta_time(), t.flow[i]));
        while (t.phase[i] >= t.step[i]) {
            t.phase[i] -= t.step[i];
        }
    }
}

Result<WaterStreamParams> parse_water_stream_params(std::string_view json_text) try {
    const Result<json::Value> doc = json::parse(json_text);
    if (!doc) {
        return Result<WaterStreamParams>::fail(doc.error());
    }
    const json::Value& root = doc.value();
    if (!root.is_object()) {
        fail("the file must be an object");
    }
    static const char* const keys[] = {"group", "gravity_cap", "spawn_speed_low", "spawn_speed_high",
                                       "spawn_speed_shift", "fall_speed_low", "fall_speed_high", "max_x", "max_y",
                                       "ordering_table_size", "slot_depth_offset", "drop_clut", "drop_page",
                                       "ribbon_clut", "ribbon_page", "ribbon_max_rows"};
    for (const auto& [key, value] : root.members()) {
        (void)value;
        if (key.rfind("_", 0) == 0 || key == "description") {
            continue;
        }
        if (std::find_if(std::begin(keys), std::end(keys), [&](const char* k) { return key == k; }) == std::end(keys)) {
            fail("unknown key \"" + key + "\"");
        }
    }
    const auto int_of = [&](const char* key, std::int32_t lo, std::int32_t hi) -> std::int32_t {
        const json::Value* v = root.find(key);
        if (v == nullptr) {
            fail(std::string("missing key \"") + key + "\"");
        }
        if (!v->is_integer() || v->integer() < lo || v->integer() > hi) {
            fail(std::string("\"") + key + "\" must be an integer in " + std::to_string(lo) + ".." + std::to_string(hi));
        }
        return static_cast<std::int32_t>(v->integer());
    };
    WaterStreamParams p;
    p.group = int_of("group", 1, 254);
    p.gravity_cap = int_of("gravity_cap", 0, 65535);
    p.spawn_speed_low = int_of("spawn_speed_low", 0, 4096);
    p.spawn_speed_high = int_of("spawn_speed_high", p.spawn_speed_low, 4096);
    p.spawn_speed_shift = int_of("spawn_speed_shift", 0, 16);
    p.fall_speed_low = int_of("fall_speed_low", 0, 4096);
    p.fall_speed_high = int_of("fall_speed_high", p.fall_speed_low, 4096);
    p.max_x = int_of("max_x", 0, 4096);
    p.max_y = int_of("max_y", 0, 4096);
    p.ordering_table_size = int_of("ordering_table_size", 1, 65536);
    p.slot_depth_offset = int_of("slot_depth_offset", 0, 1 << 20);
    p.drop_clut = int_of("drop_clut", 0, 65535);
    p.drop_page = int_of("drop_page", 0, 65535);
    p.ribbon_clut = int_of("ribbon_clut", 0, 65535);
    p.ribbon_page = int_of("ribbon_page", 0, 65535);
    p.ribbon_max_rows = int_of("ribbon_max_rows", 2, kWaterRibbonRows);
    return Result<WaterStreamParams>::ok(p);
} catch (const std::exception& e) {
    return Result<WaterStreamParams>::fail(e.what());
}

Result<WaterStreamParams> load_water_stream_params(const std::filesystem::path& gamedata_dir,
                                                   const std::filesystem::path& mods_dir, std::string* source) {
    namespace fs = std::filesystem;
    const fs::path relative = fs::path("effects") / "water_stream.json";
    // Base + every active mod's patch, in load order.
    std::string where;
    const Result<std::string> read = mods::read_data_text(gamedata_dir, mods_dir, relative, &where);
    if (source != nullptr) {
        *source = where;
    }
    if (!read) {
        return Result<WaterStreamParams>::fail(read.error());
    }
    const std::string& text = read.value();
    Result<WaterStreamParams> parsed = parse_water_stream_params(text);
    if (!parsed) {
        return Result<WaterStreamParams>::fail(where + ": " + parsed.error());
    }
    return parsed;
}

}  // namespace sh::effects
