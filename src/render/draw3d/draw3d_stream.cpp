#include "render/draw3d/draw3d_stream.h"

#include <cstring>

namespace sh::draw3d {

namespace {

using rhi::PrimVertex;

constexpr std::uint8_t kBrightTextured = 2;
constexpr std::uint8_t kBrightUntextured = 1;

// The screen position the renderer stores: the coordinate plus the (possibly fractional) draw offset, truncated.
std::int16_t screen(std::int16_t coordinate, float offset) {
    return static_cast<std::int16_t>(static_cast<int>(static_cast<float>(coordinate) + offset));
}

std::int16_t vertex_page(std::uint16_t page) {
    return static_cast<std::int16_t>((page & 0x1F) | (((page >> 7) & 0x3) << 5));
}

void texel_box(PrimVertex* v, int n) {
    std::uint8_t ulo = v[0].u, uhi = v[0].u, vlo = v[0].v, vhi = v[0].v;
    for (int i = 1; i < n; i++) {
        if (v[i].u < ulo) ulo = v[i].u;
        if (v[i].u > uhi) uhi = v[i].u;
        if (v[i].v < vlo) vlo = v[i].v;
        if (v[i].v > vhi) vhi = v[i].v;
    }
    // The largest texel coordinate lies on the polygon's far edge and is never shown.
    if (uhi > ulo) uhi--;
    if (vhi > vlo) vhi--;
    for (int i = 0; i < n; i++) {
        v[i].ulo = ulo;
        v[i].vlo = vlo;
        v[i].uhi = uhi;
        v[i].vhi = vhi;
    }
}

bool null_triangle(const Polygon& p) {
    for (int i = 0; i < 3; i++)
        if (p.v[i].xy.x != -1 || p.v[i].xy.y != -1) return false;
    return true;
}

// A polygon crossing the near plane keeps its corners' precise positions: the target clips it.
bool clip_eligible(const PrimVertex* v, int n, const StreamFrame& f, const PrimFlags& prim) {
    if (!f.near_clip || prim.affine) return false;
    int front = 0, behind = 0;
    for (int i = 0; i < n; i++) {
        if (v[i].ny < 0.5f) return false;
        if (v[i].vsz >= f.near_z) front++;
        else behind++;
    }
    return front > 0 && behind > 0;
}

void draw(const Polygon& p, StreamTarget& t) {
    const int n = p.count == 3 ? 3 : 4;
    const PrimFlags prim = t.begin_prim(p);

    // A textured flat triangle with every coordinate -1 is the SDK's way of changing the page: it draws nothing.
    if (p.textured && !p.gouraud && n == 3 && null_triangle(p)) {
        t.latch_page(p.page.word());
        return;
    }

    StreamRun run;
    run.count = static_cast<std::uint8_t>(n);
    run.textured = p.textured;
    run.gouraud = p.gouraud;
    run.translucent = p.shade.translucent;
    run.page = p.page.word();
    run.palette = p.palette;
    run.color = p.v[0].color;
    run.light = p.light;
    if (p.light != LightKind::None)
        for (int k = 0; k < n; k++) run.normals[k] = p.v[k].normal;
    PrimVertex* v = t.begin(run, n == 4 ? 6 : 3);
    if (v == nullptr) return;
    const StreamFrame f = t.frame();

    // Corner k is hardware vertex order[k]: a quad's last two are swapped so the split yields (0 1 2) (1 2 3).
    static constexpr int kQuad[4] = {0, 1, 3, 2};
    static constexpr int kTri[3] = {0, 1, 2};
    const int* order = n == 4 ? kQuad : kTri;

    std::memset(static_cast<void*>(v), 0, sizeof(PrimVertex) * n);
    for (int k = 0; k < n; k++) {
        const Vertex& src = p.v[order[k]];
        v[k].x = screen(src.xy.x, f.offset_x);
        v[k].y = screen(src.xy.y, f.offset_y);
        v[k].z = f.depth;
        if (f.mark_geometry) v[k].geom3d = 1.f;
    }

    if (f.precise) {
        for (int k = 0; k < n; k++) {
            const Vertex& src = p.v[order[k]];
            PrimVertex& c = v[k];
            bool resolved = false;
            if (!prim.affine && src.precise && src.precise->w > 0.f) {
                const Precise& e = *src.precise;
                if (e.x >= -f.edge_max && e.x <= f.edge_max && e.y >= -f.edge_max && e.y <= f.edge_max) {
                    c.ppx = e.x + f.offset_x;
                    c.ppy = e.y + f.offset_y;
                    c.ppw = e.w;
                    resolved = true;
                }
            }
            if (!resolved) {
                c.ppx = static_cast<float>(c.x);
                c.ppy = static_cast<float>(c.y);
                c.ppw = 0.f;
            }
            if (!prim.affine) t.precise(c, resolved);
        }
    }

    if (f.view_space) {
        bool beyond = f.precise;
        for (int k = 0; k < n && beyond; k++) beyond = v[k].ppw > f.view_space_reach;
        if (!beyond) {
            bool known = false;
            for (int k = 0; k < n; k++) {
                const Vertex& src = p.v[order[k]];
                PrimVertex& c = v[k];
                if (src.view) {
                    const ViewPosition& e = *src.view;
                    c.vsx = e.x;
                    c.vsy = e.y;
                    c.vsz = e.z;
                    c.nx = e.casts_shadow ? 0.f : 1.f;
                    c.ny = 1.f;
                    c.nz = e.fade;
                    t.view_projection(e.ofx, e.ofy, e.h);
                    known = true;
                }
                if (prim.no_flashlight) c.vsz = -1.f;
            }
            // One corner from the projection proves the whole polygon is scene geometry.
            if (known)
                for (int k = 0; k < n; k++) v[k].geom3d = 1.f;
        }
    }

    if (p.surface_decal)
        for (int k = 0; k < n; k++) v[k].geom3d = rhi::kGeomSurfaceDecal;

    // A polygon with some corners perspective-correct and some not shears: all of it goes affine, unless the near
    // clip takes it (it needs the corners in front intact).
    if (f.precise) {
        bool any_affine = false;
        for (int k = 0; k < n; k++) any_affine = any_affine || v[k].ppw <= 0.f;
        if (any_affine && !clip_eligible(v, n, f, prim))
            for (int k = 0; k < n; k++) v[k].ppw = 0.f;
    }

    const std::uint8_t alpha = t.depth(v, n, p);

    if (p.textured) {
        const std::uint8_t dither = (((f.page >> 9) & 1) != 0 || f.dither) ? 1 : 0;
        const std::int16_t page = vertex_page(p.page.word());
        const std::int16_t clut = static_cast<std::int16_t>(p.palette.word());
        for (int k = 0; k < n; k++) {
            const Vertex& src = p.v[order[k]];
            v[k].u = src.uv.u;
            v[k].v = src.uv.v;
            v[k].bright = kBrightTextured;
            v[k].dither = dither;
            v[k].page = page;
            v[k].clut = clut;
        }
        texel_box(v, n);
    } else {
        for (int k = 0; k < n; k++) {
            v[k].bright = kBrightUntextured;
            v[k].dither = p.gouraud ? 1 : 0;
            v[k].page = rhi::kPageUntextured;
        }
    }

    const bool shaded = p.gouraud || !p.shade.raw_texture;
    for (int k = 0; k < n; k++) {
        const Rgb c = !shaded ? Rgb{128, 128, 128} : (p.gouraud ? p.v[order[k]].color : p.v[0].color);
        v[k].r = c.r;
        v[k].g = c.g;
        v[k].b = c.b;
        v[k].a = alpha;
        v[k].fog = p.gouraud ? static_cast<std::int8_t>(p.v[order[k]].fog) : 0;
    }

    int count = n;
    if (n == 4) {
        v[4] = v[3];
        v[5] = v[2];
        v[2] = v[3];
        v[3] = v[1];
        count = 6;
    }
    t.end(run, v, count);
}

struct Emitter {
    StreamTarget& t;
    void operator()(const Polygon& p) const { draw(p, t); }
};

}  // namespace

void emit_stream(const Command& command, StreamTarget& target) {
    std::visit(Emitter{target}, command);
}

}  // namespace sh::draw3d
