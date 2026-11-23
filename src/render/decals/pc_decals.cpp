// SPDX-License-Identifier: GPL-3.0-or-later
// Bullet-hole decals -- see pc_decals.h.
//
// A mark is a quad lying on the surface it hit, projected with the view like the rest of the scene and carrying its
// corners' own view depth: the renderer tests it against the scene's depth buffer, so a wall, a corner or a prop in
// front of the mark hides it. The map is written a little farther than it stands (RENDER.md section 5), so a mark on
// a wall wins against that wall without a bias of its own.

#include "render/decals/pc_decals.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <optional>
#include <vector>

#include "engine/config/pc_config.h"
#include "engine/effects/drawer_projection.h"
#include "engine/game_state/game_state.h"
#include "engine/log/sh_log.h"
#include "engine/math/geometry_state.h"
#include "engine/math/view_projection.h"
#include "engine/world/world_atmosphere.h"
#include "render/decals/bullet_hole.h"
#include "render/draw3d/draw3d.h"
#include "render/game_view.h"
#include "render/scene/surface_query.h"
#include "render/textures/replacements.h"

namespace sh::render {
namespace {

constexpr int kMaxDecals = 64;
// Fixed-point constants as the game's Q12() makes them (float product, truncated).
constexpr std::int32_t q12(float x) { return static_cast<std::int32_t>(x * 4096.0f); }
constexpr float kHalf = 0.075f * 4096.0f;         // nominal quad half-extent (7.5 cm: the hole, its chips, the soot)
constexpr std::int32_t kLift = q12(0.004f);       // off the surface, so the mark's corners stay in front of it
// Where the drawn surface is looked for along the shot, around the collision's hit point (scene units, 256 per
// metre), and how far around the found point its triangles are collected (more than the largest mark's half-diagonal).
constexpr double kSearchBefore = 128.0;
constexpr double kSearchPast = 256.0;
constexpr double kPatchRadius = 48.0;
constexpr double kEdgeSlack = 0.3;     // a sample this far outside every triangle is still on the surface
constexpr double kMinPartArea = 1.0;   // slivers of a clipped mark are not worth a polygon
constexpr std::size_t kMaxPolygons = 32;
// Ordering-table scale of the characters, and one bucket nearer than the host wall: among the translucent
// polygons of the frame the mark is drawn after what stands behind it.
constexpr int kOtShift = 1;
constexpr int kOtLift = 1;
constexpr std::uint32_t kSheetSeed = 0x5348B011u;

// The bullet holes' sheet among the replacement pictures (render/textures/replacement_store.h). UVs are in the
// sheet's texel space, which the store maps over the whole picture: a cell of the sheet is kCellUv texels wide. The
// palette word names the sheet.
constexpr int kSheet = sh::textures::kBulletHoleSheet;
constexpr int kCellUv = 32;
constexpr int kNative = kCellUv * kBulletHoleGrid;
constexpr std::uint16_t kPalette = sh::textures::ReplacementStore::sheet_palette(kSheet);

constexpr std::int32_t mul_q12(std::int32_t a, std::int32_t b) { return (a * b) >> 12; }

// A corner of a mark: its place from the centre (Q8, the units of the view matrix's translation) and its texel in the
// hole's cell of the sheet (0..kCellUv).
struct DecalCorner {
    std::int16_t x = 0, y = 0, z = 0;
    std::uint8_t u = 0, v = 0;
};

// A quad (corners TL, TR, BL, BR) or a triangle of the mark.
struct DecalPolygon {
    std::uint8_t count = 4;
    std::array<DecalCorner, 4> corner{};
};

struct Decal {
    DecalPoint center;  // world, already lifted off the surface
    DecalPoint normal;  // unit (Q12), out of the surface
    std::uint8_t variant = 0;
    // The whole mark as one quad, or the parts of it that lie on its surface when the surface ends under it.
    std::vector<DecalPolygon> polygons;
};

using Vec2 = std::array<double, 2>;

double cross2(const Vec2& a, const Vec2& b, const Vec2& c) {
    return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]);
}

