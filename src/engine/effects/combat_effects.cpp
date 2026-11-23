// SPDX-License-Identifier: GPL-3.0-or-later
// The combat effects of the map's effect slot table (combat_effects.h).

#include "engine/effects/combat_effects.h"
#include "engine/effects/effect_view.h"

#include <algorithm>
#include <climits>

#include "engine/world/blood_color.h"
#include "engine/world/chunk_fx.h"

namespace sh::effects {

namespace {

constexpr int kTableSize = 2048;          // ordering table
constexpr std::uint8_t kQuadCode = 0x2C;  // textured quad
constexpr std::uint8_t kSemi = 0x02;      // its semi-transparency bit
constexpr std::int32_t kFarAway = 81920;  // 20 m: puddles beyond are not drawn
constexpr std::int32_t kPaletteRow = 0x13;

std::int32_t rand_below(SlotEffectHost& host, std::uint32_t n) {
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(host.rand16()) % n);
}

std::int32_t abs32(std::int32_t v) { return v < 0 ? -v : v; }

std::uint32_t color_word(std::uint32_t r, std::uint32_t g, std::uint32_t b, std::uint8_t code) {
    return (r & 0xFFu) | ((g & 0xFFu) << 8) | ((b & 0xFFu) << 16) | (static_cast<std::uint32_t>(code) << 24);
}

std::uint32_t color_word(const draw3d::Rgb& c, std::uint8_t code) { return color_word(c.r, c.g, c.b, code); }

// The quad's colour as the polygon keeps it (what the fades and caps below work on).
draw3d::Rgb& color_of(draw3d::Polygon& q) { return q.v[0].color; }

// c * keep / 256 per channel: a blood layer fading into the world fog.
void fog_fade(draw3d::Rgb& c, std::int32_t keep) {
    c.r = static_cast<std::uint8_t>((c.r * keep) >> 8);
    c.g = static_cast<std::uint8_t>((c.g * keep) >> 8);
    c.b = static_cast<std::uint8_t>((c.b * keep) >> 8);
}

// The additive blood layers are capped so a bright fog tint cannot blow their soft edge to white.
constexpr std::uint8_t kAdditiveCap = 0x80;
void cap_additive(draw3d::Rgb& c) {
    c.r = std::min(c.r, kAdditiveCap);
    c.g = std::min(c.g, kAdditiveCap);
    c.b = std::min(c.b, kAdditiveCap);
}

std::uint16_t palette_word(std::uint8_t color_slot) {
    return static_cast<std::uint16_t>((color_slot << 6) | kPaletteRow);
}

// A world point (Q12, y the slot's 16-bit word or a sum) relative to the view origin in geometry units.
Vec3s relative(const SlotView& view, std::int32_t x, std::int32_t y, std::int32_t z) {
    return Vec3s{static_cast<std::int16_t>((x >> 4) - view.origin.x), static_cast<std::int16_t>((y >> 4) - view.origin.y),
                 static_cast<std::int16_t>((z >> 4) - view.origin.z)};
}

// The four corners of a screen square around (x, y): top left, top right, bottom left, bottom right.
void square(std::int32_t x, std::int32_t y, std::int32_t half, std::uint32_t xy[4]) {
    xy[0] = xy_word(x - half, y - half);
    xy[1] = xy_word(x + half, y - half);
    xy[2] = xy_word(x - half, y + half);
    xy[3] = xy_word(x + half, y + half);
}

std::int32_t clamp_bucket(std::int32_t depth) { return std::clamp(depth >> 3, 0, kTableSize - 1); }

void follow_anchor(CombatEffectHost& host, EffectSlot& s, int anchor) {
    if (anchor == kNoAnchor || anchor >= kEffectAnchors) return;
    const AnchorMotion& m = host.motion();
    const auto i = static_cast<std::size_t>(anchor);
    s.x += m.x[i];
    s.y = static_cast<std::int16_t>(s.y + m.y[i]);
    s.z += m.z[i];
}

void release_anchor(CombatEffectHost& host, int anchor, std::uint8_t bit) {
    if (anchor == kNoAnchor || anchor >= kEffectAnchors) return;
    EffectAnchor a = host.anchor(anchor);
    if (a.refs & bit) {
        a.refs = static_cast<std::uint8_t>(a.refs - bit);
        host.store_anchor(anchor, a);
    }
}

bool special_or_below(const GroundSample g[4], std::int32_t y, std::int32_t limit) {
    for (int i = 0; i < 4; i++) {
        if (world::is_special_ground(g[i].type) || y - g[i].height < limit) return true;
    }
    return false;
}

}  // namespace

std::array<EffectAnchor, kEffectAnchors>& effect_anchors() {
    static std::array<EffectAnchor, kEffectAnchors> anchors{};
    return anchors;
}

// ---- spawning ------------------------------------------------------------------------------------------------------

