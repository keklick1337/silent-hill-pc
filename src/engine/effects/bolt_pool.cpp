// SPDX-License-Identifier: GPL-3.0-or-later
// Bolt pools: lightning laid as a chain of segments, grown, drawn as ribbons (bolt_pool.h).

#include "engine/effects/bolt_pool.h"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

#include "engine/effects/effects_file.h"
#include "formats/common/json.h"

namespace sh::effects {

namespace {

// The game's integer arithmetic wraps on overflow; so does this.
std::int32_t wmul(std::int32_t a, std::int32_t b) {
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) * static_cast<std::uint32_t>(b));
}

std::int32_t wadd(std::int32_t a, std::int32_t b) {
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) + static_cast<std::uint32_t>(b));
}

std::int32_t wsub(std::int32_t a, std::int32_t b) {
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) - static_cast<std::uint32_t>(b));
}

std::int32_t shl(std::int32_t v, int s) {
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(v) << s);
}

std::int32_t mul_q12(std::int32_t a, std::int32_t b) {
    return wmul(a, b) >> 12;
}

std::int32_t mul_q12_wide(std::int32_t a, std::int32_t b) {
    return static_cast<std::int32_t>((static_cast<std::int64_t>(a) * b) >> 12);
}

std::int32_t div_q12(std::int32_t a, std::int32_t b) {
    return b != 0 ? shl(a, 12) / b : 0;
}

// Signed angle in -2048..2047.
std::int32_t wrap_signed(std::int32_t a) {
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) << 20) >> 20;
}

// Signed angle as a 16-bit value: above half a turn wraps negative, half a turn stays positive.
std::int32_t wrap_half(std::int32_t a) {
    const std::int32_t n = static_cast<std::int16_t>(a) & 0xFFF;
    return n > 2048 ? n - 4096 : n;
}

std::int32_t abs32(std::int32_t v) {
    return v < 0 ? -v : v;
}

// True when b is further than `max` from a (Q12).
bool farther(BoltPoolHost& host, const Vec3& a, const Vec3& b, std::int32_t max) {
    const std::int32_t dx = wsub(b.x, a.x), dy = wsub(b.y, a.y), dz = wsub(b.z, a.z);
    const std::int32_t sum = wadd(wadd(mul_q12(dx, dx), mul_q12(dz, dz)), mul_q12(dy, dy));
    return max < abs32(host.sqrt12(sum));
}

// (r * v) >> 12 of a rotation and a vector.
Vec3 rotate(const BoltSegment& s, const std::int32_t (&v)[3]) {
    std::int32_t out[3];
    for (int r = 0; r < 3; r++) {
        const std::int64_t acc = static_cast<std::int64_t>(s.m[r][0]) * v[0] + static_cast<std::int64_t>(s.m[r][1]) * v[1] +
                                 static_cast<std::int64_t>(s.m[r][2]) * v[2];
        out[r] = static_cast<std::int32_t>(acc >> 12);
    }
    return Vec3{out[0], out[1], out[2]};
}

std::int16_t clamp16(std::int64_t v) {
    return static_cast<std::int16_t>(std::clamp<std::int64_t>(v, -32768, 32767));
}

}  // namespace

BoltPool::BoltPool(BoltPoolParams params) : params_(std::move(params)) {
    restore_sounds();
}

void BoltPool::restore_sounds() {
    gap_.assign(params_.sounds.size(), 0);
    for (std::size_t i = 0; i < params_.sounds.size(); i++) {
        gap_[i] = params_.sounds[i].first_gap;
    }
    if (elapsed_.size() != params_.sounds.size()) {
        elapsed_.assign(params_.sounds.size(), 0);
    }
}

bool BoltPool::attach(BoltPoolHost& host, std::string_view owner, std::int32_t x, std::int32_t z, BoltAnchor bones) {
    int found = -1;
    for (std::size_t i = 0; i < params_.owners.size(); i++) {
        if (params_.owners[i].name == owner) {
            found = static_cast<int>(i);
        }
    }
    if (found < 0) {
        return false;
    }
    constexpr std::int32_t kGrid = 64 * 4096;
    origin_x_ = (x / kGrid) * kGrid;
    origin_z_ = (z / kGrid) * kGrid;
    if (bones != 0) {
        bones_ = bones;
    }
    prev_player_ = host.player_position();
    owner_ = found;
    if (bolts_.empty()) {
        bolts_.resize(static_cast<std::size_t>(params_.capacity));
        for (Bolt& b : bolts_) {
            const auto n = static_cast<std::size_t>(params_.segments);
            b.color.assign(n, 0);
            b.twist.assign(n, 0);
            b.seg.assign(n, BoltSegment{});
        }
    }
    for (Bolt& b : bolts_) {
        b.active = false;
    }
    std::fill(elapsed_.begin(), elapsed_.end(), params_.elapsed_after_reset);
    return true;
}

void BoltPool::track(BoltPoolHost& host) {
    const std::int32_t dt = host.delta_time();
    if (dt == 0) {
        lead_x_ = 0;
        lead_z_ = 0;
        return;
    }
    const std::int32_t gain = host.easy() ? params_.lead.gain_easy : params_.lead.gain;
    const Vec3 now = host.player_position();
    lead_x_ = wmul(wsub(now.x, prev_player_.x), gain) / dt;
    lead_z_ = wmul(wsub(now.z, prev_player_.z), gain) / dt;
    prev_player_ = now;
}

