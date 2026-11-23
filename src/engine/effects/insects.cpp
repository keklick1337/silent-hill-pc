// SPDX-License-Identifier: GPL-3.0-or-later
// The insects of the first map: behaviour, drawing and the flashlight's footprint.
// The order of the random draws is the order the original
// made them, so a map that starts from the same seed plays the same swarm.

#include "engine/effects/insects.h"

#include <algorithm>
#include <utility>

namespace sh::effects {

namespace {

// Q12 seconds of one frame at 30 fps: positions and speeds are per 30 fps frame.
constexpr std::int32_t kFrame30 = 136;

constexpr std::int32_t q12_mult(std::int32_t a, std::int32_t b) { return (a * b) >> 12; }
constexpr std::int16_t s16(std::int32_t v) { return static_cast<std::int16_t>(v); }
constexpr std::int32_t clamp(std::int32_t v, std::int32_t lo, std::int32_t hi) { return v < lo ? lo : (v > hi ? hi : v); }

// A random integer in [lo, hi] from the next draw.
std::int32_t draw(InsectHost& host, std::int32_t lo, std::int32_t hi) { return host.rand16() % (hi - lo + 1) + lo; }

std::int32_t square_sum(std::int32_t a, std::int32_t b) {
    // Wraps like the 32-bit arithmetic of the original rather than overflowing.
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) * static_cast<std::uint32_t>(a) +
                                     static_cast<std::uint32_t>(b) * static_cast<std::uint32_t>(b));
}

// A speed from a sine or cosine: the product is divided as an unsigned number (so a negative one
// comes out as a large positive that the 16-bit store wraps back), then shifted.
std::int16_t speed_from(std::int32_t trig, std::int32_t times, std::int32_t divisor) {
    return s16(static_cast<std::int32_t>(static_cast<std::uint32_t>(trig * times) / static_cast<std::uint32_t>(divisor)));
}

bool contains(const std::vector<std::int32_t>& list, std::int32_t v) {
    return std::find(list.begin(), list.end(), v) != list.end();
}

}  // namespace

InsectSwarms::InsectSwarms(InsectConfig config)
    : config_(std::move(config)),
      flies_(static_cast<std::size_t>(std::max(0, config_.fly_count))),
      crawlers_(static_cast<std::size_t>(std::max(0, config_.crawler_count))) {}

bool InsectSwarms::player_near(InsectHost& host) const {
    const Vec3 p = host.player();
    const std::int32_t limit = config_.active_distance;
    for (int a = 0; a < 2; a++) {
        const Vec3& at = config_.anchors[static_cast<std::size_t>(a)];
        if (std::abs(p.x - at.x) + std::abs(p.z - at.z) < limit) {
            return true;
        }
    }
    return false;
}

bool InsectSwarms::startled(InsectHost& host, int mode, const Vec3& at) const {
    if (mode == 1) {
        return true;
    }
    if (mode == 2) {
        const Vec3 p = host.player();
        const std::int32_t dx = (p.x - at.x) >> 4;
        const std::int32_t dz = (p.z - at.z) >> 4;
        return host.sqrt(square_sum(dx, dz)) < config_.startle_radius_q8;
    }
    return false;
}