void CombatEffects::spawn_blood(CombatEffectHost& host, const CombatEffectParams& params, const Bleeder& who,
                                const Vec3& at, std::int32_t strength, std::int32_t color) {
    const bool short_blood = host.blood_option() == kShortBloodOption;
    color = short_blood ? 5 : world::blood_color_slot(color, host.blood_option());

    BloodHit hit;
    if (strength >= 0 && static_cast<std::size_t>(strength) < params.blood_hits.size()) {
        hit = params.blood_hits[static_cast<std::size_t>(strength)];
    }
    std::int32_t splats = hit.splats_min;
    if (hit.splats_random) splats = rand_below(host, hit.splats_max - hit.splats_min + 1) + hit.splats_min;
    if (short_blood) splats = 0;

    // Strength 8 follows nothing, the rest takes a free anchor (none for a type whose blood does not follow it).
    int anchor = kNoAnchor;
    if (strength != 8) {
        anchor = 0;
        while (anchor < kEffectAnchors && host.anchor(anchor).refs != 0) anchor++;
        if (!who.follows_body) anchor = kNoAnchor;
        if (anchor != kNoAnchor) {
            EffectAnchor a = host.anchor(anchor);
            a.refs = 1;
            a.chara = who.chara;
            a.x = who.anchor.x;
            a.y = static_cast<std::int16_t>(who.anchor.y);
            a.z = who.anchor.z;
            host.store_anchor(anchor, a);
        }
    }

    for (std::int32_t n = 0; n < hit.sprays; n++) {
        const int slot = host.alloc(combat_kind::kBloodSpray);
        if (slot < 0) return;
        EffectSlot s = host.load(slot);
        if (strength != 9) {
            s.x = at.x + rand_below(host, 512) - 255;
            s.y = static_cast<std::int16_t>(at.y + rand_below(host, 512) - 255);
            s.z = at.z + rand_below(host, 512) - 255;
        } else {
            s.x = at.x;
            s.y = static_cast<std::int16_t>(at.y);
            s.z = at.z;
        }
        // The spray's texture cell: columns 0..2 of any row, or 3..4 of the first two.
        std::int32_t cell;
        if (rand_below(host, 2) != 0) {
            const std::int32_t column = rand_below(host, 3);
            cell = column + rand_below(host, 8) * 8;
        } else {
            const std::int32_t column = rand_below(host, 2) + 3;
            cell = column + rand_below(host, 2) * 8;
        }
        set_byte(s.a, 0, static_cast<std::uint32_t>(cell));
        set_byte(s.a, 1, static_cast<std::uint32_t>(hit.size));
        set_byte(s.a, 2, static_cast<std::uint32_t>(color));
        s.flags = static_cast<std::uint8_t>(anchor * 4);
        set_lo16(s.b, rand_below(host, 256));
        // The depth bias: the type's (a Floatstinger's blood sits further back), a strength 9 spray ages twice as fast.
        set_byte(s.a, 3, strength == 9 ? 33 : who.depth_bias);
        host.store(slot, s);
    }

    const int splat_count = host.blood_splat_count();
    // The engine reads no more splats than the map has (the original read past them).
    splats = std::min(splats, splat_count);
    if (splats <= 0) return;

    // The splats nearest the point two units ahead of the camera are reused first.
    Vec3 eye = host.camera_position();
    const std::array<std::int16_t, 2> forward = host.camera_forward();
    eye.x += forward[0] * 2;
    eye.z += forward[1] * 2;

    std::vector<std::int32_t> dist(static_cast<std::size_t>(splat_count));
    for (int j = 0; j < splat_count; j++) {
        const std::int32_t used = host.blood_splat_slot(j);
        if (used == -1) {
            dist[static_cast<std::size_t>(j)] = INT_MAX;
            continue;
        }
        EffectSlot u = host.load(used);
        if (u.flags != 2) {
            dist[static_cast<std::size_t>(j)] = 0;
            continue;
        }
        const std::int32_t dx = abs32(eye.x - u.x);
        const std::int32_t dz = abs32(eye.z - u.z);
        std::int32_t d = std::max(dx, dz) + (std::min(dx, dz) >> 1);
        if (d > 327680) {  // 80 m: the puddle is let go
            u.kind = 0;
            host.store(used, u);
            host.set_blood_splat(j, -1);
            d = INT_MAX;
        } else {
            d += rand_below(host, 16384);
        }
        dist[static_cast<std::size_t>(j)] = d;
    }

    // The farthest splats first (a partial selection, as the original sorted it).
    for (std::int32_t j = 0; j < splats; j++) {
        auto& dj = dist[static_cast<std::size_t>(j)];
        if (dj == INT_MAX) continue;
        for (int k = j + 1; k < splat_count; k++) {
            auto& dk = dist[static_cast<std::size_t>(k)];
            const std::int32_t cur = dj;
            if (dk <= cur) continue;
            dj = dk;
            dk = cur;
            const std::int32_t a = host.blood_splat_slot(j);
            host.set_blood_splat(j, host.blood_splat_slot(k));
            host.set_blood_splat(k, a);
            if (dj == INT_MAX) break;
        }
        // A splat still fresh (not yet a puddle) ends the reuse.
        if (dj == 0) {
            splats = j;
            break;
        }
    }

    std::int32_t landed = 0;
    for (std::int32_t j = 0; j < splats; j++) {
        const std::int32_t old = host.blood_splat_slot(j);
        if (old != -1) {
            EffectSlot u = host.load(old);
            u.flags = 3;  // the old puddle fades
            u.b = 0;
            host.store(old, u);
            host.set_blood_splat(j, -1);
        }

        const int slot = host.alloc(combat_kind::kBloodDrop);
        if (slot < 0) return;
        EffectSlot s = host.load(slot);
        s.flags = static_cast<std::uint8_t>(anchor * 4);
        s.x = at.x + rand_below(host, 2048) - 1023;
        s.y = static_cast<std::int16_t>(at.y + rand_below(host, 4096) - 2047);
        s.z = at.z + rand_below(host, 2048) - 1023;
        host.store(slot, s);

        const GroundSample g = host.surface(s.x, s.z);
        s = host.load(slot);
        if (world::is_special_ground(g.type) || g.height < at.y - 819) {  // 0.2 m below the hit: too far to fall
            s.kind = 0;
        } else {
            if (g.height < s.y) s.y = static_cast<std::int16_t>(g.height);
            set_lo16(s.b, -(host.rand16() & 0x800));
            set_hi16(s.b, g.height);
            set_byte(s.a, 0, static_cast<std::uint32_t>(rand_below(host, 16)));
            set_byte(s.a, 2, static_cast<std::uint32_t>(color));
            set_byte(s.a, 1, static_cast<std::uint32_t>(rand_below(host, 16) + 6));
            host.set_blood_splat(j, slot);
            landed++;
        }
        host.store(slot, s);
    }

    if (landed != 0 && anchor != kNoAnchor) {
        EffectAnchor a = host.anchor(anchor);
        a.refs = static_cast<std::uint8_t>(a.refs + 2);
        host.store_anchor(anchor, a);
    }
}