Bolt* BoltPool::acquire() {
    for (Bolt& b : bolts_) {
        if (!b.active) {
            b.active = true;
            b.hit = false;
            b.tail = 0;
            b.state = 0;
            b.burst_done = false;
            return &b;
        }
    }
    return nullptr;
}

void BoltPool::sound(BoltPoolHost& host, int slot, const Vec3& at) {
    if (slot < 0 || static_cast<std::size_t>(slot) >= params_.sounds.size()) {
        return;
    }
    const auto i = static_cast<std::size_t>(slot);
    const BoltSound& s = params_.sounds[i];
    if (gap_[i] < elapsed_[i]) {
        const std::int32_t base = s.interval;
        const std::int32_t div = (0x7FFF / (base >> 3)) + 1;
        gap_[i] = static_cast<std::uint16_t>(base + host.rand16() / div);
        elapsed_[i] = 0;
        host.play_sound(s.id, at, s.volume);
    }
}

bool BoltPool::spawn(BoltPoolHost& host, std::string_view spawner, const Vec3& from, const Vec3& to, int bone,
                     std::int32_t step, std::int32_t life) {
    auto it = params_.spawner_names.find(std::string(spawner));
    if (it == params_.spawner_names.end() || bolts_.empty()) {
        return false;
    }
    return throw_bolt(host, it->second, from, to, bone, step, life);
}

bool BoltPool::throw_bolt(BoltPoolHost& host, int spawner, const Vec3& from, const Vec3& to, int bone, std::int32_t step,
                          std::int32_t life) {
    const BoltSpawner& sp = params_.spawners[static_cast<std::size_t>(spawner)];
    Vec3 target = to;
    if (sp.aim_at_player) {
        const auto chest = host.player_chest();
        target = Vec3{wadd(shl(chest[0], 4), to.x), wadd(shl(chest[1], 4), to.y), wadd(shl(chest[2], 4), to.z)};
    }
    Bolt* b = acquire();
    if (b == nullptr) {
        return false;
    }
    Vec3 start = from;
    if (sp.scatter != 0) {
        const auto ax = static_cast<std::int16_t>(host.rand16());
        const auto ay = static_cast<std::int16_t>(host.rand16());
        const Vec3 d = host.turn_xyz(ax, ay, static_cast<std::int16_t>(sp.scatter));
        start = Vec3{wadd(from.x, d.x), wadd(from.y, d.y), wadd(from.z, d.z)};
        target = from;
    }
    b->spawner = spawner;
    b->step = sp.step != 0 ? sp.step : step;
    b->life = sp.life != 0 ? sp.life : life;
    b->timer = b->life;
    b->bone = bone;
    b->turn = sp.turn;
    lay(host, *b, start, target);

    const std::vector<int>* sounds = &sp.sounds;
    const std::string& who = params_.owners[static_cast<std::size_t>(owner_)].name;
    if (auto o = sp.owner_sounds.find(who); o != sp.owner_sounds.end()) {
        sounds = &o->second;
    }
    if (sp.strong_step >= 0 && b->step > sp.strong_step) {
        sounds = &sp.strong_sounds;
    }
    for (int s : *sounds) {
        sound(host, s, from);
    }
    return true;
}

// ---------------------------------------------------------------------------------------------
// Laying a bolt

namespace {

// The walker's states; a path starts at one of them.
enum State : int {
    kStart = 0,
    kWander = 1,
    kWaypointPick = 2,
    kWaypointGo = 3,
    kDropAim = 4,
    kDrop = 5,
    kDriftPick = 6,
    kDrift = 7,
    kLobAim = 8,
    kLobGo = 9,
    kDiveAim = 10,
    kDiveGo = 11,
    kLandAim = 12,
    kLand = 13,
    kRoam = 14,
    kRoamWait = 15,
    kHomeAim = 16,
    kHome = 17,
    kSeek = 18,
    kSweep = 19,
};

}  // namespace