int InsectSwarms::start(InsectHost& host) {
    host.start_weather();

    const std::int32_t groups = std::max(1, config_.crawlers_per_group);
    for (std::size_t i = 0; i < crawlers_.size(); i++) {
        const Vec3& a = config_.anchors[static_cast<std::size_t>(std::min<std::int32_t>(
            static_cast<std::int32_t>(i) / groups + 1, 3))];
        Crawler& c = crawlers_[i];
        c.pos.x = a.x + static_cast<std::int32_t>(static_cast<std::uint32_t>(host.rand16()) %
                                                   static_cast<std::uint32_t>(2 * config_.crawler_spread)) -
                  config_.crawler_spread;
        c.pos.z = a.z + static_cast<std::int32_t>(static_cast<std::uint32_t>(host.rand16()) %
                                                   static_cast<std::uint32_t>(2 * config_.crawler_spread)) -
                  config_.crawler_spread;
        c.pos.y = a.y;
        c.mode = 1;
        c.timer = 0;
        c.vx = 0;
        c.vz = 0;
    }

    const Vec3& carcass = config_.anchors[0];
    for (Fly& f : flies_) {
        f.pos.x = carcass.x + draw(host, -config_.fly_spread, config_.fly_spread - 1);
        // A negative draw taken modulo the height: the remainder keeps the sign, so the fly
        // starts above the anchor (up is negative).
        f.pos.y = carcass.y + (-host.rand16()) % config_.fly_height;
        f.pos.z = carcass.z + draw(host, -config_.fly_spread, config_.fly_spread - 1);
        f.mode = 1;
        f.vx = 0;
        f.vz = 0;
    }
    return 2;
}

void InsectSwarms::update(InsectHost& host, int mode) {
    if (mode == 0) {
        return;
    }
    if (mode == 2 && !player_near(host)) {
        return;
    }

    measure_footprint(host);
    host.begin_view();

    for (std::size_t i = 0; i < crawlers_.size(); i++) {
        Crawler& c = crawlers_[i];
        const bool scared = startled(host, mode, c.pos);
        step_crawler(host, static_cast<int>(i), scared, c);
        draw_crawler(host, static_cast<int>(i), c);
    }

    for (std::size_t i = 0; i < flies_.size(); i++) {
        const bool dart = (host.rand16() % config_.dart_chance) >> 6 != 0;
        step_fly(host, static_cast<int>(i), dart, flies_[i]);
        draw_fly(host, static_cast<int>(i), flies_[i]);
    }
}

void InsectSwarms::step_fly(InsectHost& host, int index, bool dart, Fly& f) {
    if (host.paused()) {
        return;
    }

    const std::int32_t dt = host.delta_time();
    f.pos.x += (f.vx * dt) / kFrame30;
    f.pos.y += (f.vy * dt) / kFrame30;
    f.pos.z += (f.vz * dt) / kFrame30;

    f.vx = s16(clamp(f.vx, -0x100, 0xFF));
    f.vy = s16(clamp(f.vy, -0xFF, 0xFF));
    f.vz = s16(clamp(f.vz, -0xFF, 0xFF));

    // How a fly picks its next mode: once the timer passes a random threshold it picks mode
    // 1..3 afresh; a dart (the caller's chance) sends it to mode 5 at once.
    const auto retarget = [&](std::int32_t lo, std::int32_t hi) {
        if (draw(host, lo, hi) < f.timer) {
            f.mode = static_cast<std::uint8_t>(draw(host, 1, 3));
            f.timer = 0;
        } else {
            f.timer++;
        }
        if (dart) {
            f.mode = 5;
            f.timer = 0;
        }
    };

    switch (f.mode) {
        case 1:
        case 2: {
            f.vy = s16(f.vy + draw(host, -4, 4));
            f.vy = s16(clamp(f.vy, -16, 16));
            const std::int32_t zshift = f.mode == 1 ? 5 : 6;
            f.vx = s16(f.vx + q12_mult(host.sin((f.timer + index) << 5), 40));
            f.vz = s16(f.vz + q12_mult(host.cos((f.timer + index) << zshift), 40));
            retarget(128, f.mode == 1 ? 159 : 191);
            break;
        }

        case 3: {
            f.vy = s16(f.vy + draw(host, -4, 4));
            f.vy = s16(clamp(f.vy, -16, 16));
            if (f.timer == 0) {
                const std::int16_t angle = s16(draw(host, 0, 4095));
                f.vx = speed_from(host.sin(angle), 5, 1 << 7);
                f.vz = speed_from(host.sin(angle), 5, 1 << 7);
            }
            retarget(100, 227);
            break;
        }

        case 5: {
            Vec3 to;  // Q8
            if (index < draw(host, 10, 19) || !host.hand_follows()) {
                const Vec3& home = config_.anchors[0];
                to = Vec3{(home.x - f.pos.x) >> 4, (home.y - f.pos.y) >> 4, (home.z - f.pos.z) >> 4};
            } else {
                const Vec3 hand = host.hand_q8();
                to = Vec3{hand.x - (f.pos.x >> 4), hand.y - (f.pos.y >> 4), hand.z - (f.pos.z >> 4)};
            }

            const std::int32_t flat = host.sqrt(square_sum(to.x, to.z));
            const std::int16_t yaw = s16(host.atan2(to.x, to.z));
            const std::int16_t pitch = s16(host.atan2(to.y, flat));

            // A pull toward the target; the half-step less than 0.02 m is the original's.
            f.vx = s16(f.vx + q12_mult(host.sin(yaw), 80));
            f.vz = s16(f.vz + q12_mult(host.cos(yaw), 80));
            f.vy = s16(f.vy + q12_mult(host.sin(pitch), 40));

            if (draw(host, 80, 143) < f.timer || (dart && f.timer > 80)) {
                f.timer = 0;
                f.mode = static_cast<std::uint8_t>(draw(host, 1, 3));
            } else {
                f.timer++;
            }
            break;
        }

        default:
            break;
    }
}