void CombatEffects::spawn_body_pool(CombatEffectHost& host, const CombatEffectParams& params, const Vec3& at,
                                    std::int16_t yaw, std::uint8_t npc, std::int32_t pose, std::int32_t color) {
    if (host.blood_option() == kShortBloodOption) return;
    color = world::blood_color_slot(color, host.blood_option());
    if (pose < 0 || static_cast<std::size_t>(pose) >= params.body_pools.size()) return;
    const BodyPool& pool = params.body_pools[static_cast<std::size_t>(pose)];

    // The patches are laid last first.
    for (std::size_t n = pool.patches.size(); n > 0; n--) {
        const int slot = host.alloc(combat_kind::kBodyPool);
        if (slot < 0) break;
        const PoolPatch& p = pool.patches[n - 1];
        const std::int32_t forward = host.rand16() % p.forward_range + p.forward_base;
        const std::int32_t side = host.rand16() % p.side_range + p.side_base;
        const std::int32_t sin = host.sin(yaw);
        const std::int32_t cos = host.cos(yaw);

        EffectSlot s = host.load(slot);
        s.x = at.x + mul12(forward, sin) - mul12(side, cos);
        s.y = static_cast<std::int16_t>(at.y);
        s.z = at.z + mul12(forward, cos) + mul12(side, sin);
        host.store(slot, s);

        const GroundSample g = host.surface(s.x, s.z);
        s = host.load(slot);
        if (abs32(g.height - at.y) > 614) {  // 0.15 m off the floor
            s.kind = 0;
        } else {
            set_hi16(s.a, pool.size + host.rand16() % (pool.size >> 2));
            set_byte(s.a, 1, static_cast<std::uint32_t>(color));
            s.flags = static_cast<std::uint8_t>(rand_below(host, 4) + 4);  // bit 2: waits 2.5 s before spreading
            set_byte(s.a, 0, npc);
            s.b = static_cast<std::uint32_t>(rand_below(host, 8192));
        }
        host.store(slot, s);
    }
}

void CombatEffects::spawn_wound_pool(CombatEffectHost& host, const Vec3& at, std::int16_t size, std::int32_t color,
                                     std::int32_t delay) {
    color = static_cast<std::int8_t>(world::blood_color_slot(color, host.blood_option()));
    const int slot = host.alloc(combat_kind::kWoundPool);
    if (slot < 0) return;
    EffectSlot s = host.load(slot);
    s.x = at.x;
    s.y = static_cast<std::int16_t>(at.y);
    s.z = at.z;
    set_hi16(s.a, size);
    set_byte(s.a, 1, static_cast<std::uint32_t>(color));
    s.flags = static_cast<std::uint8_t>(rand_below(host, 4));
    set_byte(s.a, 0, 6);
    s.b = static_cast<std::uint32_t>(delay * 20480);
    host.store(slot, s);
}

void CombatEffects::spawn_shot(CombatEffectHost& host, const Gun& gun, std::int16_t yaw, std::int16_t pitch) {
    const Vec3 muzzle = host.transform(host.bone_frame(gun.bones), gun.muzzle);

    int flash = -1;
    if (gun.flash) flash = host.alloc(static_cast<std::uint8_t>(combat_kind::kMuzzleFlash + *gun.flash));
    const std::int32_t puffs = std::min(gun.puffs, 5);
    std::array<int, 5> smoke{};
    for (std::int32_t i = 0; i < puffs; i++) {
        smoke[static_cast<std::size_t>(i)] = host.alloc(static_cast<std::uint8_t>(combat_kind::kGunSmoke + gun.smoke));
    }

    if (gun.flash) {
        // Without its flash the shot leaves no smoke either (the puffs' slots stay taken as they were).
        if (flash < 0) return;
        EffectSlot s = host.load(flash);
        if (gun.flash_angles) {
            set_lo16(s.a, (*gun.flash_angles)[0]);
            set_hi16(s.a, (*gun.flash_angles)[1]);
        } else {
            set_lo16(s.a, yaw);
            set_hi16(s.a, pitch);
        }
        s.flags = static_cast<std::uint8_t>(rand_below(host, 2));  // mirrored texture
        set_lo16(s.b, rand_below(host, 1024));                     // spin phase
        set_hi16(s.b, 0);                                          // age
        host.store(flash, s);
    }

    for (std::int32_t i = 0; i < puffs; i++) {
        const int slot = smoke[static_cast<std::size_t>(i)];
        if (slot < 0) break;
        EffectSlot s = host.load(slot);
        s.x = (muzzle.x << 4) + rand_below(host, 256) - 128;
        s.y = static_cast<std::int16_t>((muzzle.y << 4) + rand_below(host, 256) - 128);
        s.z = (muzzle.z << 4) + rand_below(host, 256) - 128;
        s.flags = static_cast<std::uint8_t>(i < 3 ? i : rand_below(host, 3));  // texture row
        set_lo16(s.a, rand_below(host, 4096));  // drift heading
        set_hi16(s.a, 0);                       // age
        set_lo16(s.b, 0);                       // clock
        host.store(slot, s);
    }
}