// One step of the walker: moves the aim as its path says; false when this is the last segment.
bool BoltPool::walk(BoltPoolHost& host, Bolt& b, Vec3& cur, Vec3& aim, const Vec3& target) {
    const auto& w = params_.walk;
    if (b.state == kStart) {
        b.counter = 0;
        switch (params_.spawners[static_cast<std::size_t>(b.spawner)].path) {
            case BoltPath::Seek: b.state = kSeek; break;
            case BoltPath::Sweep: b.state = kSweep; break;
            case BoltPath::Lob: b.state = kLobAim; break;
            case BoltPath::Waypoint: b.state = kWaypointPick; break;
            case BoltPath::Drift: b.state = kDriftPick; break;
            case BoltPath::Roam: b.state = kRoam; break;
            case BoltPath::Wander:
                b.turn = w.wander_turn;
                b.state = kWander;
                break;
        }
    }
    for (;;) {
        switch (b.state) {
            case kWander:
                if ((host.rand16() & w.wander_mask) == 0) {
                    b.step = w.wander_step;
                    b.state = kWaypointPick;
                    b.counter = 0;
                } else if ((host.rand16() & w.wander_mask) == 0) {
                    b.state = kRoam;
                    b.counter = 0;
                } else if (cur.y <= w.wander_low) {
                    b.state = kLobAim;
                    b.counter = 0;
                } else if ((host.rand16() & w.wander_drift_mask) == 0) {
                    b.state = kDriftPick;
                    b.counter = 0;
                }
                break;

            case kWaypointPick:
                aim = params_.waypoints[static_cast<std::size_t>((host.rand16() >> 4) % w.waypoint_count)];
                b.state = kWaypointGo;
                [[fallthrough]];
            case kWaypointGo:
                if (farther(host, cur, aim, w.waypoint_reach)) {
                    b.counter++;
                } else {
                    b.counter = 0;
                    b.state = kDropAim;
                }
                break;

            case kDropAim:
                aim.y = 0;
                b.state = kDrop;
                [[fallthrough]];
            case kDrop:
                if (cur.y < 0) {
                    b.counter++;
                    break;
                }
                b.counter = 0;
                return false;

            case kDriftPick: {
                const std::int32_t a = host.rand16();
                aim.x = host.sin(a);
                aim.y = 0;
                aim.z = host.cos(a);
                b.state = kDrift;
            }
                [[fallthrough]];
            case kDrift:
                if (b.counter >= w.drift_steps) {
                    b.counter = 0;
                    return false;
                }
                b.counter++;
                break;

            case kLobAim: {
                std::int32_t dx = wsub(target.x, cur.x);
                std::int32_t dy = wsub(target.y, cur.y);
                std::int32_t dz = wsub(target.z, cur.z);
                const std::int32_t dist = host.sqrt12(wadd(wadd(mul_q12(dx, dx), mul_q12(dz, dz)), mul_q12(dy, dy)));
                dx = wsub(target.x, cur.x);
                dy = w.lob_height - dist / w.lob_reach_divisor;
                dz = wsub(target.z, cur.z);
                aim.x = wadd(wadd(cur.x, dx / 2) - w.lob_spread, host.rand16() >> w.lob_jitter_shift);
                aim.y = wadd(dy - w.lob_spread, host.rand16() >> w.lob_jitter_shift);
                aim.z = wadd(wadd(cur.z, dz / 2) - w.lob_spread, host.rand16() >> w.lob_jitter_shift);
                b.state = kLobGo;
            }
                [[fallthrough]];
            case kLobGo:
                if (farther(host, cur, aim, b.step)) {
                    b.counter++;
                    break;
                }
                b.state = kDiveAim;
                b.counter = 0;
                break;

            case kDiveAim: {
                const std::int32_t dx = wsub(target.x, cur.x);
                const std::int32_t dz = wsub(target.z, cur.z);
                aim.x = wadd(target.x - w.dive_spread, host.rand16() >> w.dive_jitter_shift) - dx / w.dive_lead_divisor;
                aim.y = (host.rand16() >> w.dive_jitter_shift) - w.dive_height;
                aim.z = wadd(target.z - w.dive_spread, host.rand16() >> w.dive_jitter_shift) - dz / w.dive_lead_divisor;
                b.state = kDiveGo;
            }
                [[fallthrough]];
            case kDiveGo:
                if (farther(host, cur, aim, mul_q12_wide(b.step, w.dive_reach))) {
                    b.counter++;
                    break;
                }
                b.state = kLandAim;
                b.counter = 0;
                break;

            case kLandAim:
                aim.x = wadd(target.x - w.dive_spread, host.rand16() >> w.dive_jitter_shift);
                aim.y = 0;
                aim.z = wadd(target.z - w.dive_spread, host.rand16() >> w.dive_jitter_shift);
                // The original goes back to the dive's test on the next segment.
                b.state = kDiveGo;
                [[fallthrough]];
            case kLand:
                if (!farther(host, cur, aim, b.step)) {
                    b.counter = 0;
                    return false;
                }
                b.counter++;
                break;

            case kRoam: {
                const auto ax = static_cast<std::int16_t>(host.rand16());
                const auto ay = static_cast<std::int16_t>(host.rand16());
                const Vec3 d = host.turn_xyz(ax, ay, static_cast<std::int16_t>(w.roam_length));
                aim = Vec3{wadd(cur.x, d.x), wadd(cur.y, d.y), wadd(cur.z, d.z)};
            }
                [[fallthrough]];
            case kRoamWait:
                if (b.counter == 0) {
                    b.counter = 1;
                    break;
                }
                if (host.rand16() & w.roam_mask) {
                    break;
                }
                b.counter = 0;
                b.state = kHomeAim;
                break;

            case kHomeAim:
                aim = params_.waypoints[static_cast<std::size_t>(w.home)];
                b.state = kHome;
                [[fallthrough]];
            case kHome:
                if (farther(host, cur, aim, b.step)) {
                    b.counter++;
                    break;
                }
                b.counter = 0;
                return false;

            case kSeek:
                if (!farther(host, cur, aim, b.step / 2)) {
                    b.counter = 0;
                    return false;
                }
                b.counter++;
                break;

            case kSweep:
                if (b.counter >= w.sweep_steps) {
                    b.counter = 0;
                    return false;
                }
                b.counter++;
                break;

            default:
                b.counter++;
                break;
        }
        if (b.counter != 0) {
            return true;
        }
    }
}

