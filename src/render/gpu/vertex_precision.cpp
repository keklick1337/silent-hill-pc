// Vertex precision of 3D primitives. The replay checks the pictures bit for bit, so every float expression
// keeps the operand order of the behaviour it reproduces.
#include "render/gpu/vertex_precision.h"

#include "render/draw3d/draw3d.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <vector>

namespace sh::gpu::precision {
namespace {

struct WeldEntry {
    unsigned gen = 0;
    float x = 0.f, y = 0.f, w = 0.f;
};
constexpr unsigned kWeldBits = 17;
constexpr unsigned kWeldMask = (1u << kWeldBits) - 1u;

struct Projection {
    float ofx = 0.f, ofy = 0.f, h = 1.f;
    bool valid = false;
};

struct PolygonState {
    bool affine = false;
    float ofs_x = 0.f, ofs_y = 0.f;
};

struct State {
    Settings settings;
    Counters counters;
    unsigned generation = 1;
    std::uint32_t sz_this = 0, sz_prev = 0;
    PolygonState polygon;
    Projection projection;
    std::vector<WeldEntry> weld;
    std::vector<std::array<float, 4>> lit;
    std::uint32_t lit_cursor = 0;
};

State& state() {
    static State s;
    return s;
}

unsigned weld_hash(int ix, int iy) {
    return (static_cast<unsigned>(ix) * 73856093u) ^ (static_cast<unsigned>(iy) * 19349663u);
}

float clamp_ndc(float z) {
    if (z < -1.0f) z = -1.0f;
    if (z > 1.0f) z = 1.0f;
    return z;
}

void lit_reserve(std::uint32_t n) {
    auto& lit = state().lit;
    if (lit.size() < n) lit.resize(std::max<std::size_t>(n, lit.size() * 2));
}

std::uint8_t lerp_byte(std::uint8_t a, std::uint8_t b, float t) {
    return static_cast<std::uint8_t>(static_cast<float>(a) + (static_cast<float>(b) - static_cast<float>(a)) * t + 0.5f);
}

std::int16_t screen_short(float v) {
    return static_cast<std::int16_t>(v < -32767.0f ? -32767.0f : (v > 32767.0f ? 32767.0f : v));
}

Projection active_projection() {
    const State& s = state();
    if (s.projection.valid) return s.projection;
    return {s.settings.screen_offset_x, s.settings.screen_offset_y, s.settings.projection_distance, false};
}

// The vertex where edge a->b crosses the near plane, reprojected with the polygon's own GTE projection; the
// attributes that do not interpolate come from a.
Vertex clip_vertex(const Vertex& a, const Vertex& b) {
    const State& s = state();
    const float near_z = s.settings.near_z;
    const float t = (near_z - a.vsz) / (b.vsz - a.vsz);

    Vertex out = a;
    out.vsx = a.vsx + (b.vsx - a.vsx) * t;
    out.vsy = a.vsy + (b.vsy - a.vsy) * t;
    out.vsz = near_z;

    // No edge clamp: with a true W the GPU clips far off-screen positions homogeneously.
    const Projection p = active_projection();
    const float hz = p.h / near_z;
    out.ppx = p.ofx + out.vsx * hz + s.polygon.ofs_x;
    out.ppy = p.ofy + out.vsy * hz + s.polygon.ofs_y;
    out.ppw = near_z;

    out.x = screen_short(out.ppx);
    out.y = screen_short(out.ppy);

    out.u = lerp_byte(a.u, b.u, t);
    out.v = lerp_byte(a.v, b.v, t);
    out.r = lerp_byte(a.r, b.r, t);
    out.g = lerp_byte(a.g, b.g, t);
    out.b = lerp_byte(a.b, b.b, t);
    out.a = lerp_byte(a.a, b.a, t);
    out.fog = static_cast<std::int8_t>(static_cast<float>(a.fog) + (static_cast<float>(b.fog) - static_cast<float>(a.fog)) * t + 0.5f);
    return out;
}

// A kept in-front corner whose precise projection missed gets one rebuilt from view space.
void reproject(Vertex& v) {
    if (v.ppw > 0.0f) return;
    const State& s = state();
    const Projection p = active_projection();
    const float hz = p.h / v.vsz;
    v.ppx = p.ofx + v.vsx * hz + s.polygon.ofs_x;
    v.ppy = p.ofy + v.vsy * hz + s.polygon.ofs_y;
    v.ppw = v.vsz;
}

void force_affine(Vertex* v, int count) {
    for (int j = 0; j < count; j++) v[j].ppw = 0.0f;
}

// Sutherland-Hodgman against z = near_z per triangle, fan-triangulated back in place.
int near_clip(Vertex* v, int count, bool room) {
    State& s = state();
    if (!s.settings.pgxp || !near_clip_eligible(v, count)) return count;
    if (!room) {
        force_affine(v, count);
        return count;
    }

    const float near_z = s.settings.near_z;
    Vertex out[12];
    int out_count = 0;
    for (int tri = 0; tri + 2 < count; tri += 3) {
        Vertex clipped[4];
        int m = 0;
        for (int i = 0; i < 3; i++) {
            const Vertex& a = v[tri + i];
            const Vertex& b = v[tri + (i + 1) % 3];
            const bool a_in = a.vsz >= near_z;
            const bool b_in = b.vsz >= near_z;
            if (a_in) clipped[m++] = a;
            if (a_in != b_in) clipped[m++] = clip_vertex(a, b);
        }
        for (int i = 0; i < m; i++) reproject(clipped[i]);
        for (int i = 2; i < m; i++) {
            out[out_count++] = clipped[0];
            out[out_count++] = clipped[i - 1];
            out[out_count++] = clipped[i];
        }
    }

    // The ortho precise path cancels W, so an out-of-band clip vertex would draw as a screen-crossing wedge:
    // abandon the clip and let the size rule judge the integer corners instead.
    const float m = s.settings.edge_max;
    for (int i = 0; i < out_count; i++) {
        if (out[i].ppx < -m || out[i].ppx > m || out[i].ppy < -m || out[i].ppy > m) {
            force_affine(v, count);
            return count;
        }
    }

    std::copy(out, out + out_count, v);
    s.counters.clipped++;
    return out_count;
}

}  // namespace

Settings& settings() { return state().settings; }
Counters& counters() { return state().counters; }

float ndc_from_view_z(float vz) {
    return clamp_ndc(1.0f - 2.0f * vz * (1.0f / kPgxpDepthSzMax));
}

void begin_frame() {
    bump_generation();
    roll_sz_max();
}

void bump_generation() { state().generation++; }

void roll_sz_max() {
    State& s = state();
    s.sz_prev = s.sz_this;
    s.sz_this = 0;
}

void note_sz(std::uint32_t sz) {
    State& s = state();
    if (sz > s.sz_this) s.sz_this = sz;
}

std::uint32_t sz_max_prev() { return state().sz_prev; }

// The scene draws the world on this scale whatever the vertex precision. The largest depth of last frame's
// polygons would put the far plane at the farthest character or effect: in a room where
// nothing stands farther than the walls, the walls behind it would be clipped away (a black veil over the picture).
float sz_max() { return kPgxpDepthSzMax; }

float item_sz_max() {
    const std::uint32_t prev = state().sz_prev;
    return prev < 1 ? 1.0f : static_cast<float>(prev);
}

void begin_polygon(bool affine, float draw_ofs_x, float draw_ofs_y) {
    state().polygon = {affine, draw_ofs_x, draw_ofs_y};
}

void set_polygon_projection(float ofs_x, float ofs_y, float h) {
    state().projection = {ofs_x, ofs_y, h, true};
}

std::uint8_t apply_depth(Vertex* vertex, const DepthIn& in, bool quad) {
    State& s = state();
    const Settings& cfg = s.settings;
    Counters& c = s.counters;
    const int n = quad ? 4 : 3;
    const std::uint32_t prev = s.sz_prev;
    // Outside the item pass: flat depth on the constant linear scale the scene uses, no normaliser needed. The
    // integer path takes it too, or its polygons would not order against the scene's world.
    const bool world_path = !cfg.force_item_depth && !(cfg.debug & 2);
    if (cfg.pgxp) c.apply_calls++;
    if (!world_path && prev < 1) return 255;

    if (cfg.whole_map_probe == 1) {
        float dev = 0.0f;
        unsigned prec = 0;
        for (int i = 0; i < n; i++) {
            if (vertex[i].ppw <= 0.0f) continue;
            float dx = vertex[i].ppx - vertex[i].x, dy = vertex[i].ppy - vertex[i].y;
            if (dx < 0) dx = -dx;
            if (dy < 0) dy = -dy;
            if (dx > dev) dev = dx;
            if (dy > dev) dev = dy;
            prec = 1;
        }
        c.wm_precise_prims += prec;
        c.wm_deviating_prims += (dev > 2.0f);
        if (in.last_bucket) {
            unsigned aff = 0;
            for (int i = 0; i < n; i++)
                if (vertex[i].ppw <= 0.0f) aff = 1;
            c.wm_last_prims++;
            c.wm_last_affine += aff;
            if (in.found) c.wm_last_hit++;
        }
    }
    if (!in.found) {
        if (cfg.pgxp) c.parse_miss++;
        return 255;
    }
    const DepthKind kind = in.kind;
    if (cfg.pgxp) {
        c.parse_hit++;
        if (kind == DepthKind::Flat) c.hit_flat++;
        else if (kind == DepthKind::Exact) c.hit_exact++;
        else c.hit_none++;
    }
    const std::uint8_t alpha = (cfg.debug & 4) ? 255 : in.alpha;

    // A quad's stream corners 2 and 3 are its hardware corners 3 and 2.
    float sv[4] = {0.f, 0.f, 0.f, 0.f};
    if (quad) {
        sv[0] = static_cast<float>(in.sz[0]);
        sv[1] = static_cast<float>(in.sz[1]);
        sv[2] = static_cast<float>(in.sz[3]);
        sv[3] = static_cast<float>(in.sz[2]);
    } else {
        sv[0] = static_cast<float>(in.sz[1]);
        sv[1] = static_cast<float>(in.sz[2]);
        sv[2] = static_cast<float>(in.sz[3]);
    }
    const float sz_avg = quad ? (sv[0] + sv[1] + sv[2] + sv[3]) * 0.25f : (sv[0] + sv[1] + sv[2]) * (1.0f / 3.0f);
    if (sz_avg < 1.0f) return alpha;  // a 2D prim keeps its bucket depth

    if (kind == DepthKind::Actor && cfg.actor_depth && !cfg.force_item_depth) {
        for (int i = 0; i < n; i++) {
            if (world_path) {
                vertex[i].z = ndc_from_view_z(sv[i]);
                vertex[i].depthMark = 2;
            } else {
                vertex[i].z = clamp_ndc(1.0f - 2.0f * sv[i] / static_cast<float>(prev));
            }
        }
        return alpha;
    }

    if (cfg.force_item_depth && kind != DepthKind::Exact) c.item_non_exact++;

    // Item pass: true per-vertex depth so a foreshortened face orders against geometry just behind it.
    if (cfg.force_item_depth && kind == DepthKind::Exact) {
        const float inv = 2.0f / static_cast<float>(prev);
        for (int i = 0; i < n; i++) vertex[i].z = clamp_ndc(1.0f - sv[i] * inv);
        return alpha;
    }

    if (world_path) {
        // Auto-captured FIFO values are lighting garbage: those prims keep the bucket seed.
        if (kind == DepthKind::None) return alpha;
        if (kind == DepthKind::Flat && !cfg.world_depth) return alpha;
        // World geometry is pushed farther so coplanar testers win LEQUAL without a tester-side bias.
        const float vz = sz_avg + ((kind == DepthKind::Flat) ? static_cast<float>(cfg.world_far_bias) : 0.0f);
        const float z = ndc_from_view_z(vz);
        for (int i = 0; i < n; i++) vertex[i].z = z;
        // Opaque world draws ALWAYS, so per-vertex depth cannot flicker there; translucent world prims test against
        // their host and keep the flat depth.
        if (kind == DepthKind::Flat && !in.semi)
            for (int i = 0; i < n; i++) vertex[i].depthMark = 1;
        if (cfg.whole_map_probe == 1 && kind == DepthKind::Flat && in.far) {
            c.wm_parsed++;
            for (int i = 0; i < n; i++) {
                if (vertex[i].ppw > 0.0f) {
                    c.wm_precise_verts++;
                    if (c.wm_w_min == 0.0f || vertex[i].ppw < c.wm_w_min) c.wm_w_min = vertex[i].ppw;
                    if (vertex[i].ppw > c.wm_w_max) c.wm_w_max = vertex[i].ppw;
                } else {
                    c.wm_affine_verts++;
                }
            }
        }
        return alpha;
    }

    const float z = clamp_ndc(1.0f - 2.0f * sz_avg * (1.0f / static_cast<float>(prev)));
    for (int i = 0; i < n; i++) vertex[i].z = z;
    return alpha;
}

void weld(float* x, float* y, float* w) {
    State& s = state();
    const float px = s.settings.weld_px;
    if (px <= 0.0f) return;
    if (s.weld.empty()) s.weld.resize(std::size_t{1} << kWeldBits);
    const float r2 = px * px;
    const int ix = static_cast<int>(*x < 0 ? *x - 0.5f : *x + 0.5f);
    const int iy = static_cast<int>(*y < 0 ? *y - 0.5f : *y + 0.5f);
    const int r = std::clamp(static_cast<int>(px + 0.999f), 1, 4);
    for (int dy = -r; dy <= r; dy++) {
        for (int dx = -r; dx <= r; dx++) {
            const WeldEntry& e = s.weld[weld_hash(ix + dx, iy + dy) & kWeldMask];
            if (e.gen != s.generation) continue;
            const float ex = e.x - *x, ey = e.y - *y;
            if (ex * ex + ey * ey > r2) continue;
            // Only the same 3D point merges: two surfaces that overlap on screen differ in depth.
            const float lo = e.w < *w ? e.w : *w, hi = e.w < *w ? *w : e.w;
            if (lo > 0.0f && hi <= lo * s.settings.weld_w_ratio) {
                *x = e.x;
                *y = e.y;
                *w = e.w;
                return;
            }
        }
    }
    s.weld[weld_hash(ix, iy) & kWeldMask] = {s.generation, *x, *y, *w};
}

bool near_clip_eligible(const Vertex* v, int count) {
    const State& s = state();
    if (!s.settings.near_clip || s.polygon.affine) return false;
    int front = 0, behind = 0;
    for (int i = 0; i < count; i++) {
        // ny marks a validated view-space entry: a corner behind the eye legitimately has vsz <= 0.
        if (v[i].ny < 0.5f) return false;
        if (v[i].vsz >= s.settings.near_z) front++;
        else behind++;
    }
    return front > 0 && behind > 0;
}

bool oversized(const Vertex* v) {
    int mnx = v[0].x, mxx = v[0].x, mny = v[0].y, mxy = v[0].y;
    for (int i = 1; i < 3; i++) {
        mnx = std::min<int>(mnx, v[i].x);
        mxx = std::max<int>(mxx, v[i].x);
        mny = std::min<int>(mny, v[i].y);
        mxy = std::max<int>(mxy, v[i].y);
    }
    return static_cast<float>(mxx - mnx) > 1023.0f * state().settings.poly_size_wide_scale || (mxy - mny) > 511;
}

int emit_polygon(Vertex* v, int count, bool room) {
    State& s = state();
    count = near_clip(v, count, room);
    // The size rule judges only integer-path polygons (a precise one may have saturated integer corners), and never
    // the whole-map far view, where box saturation is by design.
    if (!s.settings.poly_size_cull || s.settings.whole_map_far) return count;
    for (int i = 0; i < count; i++)
        if (v[i].ppw > 0.0f) return count;
    int kept = 0;
    for (int tri = 0; tri + 2 < count; tri += 3) {
        if (oversized(&v[tri])) {
            s.counters.oversize++;
            continue;
        }
        if (kept != tri) std::copy(v + tri, v + tri + 3, v + kept);
        kept += 3;
    }
    return kept;
}

void lit_zero_to(std::uint32_t end) {
    State& s = state();
    if (end <= s.lit_cursor) return;
    lit_reserve(end);
    std::fill(s.lit.begin() + s.lit_cursor, s.lit.begin() + end, std::array<float, 4>{});
    s.lit_cursor = end;
}

void lit_fill(std::span<const float> corner_normals, int kind, std::uint32_t first, int emitted, const int* corner_map) {
    State& s = state();
    lit_zero_to(first);
    lit_reserve(first + static_cast<std::uint32_t>(emitted));
    for (int i = 0; i < emitted; i++) {
        const float* n = &corner_normals[static_cast<std::size_t>(corner_map[i]) * 3];
        s.lit[first + i] = {n[0], n[1], n[2], static_cast<float>(kind)};
    }
    // Rewinds when the stream restarted: what lies past the cursor is zeroed again before it is read.
    s.lit_cursor = first + static_cast<std::uint32_t>(emitted);
}

const float* lit_finish(std::uint32_t count) {
    lit_zero_to(count);
    lit_reserve(1);
    return state().lit[0].data();
}

int end_polygon(Vertex* triangles, int vertices, std::uint32_t first, const LitRun& run, bool room) {
    const Settings& cfg = state().settings;
    // Anything the near clipper or the size rule reshapes does not line up with the corners.
    const bool lit = cfg.pixel_lit_active && run.light != 0;
    const bool reshaped = lit && cfg.pgxp && near_clip_eligible(triangles, vertices);
    const int emitted = emit_polygon(triangles, vertices, room);
    if (!cfg.pixel_lit_active || !run.textured || !run.gouraud) return emitted;
    if (lit && !reshaped && emitted == vertices) {
        static constexpr int kTriangle[3] = {0, 1, 2};
        static constexpr int kQuad[6] = {0, 1, 2, 1, 2, 3};
        lit_fill(run.normals, run.light == 1 ? 1 : 2, first, vertices, vertices == 3 ? kTriangle : kQuad);
    } else {
        lit_zero_to(first + static_cast<std::uint32_t>(emitted));
    }
    return emitted;
}

}  // namespace sh::gpu::precision