void CombatEffects::spawn_spark(CombatEffectHost& host, const Vec3& at, std::uint8_t variant, std::int16_t size) {
    const int slot = host.alloc(combat_kind::kHitSpark);
    if (slot < 0) return;
    EffectSlot s = host.load(slot);
    s.x = at.x >> 4;
    s.y = static_cast<std::int16_t>(at.y >> 4);
    s.z = at.z >> 4;
    set_byte(s.a, 0, variant);
    set_hi16(s.a, size);
    host.store(slot, s);
}

// ---- drawing ---------------------------------------------------------------------------------------------------------

bool CombatEffects::draw_spray(CombatEffectHost& host, int slot) {
    EffectSlot s = host.load(slot);
    const std::uint16_t age = static_cast<std::uint16_t>(lo16(s.b));
    const bool short_blood = host.blood_option() == kShortBloodOption;
    set_lo16(s.b, world::chunk_fx_clock_step(age, host.delta_time(), short_blood, byte_of(s.a, 3) == 33));

    const int anchor = s.flags >> 2;
    if (static_cast<std::uint16_t>(lo16(s.b)) >= world::kChunkFxClockKill) {
        s.kind = 0;
        host.store(slot, s);
        release_anchor(host, anchor, 1);
        return false;
    }
    follow_anchor(host, s, anchor);
    host.store(slot, s);

    const SlotView& view = host.view();
    const ScreenPoint p = host.project(relative(view, s.x, s.y, s.z));
    const std::int32_t bias = byte_of(s.a, 3);
    if (!world::chunk_fx_a_visible(p.x, p.y, p.depth, bias, kTableSize)) return false;

    const std::uint8_t cell = byte_of(s.a, 0);
    const world::ChunkFxHalfSize half =
        world::chunk_fx_a_half_size(p.depth, view.screen_distance, byte_of(s.a, 1), age, cell < 3);
    if (half.skip) return false;

    std::uint32_t xy[4];
    square(p.x, p.y, half.half, xy);

    const world::ChunkFxMasks m = world::chunk_fx_a_masks(cell);
    const std::uint32_t column = static_cast<std::uint32_t>(world::chunk_fx_a_uv_column(cell)) << 5;
    const std::uint32_t row = static_cast<std::uint32_t>(world::chunk_fx_a_uv_row(age)) << 5;
    const std::uint32_t page = short_blood ? 0x2B : 0x4B;  // additive or subtractive blend of the same page
    std::uint32_t uv[4];
    uv[1] = ((row + m.v1u) << 8) + (page << 16) + (column + m.v1v);
    uv[2] = ((row + m.v2u) << 8) + (column + m.v2v);
    uv[3] = ((row + m.v3u) << 8) + (column + m.v3v);

    const std::int32_t bucket = world::chunk_fx_bucket(p.depth - bias, kTableSize);
    const std::uint8_t code = kQuadCode | kSemi;

    if (host.fog_lit()) {
        uv[0] = ((row + m.v0u) << 8) + (0x93u << 16) + (column + m.v0v);
        if (short_blood) {
            const FogColors fc = host.fog_colors(0x40, p.depth * 16);
            const draw3d::Polygon glow = textured_quad(color_word(fc.fog, code), xy, uv);
            draw3d::Polygon tint = glow;
            color_of(tint) = {fc.tint.r, static_cast<std::uint8_t>(fc.tint.g >> 4),
                              static_cast<std::uint8_t>(fc.tint.b >> 4)};
            tint.palette = draw3d::Palette::from_word(palette_word(byte_of(s.a, 2)));
            tint.shade = {false, false};
            host.emit_quad(glow, bucket, true);
            host.emit_quad(tint, bucket, true);
            return true;
        }

        // Two additive fog glows, the red subtractive layer between them and a subtractive glow that cancels the
        // two exactly (their sum), so whatever the caps and fades did the glow leaves no haze.
        const FogColors fc = host.fog_colors(static_cast<std::uint8_t>(world::chunk_fx_a_fog_gray(age)), p.depth * 16);
        const draw3d::Polygon base = textured_quad(color_word(fc.fog, code), xy, uv);
        draw3d::Polygon layers[4] = {base, base, base, base};
        const std::int32_t keep = host.fog_keep(p.depth);
        color_of(layers[1]) = fc.tint;
        fog_fade(color_of(layers[1]), keep);
        fog_fade(color_of(layers[2]), keep);
        layers[0].page = draw3d::TexturePage::from_word(43);
        layers[1].palette = draw3d::Palette::from_word(palette_word(byte_of(s.a, 2)));
        layers[3].page = draw3d::TexturePage::from_word(43);
        for (int i : {0, 3}) {
            cap_additive(color_of(layers[i]));
            fog_fade(color_of(layers[i]), keep);
        }
        draw3d::Rgb& cancel = color_of(layers[2]);
        cancel.r = static_cast<std::uint8_t>(std::min(color_of(layers[0]).r + color_of(layers[3]).r, 255));
        cancel.g = static_cast<std::uint8_t>(std::min(color_of(layers[0]).g + color_of(layers[3]).g, 255));
        cancel.b = static_cast<std::uint8_t>(std::min(color_of(layers[0]).b + color_of(layers[3]).b, 255));
        for (const draw3d::Polygon& q : layers) host.emit_quad(q, bucket, true);
        return true;
    }

    std::uint32_t rgb;
    if (short_blood) {
        const std::uint8_t r = static_cast<std::uint8_t>(host.darkness(s.x, s.y, s.z));
        const std::uint8_t g = static_cast<std::uint8_t>(host.darkness(s.x, s.y, s.z));
        const std::uint8_t b = static_cast<std::uint8_t>(host.darkness(s.x, s.y, s.z));
        rgb = color_word(r, g, b, code);
    } else {
        const std::uint32_t gray = static_cast<std::uint32_t>(world::chunk_fx_a_fog_gray(age));
        rgb = color_word(gray, gray, gray, code);
    }
    uv[0] = (static_cast<std::uint32_t>(palette_word(byte_of(s.a, 2))) << 16) + ((row + m.v0v) << 8) +
            (column + m.v0u);
    host.emit_quad(textured_quad(rgb, xy, uv), bucket, true);
    return true;
}