// The rotation of a segment from cur toward aim: a yaw, then a pitch; after the first segment each
// is turned from the previous segment's by at most `turn` (the pitch by half of it).
void BoltPool::orient(BoltPoolHost& host, BoltSegment& out, const Vec3& cur, const Vec3& aim, const BoltSegment* prev,
                      std::int32_t turn) {
    std::int32_t ys = 0, yc = 4096, ps = 0, pc = 4096;
    if (prev != nullptr) {
        ys = -prev->m[2][0];
        yc = prev->m[0][0];
        ps = -prev->m[1][2];
        pc = prev->m[1][1];
    }
    const std::int32_t dx = wsub(aim.x, cur.x);
    const std::int32_t dy = wsub(aim.y, cur.y);
    const std::int32_t dz = wsub(aim.z, cur.z);
    const std::int32_t xx = mul_q12(dx, dx);
    const std::int32_t zz = mul_q12(dz, dz);
    const std::int32_t yy = mul_q12(dy, dy);
    const std::int32_t flat = host.sqrt12(wadd(xx, zz));

    std::int32_t sy, cy;
    if (flat != 0) {
        sy = div_q12(dx, flat);
        cy = div_q12(dz, flat);
    } else {
        sy = ys;
        cy = yc;
    }
    if (prev != nullptr) {
        std::int32_t base = host.atan2(ys, yc);
        const std::int32_t delta = wrap_signed(host.atan2(sy, cy) - base);
        if (turn < abs32(delta)) {
            base = delta <= 0 ? base - turn : base + turn;
            sy = host.sin(base);
            cy = host.cos(base);
        }
    }

    const std::int32_t dist = host.sqrt12(wadd(wadd(yy, xx), zz));
    std::int32_t sp, cp;
    if (dist != 0) {
        sp = -shl(dy, 12) / dist;
        cp = div_q12(flat, dist);
    } else {
        sp = ps;
        cp = pc;
    }
    turn /= 2;
    if (prev != nullptr) {
        std::int32_t base = host.atan2(ps, pc);
        const std::int32_t delta = wrap_signed(host.atan2(sp, cp) - base);
        if (turn < abs32(delta)) {
            base = delta <= 0 ? base - turn : base + turn;
            sp = host.sin(base);
            cp = host.cos(base);
        }
    }

    // yaw (about y) times pitch (about x), each element (sum of products) >> 12 kept in 16 bits.
    const std::int16_t yaw[3][3] = {{static_cast<std::int16_t>(cy), 0, static_cast<std::int16_t>(sy)},
                                    {0, 4096, 0},
                                    {static_cast<std::int16_t>(-sy), 0, static_cast<std::int16_t>(cy)}};
    const std::int16_t pitch[3][3] = {{4096, 0, 0},
                                      {0, static_cast<std::int16_t>(cp), static_cast<std::int16_t>(-sp)},
                                      {0, static_cast<std::int16_t>(sp), static_cast<std::int16_t>(cp)}};
    for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 3; c++) {
            std::int64_t acc = 0;
            for (int k = 0; k < 3; k++) {
                acc += static_cast<std::int64_t>(yaw[r][k]) * pitch[k][c];
            }
            out.m[r][c] = clamp16(acc >> 12);
        }
    }
}

void BoltPool::lay(BoltPoolHost& host, Bolt& b, const Vec3& start, const Vec3& target) {
    Vec3 cur = start;
    Vec3 aim = target;
    std::int32_t twist = 0;
    int i = 0;
    const int n = params_.segments;
    for (; i < n; i++) {
        const bool more = walk(host, b, cur, aim, target);
        BoltSegment& s = b.seg[static_cast<std::size_t>(i)];
        orient(host, s, cur, aim, i == 0 ? nullptr : &b.seg[static_cast<std::size_t>(i - 1)], i == 0 ? 0 : b.turn);
        s.t[0] = wsub(cur.x, origin_x_) >> 4;
        s.t[1] = cur.y >> 4;
        s.t[2] = wsub(cur.z, origin_z_) >> 4;
        const std::int32_t v[3] = {0, 0, static_cast<std::int16_t>(b.step)};
        const Vec3 d = rotate(s, v);
        cur = Vec3{wadd(cur.x, d.x), wadd(cur.y, d.y), wadd(cur.z, d.z)};
        b.color[static_cast<std::size_t>(i)] = params_.color;
        const std::int32_t range = params_.twist_max - params_.twist_min + 1;
        twist += host.rand16() % range + params_.twist_min;
        b.twist[static_cast<std::size_t>(i)] = twist;
        if (!more) {
            break;
        }
    }
    b.count = i;
    b.filled = std::min(i + 1, n);
}

// ---------------------------------------------------------------------------------------------
// A frame

void BoltPool::grow(Bolt& b) {
    const std::int32_t half = b.life / 2;
    if (half == 0) {
        return;
    }
    const std::int32_t rate = params_.grow_rate;
    b.head = std::min<std::int32_t>(wmul(b.life - b.timer, rate) / half, b.count);
    if (b.timer < half) {
        const std::int32_t tail = std::min<std::int32_t>(wmul(half - b.timer, rate) / half, b.count);
        b.tail = std::max(0, tail);
    }
}

Vec3 BoltPool::segment_world(const BoltSegment& seg) const {
    return Vec3{wadd(shl(seg.t[0], 4), origin_x_), shl(seg.t[1], 4), wadd(shl(seg.t[2], 4), origin_z_)};
}