void InsectSwarms::step_crawler(InsectHost& host, int index, bool scared, Crawler& c) {
    if (host.paused()) {
        return;
    }

    const std::int32_t dt = host.delta_time();
    c.pos.x += (c.vx * dt) / kFrame30;
    c.pos.z += (c.vz * dt) / kFrame30;

    const auto jitter = [&]() {
        c.vx = s16(c.vx + draw(host, -4, 4));
        c.vz = s16(c.vz + draw(host, -4, 4));
    };
    const auto flee = [&]() {
        c.mode = 5;
        c.timer = 0;
    };
    const auto pick_rest_or_idle = [&]() { return static_cast<std::uint8_t>(draw(host, 0, 1) * 3 + 1); };

    switch (c.mode) {
        case 1:
            c.timer++;
            if (scared) {
                if (draw(host, 0, 1) != 0) {
                    flee();
                } else {
                    c.mode = 6;
                }
            }
            if (c.timer > draw(host, 100, 227)) {
                c.mode = pick_rest_or_idle();
                c.timer = 0;
            }
            jitter();
            break;

        case 2:
            c.timer++;
            if (scared) {
                flee();
            }
            jitter();
            if (c.timer > draw(host, 100, 227)) {
                c.mode = static_cast<std::uint8_t>(draw(host, 3, 4));
                c.timer = 0;
                const std::int16_t heading = s16(draw(host, 0, 0xFFF));
                c.vx = speed_from(host.sin(heading), 3, 0x100);
                c.vz = speed_from(host.cos(heading), 3, 0x100);
            }
            break;

        case 3:
            c.timer++;
            if (c.timer > draw(host, 100, 227)) {
                c.mode = 2;
                c.vz = 0;
                c.vx = 0;
                c.timer = 0;
            }
            if (scared) {
                flee();
            }
            break;

        case 4: {
            c.timer++;
            const std::int32_t groups = std::max(1, config_.crawlers_per_group);
            const Vec3& home = config_.anchors[static_cast<std::size_t>(std::min(index / groups + 1, 3))];
            const std::int16_t heading = s16(host.atan2(home.x - c.pos.x, home.z - c.pos.z));
            c.vx = speed_from(host.sin(heading), 3, 0x100);
            c.vz = speed_from(host.cos(heading), 3, 0x100);
            if (scared) {
                c.mode = static_cast<std::uint8_t>(draw(host, 5, 6));
                c.timer = 0;
            }
            if (c.timer > 192) {
                c.mode = static_cast<std::uint8_t>(draw(host, 1, 2));
                c.vz = 0;
                c.vx = 0;
                c.timer = 0;
            }
            break;
        }

        case 5:
            if (c.timer == 0) {
                // Away from Harry, at the speed of a run.
                const Vec3 p = host.player();
                const std::int32_t dx = (c.pos.x - p.x) >> 4;
                const std::int32_t dz = (c.pos.z - p.z) >> 4;
                const std::int16_t heading = s16(host.atan2(dx, dz));
                c.vx = speed_from(host.sin(heading), 3, 0x10);
                c.vz = speed_from(host.cos(heading), 3, 0x10);
            }
            if (scared) {
                c.timer = 0;
            }
            if (c.timer > 16) {
                c.mode = 2;
                c.timer = 0;
            }
            c.timer++;
            break;

        case 6:
            c.timer++;
            if (c.timer > draw(host, 20, 21)) {
                if (scared) {
                    c.mode = static_cast<std::uint8_t>(draw(host, 5, 6));
                    c.timer = 0;
                } else {
                    c.mode = pick_rest_or_idle();
                }
            }
            jitter();
            break;

        default:
            break;
    }

    const std::int32_t limit = c.mode == 5 ? 64 : 20;
    c.vx = s16(clamp(c.vx, -limit, limit));
    c.vz = s16(clamp(c.vz, -limit, limit));
}

