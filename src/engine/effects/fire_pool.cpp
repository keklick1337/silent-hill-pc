// SPDX-License-Identifier: GPL-3.0-or-later
// Fire pools: flame sprites with frame animations and small behaviours (fire_pool.h).

#include "engine/effects/fire_pool.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

#include "engine/effects/effects_file.h"
#include "formats/common/json.h"

namespace sh::effects {

namespace {

std::int32_t mul_q12(std::int32_t a, std::int32_t b) {
    return static_cast<std::int32_t>((static_cast<std::int64_t>(a) * b) >> 12);
}

// rand / ((0x7FFF / range) + 1): a roll of 0..range-1; a zero divisor gives 0.
std::int32_t roll_below(std::int32_t roll, std::int32_t range) {
    const std::int32_t div = (0x7FFF / range) + 1;
    return div != 0 ? roll / div : 0;
}

std::int16_t q8(std::int32_t v) {
    return static_cast<std::int16_t>(v >> 4);
}

void advance(Vec3& pos, const Vec3& vel, std::int32_t dt) {
    pos.x += mul_q12(vel.x, dt);
    pos.y += mul_q12(vel.y, dt);
    pos.z += mul_q12(vel.z, dt);
}

}  // namespace

FirePool::FirePool(FirePoolParams params)
    : params_(std::move(params)), attached_(static_cast<std::size_t>(params_.attached_capacity)) {}

void FirePool::reset() {
    particles_.resize(static_cast<std::size_t>(params_.capacity));
    for (FireParticle& p : particles_) {
        p.active = false;
        p.pending = false;
        p.animation = params_.burn.animation;
        p.step = 0;
        p.update = FireUpdate::Invalid;
        p.draw = FireDraw::None;
    }
    paused_ = false;
}

void FirePool::clear() {
    for (FireParticle& p : particles_) {
        p.active = false;
        p.pending = false;
    }
}

FireParticle* FirePool::acquire() {
    for (FireParticle& p : particles_) {
        if (p.active || p.pending) {
            continue;
        }
        // Only these are cleared: a spawn sets the rest, or keeps what the slot had.
        p.pending = true;
        p.state = 0;
        p.z_drift = 0;
        p.fade = 0;
        p.palette = 0;
        p.hold = 0;
        return &p;
    }
    return nullptr;
}

int FirePool::step(int animation, int& at, std::int32_t& hold, std::int32_t dt) {
    const std::vector<FireStep>& steps = params_.animations[static_cast<std::size_t>(animation)].steps;
    const FireStep& now = steps[static_cast<std::size_t>(at)];
    int cur = at;
    int sprite = -1;
    if (now.kind == FireStep::Kind::Loop) {
        cur = now.target;
        sprite = steps[static_cast<std::size_t>(cur)].sprite;
    } else {
        sprite = now.sprite;
    }
    if (hold <= 0) {
        if (now.kind != FireStep::Kind::Stop && dt != 0) {
            at = cur + 1;
            hold = (steps[static_cast<std::size_t>(at)].hold * 4096) / params_.hold_rate;
        }
    } else {
        hold -= dt;
    }
    return sprite;
}

void FirePool::update(FirePoolHost& host) {
    host.begin_view(params_.origin);
    const std::int32_t dt = host.delta_time();
    for (std::size_t i = 0; i < particles_.size(); i++) {
        FireParticle& p = particles_[i];
        if (!p.active) {
            continue;
        }
        if (p.update == FireUpdate::Invalid) {
            p.active = false;
            continue;
        }
        if (p.update == FireUpdate::None) {
            continue;
        }
        if (!paused_) {
            const int s = run(host, p);
            if (s >= 0 && p.draw != FireDraw::None) {
                draw(host, p, s);
            }
        } else if (p.draw != FireDraw::None) {
            const int s = step(p, dt);
            if (s >= 0) {
                draw(host, p, s);
            }
        }
        p.pos.z += mul_q12(p.z_drift, dt);
        if (dt != 0) {
            p.alpha = std::max(0, p.alpha - p.fade);
        }
    }
    for (std::size_t i = 0; i < particles_.size(); i++) {
        FireParticle& p = particles_[i];
        if (!p.pending) {
            continue;
        }
        p.active = true;
        p.pending = false;
        if (p.update == FireUpdate::Invalid) {
            p.active = false;
            continue;
        }
        if (p.update == FireUpdate::None) {
            continue;
        }
        if (!paused_) {
            const int s = run(host, p);
            if (s >= 0 && p.draw != FireDraw::None) {
                draw(host, p, s);
            }
        } else if (p.draw != FireDraw::None) {
            const int s = step(p, dt);
            if (s >= 0) {
                draw(host, p, s);
            }
        }
    }
}

int FirePool::run(FirePoolHost& host, FireParticle& p) {
    switch (p.update) {
        case FireUpdate::Ember:
            return ember(host, p);
        case FireUpdate::Rise:
            return rise(host, p);
        case FireUpdate::Burn:
            return burn(host, p);
        case FireUpdate::Fall:
            return fall(host, p);
        case FireUpdate::Flash:
            return flash(host, p);
        default:
            return -1;
    }
}

static bool stopped(const FirePoolParams& params, const FireParticle& p) {
    return params.animations[static_cast<std::size_t>(p.animation)].steps[static_cast<std::size_t>(p.step)].kind ==
           FireStep::Kind::Stop;
}

int FirePool::ember(FirePoolHost& host, FireParticle& p) {
    const std::int32_t dt = host.delta_time();
    advance(p.pos, p.vel, dt);
    const int s = step(p, dt);
    if (stopped(params_, p)) {
        p.active = false;
        p.pending = false;
    }
    p.vel.x = mul_q12(p.vel.x, params_.ember.damping);
    p.vel.y = mul_q12(p.vel.y, params_.ember.damping);
    p.vel.z = mul_q12(p.vel.z, params_.ember.damping);
    return s;
}

int FirePool::rise(FirePoolHost& host, FireParticle& p) {
    const std::int32_t dt = host.delta_time();
    advance(p.pos, p.vel, dt);
    const int s = step(p, dt);
    if (stopped(params_, p)) {
        p.active = false;
        p.pending = false;
    }
    return s;
}

int FirePool::burn(FirePoolHost& host, FireParticle& p) {
    const FirePoolParams::Burn& b = params_.burn;
    const std::int32_t dt = host.delta_time();
    int s = -1;
    switch (p.state) {
        case 0:
            p.animation = b.animation;
            p.step = host.rand16() / b.start_divisor;
            p.draw = FireDraw::Billboard;
            if (host.sound_allowed()) {
                const Vec3 at{p.pos.x + params_.origin.x, p.pos.y + params_.origin.y, p.pos.z + params_.origin.z};
                host.play_sound(b.sound, at, b.volume);
            }
            p.state++;
            [[fallthrough]];
        case 1:
            s = step(p, dt);
            if (s == b.tongue_sprite) {
                spawn_tongue(p);
            }
            if (host.rand16() == 0) {
                spawn_spark(p);
            }
            if (p.timer < 0) {
                p.timer = b.fade_time;
                p.state++;
            }
            break;
        case 2:
            s = step(p, dt);
            p.alpha = mul_q12(p.alpha, b.fade);
            if (p.timer < 0) {
                p.active = false;
                p.pending = false;
            }
            break;
        default:
            break;
    }
    p.timer -= dt;
    return s;
}

int FirePool::fall(FirePoolHost& host, FireParticle& p) {
    const FirePoolParams::Fireball& f = params_.fireball;
    const std::int32_t dt = host.delta_time();
    int s = -1;
    switch (p.state) {
        case 0:
            advance(p.pos, p.vel, dt);
            if (p.pos.y > f.ceiling) {
                p.active = false;
                p.pending = false;
            } else {
                const std::int32_t height = p.pos.y + params_.origin.y;
                const std::int32_t ground = host.ground_height(p.pos.x + params_.origin.x, p.pos.z + params_.origin.z);
                if (ground < height) {
                    p.pos.y = ground;
                    p.draw = FireDraw::None;
                    p.timer = f.land_time;
                    p.state++;
                    for (int i = 0; i < f.embers; i++) {
                        spawn_ember(host, p);
                    }
                }
            }
            s = step(p, dt);
            if (s == f.trail_sprite) {
                spawn_trail(host, p);
            }
            break;
        case 1:
            p.state = 0;
            p.timer = f.burn_time;
            p.update = FireUpdate::Burn;
            p.scale = mul_q12(p.scale, f.burn_scale);
            break;
        default:
            break;
    }
    p.timer -= dt;
    return s;
}

int FirePool::flash(FirePoolHost& host, FireParticle& p) {
    const std::int32_t dt = host.delta_time();
    switch (p.state) {
        case 0:
            if (p.timer <= 0) {
                p.timer = params_.flash.wait;
                p.state++;
            } else {
                p.timer -= dt;
            }
            return -1;
        case 1: {
            if (p.timer < 0) {
                p.fade = params_.flash.fade;
            }
            p.timer--;
            advance(p.pos, p.vel, dt);
            const int s = step(p, dt);
            if (stopped(params_, p)) {
                p.active = false;
                p.pending = false;
            }
            return s;
        }
        default:
            return -1;
    }
}

void FirePool::spawn_ember(FirePoolHost& host, const FireParticle& from) {
    FireParticle* q = acquire();
    if (q == nullptr) {
        return;
    }
    const FirePoolParams::Ember& e = params_.ember;
    q->alpha = from.alpha;
    q->pos = from.pos;
    q->pos.y = 0;
    const std::int32_t angle = host.rand16();
    q->vel.x = mul_q12(host.sin(angle), e.speed);
    q->vel.y = 0;
    q->vel.z = mul_q12(host.cos(angle), e.speed);
    q->scale = mul_q12(from.scale, e.scale);
    q->animation = e.animation;
    q->step = 0;
    q->update = FireUpdate::Ember;
    q->draw = FireDraw::Billboard;
}

void FirePool::spawn_tongue(const FireParticle& from) {
    FireParticle* q = acquire();
    if (q == nullptr) {
        return;
    }
    q->pos = from.pos;
    q->alpha = from.alpha;
    q->animation = params_.tongue.animation;
    q->step = 0;
    q->vel.x = 0;
    q->vel.z = 0;
    q->update = FireUpdate::Rise;
    q->draw = FireDraw::Billboard;
    q->scale = from.scale;
    q->pos.y += mul_q12(q->scale, params_.tongue.lift);
    q->vel.y = mul_q12(q->scale, params_.tongue.rise);
}

void FirePool::spawn_spark(const FireParticle& from) {
    FireParticle* q = acquire();
    if (q == nullptr) {
        return;
    }
    q->pos = from.pos;
    q->vel = Vec3{0, params_.spark.rise, 0};
    q->animation = params_.spark.animation;
    q->step = 0;
    q->update = FireUpdate::Rise;
    q->draw = FireDraw::Billboard;
    q->alpha = from.alpha;
    q->scale = from.scale;
}

void FirePool::spawn_trail(FirePoolHost& host, const FireParticle& from) {
    FireParticle* q = acquire();
    if (q == nullptr) {
        return;
    }
    q->pos = from.pos;
    const std::int32_t speed = host.rand16() / params_.trail.speed_divisor + params_.trail.speed_base;
    q->vel.x = mul_q12(from.vel.x, speed);
    q->vel.y = mul_q12(from.vel.y, speed);
    q->vel.z = mul_q12(from.vel.z, speed);
    q->animation = params_.trail.animation;
    q->step = 0;
    q->update = FireUpdate::Rise;
    q->draw = FireDraw::Rolled;
    q->scale = from.scale;
    q->alpha = from.alpha;
    q->roll = from.roll;
}

namespace {

std::int32_t rand_between(FirePoolHost& host, std::int32_t lo, std::int32_t hi) {
    if (hi < lo) {
        std::swap(lo, hi);
    }
    const std::int32_t range = hi - lo;
    if (range != 0) {
        lo += roll_below(host.rand16(), range);
    }
    return lo;
}

// pos.x moves along vel by the height: pos.x += vel.x * pos.y / vel.y, the product kept to 32 bits.
void slide(Vec3& pos, const Vec3& vel) {
    if (vel.y == 0) {
        return;
    }
    const auto product = static_cast<std::int32_t>(static_cast<std::uint32_t>(vel.x) * static_cast<std::uint32_t>(pos.y));
    if (vel.y == -1 && product == INT32_MIN) {
        return;
    }
    pos.x += product / vel.y;
}

}  // namespace

std::int32_t FirePool::spread(FirePoolHost& host, const FireCue& c, Vec3& out) {
    const FirePoolParams::Cue& k = params_.cue;
    const Vec3& o = params_.origin;
    std::int32_t brightness = k.brightness;
    switch (c.shape) {
        case 0: {
            const std::int32_t range = (c.far - c.near) >> 3;
            std::int32_t t = range == 0 ? 0 : roll_below(host.rand16(), range);
            t *= 8;
            const std::int32_t dist = c.near + t;
            brightness = (k.brightness_top - t) / k.brightness_divisor;
            const std::int32_t angle = host.rand16();
            const std::int32_t sx = mul_q12(host.sin(angle), dist);
            const std::int32_t cz = mul_q12(host.cos(angle), dist);
            out.x = (sx + c.position.x) - o.x;
            out.y = c.position.y - o.y;
            out.z = (cz + c.position.z) - o.z;
            break;
        }
        case 1: {
            std::int32_t t = c.near == 0 ? 0 : roll_below(host.rand16(), c.near);
            const std::int32_t sx = t - c.near / 2;
            t = c.far == 0 ? 0 : roll_below(host.rand16(), c.far);
            out.x = (sx + c.position.x) - o.x;
            out.y = c.position.y - o.y;
            out.z = ((t - c.far / 2) + c.position.z) - o.z;
            break;
        }
        default:
            break;
    }
    return brightness;
}

std::int32_t FirePool::tilt(FirePoolHost& host, Vec3& vel) {
    const FirePoolParams::Cue& k = params_.cue;
    vel.x = mul_q12(host.sin(k.tilt), k.tilt_speed);
    vel.y = mul_q12(host.cos(k.tilt), k.tilt_speed);
    vel.z = 0;
    return k.tilt;
}

void FirePool::spawn_cue(FirePoolHost& host, const FireCue& c) {
    FireParticle* q = acquire();
    if (q == nullptr) {
        return;
    }
    q->alpha = spread(host, c, q->pos);
    q->scale = rand_between(host, c.scale_min, c.scale_max);
    const std::int32_t lead = rand_between(host, c.lead_min, c.lead_max);
    switch (c.kind) {
        case 0:
            q->z_drift = c.z_drift;
            q->fade = c.fade;
            q->roll = tilt(host, q->vel);
            slide(q->pos, q->vel);
            q->animation = params_.fireball.animation;
            q->step = 0;
            q->update = FireUpdate::Fall;
            q->draw = FireDraw::Rolled;
            q->pos.x += mul_q12(q->vel.x, lead);
            q->pos.y += mul_q12(q->vel.y, lead);
            break;
        case 1:
            q->scale *= 2;
            q->z_drift = c.z_drift;
            q->fade = c.fade;
            q->animation = params_.burn.animation;
            q->step = host.rand16() / params_.burn.start_divisor;
            q->update = FireUpdate::Burn;
            q->timer = lead;
            q->draw = FireDraw::Billboard;
            break;
        case 2:
            q->roll = tilt(host, q->vel);
            slide(q->pos, q->vel);
            break;
        default:
            break;
    }
}

void FirePool::cue(FirePoolHost& host, const FireCue& c) {
    if (c.kind == 99) {
        host.reseed(0);
        clear();
        return;
    }
    const FirePoolParams::Cue& k = params_.cue;
    std::int32_t area = 0;
    if (c.shape == 0) {
        std::int32_t a = c.near, b = c.far;
        if (b < a) {
            std::swap(a, b);
        }
        area = mul_q12(mul_q12(b, b), k.area) - mul_q12(mul_q12(a, a), k.area);
    } else if (c.shape == 1) {
        area = mul_q12(c.near, c.far);
    }
    std::int32_t lo = c.lead_min, hi = c.lead_max;
    if (hi < lo) {
        std::swap(lo, hi);
    }
    const std::int32_t span = hi - lo;
    std::int32_t amount = mul_q12(area, c.rate);
    amount = ((c.flags & 2) != 0 && span != 0) ? mul_q12(amount, span) : mul_q12(amount, host.delta_time());

    int count = amount >> 12;
    amount %= 4096;
    const std::int32_t roll = host.rand16() % 4096;
    if (amount > roll) {
        count++;
    }
    if (count > k.reseed_above) {
        host.reseed(0);
    }
    for (int i = 0; i < count; i++) {
        spawn_cue(host, c);
    }
}

void FirePool::spark_at(const Vec3& world, int animation, std::int32_t palette, std::int32_t delay,
                        std::int32_t scale) {
    FireParticle* q = acquire();
    if (q == nullptr) {
        return;
    }
    q->pos = Vec3{world.x - params_.origin.x, world.y - params_.origin.y, world.z - params_.origin.z};
    q->vel = Vec3{};
    q->alpha = params_.burst.alpha;
    q->scale = scale;
    q->animation = animation;
    q->step = 0;
    q->palette = static_cast<std::int16_t>(palette << 6);
    q->update = FireUpdate::Flash;
    q->timer = delay;
    q->draw = FireDraw::Billboard;
}

void FirePool::glow(FirePoolHost&, const Vec3& world, std::int32_t palette, std::int32_t delay, std::int32_t scale) {
    FireParticle* q = acquire();
    if (q == nullptr) {
        return;
    }
    q->pos = Vec3{world.x - params_.origin.x, world.y - params_.origin.y, world.z - params_.origin.z};
    q->alpha = params_.glow.alpha;
    q->vel = Vec3{};
    q->animation = params_.glow.animation;
    q->step = 0;
    q->scale = scale;
    q->palette = static_cast<std::int16_t>(palette << 6);
    q->timer = delay;
    q->update = FireUpdate::Flash;
    q->draw = FireDraw::Billboard;
}

void FirePool::burst(FirePoolHost& host, const Vec3& world, std::int32_t palette) {
    const FirePoolParams::Burst& b = params_.burst;
    const int count = (host.rand16() & b.count_mask) != 0 ? b.count_if_set : b.count_if_clear;
    for (int i = 0; i < count; i++) {
        // The original rolls the scale before the animation (its arguments are evaluated right to left).
        const auto scale = static_cast<std::size_t>(host.rand16() / b.pick_divisor);
        const auto animation = static_cast<std::size_t>(host.rand16() / b.pick_divisor);
        spark_at(world, b.animations[std::min(animation, b.animations.size() - 1)], palette,
                 b.delays[std::min(static_cast<std::size_t>(i), b.delays.size() - 1)],
                 b.scales[std::min(scale, b.scales.size() - 1)]);
    }
}

void FirePool::glow_burst(FirePoolHost& host, const Vec3& world, std::int32_t palette) {
    glow(host, world, palette, params_.glow.delay, params_.glow.scale);
    burst(host, world, palette);
}

void FirePool::attached_reset() {
    for (AttachedFlame& f : attached_) {
        f.active = false;
    }
}

void FirePool::attach(FirePoolHost& host) {
    const FirePoolParams::Attached& a = params_.attached;
    for (int bone = a.bone_first; bone <= a.bone_last; bone++) {
        AttachedFlame* f = nullptr;
        for (AttachedFlame& c : attached_) {
            if (!c.active) {
                f = &c;
                break;
            }
        }
        if (f == nullptr) {
            continue;
        }
        f->hold = 0;
        f->active = true;
        f->animation = a.animation;
        f->step = host.rand16() / a.start_divisor;
        f->scale = host.rand16() / a.scale_divisor + a.scale_base;
        f->alpha = a.alpha;
        f->bone = bone;
    }
}

void FirePool::draw_attached(FirePoolHost& host) {
    const FirePoolParams::Attached& a = params_.attached;
    std::vector<Vec3> bones(static_cast<std::size_t>(a.bone_last + 1));
    for (int bone = a.bone_first; bone <= a.bone_last; bone++) {
        const Vec3 w = host.bone_position(bone);
        bones[static_cast<std::size_t>(bone)] =
            Vec3{w.x - params_.origin.x, w.y - params_.origin.y, w.z - params_.origin.z};
    }
    host.begin_view(params_.origin);
    const std::int32_t dt = host.delta_time();
    for (AttachedFlame& f : attached_) {
        if (!f.active || f.bone < a.bone_first || f.bone > a.bone_last) {
            continue;
        }
        const Vec3 pos = bones[static_cast<std::size_t>(f.bone)];
        const int s = step(f.animation, f.step, f.hold, dt);
        if (s >= 0) {
            billboard(host, pos, f.scale, f.alpha, 0, s);
        }
    }
}

void FirePool::draw(FirePoolHost& host, const FireParticle& p, int sprite) {
    if (p.draw != FireDraw::None) {
        const float scale = std::min(static_cast<float>(p.scale) / 4096.0f, 4.0f);
        const Vec3& o = params_.origin;
        host.flame_shown({static_cast<float>((p.pos.x + o.x) >> 4), static_cast<float>((p.pos.y + o.y) >> 4),
                          static_cast<float>((p.pos.z + o.z) >> 4)},
                         static_cast<float>(std::clamp(p.alpha, 0, 255)) / 255.0f * std::max(scale, 0.0f));
    }
    if (p.draw == FireDraw::Billboard) {
        billboard(host, p.pos, p.scale, p.alpha, p.palette, sprite);
    } else if (p.draw == FireDraw::Rolled) {
        rolled(host, p, sprite);
    }
}

void FirePool::billboard(FirePoolHost& host, const Vec3& pos, std::int32_t scale, std::int32_t alpha,
                         std::int32_t palette, int sprite) {
    const FireSprite& sp = params_.sprites[static_cast<std::size_t>(sprite)];
    const auto color = static_cast<std::uint32_t>(alpha) * 0x10101u;
    const FireProjection pr = host.project(Vec3s{q8(pos.x), q8(pos.y), q8(pos.z)});
    // A point on the camera plane has depth 0 without the overflow flag; it is not drawn.
    if (pr.near || pr.depth == 0) {
        return;
    }
    const std::int32_t g = host.geom_screen();
    const std::int32_t d = pr.depth;
    std::int32_t w = ((sp.w * g) / 4) / d;
    std::int32_t h = ((sp.h * g) / 4) / d;
    std::int32_t ox = ((sp.ox / 4) * g) / d;
    std::int32_t oy = ((sp.oy / 4) * g) / d;
    w = mul_q12(w, scale);
    h = mul_q12(h, scale);
    ox = mul_q12(ox, scale);
    oy = mul_q12(oy, scale);
    const auto x0 = static_cast<std::int16_t>(pr.x - ox);
    const auto y0 = static_cast<std::int16_t>(pr.y - oy);
    const std::array<std::int32_t, 4>& c = params_.cull;
    if (x0 < (-w + c[0]) || x0 > c[1] || y0 < (-h + c[2]) || y0 > c[3]) {
        return;
    }
    FireQuad q;
    q.x = {x0, static_cast<std::int16_t>(x0 + w), x0, static_cast<std::int16_t>(x0 + w)};
    q.y = {y0, y0, static_cast<std::int16_t>(y0 + h), static_cast<std::int16_t>(y0 + h)};
    const auto u0 = static_cast<std::uint8_t>(sp.u), u1 = static_cast<std::uint8_t>(sp.u + sp.w);
    const auto v0 = static_cast<std::uint8_t>(sp.v), v1 = static_cast<std::uint8_t>(sp.v + sp.h);
    q.u = {u0, u1, u0, u1};
    q.v = {v0, v0, v1, v1};
    q.color = color;
    q.tpage = static_cast<std::uint16_t>(sp.tpage);
    q.clut = static_cast<std::uint16_t>(sp.clut + palette);
    q.slot = d >> 1;
    host.emit_sprite(q);
}

void FirePool::rolled(FirePoolHost& host, const FireParticle& p, int sprite) {
    const FireSprite& sp = params_.sprites[static_cast<std::size_t>(sprite)];
    const auto hw = static_cast<std::int16_t>(mul_q12(sp.w, p.scale));
    const auto hh = static_cast<std::int16_t>(mul_q12(sp.h, p.scale));
    const auto nw = static_cast<std::int16_t>(-hw), nh = static_cast<std::int16_t>(-hh);
    const std::array<Vec3s, 4> corners{Vec3s{nw, nh, 0}, Vec3s{hw, nh, 0}, Vec3s{nw, hh, 0}, Vec3s{hw, hh, 0}};
    const FireRolledProjection pr = host.project_rolled(Vec3s{q8(p.pos.x), q8(p.pos.y), q8(p.pos.z)}, p.roll, corners);
    if (pr.near || pr.far) {
        return;
    }
    FireQuad q;
    q.x = pr.x;
    q.y = pr.y;
    const auto u0 = static_cast<std::uint8_t>(sp.u), u1 = static_cast<std::uint8_t>(sp.u + sp.w);
    const auto v0 = static_cast<std::uint8_t>(sp.v), v1 = static_cast<std::uint8_t>(sp.v + sp.h);
    q.u = {u0, u1, u0, u1};
    q.v = {v0, v0, v1, v1};
    q.color = static_cast<std::uint32_t>(p.alpha) * 0x10101u;
    q.tpage = static_cast<std::uint16_t>(sp.tpage);
    q.clut = static_cast<std::uint16_t>(sp.clut);
    q.slot = pr.depth >> 1;
    host.emit_sprite(q);
}

// ---------------------------------------------------------------------------------------------
// gamedata/effects/fire_pools.json

namespace {

namespace p = parse;

constexpr std::int64_t kBig = std::int64_t{1} << 30;

std::int32_t opt(const json::Value& v, std::string_view key, std::int32_t def, std::int64_t lo = -kBig,
                 std::int64_t hi = kBig) {
    return v.find(key) != nullptr ? p::integer_of(v, key, lo, hi) : def;
}

int animation_ref(const json::Value& v, std::string_view key, const std::map<std::string, int>& names) {
    const json::Value& n = p::member(v, key);
    if (!n.is_string()) {
        p::fail(n, "an animation name is expected");
    }
    auto it = names.find(n.string());
    if (it == names.end()) {
        p::fail(n, "no such animation in \"animations\"");
    }
    return it->second;
}

int sprite_ref(const json::Value& v, std::string_view key, std::size_t sprites) {
    return p::integer_of(v, key, 0, static_cast<std::int64_t>(sprites) - 1);
}

FireAnimation parse_animation(const json::Value& v, std::size_t sprites) {
    if (!v.is_array() || v.array().size() < 2) {
        p::fail(v, "an animation is an array of steps ending with a loop or a stop");
    }
    FireAnimation a;
    for (const json::Value& s : v.array()) {
        FireStep step;
        if (s.is_array()) {
            if (s.array().size() != 2) {
                p::fail(s, "a shown step is [hold, sprite]");
            }
            step.hold = p::integer(s.array()[0], 0, 65535);
            step.sprite = p::integer(s.array()[1], 0, static_cast<std::int64_t>(sprites) - 1);
        } else {
            p::reject_unknown(s, {"hold", "loop", "stop", "sprite"});
            step.hold = opt(s, "hold", 0, 0, 65535);
            if (s.find("loop") != nullptr) {
                if (s.find("sprite") != nullptr) {
                    p::fail(s, "a loop shows its target's sprite; \"sprite\" belongs to a stop");
                }
                step.kind = FireStep::Kind::Loop;
                step.target = p::integer_of(s, "loop", 0, static_cast<std::int64_t>(v.array().size()) - 1);
            } else if (p::boolean_of(s, "stop")) {
                step.kind = FireStep::Kind::Stop;
                step.sprite = s.find("sprite") != nullptr
                                  ? p::integer_of(s, "sprite", 0, static_cast<std::int64_t>(sprites) - 1)
                                  : -1;
            } else {
                p::fail(s, "a step object is {\"loop\": step} or {\"stop\": true}");
            }
        }
        a.steps.push_back(step);
    }
    if (a.steps.back().kind == FireStep::Kind::Show) {
        p::fail(v.array().back(), "the last step must loop or stop");
    }
    for (std::size_t i = 0; i < a.steps.size(); i++) {
        const FireStep& s = a.steps[i];
        if (s.kind == FireStep::Kind::Loop &&
            (a.steps[static_cast<std::size_t>(s.target)].kind != FireStep::Kind::Show)) {
            p::fail(v.array()[i], "a loop must jump to a shown step");
        }
    }
    return a;
}

std::vector<std::int32_t> int_list(const json::Value& v, std::string_view key) {
    const json::Value& l = p::member(v, key);
    if (!l.is_array() || l.array().empty()) {
        p::fail(l, "a non-empty array of integers is expected");
    }
    std::vector<std::int32_t> out;
    for (const json::Value& x : l.array()) {
        out.push_back(p::integer(x, -kBig, kBig));
    }
    return out;
}

FirePoolParams parse_one(const json::Value& v) {
    p::reject_unknown(v, {"capacity", "attached_capacity", "origin", "hold_rate", "cull", "sprites", "animations",
                          "ember", "tongue", "spark", "trail", "burn", "fireball", "flash", "glow", "burst", "cue",
                          "attached"});
    FirePoolParams out;
    out.capacity = p::integer_of(v, "capacity", 0, 4096);
    out.attached_capacity = p::integer_of(v, "attached_capacity", 0, 4096);
    const json::Value& o = p::member(v, "origin");
    if (!o.is_array() || o.array().size() != 3) {
        p::fail(o, "\"origin\" is [x, y, z] (Q12)");
    }
    out.origin = Vec3{p::integer(o.array()[0], -kBig, kBig), p::integer(o.array()[1], -kBig, kBig),
                      p::integer(o.array()[2], -kBig, kBig)};
    out.hold_rate = p::integer_of(v, "hold_rate", 1, 1000);
    const json::Value& cull = p::member(v, "cull");
    if (!cull.is_array() || cull.array().size() != 4) {
        p::fail(cull, "\"cull\" is [left, right, top, bottom] (pixels)");
    }
    for (std::size_t i = 0; i < 4; i++) {
        out.cull[i] = p::integer(cull.array()[i], -32768, 32767);
    }

    const json::Value& sprites = p::member(v, "sprites");
    if (!sprites.is_array() || sprites.array().empty()) {
        p::fail(sprites, "\"sprites\" must be a non-empty array of [u, v, w, h, tpage, clut, ox, oy]");
    }
    for (const json::Value& s : sprites.array()) {
        if (!s.is_array() || s.array().size() != 8) {
            p::fail(s, "a sprite is [u, v, w, h, tpage, clut, ox, oy]");
        }
        const auto& a = s.array();
        FireSprite sp;
        sp.u = static_cast<std::int16_t>(p::integer(a[0], -32768, 32767));
        sp.v = static_cast<std::int16_t>(p::integer(a[1], -32768, 32767));
        sp.w = static_cast<std::int16_t>(p::integer(a[2], -32768, 32767));
        sp.h = static_cast<std::int16_t>(p::integer(a[3], -32768, 32767));
        sp.tpage = static_cast<std::int16_t>(p::integer(a[4], -32768, 32767));
        sp.clut = static_cast<std::int16_t>(p::integer(a[5], -32768, 32767));
        sp.ox = static_cast<std::uint16_t>(p::integer(a[6], 0, 65535));
        sp.oy = static_cast<std::uint16_t>(p::integer(a[7], 0, 65535));
        out.sprites.push_back(sp);
    }

    const json::Value& anims = p::member(v, "animations");
    if (!anims.is_object()) {
        p::fail(anims, "\"animations\" must be an object of named animations");
    }
    for (const auto& [name, value] : anims.members()) {
        if (name.rfind("_", 0) == 0) {
            continue;
        }
        out.animation_names.emplace(name, static_cast<int>(out.animations.size()));
        out.animations.push_back(parse_animation(value, out.sprites.size()));
    }
    const auto& names = out.animation_names;
    const std::size_t ns = out.sprites.size();

    const json::Value& e = p::member(v, "ember");
    p::reject_unknown(e, {"animation", "speed", "scale", "damping"});
    out.ember.animation = animation_ref(e, "animation", names);
    out.ember.speed = p::integer_of(e, "speed", -kBig, kBig);
    out.ember.scale = p::integer_of(e, "scale", -kBig, kBig);
    out.ember.damping = p::integer_of(e, "damping", -kBig, kBig);

    const json::Value& t = p::member(v, "tongue");
    p::reject_unknown(t, {"animation", "lift", "rise"});
    out.tongue.animation = animation_ref(t, "animation", names);
    out.tongue.lift = p::integer_of(t, "lift", -kBig, kBig);
    out.tongue.rise = p::integer_of(t, "rise", -kBig, kBig);

    const json::Value& sk = p::member(v, "spark");
    p::reject_unknown(sk, {"animation", "rise"});
    out.spark.animation = animation_ref(sk, "animation", names);
    out.spark.rise = p::integer_of(sk, "rise", -kBig, kBig);

    const json::Value& tr = p::member(v, "trail");
    p::reject_unknown(tr, {"animation", "speed_divisor", "speed_base"});
    out.trail.animation = animation_ref(tr, "animation", names);
    out.trail.speed_divisor = p::integer_of(tr, "speed_divisor", 1, kBig);
    out.trail.speed_base = p::integer_of(tr, "speed_base", -kBig, kBig);

    const json::Value& b = p::member(v, "burn");
    p::reject_unknown(b, {"animation", "start_divisor", "sound", "volume", "tongue_sprite", "fade_time", "fade"});
    out.burn.animation = animation_ref(b, "animation", names);
    out.burn.start_divisor = p::integer_of(b, "start_divisor", 1, kBig);
    out.burn.sound = p::sound_of(b, "sound");
    out.burn.volume = p::integer_of(b, "volume", 0, 255);
    out.burn.tongue_sprite = sprite_ref(b, "tongue_sprite", ns);
    out.burn.fade_time = p::integer_of(b, "fade_time", -kBig, kBig);
    out.burn.fade = p::integer_of(b, "fade", -kBig, kBig);

    const json::Value& f = p::member(v, "fireball");
    p::reject_unknown(f, {"animation", "ceiling", "embers", "land_time", "burn_time", "burn_scale", "trail_sprite"});
    out.fireball.animation = animation_ref(f, "animation", names);
    out.fireball.ceiling = p::integer_of(f, "ceiling", -kBig, kBig);
    out.fireball.embers = p::integer_of(f, "embers", 0, 1024);
    out.fireball.land_time = p::integer_of(f, "land_time", -kBig, kBig);
    out.fireball.burn_time = p::integer_of(f, "burn_time", -kBig, kBig);
    out.fireball.burn_scale = p::integer_of(f, "burn_scale", -kBig, kBig);
    out.fireball.trail_sprite = sprite_ref(f, "trail_sprite", ns);

    const json::Value& fl = p::member(v, "flash");
    p::reject_unknown(fl, {"wait", "fade"});
    out.flash.wait = p::integer_of(fl, "wait", -kBig, kBig);
    out.flash.fade = p::integer_of(fl, "fade", -kBig, kBig);

    const json::Value& g = p::member(v, "glow");
    p::reject_unknown(g, {"animation", "alpha", "delay", "scale"});
    out.glow.animation = animation_ref(g, "animation", names);
    out.glow.alpha = p::integer_of(g, "alpha", 0, 65535);
    out.glow.delay = p::integer_of(g, "delay", -kBig, kBig);
    out.glow.scale = p::integer_of(g, "scale", -kBig, kBig);

    const json::Value& bu = p::member(v, "burst");
    p::reject_unknown(bu, {"animations", "alpha", "pick_divisor", "count_mask", "count_if_set", "count_if_clear",
                           "delays", "scales"});
    const json::Value& ba = p::member(bu, "animations");
    if (!ba.is_array() || ba.array().empty()) {
        p::fail(ba, "\"animations\" must be a non-empty array of animation names");
    }
    for (const json::Value& n : ba.array()) {
        if (!n.is_string() || names.find(n.string()) == names.end()) {
            p::fail(n, "no such animation in \"animations\"");
        }
        out.burst.animations.push_back(names.at(n.string()));
    }
    out.burst.alpha = p::integer_of(bu, "alpha", 0, 65535);
    out.burst.pick_divisor = p::integer_of(bu, "pick_divisor", 1, kBig);
    out.burst.count_mask = p::integer_of(bu, "count_mask", 0, 0x7FFF);
    out.burst.count_if_set = p::integer_of(bu, "count_if_set", 0, 1024);
    out.burst.count_if_clear = p::integer_of(bu, "count_if_clear", 0, 1024);
    out.burst.delays = int_list(bu, "delays");
    out.burst.scales = int_list(bu, "scales");

    const json::Value& c = p::member(v, "cue");
    p::reject_unknown(c, {"tilt", "tilt_speed", "area", "brightness", "brightness_top", "brightness_divisor",
                          "reseed_above"});
    out.cue.tilt = p::integer_of(c, "tilt", -kBig, kBig);
    out.cue.tilt_speed = p::integer_of(c, "tilt_speed", -kBig, kBig);
    out.cue.area = p::integer_of(c, "area", -kBig, kBig);
    out.cue.brightness = p::integer_of(c, "brightness", 0, 65535);
    out.cue.brightness_top = p::integer_of(c, "brightness_top", -kBig, kBig);
    out.cue.brightness_divisor = p::integer_of(c, "brightness_divisor", 1, kBig);
    out.cue.reseed_above = p::integer_of(c, "reseed_above", 0, 1 << 20);

    const json::Value& at = p::member(v, "attached");
    p::reject_unknown(at, {"animation", "start_divisor", "bone_first", "bone_last", "scale_divisor", "scale_base",
                           "alpha"});
    out.attached.animation = animation_ref(at, "animation", names);
    out.attached.start_divisor = p::integer_of(at, "start_divisor", 1, kBig);
    out.attached.bone_first = p::integer_of(at, "bone_first", 0, 255);
    out.attached.bone_last = p::integer_of(at, "bone_last", out.attached.bone_first, 255);
    out.attached.scale_divisor = p::integer_of(at, "scale_divisor", 1, kBig);
    out.attached.scale_base = p::integer_of(at, "scale_base", -kBig, kBig);
    out.attached.alpha = p::integer_of(at, "alpha", 0, 65535);

    // A random start step must stay inside its animation.
    auto starts_fit = [&](int anim, std::int32_t divisor, const json::Value& at_value) {
        if (static_cast<std::size_t>(0x7FFF / divisor) >= out.animations[static_cast<std::size_t>(anim)].steps.size()) {
            p::fail(at_value, "rand16() / start_divisor can pass the end of the animation");
        }
    };
    starts_fit(out.burn.animation, out.burn.start_divisor, b);
    starts_fit(out.attached.animation, out.attached.start_divisor, at);
    return out;
}

}  // namespace

Result<std::map<std::string, FirePoolParams>> parse_fire_pools(std::string_view json_text) try {
    using R = Result<std::map<std::string, FirePoolParams>>;
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
    std::map<std::string, FirePoolParams> out;
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
    return Result<std::map<std::string, FirePoolParams>>::fail(e.what());
}

Result<std::map<std::string, FirePoolParams>> load_fire_pools(const std::filesystem::path& gamedata_dir,
                                                             const std::filesystem::path& mods_dir,
                                                             std::string* source) {
    using R = Result<std::map<std::string, FirePoolParams>>;
    std::string where;
    Result<std::string> text =
        read_effect_file(gamedata_dir, mods_dir, std::filesystem::path("effects") / "fire_pools.json", &where);
    if (source != nullptr) {
        *source = where;
    }
    if (!text) {
        return R::fail(text.error());
    }
    R parsed = parse_fire_pools(text.value());
    if (!parsed) {
        return R::fail(where + ": " + parsed.error());
    }
    return parsed;
}

}  // namespace sh::effects