bool BoltPool::hits_player(BoltPoolHost& host, const BoltSegment& seg, Vec3& at) {
    const auto chest = host.player_chest();
    const std::int32_t dx = wsub(chest[0], origin_x_ >> 4);
    const std::int32_t dy = chest[1];
    const std::int32_t dz = wsub(chest[2], origin_z_ >> 4);
    const std::int32_t ex = wsub(dx, seg.t[0]), ey = wsub(dy, seg.t[1]), ez = wsub(dz, seg.t[2]);
    const std::int32_t sum = wadd(wadd(wmul(ex, ex) >> 4, wmul(ez, ez) >> 4), wmul(ey, ey) >> 4);
    if (sum <= params_.hit.radius_sq) {
        at = Vec3{wadd(shl(dx, 4), origin_x_), shl(dy, 4), wadd(shl(dz, 4), origin_z_)};
        return true;
    }
    return false;
}

void BoltPool::hurt(BoltPoolHost& host, std::int32_t amount) {
    if (streak_timer_ > params_.hit.streak_window) {
        streak_ = 1;
    } else {
        streak_++;
    }
    streak_timer_ = 0;
    for (int i = 1; i < streak_; i++) {
        amount >>= 1;
    }
    host.hurt_player(params_.hit.attack, amount);
}

void BoltPool::draw(BoltPoolHost& host, Bolt& b) {
    std::int32_t clut = params_.clut;
    if (params_.clut_npc >= 0 && host.npc_is(params_.clut_npc, params_.clut_chara)) {
        clut = params_.clut_alt;
    }
    if (b.tail >= b.filled) {
        return;
    }
    const std::int32_t width = mul_q12_wide(b.step, params_.width);
    const BoltOwner& owner = params_.owners[static_cast<std::size_t>(owner_)];
    const std::int32_t dt = host.delta_time();

    auto cross = [&](std::int32_t angle) {
        std::array<Vec3s, 2> v;
        v[0].x = static_cast<std::int16_t>(mul_q12_wide(host.sin(angle), width) >> 4);
        v[0].y = static_cast<std::int16_t>(mul_q12_wide(host.cos(angle), width) >> 4);
        const std::int32_t side = angle + 1024;
        v[1].x = static_cast<std::int16_t>(mul_q12_wide(host.sin(side), width) >> 4);
        v[1].y = static_cast<std::int16_t>(mul_q12_wide(host.cos(side), width) >> 4);
        return v;
    };

    int i = b.tail;
    std::int32_t angle = b.twist[static_cast<std::size_t>(i)];
    std::uint32_t xy0 = 0, xy1 = 0;
    BoltPair pr = host.project_pair(b.seg[static_cast<std::size_t>(i)], cross(angle));
    xy0 = pr.xy0;
    if (pr.second) {
        xy1 = pr.xy1;
    }
    std::int32_t depth = pr.depth;
    int end = b.head;
    for (i++; i < end; i++) {
        const std::int32_t prev_angle = angle;
        angle = b.twist[static_cast<std::size_t>(i)];
        const std::int32_t prev_depth = depth;
        const std::int32_t bend = wrap_half(angle - prev_angle);
        std::uint32_t a, c;
        if (abs32(bend) < 1024) {
            a = xy0;
            c = xy1;
        } else {
            a = xy1;
            c = xy0;
        }
        const std::array<Vec3s, 2> v = cross(angle);
        const BoltSegment& seg = b.seg[static_cast<std::size_t>(i)];
        if (!b.hit) {
            Vec3 at;
            if (hits_player(host, seg, at) && dt != 0 && owner.hits) {
                hurt(host, owner.damage);
                host.fire_flash(params_.fire_pool, at, owner.hit_palette);
                for (int s : owner.hit_sounds) {
                    sound(host, s, host.player_position());
                }
                // The bolt ends at the hit.
                end = i < b.count ? i + 1 : i;
                b.count = end;
                b.head = end;
                b.hit = true;
            }
        }
        pr = host.project_pair(seg, v);
        xy0 = pr.xy0;
        if (pr.second) {
            xy1 = pr.xy1;
        }
        depth = pr.depth;
        if (depth == 0 || prev_depth == 0) {
            continue;
        }
        std::uint32_t& col = b.color[static_cast<std::size_t>(i)];
        if (col == 0) {
            continue;
        }
        BoltQuad q;
        q.xy = {a, c, xy0, xy1};
        const auto v0 = static_cast<std::uint8_t>(params_.v[static_cast<std::size_t>(i & 1)]);
        const auto v1 = static_cast<std::uint8_t>(v0 + params_.v_height);
        const auto u0 = static_cast<std::uint8_t>(params_.u[0]);
        const auto u1 = static_cast<std::uint8_t>(params_.u[1]);
        q.u = {u0, u1, u0, u1};
        q.v = {v0, v0, v1, v1};
        q.tpage = static_cast<std::uint16_t>(params_.tpage);
        q.clut = static_cast<std::uint16_t>(clut);
        if (dt != 0) {
            col -= params_.fade;
        }
        q.color = col;
        q.slot = ((prev_depth + depth) / 2) >> 1;
        host.emit_quad(q);
    }
}

void BoltPool::end(BoltPoolHost& host, Bolt& b) {
    const BoltSpawner& sp = params_.spawners[static_cast<std::size_t>(b.spawner)];
    if (sp.end_bolt < 0 || b.count <= 0) {
        return;
    }
    const Vec3 from = segment_world(b.seg[static_cast<std::size_t>(b.count - 1)]);
    const auto t = host.bone(bones_, b.bone);
    const Vec3 to{shl(t[0], 4), shl(t[1], 4), shl(t[2], 4)};
    throw_bolt(host, sp.end_bolt, from, to, 0, 0, 0);
}