EffectPrim InsectSwarms::sprite(InsectHost& host, const InsectSheet& sheet, int index) const {
    // Every insect starts on its own cell; the whole pool steps to the next cell every
    // `cell_frames` frames unless the map holds the particles still.
    const std::int32_t cells = std::max(1, sheet.cells);
    std::int32_t phase = 0;
    if (!host.paused()) {
        const std::uint32_t period = static_cast<std::uint32_t>(cells * std::max(1, sheet.cell_frames));
        phase = static_cast<std::int32_t>(static_cast<std::uint32_t>(host.frame_counter()) % period) /
                std::max(1, sheet.cell_frames);
    }
    const std::int32_t cell = (index % cells + phase) % cells;
    const auto u0 = static_cast<std::uint8_t>(sheet.u_base + cell * sheet.cell_size);
    const auto u1 = static_cast<std::uint8_t>(sheet.u_base + (cell + 1) * sheet.cell_size);

    EffectPrim p;
    p.kind = EffectPrim::Kind::Quad;
    p.texture = sheet.texture;
    p.u = {u0, u0, u1, u1};
    p.v = {static_cast<std::uint8_t>(sheet.v_top), static_cast<std::uint8_t>(sheet.v_bottom),
           static_cast<std::uint8_t>(sheet.v_top), static_cast<std::uint8_t>(sheet.v_bottom)};
    return p;
}

void InsectSwarms::draw_fly(InsectHost& host, int index, const Fly& f) const {
    const InsectLook& look = config_.look;
    EffectPrim p = sprite(host, look.flies, index);

    // The colour: the map's light colour plus what the flashlight and the darkness at the
    // fly's place add, as the light mode says.
    const Rgb light = host.light_color();
    const auto lit = [&](std::int32_t add) {
        return Rgb{static_cast<std::uint8_t>(add + light.r), static_cast<std::uint8_t>(add + light.g),
                   static_cast<std::uint8_t>(add + light.b)};
    };
    const auto darkness_here = [&]() {
        // The original measures at the fly's offset added to Harry's place, not at the fly.
        const Vec3 harry = host.player();
        const std::int32_t d = host.darkness(Vec3{harry.x + f.pos.x, harry.y + f.pos.y, harry.z + f.pos.z});
        return static_cast<std::uint8_t>(d != 0 ? (d <= look.darkness_cap ? d : look.darkness_cap) : 1);
    };
    const std::int32_t mode = host.light_mode();
    if (host.flashlight_on()) {
        if (contains(look.full_darkness_modes, mode)) {
            p.rgb[0] = lit(darkness_here());
        } else if (contains(look.plain_modes, mode)) {
            p.rgb[0] = light;
        } else {
            p.rgb[0] = lit(darkness_here() >> 3);
        }
    } else if (contains(look.full_darkness_modes, mode)) {
        p.rgb[0] = lit(darkness_here());
    } else {
        p.rgb[0] = light;
    }

    const ScreenPoint at = host.project(relative_q8(f.pos, Vec3{}));
    const std::int32_t slot = at.depth >> 1;
    if (slot <= look.flies.min_slot || slot >= look.ordering_table_size / 2 - 1) {
        return;
    }

    const std::int32_t steps = at.depth >> 4;
    std::int32_t size = steps >= look.size_far_steps ? look.size_min : look.size_max - steps;
    size = clamp(size, look.size_min, look.size_max);

    p.x = {at.x, at.x, s16(at.x + size), s16(at.x + size)};
    p.y = {at.y, s16(at.y + size), at.y, s16(at.y + size)};
    p.slot = slot;
    p.view_depth = at.depth << 2;
    host.emit(p);
}