bool inside(const std::array<Vec2, 3>& t, const Vec2& p, double slack) {
    const double area = cross2(t[0], t[1], t[2]);
    const double sign = area < 0.0 ? -1.0 : 1.0;
    for (std::size_t k = 0; k < 3; ++k) {
        const Vec2& a = t[k];
        const Vec2& b = t[(k + 1) % 3];
        const double length = std::hypot(b[0] - a[0], b[1] - a[1]);
        if (length < 1e-9 || sign * cross2(a, b, p) < -slack * length) {
            return false;
        }
    }
    return true;
}

// `polygon` (counter-clockwise) cut to triangle `t`.
std::vector<Vec2> clipped(std::vector<Vec2> polygon, std::array<Vec2, 3> t) {
    if (cross2(t[0], t[1], t[2]) < 0.0) {
        std::swap(t[1], t[2]);
    }
    for (std::size_t k = 0; k < 3 && !polygon.empty(); ++k) {
        const Vec2& a = t[k];
        const Vec2& b = t[(k + 1) % 3];
        std::vector<Vec2> out;
        for (std::size_t i = 0; i < polygon.size(); ++i) {
            const Vec2& p = polygon[i];
            const Vec2& q = polygon[(i + 1) % polygon.size()];
            const double sp = cross2(a, b, p);
            const double sq = cross2(a, b, q);
            if (sp >= 0.0) {
                out.push_back(p);
            }
            if ((sp >= 0.0) != (sq >= 0.0)) {
                const double f = sp / (sp - sq);
                out.push_back({p[0] + (q[0] - p[0]) * f, p[1] + (q[1] - p[1]) * f});
            }
        }
        polygon = std::move(out);
    }
    return polygon;
}

class BulletDecals {
public:
    void add(const DecalPoint& hit, const DecalPoint& direction, const DecalPoint& origin);
    void draw();
    void reset() {
        count_ = 0;
        head_ = 0;
        registered_ = false;
    }

private:
    bool ensure_texture();

    std::array<Decal, kMaxDecals> decals_{};
    int count_ = 0;
    int head_ = 0;  // the next slot written (oldest first overwritten)
    std::vector<std::uint8_t> sheet_;  // kept for registering again after pool resets
    bool registered_ = false;
};

BulletDecals& decals() {
    static BulletDecals d;
    return d;
}