namespace {

// A gray colour as the original stored it: the level in red, the level plus its high byte in green (a 16-bit
// store of level + level * 256), its low byte in blue.
std::uint32_t gray_word(std::int32_t level, std::uint8_t code) {
    const std::uint32_t l = static_cast<std::uint32_t>(level);
    return ((l + (l << 8)) & 0xFFFFu) | ((l & 0xFFu) << 16) | (static_cast<std::uint32_t>(code) << 24);
}

// The splat's texture corners by its 3-bit orientation (flips and quarter turns of the 32x32 cell).
struct SplatCorners {
    std::uint32_t u[4];
    std::uint32_t v[4];
};

SplatCorners splat_corners(std::uint32_t turn) {
    const bool b0 = (turn & 1) != 0;
    const bool b1 = (turn & 2) != 0;
    const bool flipped = (turn & 4) != 0;
    // Each corner is the far edge (31) or the near one (0) of the cell.
    const auto pick = [&](bool when_flipped, bool when_not) -> std::uint32_t { return (flipped ? when_flipped : when_not) ? 31 : 0; };
    SplatCorners c;
    c.u[0] = pick(!(b0 != b1), b0 != b1);
    c.v[0] = flipped ? 31 : 0;
    c.u[1] = pick(b0, !b0);
    c.v[1] = pick(!b1, b1);
    c.u[2] = pick(!b0, b0);
    c.v[2] = pick(b1, !b1);
    c.u[3] = pick(b0 != b1, !(b0 != b1));
    c.v[3] = flipped ? 0 : 31;
    return c;
}

}  // namespace

bool CombatEffects::draw_drop(CombatEffectHost& host, int slot) {
    EffectSlot s = host.load(slot);
    const std::int32_t dt = host.delta_time();

    if ((s.flags & 3) == 0) {
        // Falling: follows its character, gains speed, splats on the ground it was dropped over.
        const int anchor = s.flags >> 2;
        follow_anchor(host, s, anchor);
        set_lo16(s.b, std::min(lo16(s.b) + (host.gravity_step() >> 2), 0x2000));
        s.y = static_cast<std::int16_t>(s.y + lo16(s.b));
        if (s.y > hi16(s.b)) {
            release_anchor(host, anchor, 2);
            s.y = hi16(s.b);
            host.store(slot, s);
            const std::int32_t r = byte_of(s.a, 1) << 5;
            GroundSample g[4];
            g[0] = host.surface(s.x + r, s.z);
            g[1] = host.surface(s.x - r, s.z);
            g[2] = host.surface(s.x, s.z + r);
            g[3] = host.surface(s.x, s.z - r);
            s = host.load(slot);
            if (special_or_below(g, s.y, -819)) {  // an edge or a step under the splat
                s.kind = 0;
            } else {
                s.flags = 1;
                s.b = static_cast<std::uint32_t>(rand_below(host, 256));
            }
            host.store(slot, s);
            host.restore_view();
            return true;
        }
        host.store(slot, s);
        return true;
    }

    // The timer word is unsigned; the clock read from it is not.
    std::int32_t clock = static_cast<std::int32_t>(s.b);
    if (s.flags == 2) {
        // A puddle grows on while it rains.
        if (host.raining()) {
            s.b = std::min(s.b + static_cast<std::uint32_t>(dt), 204800u);
            clock = 0xCC0;
        }
    } else {
        s.b += static_cast<std::uint32_t>(dt);
        if (s.b > 0xCC0u) {
            if (s.flags == 1) {
                s.b = 0xCC0;
                s.flags = 2;
                set_byte(s.a, 3, 0x80);
            } else {
                s.kind = 0;
                host.store(slot, s);
                return false;
            }
        }
    }
    host.store(slot, s);

    const Vec3 eye = host.camera_position();
    if (abs32(eye.x - s.x) + abs32(eye.z - s.z) > kFarAway) {
        if (s.flags == 3) {
            s.kind = 0;
            host.store(slot, s);
        }
        return false;
    }

    const std::int32_t size = byte_of(s.a, 1);
    std::int32_t r;
    if ((s.flags & 3) < 3) {
        r = std::min((clock * (size * 16)) / 816, size << 5);
    } else {
        r = size << 5;
    }

    const SlotView& view = host.view();
    const Vec3s corners[4] = {relative(view, s.x - r, s.y, s.z + r), relative(view, s.x + r, s.y, s.z + r),
                              relative(view, s.x - r, s.y, s.z - r), relative(view, s.x + r, s.y, s.z - r)};
    ScreenPoint p[4];
    host.project_quad(corners, p);
    const std::int32_t depth = (p[0].depth + p[1].depth + p[2].depth + p[3].depth) >> 2;
    if (depth - 8 <= 0 || ((depth - 8) >> 3) >= kTableSize) return false;
    if (abs32(p[3].x) > 200 || abs32(p[3].y) > 160) return false;

    const std::uint32_t column = (byte_of(s.a, 0) & 8) ? 0 : 32;
    const std::uint32_t row = s.flags == 3 ? 224 : static_cast<std::uint32_t>(std::min(clock / 408, 7) << 5);
    const SplatCorners c = splat_corners(byte_of(s.a, 0) & 7);

    std::int32_t level;
    if (s.flags == 1) {
        level = std::max(255 - (clock * 16) / 204, 0x80);
    } else if (s.flags == 2) {
        if (host.raining()) {
            // A growing puddle thins out as it spreads.
            const std::uint32_t t = 0x80u - (((s.b - 0xCC0u) << 7) / 204800u);
            const std::uint32_t a = t < 0x20u ? 0x20u : t;
            set_byte(s.a, 3, a);
            host.store(slot, s);
            level = static_cast<std::int32_t>(a & 0xFF);
        } else {
            level = byte_of(s.a, 3);
        }
    } else {
        level = std::max(byte_of(s.a, 3) - (clock * 8) / 204, 0);
    }

    std::uint32_t xy[4];
    for (int i = 0; i < 4; i++) xy[i] = xy_word(p[i].x, p[i].y);
    std::uint32_t uv[4];
    uv[1] = ((row + c.v[1]) << 8) + (0x4Bu << 16) + (column + c.u[1]);
    uv[2] = ((row + c.v[2]) << 8) + (column + c.u[2]);
    uv[3] = ((row + c.v[3]) << 8) + (column + c.u[3]);
    const std::uint8_t code = kQuadCode | kSemi;

    draw3d::Polygon q;
    if (host.fog_lit()) {
        uv[0] = ((row + c.v[0]) << 8) + (0x93u << 16) + (column + c.u[0]);
        const FogColors fc = host.fog_colors(static_cast<std::uint8_t>(level), depth * 16);
        q = textured_quad(color_word(fc.fog, code), xy, uv);
        // Subtractive with the cyan blood palette leaves red on any floor.
        q.page = draw3d::TexturePage::from_word(0x4B);
        q.palette = draw3d::Palette::from_word(palette_word(byte_of(s.a, 2)));
    } else {
        uv[0] = (static_cast<std::uint32_t>(palette_word(byte_of(s.a, 2))) << 16) + ((row + c.v[0]) << 8) +
                (column + c.u[0]);
        q = textured_quad(gray_word(level, code), xy, uv);
    }
    fog_fade(color_of(q), host.fog_keep(depth));
    host.emit_quad(q, clamp_bucket(depth), true);
    return true;
}