void BoltPool::update(BoltPoolHost& host) {
    if (bolts_.empty()) {
        return;
    }
    host.begin_view(origin_x_, origin_z_);
    for (Bolt& b : bolts_) {
        if (!b.active) {
            continue;
        }
        if (b.timer > 0 || b.tail != b.count) {
            grow(b);
            const BoltSpawner& sp = params_.spawners[static_cast<std::size_t>(b.spawner)];
            if (sp.head_burst && !b.burst_done && b.count == b.head && b.count > 0) {
                const Vec3 at = segment_world(b.seg[static_cast<std::size_t>(b.count - 1)]);
                host.fire_burst(params_.fire_pool, at, params_.owners[static_cast<std::size_t>(owner_)].burst_palette);
                b.burst_done = true;
            }
            draw(host, b);
            b.timer -= host.delta_time();
        } else {
            b.active = false;
            end(host, b);
        }
    }
    const std::int32_t dt = host.delta_time();
    streak_timer_ += dt;
    for (std::int32_t& e : elapsed_) {
        e += dt;
    }
}

// ---------------------------------------------------------------------------------------------
// gamedata/effects/bolt_pools.json

namespace {

namespace p = parse;

constexpr std::int64_t kBig = std::int64_t{1} << 30;

std::int32_t opt(const json::Value& v, std::string_view key, std::int32_t def, std::int64_t lo = -kBig,
                 std::int64_t hi = kBig) {
    return v.find(key) != nullptr ? p::integer_of(v, key, lo, hi) : def;
}

std::uint32_t colour_word(const json::Value& v) {
    std::uint8_t c[3];
    p::rgb(v, c);
    return static_cast<std::uint32_t>(c[0]) | (static_cast<std::uint32_t>(c[1]) << 8) |
           (static_cast<std::uint32_t>(c[2]) << 16);
}

std::vector<int> slots(const json::Value& v, std::size_t count) {
    if (!v.is_array()) {
        p::fail(v, "a list of sound slots is expected");
    }
    std::vector<int> out;
    for (const json::Value& s : v.array()) {
        out.push_back(p::integer(s, 0, static_cast<std::int64_t>(count) - 1));
    }
    return out;
}

BoltPath path_of(const json::Value& v) {
    if (!v.is_string()) {
        p::fail(v, "a path name is expected");
    }
    static const std::pair<const char*, BoltPath> kPaths[] = {
        {"Wander", BoltPath::Wander}, {"Waypoint", BoltPath::Waypoint}, {"Drift", BoltPath::Drift},
        {"Lob", BoltPath::Lob},       {"Roam", BoltPath::Roam},         {"Seek", BoltPath::Seek},
        {"Sweep", BoltPath::Sweep},
    };
    for (const auto& [name, path] : kPaths) {
        if (v.string() == name) {
            return path;
        }
    }
    p::fail(v, "unknown path (Wander, Waypoint, Drift, Lob, Roam, Seek, Sweep)");
}

BoltPoolParams parse_one(const json::Value& v) {
    if (!v.is_object()) {
        p::fail(v, "a pool must be an object");
    }
    p::reject_unknown(v, {"capacity", "segments", "grow_rate", "width", "twist", "color", "fade", "texture", "hit", "lead",
                          "walk", "waypoints", "sounds", "owners", "spawners", "fire_pool", "elapsed_after_reset"});
    BoltPoolParams out;
    out.capacity = p::integer_of(v, "capacity", 0, 1024);
    out.segments = p::integer_of(v, "segments", 1, 1024);
    out.grow_rate = p::integer_of(v, "grow_rate", 1, kBig);
    out.width = p::integer_of(v, "width", -kBig, kBig);
    out.elapsed_after_reset = opt(v, "elapsed_after_reset", 4096);
    const json::Value& tw = p::member(v, "twist");
    if (!tw.is_array() || tw.array().size() != 2) {
        p::fail(tw, "\"twist\" is [min, max]");
    }
    out.twist_min = p::integer(tw.array()[0], -32768, 32767);
    out.twist_max = p::integer(tw.array()[1], out.twist_min, 32767);
    out.color = colour_word(p::member(v, "color"));
    out.fade = colour_word(p::member(v, "fade"));
    if (out.fade == 0 || out.color % out.fade != 0) {
        p::fail(p::member(v, "fade"), "the colour must fade to black in whole steps of \"fade\"");
    }

    const json::Value& tx = p::member(v, "texture");
    p::reject_unknown(tx, {"tpage", "clut", "clut_when", "u", "v", "v_height"});
    out.tpage = p::integer_of(tx, "tpage", 0, 0xFFFF);
    out.clut = p::integer_of(tx, "clut", 0, 0xFFFF);
    if (const json::Value* cw = tx.find("clut_when")) {
        p::reject_unknown(*cw, {"npc", "chara", "clut"});
        out.clut_npc = p::integer_of(*cw, "npc", 0, 255);
        const json::Value& ch = p::member(*cw, "chara");
        if (!ch.is_string() || ch.string().empty()) {
            p::fail(ch, "a character name is expected");
        }
        out.clut_chara = ch.string();
        out.clut_alt = p::integer_of(*cw, "clut", 0, 0xFFFF);
    }
    const json::Value& u = p::member(tx, "u");
    const json::Value& vv = p::member(tx, "v");
    if (!u.is_array() || u.array().size() != 2 || !vv.is_array() || vv.array().size() != 2) {
        p::fail(tx, "\"u\" is [left, right], \"v\" is [even segment top, odd segment top]");
    }
    for (int k = 0; k < 2; k++) {
        out.u[static_cast<std::size_t>(k)] = p::integer(u.array()[static_cast<std::size_t>(k)], 0, 255);
        out.v[static_cast<std::size_t>(k)] = p::integer(vv.array()[static_cast<std::size_t>(k)], 0, 255);
    }
    out.v_height = p::integer_of(tx, "v_height", 0, 255);

    const json::Value& h = p::member(v, "hit");
    p::reject_unknown(h, {"radius_sq", "attack", "streak_window"});
    out.hit.radius_sq = p::integer_of(h, "radius_sq", 0, kBig);
    out.hit.attack = p::integer_of(h, "attack", 0, 255);
    out.hit.streak_window = p::integer_of(h, "streak_window", 0, kBig);

    const json::Value& l = p::member(v, "lead");
    p::reject_unknown(l, {"gain_easy", "gain"});
    out.lead.gain_easy = p::integer_of(l, "gain_easy", -kBig, kBig);
    out.lead.gain = p::integer_of(l, "gain", -kBig, kBig);

    const json::Value& w = p::member(v, "walk");
    p::reject_unknown(w, {"wander_mask", "wander_step", "wander_low", "wander_drift_mask", "wander_turn",
                          "waypoint_count", "waypoint_reach", "drift_steps", "lob_height", "lob_spread",
                          "lob_jitter_shift", "lob_reach_divisor", "dive_spread", "dive_jitter_shift", "dive_height",
                          "dive_reach", "dive_lead_divisor", "roam_length", "roam_mask", "home", "sweep_steps"});
    auto& k = out.walk;
    k.wander_mask = p::integer_of(w, "wander_mask", 0, 0x7FFF);
    k.wander_step = p::integer_of(w, "wander_step", 0, 32767);
    k.wander_low = p::integer_of(w, "wander_low", -kBig, kBig);
    k.wander_drift_mask = p::integer_of(w, "wander_drift_mask", 0, 0x7FFF);
    k.wander_turn = p::integer_of(w, "wander_turn", 0, 4096);
    k.waypoint_reach = p::integer_of(w, "waypoint_reach", 0, kBig);
    k.drift_steps = p::integer_of(w, "drift_steps", 0, 1024);
    k.lob_height = p::integer_of(w, "lob_height", -kBig, kBig);
    k.lob_spread = p::integer_of(w, "lob_spread", -kBig, kBig);
    k.lob_jitter_shift = p::integer_of(w, "lob_jitter_shift", 0, 15);
    k.lob_reach_divisor = p::integer_of(w, "lob_reach_divisor", 1, kBig);
    k.dive_spread = p::integer_of(w, "dive_spread", -kBig, kBig);
    k.dive_jitter_shift = p::integer_of(w, "dive_jitter_shift", 0, 15);
    k.dive_height = p::integer_of(w, "dive_height", -kBig, kBig);
    k.dive_reach = p::integer_of(w, "dive_reach", 0, kBig);
    k.dive_lead_divisor = p::integer_of(w, "dive_lead_divisor", 1, kBig);
    k.roam_length = p::integer_of(w, "roam_length", 0, 32767);
    k.roam_mask = p::integer_of(w, "roam_mask", 0, 0x7FFF);
    k.sweep_steps = p::integer_of(w, "sweep_steps", 0, 1024);

    const json::Value& wp = p::member(v, "waypoints");
    if (!wp.is_array() || wp.array().empty()) {
        p::fail(wp, "\"waypoints\" is a list of [x, y, z]");
    }
    for (const json::Value& pt : wp.array()) {
        if (!pt.is_array() || pt.array().size() != 3) {
            p::fail(pt, "a waypoint is [x, y, z]");
        }
        out.waypoints.push_back(Vec3{p::integer(pt.array()[0], -kBig, kBig), p::integer(pt.array()[1], -kBig, kBig),
                                     p::integer(pt.array()[2], -kBig, kBig)});
    }
    const auto nwp = static_cast<std::int64_t>(out.waypoints.size());
    k.waypoint_count = p::integer_of(w, "waypoint_count", 1, nwp);
    k.home = p::integer_of(w, "home", 0, nwp - 1);

    const json::Value& snd = p::member(v, "sounds");
    if (!snd.is_array()) {
        p::fail(snd, "\"sounds\" is a list of sound slots");
    }
    for (const json::Value& s : snd.array()) {
        p::reject_unknown(s, {"sound", "volume", "interval", "first_gap"});
        BoltSound bs;
        bs.id = p::sound_of(s, "sound");
        bs.volume = p::integer_of(s, "volume", 0, 255);
        bs.interval = p::integer_of(s, "interval", 8, 0xFFFF);
        bs.first_gap = p::integer_of(s, "first_gap", 0, 0xFFFF);
        out.sounds.push_back(bs);
    }

    const json::Value& own = p::member(v, "owners");
    if (!own.is_array() || own.array().empty()) {
        p::fail(own, "\"owners\" is a list");
    }
    for (const json::Value& o : own.array()) {
        p::reject_unknown(o, {"name", "hits", "damage", "hit_palette", "hit_sounds", "burst_palette"});
        BoltOwner bo;
        const json::Value& n = p::member(o, "name");
        if (!n.is_string() || n.string().empty()) {
            p::fail(n, "an owner name is expected");
        }
        bo.name = n.string();
        bo.hits = p::boolean_of(o, "hits");
        if (bo.hits) {
            bo.damage = p::integer_of(o, "damage", 0, kBig);
            bo.hit_palette = p::integer_of(o, "hit_palette", 0, 255);
            bo.hit_sounds = slots(p::member(o, "hit_sounds"), out.sounds.size());
        }
        bo.burst_palette = p::integer_of(o, "burst_palette", 0, 255);
        out.owners.push_back(std::move(bo));
    }

    const json::Value& sps = p::member(v, "spawners");
    if (!sps.is_object()) {
        p::fail(sps, "\"spawners\" is an object of named spawners");
    }
    for (const auto& [name, s] : sps.members()) {
        if (name.rfind("_", 0) == 0) {
            continue;
        }
        out.spawner_names.emplace(name, static_cast<int>(out.spawners.size()));
        out.spawners.emplace_back();
    }
    for (const auto& [name, s] : sps.members()) {
        if (name.rfind("_", 0) == 0) {
            continue;
        }
        BoltSpawner& sp = out.spawners[static_cast<std::size_t>(out.spawner_names.at(name))];
        p::reject_unknown(s, {"path", "step", "life", "turn", "scatter", "aim_at_player", "sounds", "owner_sounds",
                              "strong", "head_burst", "end_bolt"});
        sp.path = path_of(p::member(s, "path"));
        sp.step = opt(s, "step", 0, 0, 32767);
        sp.life = opt(s, "life", 0, 0, kBig);
        sp.turn = p::integer_of(s, "turn", 0, 4096);
        sp.scatter = opt(s, "scatter", 0, 0, 32767);
        sp.aim_at_player = s.find("aim_at_player") != nullptr && p::boolean_of(s, "aim_at_player");
        sp.head_burst = s.find("head_burst") != nullptr && p::boolean_of(s, "head_burst");
        if (const json::Value* ss = s.find("sounds")) {
            sp.sounds = slots(*ss, out.sounds.size());
        }
        if (const json::Value* os = s.find("owner_sounds")) {
            if (!os->is_object()) {
                p::fail(*os, "\"owner_sounds\" is an object of owner name: [slots]");
            }
            for (const auto& [who, list] : os->members()) {
                if (who.rfind("_", 0) == 0) {
                    continue;
                }
                const bool known = std::any_of(out.owners.begin(), out.owners.end(),
                                               [&](const BoltOwner& o) { return o.name == who; });
                if (!known) {
                    p::fail(list, "no such owner in \"owners\"");
                }
                sp.owner_sounds.emplace(who, slots(list, out.sounds.size()));
            }
        }
        if (const json::Value* st = s.find("strong")) {
            p::reject_unknown(*st, {"step_above", "sounds"});
            sp.strong_step = p::integer_of(*st, "step_above", 0, 32767);
            sp.strong_sounds = slots(p::member(*st, "sounds"), out.sounds.size());
        }
        if (const json::Value* e = s.find("end_bolt")) {
            if (!e->is_string() || out.spawner_names.count(e->string()) == 0) {
                p::fail(*e, "\"end_bolt\" names a spawner of this pool");
            }
            sp.end_bolt = out.spawner_names.at(e->string());
        }
    }
    if (out.spawners.empty()) {
        p::fail(sps, "a pool needs a spawner");
    }
    const json::Value& fp = p::member(v, "fire_pool");
    if (!fp.is_string()) {
        p::fail(fp, "\"fire_pool\" names a pool of effects/fire_pools.json");
    }
    out.fire_pool = fp.string();
    return out;
}

}  // namespace