void BulletDecals::add(const DecalPoint& hit, const DecalPoint& direction, const DecalPoint& origin) {
    if (!g_PcConfig.bulletDecals) {
        return;
    }
    // The collision stopped the bullet at `hit`; the mark belongs to what is drawn there. The drawn surface is
    // looked for along the shot around that point: collision shapes are simpler than the walls they stand for.
    const double q8 = 1.0 / 16.0;  // Q12 to the scene's units (256 per metre)
    const sh::scene::SurfaceVec at = {hit.x * q8, hit.y * q8, hit.z * q8};
    sh::scene::SurfaceVec dir = {static_cast<double>(direction.x), static_cast<double>(direction.y),
                                 static_cast<double>(direction.z)};
    const double length = std::sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
    if (length < 1.0) {
        return;
    }
    const sh::scene::SurfaceVec flown = {at[0] - origin.x * q8, at[1] - origin.y * q8, at[2] - origin.z * q8};
    const double before = std::min(kSearchBefore, std::sqrt(flown[0] * flown[0] + flown[1] * flown[1] + flown[2] * flown[2]));
    sh::scene::SurfaceVec from, to;
    for (std::size_t k = 0; k < 3; ++k) {
        dir[k] /= length;
        from[k] = at[k] - dir[k] * before;
        to[k] = at[k] + dir[k] * kSearchPast;
    }
    // Nothing solid there (the bullet met a hole of a fence's or a grate's texture, glass, or a wall that is not
    // drawn): no mark.
    const std::optional<sh::scene::SurfaceHit> surface = sh::scene::solid_surface_on_segment(from, to, kPatchRadius);
    if (!surface) {
        return;
    }
    const sh::scene::SurfaceVec& n = surface->normal;
    const DecalPoint point = {static_cast<std::int32_t>(std::lround(surface->point[0] * 16.0)),
                              static_cast<std::int32_t>(std::lround(surface->point[1] * 16.0)),
                              static_cast<std::int32_t>(std::lround(surface->point[2] * 16.0))};
    Decal d;
    d.normal = {static_cast<std::int32_t>(n[0] * 4096.0), static_cast<std::int32_t>(n[1] * 4096.0),
                static_cast<std::int32_t>(n[2] * 4096.0)};
    d.center = {point.x + mul_q12(d.normal.x, kLift), point.y + mul_q12(d.normal.y, kLift),
                point.z + mul_q12(d.normal.z, kLift)};
    // The surface's two unit directions: along the wall and across it; a floor's are the world's X and Z.
    sh::scene::SurfaceVec eu = {1.0, 0.0, 0.0};
    if (std::abs(n[1]) < 0.99) {
        const double flat = std::sqrt(n[0] * n[0] + n[2] * n[2]);
        eu = {n[2] / flat, 0.0, -n[0] / flat};
    } else {
        // Square to the normal, which a floor's slope tilts a little.
        const double along = eu[0] * n[0];
        eu = {eu[0] - n[0] * along, -n[1] * along, -n[2] * along};
        const double l = std::sqrt(eu[0] * eu[0] + eu[1] * eu[1] + eu[2] * eu[2]);
        for (double& c : eu) c /= l;
    }
    const sh::scene::SurfaceVec ev = {n[1] * eu[2] - n[2] * eu[1], n[2] * eu[0] - n[0] * eu[2],
                                      n[0] * eu[1] - n[1] * eu[0]};

    // The mark in the plane (scene units from the hit point): its half-axes, turned and scaled for this hit.
    const BulletHoleLook look = bullet_hole_look(hit.x, hit.y, hit.z);
    const double half = static_cast<double>(look.scale) * kHalf / 16.0;
    const Vec2 au = {std::cos(look.angle) * half, std::sin(look.angle) * half};
    const Vec2 av = {-au[1], au[0]};
    const auto corner_at = [&](const Vec2& p) {
        DecalCorner c;
        c.x = static_cast<std::int16_t>(std::lround(eu[0] * p[0] + ev[0] * p[1]));
        c.y = static_cast<std::int16_t>(std::lround(eu[1] * p[0] + ev[1] * p[1]));
        c.z = static_cast<std::int16_t>(std::lround(eu[2] * p[0] + ev[2] * p[1]));
        const double a = (p[0] * au[0] + p[1] * au[1]) / (half * half);
        const double b = (p[0] * av[0] + p[1] * av[1]) / (half * half);
        const double u = std::clamp((a + 1.0) * 0.5, 0.0, 1.0);
        c.u = static_cast<std::uint8_t>(std::lround((look.mirrored ? 1.0 - u : u) * kCellUv));
        c.v = static_cast<std::uint8_t>(std::lround(std::clamp((b + 1.0) * 0.5, 0.0, 1.0) * kCellUv));
        return c;
    };
    const auto quad_point = [&](double su, double sv) { return Vec2{su * au[0] + sv * av[0], su * au[1] + sv * av[1]}; };

    std::vector<std::array<Vec2, 3>> patch;
    for (const auto& tri : surface->patch) {
        std::array<Vec2, 3> t;
        for (std::size_t k = 0; k < 3; ++k) {
            const sh::scene::SurfaceVec rel = {tri[k][0] - surface->point[0], tri[k][1] - surface->point[1],
                                               tri[k][2] - surface->point[2]};
            t[k] = {rel[0] * eu[0] + rel[1] * eu[1] + rel[2] * eu[2], rel[0] * ev[0] + rel[1] * ev[1] + rel[2] * ev[2]};
        }
        patch.push_back(t);
    }
    // Does the surface carry the whole mark? Sampled: the patch is a union of triangles.
    bool whole = true;
    constexpr int kSamples = 6;
    for (int i = 0; i <= kSamples && whole; ++i) {
        for (int j = 0; j <= kSamples && whole; ++j) {
            const Vec2 p = quad_point(2.0 * i / kSamples - 1.0, 2.0 * j / kSamples - 1.0);
            whole = std::any_of(patch.begin(), patch.end(), [&](const auto& t) { return inside(t, p, kEdgeSlack); });
        }
    }
    if (whole) {
        DecalPolygon quad;
        for (std::size_t k = 0; k < 4; ++k) {
            quad.corner[k] = corner_at(quad_point((k & 1) ? 1.0 : -1.0, (k & 2) ? 1.0 : -1.0));
        }
        d.polygons.push_back(quad);
    } else {
        // The surface ends under the mark (an edge, a corner, a pillar narrower than the mark): only the parts on it
        // are kept. Surfaces lying on one another there (a poster on the wall) would each take a copy; a part
        // whose middle another part already covers is dropped.
        const std::vector<Vec2> outline = {quad_point(-1, -1), quad_point(1, -1), quad_point(1, 1), quad_point(-1, 1)};
        std::vector<std::vector<Vec2>> parts;
        for (const auto& t : patch) {
            std::vector<Vec2> part = clipped(outline, t);
            if (part.size() < 3) {
                continue;
            }
            Vec2 middle = {0.0, 0.0};
            double area = 0.0;
            for (std::size_t k = 0; k < part.size(); ++k) {
                middle[0] += part[k][0] / static_cast<double>(part.size());
                middle[1] += part[k][1] / static_cast<double>(part.size());
                if (k >= 2) {
                    area += cross2(part[0], part[k - 1], part[k]) * 0.5;
                }
            }
            if (area < kMinPartArea) {
                continue;
            }
            const bool covered = std::any_of(parts.begin(), parts.end(), [&](const std::vector<Vec2>& other) {
                for (std::size_t k = 2; k < other.size(); ++k) {
                    if (inside({other[0], other[k - 1], other[k]}, middle, 0.0)) {
                        return true;
                    }
                }
                return false;
            });
            if (!covered) {
                parts.push_back(std::move(part));
            }
        }
        for (const std::vector<Vec2>& part : parts) {
            for (std::size_t k = 2; k < part.size() && d.polygons.size() < kMaxPolygons; ++k) {
                DecalPolygon triangle;
                triangle.count = 3;
                triangle.corner[0] = corner_at(part[0]);
                triangle.corner[1] = corner_at(part[k - 1]);
                triangle.corner[2] = corner_at(part[k]);
                d.polygons.push_back(triangle);
            }
        }
        if (d.polygons.empty()) {
            return;
        }
    }
    d.variant = static_cast<std::uint8_t>(look.variant);
    SH_DBG("[DECAL] hit at %.2f %.2f %.2f, normal %.2f %.2f %.2f (%.0f deg off the shot), hole %d, %d polygon(s)",
           static_cast<double>(point.x) / 4096.0, static_cast<double>(point.y) / 4096.0,
           static_cast<double>(point.z) / 4096.0, n[0], n[1], n[2],
           std::acos(std::clamp(-(n[0] * dir[0] + n[1] * dir[1] + n[2] * dir[2]), -1.0, 1.0)) * 57.29578,
           look.variant, static_cast<int>(d.polygons.size()));
    decals_[static_cast<std::size_t>(head_)] = d;
    head_ = (head_ + 1) % kMaxDecals;
    if (count_ < kMaxDecals) {
        ++count_;
    }
}