bool CombatEffects::draw_pool(CombatEffectHost& host, int slot) {
    EffectSlot s = host.load(slot);
    const std::int32_t dt = host.delta_time();

    std::int32_t bias = 256;
    if (s.kind == combat_kind::kBodyPool) {
        bias = 12;
        // The body went away with its character.
        if (!host.npc_present(byte_of(s.a, 0))) {
            s.kind = 0;
            host.store(slot, s);
            return false;
        }
    }

    if (s.flags & 4) {
        // Waiting before it starts to spread.
        s.b += static_cast<std::uint32_t>(dt);
        if (s.b > 10240u) {
            s.flags = static_cast<std::uint8_t>(s.flags - 4);
            s.b = 0;
        }
        host.store(slot, s);
        return false;
    }

    // The pool's timer is unsigned.
    const std::uint32_t clock = s.b;
    if (host.raining()) {
        s.b = std::min(clock + static_cast<std::uint32_t>(dt), 184320u);
    } else if (clock < 20480u) {
        s.b = clock + static_cast<std::uint32_t>(dt);
    }
    host.store(slot, s);

    const Vec3 eye = host.camera_position();
    if (abs32(eye.x - s.x) + abs32(eye.z - s.z) > kFarAway) return false;

    const std::uint16_t full = static_cast<std::uint16_t>(hi16(s.a));
    std::int32_t size;
    if (clock >= 20480u) {
        size = full;
    } else {
        size = static_cast<std::int32_t>((clock * full) / 20480u);
        const std::int32_t r = (size >> 1) * 16;
        GroundSample g[4];
        if (rand_below(host, 2) != 0) {
            g[0] = host.surface(s.x - r, s.z);
            g[1] = host.surface(s.x + r, s.z);
            g[2] = host.surface(s.x, s.z - r);
            g[3] = host.surface(s.x, s.z + r);
        } else {
            g[0] = host.surface(s.x - r, s.z - r);
            g[1] = host.surface(s.x + r, s.z - r);
            g[2] = host.surface(s.x - r, s.z + r);
            g[3] = host.surface(s.x + r, s.z + r);
        }
        s = host.load(slot);
        bool stop = false;
        for (const GroundSample& gs : g) {
            if (world::is_special_ground(gs.type) || abs32(gs.height - s.y) > 614) stop = true;
        }
        // The pool stops where the floor ends or steps.
        if (stop) {
            set_hi16(s.a, size);
            s.b = 20480;
            host.store(slot, s);
        }
        host.restore_view();
    }

    // A 5x5 grid of points, 4x4 cells of the pool texture.
    const SlotView& view = host.view();
    ScreenPoint grid[25];
    for (int i = 0; i < 5; i++) {
        for (int j = 0; j < 5; j++) {
            const Vec3s at{static_cast<std::int16_t>((s.x >> 4) - view.origin.x - size + (size >> 1) * j),
                           static_cast<std::int16_t>((s.y >> 4) - view.origin.y),
                           static_cast<std::int16_t>((s.z >> 4) - view.origin.z - size + (size >> 1) * i)};
            grid[i * 5 + j] = host.project(at);
        }
    }

    std::int32_t level = 160;
    if (clock > 20480u) {
        const std::uint32_t l = 160u - ((clock - 20480u) >> 10);
        level = l >= 32 ? static_cast<std::int32_t>(l) : 32;
    }
    const std::uint32_t base = (static_cast<std::uint32_t>(s.flags) << 13) + 224;
    const std::uint8_t code = kQuadCode | kSemi;

    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            const int k = i * 5 + j;
            // Only drawing follows: the box may follow the frame (effect_view.h).
            if (abs32(grid[k].x) > screen_box_half_width() || abs32(grid[k].y) > 160) continue;
            const std::int32_t depth = (grid[k].depth + grid[k + 1].depth + grid[k + 5].depth + grid[k + 6].depth) >> 2;
            if (depth <= 0 || (depth >> 3) >= kTableSize) continue;

            const std::uint32_t xy[4] = {xy_word(grid[k].x, grid[k].y), xy_word(grid[k + 1].x, grid[k + 1].y),
                                         xy_word(grid[k + 5].x, grid[k + 5].y), xy_word(grid[k + 6].x, grid[k + 6].y)};
            const std::uint32_t texel = static_cast<std::uint32_t>(j * 8) + (static_cast<std::uint32_t>(i) << 11) + base;
            const std::uint32_t uv[4] = {(static_cast<std::uint32_t>(palette_word(byte_of(s.a, 1))) << 16) + texel,
                                         texel + 0x4B0007, texel + 0x700, texel + 0x707};
            const std::int32_t bucket = std::clamp((depth + bias) >> 3, 0, kTableSize - 1);

            if (host.fog_lit()) {
                // One additive fog glow over two subtractive layers: the red one and the glow's cancel.
                const FogColors fc = host.fog_colors(static_cast<std::uint8_t>(level), depth * 16);
                const draw3d::Polygon base_quad = textured_quad(color_word(fc.fog, code), xy, uv);
                draw3d::Polygon layers[3] = {base_quad, base_quad, base_quad};
                const std::int32_t keep = host.fog_keep(depth);
                layers[0].page = draw3d::TexturePage::from_word(43);
                layers[0].palette = draw3d::Palette::from_word(147);
                layers[2].palette = draw3d::Palette::from_word(147);
                color_of(layers[1]) = fc.tint;
                fog_fade(color_of(layers[1]), keep);
                fog_fade(color_of(layers[2]), keep);
                cap_additive(color_of(layers[0]));
                fog_fade(color_of(layers[0]), keep);
                for (const draw3d::Polygon& q : layers) host.emit_quad(q, bucket, false);
            } else {
                host.emit_quad(textured_quad(gray_word(level, code), xy, uv), bucket, false);
            }
        }
    }
    return true;
}