void InsectSwarms::draw_crawler(InsectHost& host, int index, const Crawler& c) const {
    const InsectLook& look = config_.look;
    EffectPrim p = sprite(host, look.crawlers, index);
    p.semi_transparent = true;
    p.rgb[0] = Rgb{look.crawler_brightness, look.crawler_brightness, look.crawler_brightness};

    const ScreenPoint at = host.project(relative_q8(c.pos, Vec3{}));
    const std::int32_t slot = at.depth >> 1;
    if (slot <= look.crawlers.min_slot || slot >= look.ordering_table_size / 2 - 1) {
        return;
    }

    const std::int32_t steps = at.depth >> 4;
    std::int32_t size = steps >= look.size_far_steps ? look.size_min : look.size_max - steps;
    size = clamp(size, look.size_min, look.size_max);

    p.x = {at.x, at.x, s16(at.x + size), s16(at.x + size)};
    p.y = {at.y, s16(at.y + size), at.y, s16(at.y + size)};
    p.slot = slot;
    p.view_depth = at.depth << 2;
    host.emit(p);
}

// The flashlight's cone is a 12 point prism (two hexagons, the near one small and the far one
// wide); its edges cut the ground plane in up to 12 points whose outline is the lit patch.
void InsectSwarms::measure_footprint(InsectHost& host) {
    const std::int32_t size = host.cone_size();
    const std::int32_t near_depth = size >> 6;
    const std::int32_t far_depth = size >> 3;
    const std::int32_t near_radius = size / 20;
    const std::int32_t far_radius = size / 12;

    Vec3s corners[12];
    for (int i = 0; i < 6; i++) {
        const std::int32_t angle = (i * 245760) / 360;  // i * 60 degrees
        const std::int32_t s = host.sin(angle);
        const std::int32_t c = host.cos(angle);
        corners[i] = Vec3s{s16(q12_mult(near_radius, s)), s16(q12_mult(near_radius, c)), s16(-near_depth)};
        corners[i + 6] = Vec3s{s16(q12_mult(far_radius, s)), s16(q12_mult(far_radius, c)), s16(far_depth)};
    }

    static constexpr int kEdges[18][2] = {{0, 6}, {1, 7}, {2, 8},  {3, 9},  {4, 10},  {5, 11},
                                          {0, 1}, {1, 2}, {2, 3},  {3, 4},  {4, 5},   {5, 0},
                                          {6, 7}, {7, 8}, {8, 9},  {9, 10}, {10, 11}, {11, 6}};

    host.cone_begin();
    const Vec3 origin = host.cone_origin();
    const Vec3 player = host.player();
    const std::int32_t shift_x = (origin.x - player.x) >> 4;
    const std::int32_t shift_y = (origin.y - player.y) >> 4;
    const std::int32_t shift_z = (origin.z - player.z) >> 4;
    const auto placed = [&](const Vec3s& corner) {
        Vec3s r = host.cone_rotate(corner);
        r.x = s16(r.x + shift_x);
        r.y = s16(r.y + shift_y);
        r.z = s16(r.z + shift_z);
        return r;
    };

    ConeCorner cuts[18] = {};
    int count = 0;
    for (const auto& edge : kEdges) {
        const Vec3s a = placed(corners[edge[0]]);
        const Vec3s b = placed(corners[edge[1]]);
        if ((a.y >= 0 && b.y < 0) || (a.y < 0 && b.y >= 0)) {
            cuts[count].x = s16(a.x - (a.y * (b.x - a.x)) / (b.y - a.y));
            cuts[count].y = s16(a.z - (a.y * (b.z - a.z)) / (b.y - a.y));
            count++;
        }
    }

    if (count == 0) {
        return;
    }

    // Sorted by x with the original's exchange sort (the outline below depends on the order of
    // equal points).
    for (int i = 0; i < count - 1; i++) {
        for (int j = i + 1; j < count; j++) {
            if (cuts[i].x > cuts[j].x) {
                std::swap(cuts[i], cuts[j]);
            }
        }
    }
    footprint_corners_ = convex_outline(cuts, count);
}