// Puts the sheet among the replacement pictures. Runs from the draw, where the device exists, and again after every
// reset.
bool BulletDecals::ensure_texture() {
    if (registered_) {
        return true;
    }
    if (sheet_.empty()) {
        sheet_ = bullet_hole_sheet(kSheetSeed);
    }
    if (!sh::textures::replacements().put_sheet(kSheet, sheet_, kBulletHoleSheet, kBulletHoleSheet, kNative, kNative)) {
        return false;
    }
    SH_DBG("[DECAL] bullet hole sheet %dx%d put as sheet %d (palette word 0x%04X)", kBulletHoleSheet,
           kBulletHoleSheet, kSheet, static_cast<unsigned>(kPalette));
    registered_ = true;
    return true;
}

// The fog amount of a corner at view depth `z`, as the world's vertices carry it: the ramp at that depth plus the
// area's density. The shader then fogs the mark exactly like the wall under it.
std::uint8_t fog_byte(const sh::world::AtmosphereState& air, std::int32_t z) {
    if (!air.fog_enabled) {
        return 0;
    }
    std::int32_t amount = sh::world::atmosphere().fog_ramp_at(z) * 16 + air.fog.intensity;
    amount = amount < 0 ? 0 : (amount > 4096 ? 4096 : amount);
    return static_cast<std::uint8_t>((amount * 127 + 2048) / 4096);
}