bool CombatEffects::draw_flash(CombatEffectHost& host, const CombatEffectParams& params, int slot) {
    EffectSlot s = host.load(slot);
    const int index = s.kind - combat_kind::kMuzzleFlash;
    if (index < 0 || static_cast<std::size_t>(index) >= params.flashes.size()) return false;
    const MuzzleFlash& f = params.flashes[static_cast<std::size_t>(index)];
    const math::Transform bone = host.bone_frame(f.bones);

    const std::int32_t age = hi16(s.b);
    if (age == 0) {
        host.flash_light();
    } else if (f.life < hi16(s.b)) {
        s.kind = 0;
        host.store(slot, s);
        return false;
    }
    set_hi16(s.b, hi16(s.b) + host.delta_time());

    const Vec3 muzzle = host.transform(bone, f.muzzle);
    host.restore_view();
    host.flash_shown(slot, index, muzzle, age, f.life);
    s.x = muzzle.x << 4;
    s.y = static_cast<std::int16_t>(muzzle.y << 4);
    s.z = muzzle.z << 4;
    host.store(slot, s);

    const SlotView& view = host.view();
    const std::uint32_t rgb = host.fog_lit() ? 0x2E103030u : 0x2E406060u;
    // Two mirror images of the flash texture.
    const std::uint32_t uv[4] = {s.flags ? 0xD380FFu : 0xD380E0u, s.flags ? 0x2B80E0u : 0x2B80FFu,
                                 s.flags ? 0x9FFFu : 0x9FE0u, s.flags ? 0x9FE0u : 0x9FFFu};

    // Six blades spinning around the barrel.
    for (int k = 0; k < 6; k++) {
        const std::int16_t phase = static_cast<std::int16_t>(static_cast<std::int32_t>(s.b) + (4096 * k) / 6);
        const Vec3s angles{
            static_cast<std::int16_t>(static_cast<std::uint16_t>(lo16(s.a)) + mul12(f.swirl, host.cos(phase)) - 1024),
            static_cast<std::int16_t>(hi16(s.a) + mul12(f.swirl, host.sin(phase))), phase};
        const math::Matrix3 turn = math::rot_matrix_zxy_neg(math::Vec3s{angles.x, angles.y, angles.z});
        Vec3s corners[4];
        for (int c = 0; c < 4; c++) {
            const math::Vec3s local{static_cast<std::int16_t>((c & 1) ? f.half_width : -f.half_width), 0,
                                   static_cast<std::int16_t>(c < 2 ? f.length : 0)};
            const math::Vec3i r = math::matrix_vector(turn, local, math::Offset::Translation, {}).full;
            corners[c] = relative(view, s.x + r.x, s.y + r.y, s.z + r.z);
        }
        ScreenPoint p[4];
        host.project_quad(corners, p);
        const std::int32_t depth = (p[0].depth + p[1].depth + p[2].depth + p[3].depth) >> 2;
        if (depth <= 0 || (depth >> 3) >= kTableSize || abs32(p[3].x) > 200 || abs32(p[3].y) > 160) return false;

        std::uint32_t xy[4];
        for (int i = 0; i < 4; i++) xy[i] = xy_word(p[i].x, p[i].y);
        host.emit_quad(textured_quad(rgb, xy, uv), clamp_bucket(depth), true);
    }
    return true;
}