Result<std::map<std::string, BoltPoolParams>> parse_bolt_pools(std::string_view json_text) try {
    using R = Result<std::map<std::string, BoltPoolParams>>;
    Result<json::Value> doc = json::parse(json_text);
    if (!doc) {
        return R::fail(doc.error());
    }
    const json::Value& root = doc.value();
    p::reject_unknown(root, {"pools"});
    const json::Value& pools = p::member(root, "pools");
    if (!pools.is_object()) {
        p::fail(pools, "\"pools\" must be an object of named pools");
    }
    std::map<std::string, BoltPoolParams> out;
    for (const auto& [name, value] : pools.members()) {
        if (name.rfind("_", 0) == 0) {
            continue;
        }
        try {
            out.emplace(name, parse_one(value));
        } catch (const std::exception& e) {
            throw std::runtime_error("pool \"" + name + "\": " + e.what());
        }
    }
    return R::ok(std::move(out));
} catch (const std::exception& e) {
    return Result<std::map<std::string, BoltPoolParams>>::fail(e.what());
}

Result<std::map<std::string, BoltPoolParams>> load_bolt_pools(const std::filesystem::path& gamedata_dir,
                                                             const std::filesystem::path& mods_dir,
                                                             std::string* source) {
    using R = Result<std::map<std::string, BoltPoolParams>>;
    std::string where;
    Result<std::string> text =
        read_effect_file(gamedata_dir, mods_dir, std::filesystem::path("effects") / "bolt_pools.json", &where);
    if (source != nullptr) {
        *source = where;
    }
    if (!text) {
        return R::fail(text.error());
    }
    R parsed = parse_bolt_pools(text.value());
    if (!parsed) {
        return R::fail(where + ": " + parsed.error());
    }
    return parsed;
}

}  // namespace sh::effects