void BulletDecals::draw() {
    if (!g_PcConfig.bulletDecals || count_ == 0 || !ensure_texture()) {
        return;
    }
    const sh::world::AtmosphereState& air = sh::world::atmosphere().state();
    // The mark has no light of its own: the shader paints it into the lit surface under it (prim_batch.frag), so
    // the colour is unused and only the corner fog matters.
    const sh::draw3d::Rgb color = {128, 128, 128};

    for (int i = 0; i < count_; ++i) {
        const Decal& d = decals_[static_cast<std::size_t>(i)];
        Transform view;
        world_screen_at(view, d.center.x, d.center.y, d.center.z);
        projection_matrix_set(view);
        sh::math::ViewProjection vp = sh::math::game_view_projection();
        sh::effects::drawer::set_current(vp);

        // Seen from behind (through the back of a thin wall, from under a floor) the mark is not there: the map's
        // depth alone cannot tell, it is written farther than the wall stands.
        const sh::math::Vec3i eye_to_mark = vp.transform({0, 0, 0});
        const sh::math::Vec3i tip = vp.transform({static_cast<std::int16_t>(d.normal.x >> 4),
                                                  static_cast<std::int16_t>(d.normal.y >> 4),
                                                  static_cast<std::int16_t>(d.normal.z >> 4)});
        const std::int64_t facing = static_cast<std::int64_t>(tip.x - eye_to_mark.x) * eye_to_mark.x +
                                    static_cast<std::int64_t>(tip.y - eye_to_mark.y) * eye_to_mark.y +
                                    static_cast<std::int64_t>(tip.z - eye_to_mark.z) * eye_to_mark.z;
        if (facing >= 0) {
            continue;
        }

        for (const DecalPolygon& part : d.polygons) {
        sh::draw3d::Polygon quad;
        quad.count = part.count;
        quad.textured = true;
        quad.gouraud = true;  // per-corner fog
        quad.shade = {true, false};
        quad.surface_decal = true;
        // The palette word alone selects the override texture; the page never samples VRAM.
        quad.page = sh::draw3d::TexturePage::from_word(0);
        quad.palette = sh::draw3d::Palette::from_word(kPalette);
        sh::draw3d::Depth depth;
        depth.source = sh::draw3d::DepthSource::Actor;

        const int cell_u = (d.variant % kBulletHoleGrid) * kCellUv;
        const int cell_v = (d.variant / kBulletHoleGrid) * kCellUv;
        std::uint32_t depth_sum = 0;
        std::uint32_t depth_max = 0;
        bool visible = true;
        for (int k = 0; k < part.count && visible; ++k) {
            const DecalCorner& at = part.corner[static_cast<std::size_t>(k)];
            const sh::math::Vec3s corner = {at.x, at.y, at.z};
            const sh::math::PreciseVertex r = vp.project(corner);
            // Behind or at the near plane, or off the projection's range.
            if (r.v.sz <= 32 || r.v.depth_clamped || std::abs(r.v.sx) > 1000 || std::abs(r.v.sy) > 1000) {
                visible = false;
                break;
            }
            sh::draw3d::Vertex& v = quad.v[static_cast<std::size_t>(k)];
            v.xy = {r.v.sx, r.v.sy};
            v.uv = {static_cast<std::uint8_t>(cell_u + at.u), static_cast<std::uint8_t>(cell_v + at.v)};
            v.color = color;
            v.fog = fog_byte(air, r.v.sz);
            sh::effects::drawer::corner_records(r, v);
            depth.sz[static_cast<std::size_t>(k)] = r.v.sz;
            depth_sum += r.v.sz;
            depth_max = r.v.sz > depth_max ? r.v.sz : depth_max;
        }
        if (!visible) {
            continue;
        }
        quad.depth = depth;
        // A quarter of the corners' mean depth, as the ordering table of the characters counts it.
        const int bucket = static_cast<int>(((depth_sum / part.count) >> 2) >> kOtShift) - kOtLift;
        sh::draw3d::submit(quad, world_order_key(bucket), depth_max);
        }
    }
}

}  // namespace

void bullet_decal_add(const DecalPoint& hit, const DecalPoint& direction, const DecalPoint& origin) {
    decals().add(hit, direction, origin);
}

}  // namespace sh::render

extern "C" void Pc_DecalsDraw(void* /*ot: the world layer*/) {
    sh::render::decals().draw();
}

extern "C" void Pc_DecalsReset(void) {
    sh::render::decals().reset();
}