bool CombatEffects::draw_smoke(CombatEffectHost& host, const CombatEffectParams& params, int slot) {
    EffectSlot s = host.load(slot);
    const std::int32_t step = mul12_wide(host.delta_time(), rand_below(host, 1638) + 3276);  // 0.8..1.2 of the frame

    GunSmoke smoke;
    const int index = s.kind - combat_kind::kGunSmoke;
    if (index >= 0 && static_cast<std::size_t>(index) < params.smoke.size()) {
        smoke = params.smoke[static_cast<std::size_t>(index)];
    }

    const std::uint16_t age = static_cast<std::uint16_t>(hi16(s.a));
    const std::array<std::int32_t, 2> wind = host.wind();
    const std::int32_t wx = wind[0] >> 6;
    const std::int32_t wz = wind[1] >> 6;
    // The wind makes the puff age faster.
    set_hi16(s.a, hi16(s.a) + step + (((host.sqrt(wx * wx + wz * wz) << 6) * step) >> 10));
    if (smoke.life < static_cast<std::uint16_t>(hi16(s.a))) s.kind = 0;

    const std::uint16_t clock = static_cast<std::uint16_t>(s.b);
    set_lo16(s.b, lo16(s.b) + step);
    host.store(slot, s);

    const SlotView& view = host.view();
    const ScreenPoint p = host.project(relative(view, s.x, s.y, s.z));
    if (p.depth <= 0 || (p.depth >> 3) >= kTableSize || abs32(p.x) > 200 || abs32(p.y) > 160) return false;

    const std::int32_t half = (((clock >> 6) + 16) * view.screen_distance) / p.depth;
    std::uint32_t xy[4];
    square(p.x, p.y, half, xy);
    const std::uint32_t row = (static_cast<std::uint32_t>(s.flags) << 5) + 160;
    const std::uint32_t uv[4] = {(row << 8) + 0x011300E0u, (row << 8) + 0x2B00FFu,
                                 (((row + 31) << 8) + 0xE0u) & 0xFFFFu, (((row + 31) << 8) + 0xFFu) & 0xFFFFu};

    std::uint8_t level;
    if (age < (smoke.life >> 3)) {
        level = static_cast<std::uint8_t>(std::min(((age << 9) / smoke.life) + 48, 128));
    } else {
        level = static_cast<std::uint8_t>(std::max(120 - (age * 120) / smoke.life, 0));
    }

    draw3d::Rgb c = host.faded_tint(level);
    if (host.fog_lit()) {
        // Dimmed so the single quad reads as a fading puff rather than an opaque one.
        c = {static_cast<std::uint8_t>((c.r * 5) >> 3), static_cast<std::uint8_t>((c.g * 5) >> 3),
             static_cast<std::uint8_t>((c.b * 5) >> 3)};
    } else {
        fog_fade(c, host.fog_keep(p.depth));
    }
    host.emit_quad(textured_quad(color_word(c, kQuadCode | kSemi), xy, uv), clamp_bucket(p.depth), false);

    if (smoke.drift_after < clock) {
        // Drifts along its heading and with the wind (less in the rain, more in the snow).
        s = host.load(slot);
        const std::int32_t sin = host.sin(lo16(s.a));
        const std::int32_t along = mul12_wide(step, 409);
        s.x += mul12(along, sin);
        s.z += mul12(along, host.cos(lo16(s.a)));
        if (host.raining()) {
            s.x += ((wind[0] * step) / 136) >> 2;
            s.z += ((wind[1] * step) / 136) >> 2;
        } else if (host.snowing()) {
            s.x += ((wind[0] * step) / 136) >> 1;
            s.z += ((wind[1] * step) / 136) >> 1;
        } else {
            s.x += (((wind[0] * step) / 136) * 2) / 3;
            s.z += (((wind[1] * step) / 136) * 2) / 3;
        }
        host.store(slot, s);
    }
    return true;
}

bool CombatEffects::draw_spark(CombatEffectHost& host, int slot) {
    EffectSlot s = host.load(slot);
    s.kind = 0;  // one frame only
    host.store(slot, s);

    const SlotView& view = host.view();
    const Vec3s at{static_cast<std::int16_t>(s.x - view.origin.x), static_cast<std::int16_t>(s.y - view.origin.y),
                   static_cast<std::int16_t>(s.z - view.origin.z)};
    const ScreenPoint p = host.project(at);
    if (!world::chunk_fx_f_visible(p.x, p.y)) return false;
    const world::ChunkFxSparkSize size = world::chunk_fx_f_half_size(hi16(s.a), view.screen_distance, p.depth);
    if (size.skip) return false;

    // Upside down: the first two corners are the bottom ones.
    const std::uint32_t xy[4] = {xy_word(p.x - size.half, p.y + size.half), xy_word(p.x + size.half, p.y + size.half),
                                 xy_word(p.x - size.half, p.y - size.half), xy_word(p.x + size.half, p.y - size.half)};
    const std::uint32_t uv[4] = {0x018C0000u, 0x2C003Fu, 0x3F00u, 0x3F3Fu};
    host.emit_quad(textured_quad(color_word(0x20, 0x10, 0x10, kQuadCode | kSemi), xy, uv),
                   world::chunk_fx_bucket(p.depth - 0x20, kTableSize), false);
    return true;
}

}  // namespace sh::effects
