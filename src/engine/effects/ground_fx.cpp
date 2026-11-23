// SPDX-License-Identifier: GPL-3.0-or-later
// Ground effects: dust clouds, floor marks, craters, rings and hole timelines (ground_fx.h).

#include "engine/effects/ground_fx.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <utility>

#include "engine/effects/effects_file.h"
#include "formats/common/json.h"

namespace sh::effects {

namespace {

std::int32_t mul_q12(std::int32_t a, std::int32_t b) {
    return static_cast<std::int32_t>((static_cast<std::int64_t>(a) * b) >> 12);
}

std::int16_t s16(std::int32_t v) { return static_cast<std::int16_t>(v); }

std::uint16_t palette_word(int x, int y) {
    return static_cast<std::uint16_t>((y << 6) | ((x >> 4) & 0x3F));
}

std::int32_t clamp5(std::int32_t v) { return std::min(std::max(v, 0), 0x1F); }

// Grid texture coordinates: three steps from the origin, the last one a texel short (it is the
// texel the face ends on).
std::array<std::uint16_t, 9> grid_uv(const std::array<std::uint8_t, 2>& origin, std::uint8_t step) {
    std::array<std::uint16_t, 9> out{};
    for (int i = 0; i < 3; i++) {
        std::int32_t v = (origin[1] + i * step) & 0xFF;
        if (i == 2) {
            v -= 1;
        }
        for (int k = 0; k < 3; k++) {
            std::int32_t u = (origin[0] + k * step) & 0xFF;
            if (k == 2) {
                u -= 1;
            }
            out[static_cast<std::size_t>(i * 3 + k)] = static_cast<std::uint16_t>((u & 0xFF) | ((v & 0xFF) << 8));
        }
    }
    return out;
}

}  // namespace

GroundFx::GroundFx(GroundFxParams params)
    : params_(std::move(params)),
      dust_(static_cast<std::size_t>(params_.dust.count)),
      marks_(static_cast<std::size_t>(params_.marks.count)),
      craters_(static_cast<std::size_t>(params_.craters.count)),
      rings_(static_cast<std::size_t>(params_.rings.count)),
      holes_(static_cast<std::size_t>(params_.hole_count)) {}

Vec3s GroundFx::relative(const Vec3& at) const {
    return Vec3s{s16((at.x - origin_x_) >> 4), s16(at.y >> 4), s16((at.z - origin_z_) >> 4)};
}

void GroundFx::reset(GroundFxHost& host, const Vec3& at) {
    const std::int32_t round = (1 << params_.grid_shift) - 1;
    std::int32_t x = at.x;
    if (x < 0) {
        x += round;
    }
    std::int32_t z = at.z;
    if (z < 0) {
        z += round;
    }
    origin_x_ = (x >> params_.grid_shift) << params_.grid_shift;
    origin_z_ = (z >> params_.grid_shift) << params_.grid_shift;

    textures(host);
    build_palettes(host);
    for (DustCloud& c : dust_) {
        c.life = 0;
        c.phase = 0;
        c.fades = false;
        c.heat.fill(0);
    }
    for (GroundMark& m : marks_) {
        m.timer = 0;
        m.alpha = params_.marks.alpha_reset;
    }
    for (GroundGrid& g : craters_) {
        g.life = 0;
    }
    for (GroundGrid& g : rings_) {
        g.life = 0;
    }
    for (GroundHole& h : holes_) {
        h.timeline = -1;
    }
}

void GroundFx::textures(GroundFxHost& host) {
    std::uint16_t page = 0, px = 0, py = 0;
    // A texture not loaded yet keeps the pages of the last reset.
    if (!host.texture(params_.palette.texture, page, px, py)) {
        return;
    }
    const GroundPalette& p = params_.palette;
    page_a_ = static_cast<std::uint16_t>(page | p.page_layer_a);
    page_b_ = static_cast<std::uint16_t>(page | p.page_layer_b);
    // The palette words count the rows from the texture's palette; the rows written below are
    // video memory rows (the original layout puts that palette on row 0).
    palette_above_ = palette_word(px, py + p.above_row);
    palette_below_ = palette_word(px, py + p.below_row);
    palette_ramp_ = palette_word(px, py + p.ramp_row);
    palette_x_ = static_cast<std::uint16_t>(px >> 4);
}

void GroundFx::build_palettes(GroundFxHost& host) {
    const GroundPalette& p = params_.palette;
    std::array<std::uint16_t, 48>& buf = rows_;
    const int x = palette_x_ * 16;
    host.read_palette_row(x, p.read_row, buf.data());

    const std::uint16_t ref = buf[static_cast<std::size_t>(p.reference_entry & 15)];
    const std::int32_t rr = (ref & 0x1F) + p.reference_offset[0];
    const std::int32_t rg = ((ref >> 5) & 0x1F) + p.reference_offset[1];
    const std::int32_t rb = ((ref >> 10) & 0x1F) + p.reference_offset[2];
    std::uint16_t* above = &buf[16];
    std::uint16_t* below = &buf[32];
    const auto shade = [](std::int32_t dr, std::int32_t dg, std::int32_t db, std::int32_t gain) {
        const std::int32_t r = clamp5((dr * gain) >> 7);
        const std::int32_t g = clamp5((dg * gain) >> 7);
        const std::int32_t b = clamp5((db * gain) >> 7);
        return static_cast<std::uint16_t>(r | (g << 5) | (b << 10) | 0x8000);
    };
    int i = 0;
    for (; i < p.split && i < 16; i++) {
        above[i] = 0;
        const std::uint16_t c = buf[static_cast<std::size_t>(i)];
        const std::uint16_t s =
            shade(rr - (c & 0x1F), rg - ((c >> 5) & 0x1F), rb - ((c >> 10) & 0x1F), p.gain_below);
        below[i] = c != 0 ? s : 0;
    }
    for (; i < 16; i++) {
        const std::uint16_t c = buf[static_cast<std::size_t>(i)];
        const std::uint16_t s =
            shade((c & 0x1F) - rr, ((c >> 5) & 0x1F) - rg, ((c >> 10) & 0x1F) - rb, p.gain_above);
        above[i] = c != 0 ? s : 0;
        below[i] = 0;
    }
    if (p.below_row == p.above_row + 1) {
        host.write_palette_rows(x, p.above_row, 2, above);
    } else {
        host.write_palette_rows(x, p.above_row, 1, above);
        host.write_palette_rows(x, p.below_row, 1, below);
    }

    buf[0] = 0;
    for (int e = 1; e < 16; e++) {
        const std::int32_t r = clamp5(mul_q12(e * 2, p.ramp_red));
        const std::int32_t gb = clamp5(mul_q12(e * 2, p.ramp_green_blue));
        buf[static_cast<std::size_t>(e)] = static_cast<std::uint16_t>((gb << 5) | (gb << 10) | r | 0x8000);
    }
    host.write_palette_rows(x, p.ramp_row, 1, buf.data());
}

void GroundFx::dust_at(const Vec3& at, int index) {
    if (index < 0 || index >= static_cast<int>(dust_.size())) {
        return;
    }
    DustCloud& c = dust_[static_cast<std::size_t>(index)];
    if (c.life > 0) {
        return;
    }
    c.kind = index & 1;
    c.index = index;
    c.life = params_.dust.life;
    c.abr = params_.dust.abr;
    c.fades = false;
    c.phase = 0;
    c.alpha = 4096;
    c.heat.fill(0);
    c.pos = relative(at);
}

int GroundFx::dust_free(const Vec3& at, int kind) {
    for (std::size_t i = 0; i < dust_.size(); i++) {
        DustCloud& c = dust_[i];
        if (c.life <= 0) {
            // The light (alpha) and the delay stay what they were.
            c.fades = false;
            c.phase = 0;
            c.index = static_cast<std::int32_t>(i);
            c.life = params_.dust.life;
            c.kind = kind;
            c.abr = params_.dust.abr;
            c.pos = relative(at);
            c.heat.fill(0);
            return static_cast<int>(i);
        }
    }
    return -1;
}

void GroundFx::mark_at(const Vec3& at, int index) {
    if (index < 0 || index >= static_cast<int>(marks_.size())) {
        return;
    }
    GroundMark& m = marks_[static_cast<std::size_t>(index)];
    if (m.timer <= 0) {
        m.timer = params_.marks.life;
        m.pos = relative(at);
    }
}

void GroundFx::crater_at(const Vec3& at) {
    for (GroundGrid& g : craters_) {
        if (g.life <= 0) {
            g.life = params_.craters.life;
            g.alpha = params_.craters.alpha_start;
            g.scale = params_.craters.scale_start;
            g.pos = relative(at);
            return;
        }
    }
}

void GroundFx::ring_at(const Vec3& at) {
    for (GroundGrid& g : rings_) {
        if (g.life <= 0) {
            g.life = params_.rings.life;
            g.alpha = params_.rings.alpha_start;
            g.scale = params_.rings.scale_start;
            g.pos = relative(at);
            return;
        }
    }
}

bool GroundFx::hole(GroundFxHost& host, int timeline, std::uint64_t owner, const Vec3& owner_pos) {
    for (GroundHole& h : holes_) {
        if (h.timeline < 0) {
            h.owner = owner;
            h.position = Vec3{owner_pos.x, host.ground_height(owner_pos.x, owner_pos.z), owner_pos.z};
            h.timeline = timeline;
            h.time = 0;
            h.step = 0;
            return true;
        }
    }
    return false;
}

void GroundFx::dust_ring(GroundFxHost& host) {
    const DustRingParams& r = params_.dust_ring;
    std::int32_t angle = host.rand16();
    for (int i = 0; i < r.count; i++) {
        const int k = dust_free(burst_point_, 2);
        if (k >= 0) {
            DustCloud& c = dust_[static_cast<std::size_t>(k)];
            angle += static_cast<std::int32_t>(static_cast<std::uint32_t>(host.rand16()) %
                                               static_cast<std::uint32_t>(2 * r.spread)) -
                     r.spread;
            c.velocity.x = s16(mul_q12(host.sin(angle), r.speed));
            c.velocity.y = s16(static_cast<std::int32_t>(static_cast<std::uint32_t>(host.rand16()) %
                                                         static_cast<std::uint32_t>(r.rise_high - r.rise_low + 1)) +
                               r.rise_low);
            c.velocity.z = s16(mul_q12(host.cos(angle), r.speed));
            c.life = r.life;
            c.delay = static_cast<std::int32_t>(static_cast<std::uint32_t>(host.rand16()) %
                                                static_cast<std::uint32_t>(r.delay_max + 1));
            c.alpha = r.alpha;
            c.phase = 0;
            c.fades = true;
            c.abr = params_.dust.abr;
        }
        angle += r.step;
    }
}

void GroundFx::blood_drop(GroundFxHost& host) {
    const BloodParams& b = params_.blood;
    if ((host.rand16() & b.skip_mask) != 0) {
        return;
    }
    Vec3 at = burst_point_;
    at.y += b.lift;
    const int k = dust_free(at, 2);
    if (k < 0) {
        return;
    }
    DustCloud& c = dust_[static_cast<std::size_t>(k)];
    c.velocity.x = s16(host.rand16() % b.spread - b.centre);
    c.velocity.y = s16(b.rise);
    c.velocity.z = s16(host.rand16() % b.spread - b.centre);
    c.abr = params_.dust.abr;
}

void GroundFx::run_actions(GroundFxHost& host, GroundHole& h, const std::vector<HoleStep::Act>& acts) {
    const auto ahead = [&](std::int32_t d) {
        const std::int32_t heading = host.owner_heading(h.owner);
        return Vec3{h.position.x + mul_q12(host.sin(heading), d), h.position.y,
                    h.position.z + mul_q12(host.cos(heading), d)};
    };
    for (const HoleStep::Act& a : acts) {
        switch (a.kind) {
            case HoleStep::Action::BurstPoint:
                burst_point_ = ahead(a.distance);
                break;
            case HoleStep::Action::DustRing:
                dust_ring(host);
                break;
            case HoleStep::Action::Blood:
                blood_drop(host);
                break;
            case HoleStep::Action::Crater:
                crater_at(ahead(a.distance));
                break;
            case HoleStep::Action::Ring:
                ring_at(ahead(a.distance));
                break;
        }
    }
}

void GroundFx::run_holes(GroundFxHost& host) {
    // Every busy hole runs its timeline on the state of the first hole, as the original did.
    GroundHole& h = holes_.front();
    for (std::size_t i = 0; i < holes_.size(); i++) {
        const int t = holes_[i].timeline;
        if (t < 0 || t >= static_cast<int>(params_.holes.size())) {
            continue;
        }
        const HoleTimeline& tl = params_.holes[static_cast<std::size_t>(t)];
        if (h.step >= 0 && h.step < static_cast<int>(tl.steps.size())) {
            const HoleStep& s = tl.steps[static_cast<std::size_t>(h.step)];
            if (!s.has_at || h.time >= s.at) {
                run_actions(host, h, s.run);
                if (s.end) {
                    h.timeline = -1;
                } else {
                    h.step++;
                }
            }
            if (s.has_each && (s.each_mask == 0 || (host.rand16() & s.each_mask) != 0)) {
                run_actions(host, h, s.each);
            }
        }
        h.time += host.delta_time();
    }
}

void GroundFx::frame(GroundFxHost& host, const Vec3s& rotation) {
    if (host.delta_time() != 0) {
        run_holes(host);
    }
    host.begin_frame(origin_x_, origin_z_, rotation);
    draw_dust(host, rotation.y);
    draw_marks(host);
    draw_grids(host, craters_, params_.craters, 0);
    draw_grids(host, rings_, params_.rings, host.view_yaw() + params_.rings_yaw_offset);
}

void GroundFx::draw_dust(GroundFxHost& host, std::int32_t heading) {
    const DustParams& d = params_.dust;
    const DustRingParams& r = params_.dust_ring;
    const std::int32_t dt = host.delta_time();
    for (DustCloud& c : dust_) {
        if (c.life <= 0) {
            continue;
        }
        if (c.fades) {
            if (c.phase == 0) {
                if (c.delay < 0) {
                    c.phase = 1;
                }
                c.delay -= dt;
                continue;
            }
            if (c.phase == 1) {
                if (c.life > r.fade_after) {
                    c.alpha = c.alpha < r.peak ? c.alpha + mul_q12(dt, r.rise_rate) : r.peak;
                } else {
                    c.alpha = c.alpha > 0 ? c.alpha - mul_q12(dt, r.fall_rate) : 0;
                }
            }
        }

        std::int32_t vx = 0, vy = 0, vz = 0;
        switch (c.kind) {
            case 0:
            case 1: {
                const bool hot = c.life > d.hot_life;
                const std::int32_t dist = hot ? d.speed[0] : d.speed[1];
                const std::int32_t h = heading + (c.kind != 0 ? -d.turn : d.turn);
                vx = mul_q12(dist, host.sin(h));
                vz = mul_q12(dist, host.cos(h));
                vy = hot ? d.rise[0] : d.rise[1];
                break;
            }
            case 2:
                vx = c.velocity.x;
                vy = c.velocity.y;
                vz = c.velocity.z;
                break;
            default:
                break;
        }
        c.pos.x = s16(c.pos.x + mul_q12(vx, dt));
        c.pos.y = s16(c.pos.y + mul_q12(vy, dt));
        c.pos.z = s16(c.pos.z + mul_q12(vz, dt));

        host.local_frame(c.pos, 0);
        const std::int32_t light = host.light(Vec3s{}, d.normal);
        host.view_at(0, c.pos);

        if (dt != 0) {
            // A new seed row at the bottom, then every row takes the average of itself, its
            // neighbours and the row below (in place, left to right).
            const std::size_t seed = 1 + static_cast<std::size_t>(d.stride * (d.buffer_rows - 1));
            std::memset(&c.heat[seed], 0, static_cast<std::size_t>(d.stride));
            if (c.life > d.hot_life) {
                for (int t = 0; t < d.seed_tries; t++) {
                    if ((host.rand16() & d.seed_mask) != 0) {
                        const std::int32_t rnd = host.rand16() >> 2;
                        const std::int32_t at = rnd % (d.stride / 4) + d.stride / 4;
                        c.heat[seed + static_cast<std::size_t>(at) + 1] = 0xFF;
                    }
                }
            }
            for (int i = 0; i < d.buffer_rows - 1; i++) {
                const std::size_t b = 1 + static_cast<std::size_t>(i * d.stride);
                for (int j = 0; j < d.stride; j++) {
                    const std::size_t at = b + static_cast<std::size_t>(j);
                    const std::int32_t sum =
                        (c.heat[at + static_cast<std::size_t>(d.stride)] + c.heat[at - 1] + c.heat[at] + c.heat[at + 1]) >> 2;
                    c.heat[at] = static_cast<std::uint8_t>(sum <= 0 ? 0 : sum);
                }
            }
        }

        draw_cloud(host, c, static_cast<std::int32_t>((static_cast<std::int64_t>(light) * c.alpha) >> 12));
        c.life -= dt;
    }
}

void GroundFx::draw_cloud(GroundFxHost& host, DustCloud& c, std::int32_t light) {
    const DustParams& d = params_.dust;
    const std::int32_t dt = host.delta_time();
    const auto colour = [&](std::int32_t v) -> std::uint32_t {
        std::int32_t index = (v * light) >> 8;
        if (index != 0 && dt != 0) {
            index += static_cast<std::int32_t>(static_cast<std::uint32_t>(host.rand16()) %
                                               static_cast<std::uint32_t>(d.jitter_high - d.jitter_low + 1)) +
                     d.jitter_low;
            index = std::min(255, index);
            index = std::max(index, 0);
        }
        // Without the jitter the index is not clamped; keep it inside the ramp.
        index = std::clamp(index, 0, static_cast<std::int32_t>(d.ramp.size()) - 1);
        return d.ramp[static_cast<std::size_t>(index)];
    };

    const std::uint32_t base = d.ramp[0];
    const GroundProjected o = host.project_origin();
    if (o.too_near || o.depth == 0) {
        return;
    }
    const std::int32_t slot = std::clamp(o.depth >> 1, 0, host.ordering_size() - 1);
    const std::int32_t screen = host.geom_screen();
    const std::int32_t w = (screen * d.cell_w) / o.depth + 1;
    const std::int32_t h = (screen * d.cell_h) / o.depth + 1;
    const std::int32_t xbase = o.x - (d.cols / 2) * w;
    const std::int32_t ybase = o.y - d.rows * h;

    for (int i = 1; i < d.rows + 1; i++) {
        std::uint32_t c1 = base, c3 = base;
        const std::int32_t y = (i - 1) * h + ybase;
        for (int j = 1; j < d.cols + 1; j++) {
            const std::uint32_t c0 = c1;
            const std::uint32_t c2 = c3;
            if (j == d.cols) {
                c1 = c3 = base;
            } else {
                c1 = colour(c.heat[static_cast<std::size_t>((i - 1) * d.stride + j + 1)]);
                c3 = i == d.rows ? base : colour(c.heat[static_cast<std::size_t>(i * d.stride + j + 1)]);
            }
            const bool drawn = c0 != base || c1 != c0 || c2 != c1 || c3 != c2;
            host.box(s16((j - 1) * w + xbase), s16(y), s16(w), s16(h), {c0, c1, c2, c3}, slot, drawn);
        }
    }
    const std::uint16_t page = static_cast<std::uint16_t>(((c.abr & 3) << 5) | ((d.page_x & 0x3FF) >> 6));
    host.draw_page(slot, 0, 1, page);
}

void GroundFx::draw_marks(GroundFxHost& host) {
    const MarkParams& p = params_.marks;
    const std::int32_t dt = host.delta_time();
    host.local_frame(Vec3s{}, 0);
    host.frame_view();
    for (GroundMark& m : marks_) {
        if (m.timer <= 0) {
            continue;
        }
        if (m.timer > p.hold) {
            m.alpha = 4096;
        } else {
            m.alpha -= mul_q12(dt, p.fade_rate);
            m.alpha = std::max(m.alpha, 0);
        }

        host.push_view();
        const std::int32_t lit = host.light(m.pos, p.normal) & 0xFF;
        const std::int32_t a = mul_q12(lit, m.alpha);
        const std::uint32_t colour = host.tint(static_cast<std::uint8_t>((a < 0x100 ? a : 0xFF) & 0xFF));
        host.pop_view();

        const std::int16_t x0 = s16(m.pos.x - p.half), x1 = s16(m.pos.x + p.half);
        const std::int16_t z0 = s16(m.pos.z - p.half), z1 = s16(m.pos.z + p.half);
        const std::array<Vec3s, 4> corners{Vec3s{x0, m.pos.y, z0}, Vec3s{x1, m.pos.y, z0}, Vec3s{x0, m.pos.y, z1},
                                           Vec3s{x1, m.pos.y, z1}};
        std::array<GroundProjected, 4> screen{};
        const GroundProjected r = host.project4(corners, screen);
        if (!r.too_near) {
            const std::int32_t b = std::clamp(static_cast<std::int32_t>(static_cast<std::uint32_t>(r.depth) >> 1), 0,
                                              host.ordering_size() - 1 - p.slot_bias);
            host.mark(screen, colour, p.uv, page_a_, palette_above_, b + p.slot_bias);
            host.mark(screen, colour, p.uv, page_b_, palette_below_, b + p.slot_bias);
        }
        m.timer -= dt;
    }
}

void GroundFx::draw_grids(GroundFxHost& host, std::vector<GroundGrid>& pool, const GridParams& p, std::int32_t yaw) {
    const std::int32_t dt = host.delta_time();
    const std::array<std::uint16_t, 9> uv_a = grid_uv(p.uv_a, p.uv_step);
    const std::array<std::uint16_t, 9> uv_b = grid_uv(p.uv_b, p.uv_step);
    for (GroundGrid& g : pool) {
        if (g.life <= 0) {
            continue;
        }
        g.scale += mul_q12(dt, p.grow_rate);
        g.scale = std::min(4096, g.scale);
        if (g.life > p.hold) {
            if (p.alpha_rise < 0) {
                g.alpha = 4096;
            } else {
                const std::int32_t v = g.alpha + mul_q12(dt, p.alpha_rise);
                g.alpha = v > 4096 ? 4096 : v;
            }
        } else {
            g.alpha -= mul_q12(dt, p.fade_rate);
            g.alpha = std::max(g.alpha, 0);
        }
        host.local_frame(g.pos, yaw);
        host.view_at(yaw, g.pos);

        const std::int32_t half = mul_q12(g.scale, p.size);
        std::array<Vec3s, 9> pts{};
        for (int i = -1; i < 2; i++) {
            for (int k = -1; k < 2; k++) {
                pts[static_cast<std::size_t>((i + 1) * 3 + (k + 1))] = Vec3s{s16(half * k), 0, s16(half * i)};
            }
        }
        std::array<std::uint32_t, 9> colours{};
        host.push_view();
        for (std::size_t j = 0; j < 9; j++) {
            const std::int32_t v = mul_q12(host.light(pts[j], p.normal), g.alpha);
            colours[j] = host.tint(static_cast<std::uint8_t>(p.wrap_amount ? v : std::min(v, 0xFF)));
        }
        host.pop_view();

        const std::int32_t depth = host.project_grid(pts);
        const std::int32_t b = std::clamp(depth >> 1, 0, host.ordering_size() - 1 - p.slot_bias);
        for (const std::array<std::uint8_t, 4>& q : params_.grid_quads) {
            GroundGridQuad a, c;
            a.corner = q;
            c.corner = q;
            for (std::size_t n = 0; n < 4; n++) {
                a.uv[n] = uv_a[q[n]];
                c.uv[n] = uv_b[q[n]];
                a.color[n] = colours[q[n]];
                c.color[n] = colours[q[n]];
            }
            if (p.ramp_layers) {
                a.page = c.page = p.page;
                a.palette = c.palette = palette_ramp_;
                host.grid_quad(a, b + p.slot_bias);
                host.grid_quad(c, b);
            } else {
                a.page = page_a_;
                a.palette = palette_above_;
                c.page = page_b_;
                c.palette = palette_below_;
                host.grid_quad(a, b + p.slot_bias);
                host.grid_quad(c, b + p.slot_bias);
            }
        }
        g.life -= dt;
    }
}

namespace {

namespace p = parse;

constexpr std::int64_t kBig = 1 << 24;

std::int32_t opt(const json::Value& v, std::string_view key, std::int32_t def, std::int64_t lo, std::int64_t hi) {
    return v.find(key) != nullptr ? p::integer_of(v, key, lo, hi) : def;
}

Vec3s vec3s(const json::Value& v) {
    if (!v.is_array() || v.array().size() != 3) {
        p::fail(v, "[x, y, z] is expected");
    }
    return Vec3s{static_cast<std::int16_t>(p::integer(v.array()[0], -32768, 32767)),
                 static_cast<std::int16_t>(p::integer(v.array()[1], -32768, 32767)),
                 static_cast<std::int16_t>(p::integer(v.array()[2], -32768, 32767))};
}

template <std::size_t N>
std::array<std::uint8_t, N> bytes(const json::Value& v) {
    if (!v.is_array() || v.array().size() != N) {
        p::fail(v, "an array of " + std::to_string(N) + " bytes is expected");
    }
    std::array<std::uint8_t, N> out{};
    for (std::size_t i = 0; i < N; i++) {
        out[i] = static_cast<std::uint8_t>(p::integer(v.array()[i], 0, 255));
    }
    return out;
}

GroundPalette parse_palette(const json::Value& v) {
    p::reject_unknown(v, {"texture", "read_row", "below_row", "above_row", "ramp_row", "split", "reference_entry",
                          "reference_offset", "gain_below", "gain_above", "ramp_red", "ramp_green_blue", "page_layer_a",
                          "page_layer_b"});
    GroundPalette o;
    const json::Value& tex = p::member(v, "texture");
    if (!tex.is_string() || tex.string().empty()) {
        p::fail(tex, "\"texture\" must be a texture name");
    }
    o.texture = tex.string();
    o.read_row = p::integer_of(v, "read_row", 0, 511);
    o.below_row = p::integer_of(v, "below_row", 0, 511);
    o.above_row = p::integer_of(v, "above_row", 0, 511);
    o.ramp_row = p::integer_of(v, "ramp_row", 0, 511);
    o.split = p::integer_of(v, "split", 0, 16);
    o.reference_entry = p::integer_of(v, "reference_entry", 0, 15);
    const json::Value& off = p::member(v, "reference_offset");
    if (!off.is_array() || off.array().size() != 3) {
        p::fail(off, "\"reference_offset\" is [r, g, b]");
    }
    for (std::size_t i = 0; i < 3; i++) {
        o.reference_offset[i] = p::integer(off.array()[i], -31, 31);
    }
    o.gain_below = p::integer_of(v, "gain_below", 0, kBig);
    o.gain_above = p::integer_of(v, "gain_above", 0, kBig);
    o.ramp_red = p::integer_of(v, "ramp_red", 0, kBig);
    o.ramp_green_blue = p::integer_of(v, "ramp_green_blue", 0, kBig);
    o.page_layer_a = static_cast<std::uint16_t>(p::integer_of(v, "page_layer_a", 0, 0xFFFF));
    o.page_layer_b = static_cast<std::uint16_t>(p::integer_of(v, "page_layer_b", 0, 0xFFFF));
    return o;
}

DustParams parse_dust(const json::Value& v) {
    p::reject_unknown(v, {"count", "cols", "rows", "stride", "buffer_rows", "cell_w", "cell_h", "life", "hot_life",
                          "seed_tries", "seed_mask", "jitter", "page_x", "abr", "normal", "turn", "speed", "rise",
                          "ramp"});
    DustParams o;
    o.count = p::integer_of(v, "count", 0, 256);
    o.cols = p::integer_of(v, "cols", 1, 64);
    o.rows = p::integer_of(v, "rows", 1, 64);
    o.stride = p::integer_of(v, "stride", 4, 64);
    o.buffer_rows = p::integer_of(v, "buffer_rows", 2, 64);
    // The draw reads cols + 1 cells of rows + 1 rows; the buffer is one fixed block.
    if (o.stride * o.buffer_rows + 1 > 133 || o.cols + 1 > o.stride || o.rows + 1 > o.buffer_rows) {
        p::fail(v, "the heat buffer (stride x buffer_rows) must fit 132 cells and hold (cols + 1) x (rows + 1)");
    }
    o.cell_w = p::integer_of(v, "cell_w", 0, 4096);
    o.cell_h = p::integer_of(v, "cell_h", 0, 4096);
    o.life = p::integer_of(v, "life", 0, kBig);
    o.hot_life = p::integer_of(v, "hot_life", 0, kBig);
    o.seed_tries = p::integer_of(v, "seed_tries", 0, 64);
    o.seed_mask = p::integer_of(v, "seed_mask", 0, 0x7FFF);
    const json::Value& jit = p::member(v, "jitter");
    if (!jit.is_array() || jit.array().size() != 2) {
        p::fail(jit, "\"jitter\" is [low, high]");
    }
    o.jitter_low = p::integer(jit.array()[0], -255, 255);
    o.jitter_high = p::integer(jit.array()[1], o.jitter_low, 255);
    o.page_x = p::integer_of(v, "page_x", 0, 1023);
    o.abr = p::integer_of(v, "abr", 0, 3);
    o.normal = vec3s(p::member(v, "normal"));
    o.turn = p::integer_of(v, "turn", -kBig, kBig);
    const json::Value& sp = p::member(v, "speed");
    const json::Value& ri = p::member(v, "rise");
    if (!sp.is_array() || sp.array().size() != 2 || !ri.is_array() || ri.array().size() != 2) {
        p::fail(v, "\"speed\" and \"rise\" are [hot, cool]");
    }
    for (std::size_t i = 0; i < 2; i++) {
        o.speed[i] = p::integer(sp.array()[i], -kBig, kBig);
        o.rise[i] = p::integer(ri.array()[i], -kBig, kBig);
    }
    const json::Value& ramp = p::member(v, "ramp");
    if (!ramp.is_array() || ramp.array().size() != 256) {
        p::fail(ramp, "\"ramp\" must be 256 colour words");
    }
    for (std::size_t i = 0; i < 256; i++) {
        const json::Value& w = ramp.array()[i];
        if (!w.is_integer() || w.integer() < 0 || w.integer() > 0xFFFFFFFFll) {
            p::fail(w, "an unsigned 32-bit colour word is expected");
        }
        o.ramp[i] = static_cast<std::uint32_t>(w.integer());
    }
    return o;
}

DustRingParams parse_dust_ring(const json::Value& v) {
    p::reject_unknown(v, {"count", "speed", "spread", "step", "rise", "life", "delay_max", "alpha", "fade_after", "peak",
                          "rise_rate", "fall_rate"});
    DustRingParams o;
    o.count = p::integer_of(v, "count", 0, 256);
    o.speed = p::integer_of(v, "speed", -kBig, kBig);
    o.spread = p::integer_of(v, "spread", 1, 0x4000);
    o.step = p::integer_of(v, "step", -kBig, kBig);
    const json::Value& r = p::member(v, "rise");
    if (!r.is_array() || r.array().size() != 2) {
        p::fail(r, "\"rise\" is [low, high]");
    }
    o.rise_low = p::integer(r.array()[0], -32768, 32767);
    o.rise_high = p::integer(r.array()[1], o.rise_low, 32767);
    o.life = p::integer_of(v, "life", 0, kBig);
    o.delay_max = p::integer_of(v, "delay_max", 0, 0x7FFF);
    o.alpha = p::integer_of(v, "alpha", -kBig, kBig);
    o.fade_after = p::integer_of(v, "fade_after", -kBig, kBig);
    o.peak = p::integer_of(v, "peak", -kBig, kBig);
    o.rise_rate = p::integer_of(v, "rise_rate", -kBig, kBig);
    o.fall_rate = p::integer_of(v, "fall_rate", -kBig, kBig);
    return o;
}

BloodParams parse_blood(const json::Value& v) {
    p::reject_unknown(v, {"skip_mask", "lift", "spread", "centre", "rise"});
    BloodParams o;
    o.skip_mask = p::integer_of(v, "skip_mask", 0, 0x7FFF);
    o.lift = p::integer_of(v, "lift", -kBig, kBig);
    o.spread = p::integer_of(v, "spread", 1, 0x7FFF);
    o.centre = p::integer_of(v, "centre", -32768, 32767);
    o.rise = p::integer_of(v, "rise", -32768, 32767);
    return o;
}

MarkParams parse_marks(const json::Value& v) {
    p::reject_unknown(v, {"count", "life", "hold", "fade_rate", "alpha_reset", "half", "uv", "normal", "slot_bias"});
    MarkParams o;
    o.count = p::integer_of(v, "count", 0, 256);
    o.life = p::integer_of(v, "life", 0, kBig);
    o.hold = p::integer_of(v, "hold", -kBig, kBig);
    o.fade_rate = p::integer_of(v, "fade_rate", -kBig, kBig);
    o.alpha_reset = p::integer_of(v, "alpha_reset", -kBig, kBig);
    o.half = static_cast<std::int16_t>(p::integer_of(v, "half", 0, 0x4000));
    o.uv = bytes<4>(p::member(v, "uv"));
    o.normal = vec3s(p::member(v, "normal"));
    o.slot_bias = p::integer_of(v, "slot_bias", 0, 1024);
    return o;
}

GridParams parse_grid(const json::Value& v) {
    p::reject_unknown(v, {"count", "life", "hold", "alpha_start", "scale_start", "alpha_rise", "fade_rate", "grow_rate",
                          "size", "wrap_amount", "uv_a", "uv_b", "uv_step", "normal", "slot_bias", "ramp_layers",
                          "page"});
    GridParams o;
    o.count = p::integer_of(v, "count", 0, 256);
    o.life = p::integer_of(v, "life", 0, kBig);
    o.hold = p::integer_of(v, "hold", -kBig, kBig);
    o.alpha_start = p::integer_of(v, "alpha_start", -kBig, kBig);
    o.scale_start = p::integer_of(v, "scale_start", -kBig, kBig);
    o.alpha_rise = opt(v, "alpha_rise", -1, -1, kBig);
    o.fade_rate = p::integer_of(v, "fade_rate", -kBig, kBig);
    o.grow_rate = p::integer_of(v, "grow_rate", -kBig, kBig);
    o.size = p::integer_of(v, "size", 0, 0x10000);
    o.wrap_amount = v.find("wrap_amount") != nullptr && p::boolean_of(v, "wrap_amount");
    o.uv_a = bytes<2>(p::member(v, "uv_a"));
    o.uv_b = bytes<2>(p::member(v, "uv_b"));
    o.uv_step = static_cast<std::uint8_t>(p::integer_of(v, "uv_step", 0, 255));
    o.normal = vec3s(p::member(v, "normal"));
    o.slot_bias = p::integer_of(v, "slot_bias", 0, 1024);
    o.ramp_layers = v.find("ramp_layers") != nullptr && p::boolean_of(v, "ramp_layers");
    o.page = static_cast<std::uint16_t>(opt(v, "page", 0, 0, 0xFFFF));
    return o;
}

std::vector<HoleStep::Act> parse_actions(const json::Value& v) {
    if (!v.is_array()) {
        p::fail(v, "an array of actions is expected");
    }
    std::vector<HoleStep::Act> out;
    for (const json::Value& a : v.array()) {
        HoleStep::Act act;
        if (a.is_string()) {
            if (a.string() == "dust_ring") {
                act.kind = HoleStep::Action::DustRing;
            } else if (a.string() == "blood") {
                act.kind = HoleStep::Action::Blood;
            } else {
                p::fail(a, "an action is \"dust_ring\", \"blood\" or {\"burst_point\"|\"crater\"|\"ring\": distance}");
            }
        } else if (a.is_object() && a.members().size() == 1) {
            const auto& [key, value] = *a.members().begin();
            if (key == "burst_point") {
                act.kind = HoleStep::Action::BurstPoint;
            } else if (key == "crater") {
                act.kind = HoleStep::Action::Crater;
            } else if (key == "ring") {
                act.kind = HoleStep::Action::Ring;
            } else {
                p::fail(a, "an action object is {\"burst_point\"|\"crater\"|\"ring\": distance}");
            }
            act.distance = p::integer(value, -kBig, kBig);
        } else {
            p::fail(a, "an action is a name or a one-key object");
        }
        out.push_back(act);
    }
    return out;
}

HoleTimeline parse_hole(const json::Value& v) {
    p::reject_unknown(v, {"name", "steps"});
    HoleTimeline o;
    const json::Value& name = p::member(v, "name");
    if (!name.is_string()) {
        p::fail(name, "\"name\" must be a string");
    }
    o.name = name.string();
    const json::Value& steps = p::member(v, "steps");
    if (!steps.is_array()) {
        p::fail(steps, "\"steps\" must be an array");
    }
    for (const json::Value& s : steps.array()) {
        p::reject_unknown(s, {"at", "do", "end", "each_frame"});
        HoleStep st;
        if (s.find("at") != nullptr) {
            st.has_at = true;
            st.at = p::integer_of(s, "at", -kBig, kBig);
        }
        if (const json::Value* d = s.find("do"); d != nullptr) {
            st.run = parse_actions(*d);
        }
        st.end = s.find("end") != nullptr && p::boolean_of(s, "end");
        if (const json::Value* e = s.find("each_frame"); e != nullptr) {
            p::reject_unknown(*e, {"chance_mask", "do"});
            st.has_each = true;
            st.each_mask = opt(*e, "chance_mask", 0, 0, 0x7FFF);
            st.each = parse_actions(p::member(*e, "do"));
        }
        o.steps.push_back(std::move(st));
    }
    return o;
}

GroundFxParams parse_one(const json::Value& v) {
    p::reject_unknown(v, {"grid_shift", "palette", "dust", "dust_ring", "blood", "marks", "craters", "rings",
                          "rings_yaw_offset", "grid_quads", "hole_count", "holes"});
    GroundFxParams o;
    o.grid_shift = p::integer_of(v, "grid_shift", 0, 30);
    o.palette = parse_palette(p::member(v, "palette"));
    o.dust = parse_dust(p::member(v, "dust"));
    o.dust_ring = parse_dust_ring(p::member(v, "dust_ring"));
    o.blood = parse_blood(p::member(v, "blood"));
    o.marks = parse_marks(p::member(v, "marks"));
    o.craters = parse_grid(p::member(v, "craters"));
    o.rings = parse_grid(p::member(v, "rings"));
    o.rings_yaw_offset = opt(v, "rings_yaw_offset", 0, -kBig, kBig);
    const json::Value& quads = p::member(v, "grid_quads");
    if (!quads.is_array() || quads.array().size() != 4) {
        p::fail(quads, "\"grid_quads\" must be 4 quads of 4 grid point indices");
    }
    for (std::size_t i = 0; i < 4; i++) {
        o.grid_quads[i] = bytes<4>(quads.array()[i]);
        for (std::uint8_t c : o.grid_quads[i]) {
            if (c > 8) {
                p::fail(quads.array()[i], "a grid point index is 0..8");
            }
        }
    }
    o.hole_count = p::integer_of(v, "hole_count", 1, 64);
    const json::Value& holes = p::member(v, "holes");
    if (!holes.is_array()) {
        p::fail(holes, "\"holes\" must be an array of timelines");
    }
    for (const json::Value& h : holes.array()) {
        o.holes.push_back(parse_hole(h));
    }
    return o;
}

}  // namespace

Result<std::map<std::string, GroundFxParams>> parse_ground_fx(std::string_view json_text) try {
    using R = Result<std::map<std::string, GroundFxParams>>;
    Result<json::Value> doc = json::parse(json_text);
    if (!doc) {
        return R::fail(doc.error());
    }
    const json::Value& root = doc.value();
    p::reject_unknown(root, {"sets"});
    const json::Value& sets = p::member(root, "sets");
    if (!sets.is_object()) {
        p::fail(sets, "\"sets\" must be an object of named effect sets");
    }
    std::map<std::string, GroundFxParams> out;
    for (const auto& [name, value] : sets.members()) {
        if (name.rfind("_", 0) == 0) {
            continue;
        }
        try {
            out.emplace(name, parse_one(value));
        } catch (const std::exception& e) {
            throw std::runtime_error("set \"" + name + "\": " + e.what());
        }
    }
    return R::ok(std::move(out));
} catch (const std::exception& e) {
    return Result<std::map<std::string, GroundFxParams>>::fail(e.what());
}

Result<std::map<std::string, GroundFxParams>> load_ground_fx(const std::filesystem::path& gamedata_dir,
                                                            const std::filesystem::path& mods_dir, std::string* source) {
    using R = Result<std::map<std::string, GroundFxParams>>;
    std::string where;
    Result<std::string> text =
        read_effect_file(gamedata_dir, mods_dir, std::filesystem::path("effects") / "ground_fx.json", &where);
    if (source != nullptr) {
        *source = where;
    }
    if (!text) {
        return R::fail(text.error());
    }
    R parsed = parse_ground_fx(text.value());
    if (!parsed) {
        return R::fail(where + ": " + parsed.error());
    }
    return parsed;
}

}  // namespace sh::effects