std::int32_t convex_outline(const ConeCorner* points, int n) {
    struct Node {
        ConeCorner p;
        std::uint8_t prev = 0;
        std::uint8_t next = 0;
    };
    constexpr int kMax = 18;
    ConeCorner pts[kMax] = {};
    n = std::min(n, kMax);
    for (int i = 0; i < n; i++) {
        pts[i] = points[i];
    }
    Node ring[kMax];

    for (int j = 0; j < 3; j++) {
        ring[j].p = pts[j];
    }
    // The first three points in counter-clockwise order.
    if ((ring[1].p.x - ring[0].p.x) * (ring[2].p.y - ring[0].p.y) <=
        (ring[1].p.y - ring[0].p.y) * (ring[2].p.x - ring[0].p.x)) {
        std::swap(ring[1].p, ring[2].p);
    }
    for (int j = 0; j < 3; j++) {
        ring[j].prev = static_cast<std::uint8_t>(j - 1);
        ring[j].next = static_cast<std::uint8_t>(j + 1);
    }
    ring[0].prev = 2;
    ring[2].next = 0;

    const auto at = [&](int i) -> Node& { return ring[std::min<int>(i, kMax - 1)]; };

    for (int i = 3; i < n; i++) {
        const ConeCorner q = pts[i];

        // The corner from which the new point leaves the outline, and the one it returns to.
        int j = 0;
        for (int guard = 0; guard < kMax; guard++) {
            const Node& a = at(j);
            const Node& b = at(a.next);
            if (!((b.p.x - a.p.x) * (q.y - a.p.y) > (b.p.y - a.p.y) * (q.x - a.p.x))) {
                break;
            }
            j = a.next;
        }
        const int leave = j;

        j = 0;
        for (int guard = 0; guard < kMax; guard++) {
            const Node& a = at(j);
            const Node& b = at(a.prev);
            if (!((a.p.x - b.p.x) * (q.y - b.p.y) > (a.p.y - b.p.y) * (q.x - b.p.x))) {
                break;
            }
            j = a.next;
        }
        const int enter = j;

        at(leave).next = static_cast<std::uint8_t>(i);
        at(enter).prev = static_cast<std::uint8_t>(i);
        ring[i].next = static_cast<std::uint8_t>(enter);
        ring[i].prev = static_cast<std::uint8_t>(leave);
        ring[i].p = q;
    }

    std::int32_t corners = 1;
    int i = 0;
    for (int guard = 0; ring[i].next != 0 && guard < kMax; guard++) {
        i = std::min<int>(ring[i].next, kMax - 1);
        corners++;
    }
    return corners;
}

}  // namespace sh::effects
