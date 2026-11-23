#include "render/draw2d/draw2d_stream.h"

#include <cstdlib>
#include <cstring>
#include <utility>

namespace sh::draw2d {

namespace {

using rhi::PrimVertex;

constexpr std::uint8_t kBrightTextured = 2;
constexpr std::uint8_t kBrightUntextured = 1;

std::int16_t s16(int v) { return static_cast<std::int16_t>(v); }

std::int16_t vertex_page(std::uint16_t page) {
    return static_cast<std::int16_t>((page & 0x1F) | (((page >> 7) & 0x3) << 5));
}

void clear(PrimVertex* v, int n) { std::memset(static_cast<void*>(v), 0, sizeof(PrimVertex) * n); }

void place(PrimVertex& v, const Point& p, const StreamFrame& f) {
    v.x = s16(p.x + f.offset_x);
    v.y = s16(p.y + f.offset_y);
    v.z = f.depth;
}

void untextured(PrimVertex* v, int n, std::uint8_t dither) {
    for (int i = 0; i < n; i++) {
        v[i].u = v[i].v = 0;
        v[i].bright = kBrightUntextured;
        v[i].dither = dither;
        v[i].page = rhi::kPageUntextured;
        v[i].clut = 0;
    }
}

void paint(PrimVertex& v, const Rgb& c) {
    v.r = c.r;
    v.g = c.g;
    v.b = c.b;
    v.a = 255;
}

// The box of texels a shape reads, for the filter's clamp. A polygon's largest texel coordinate lies on its far
// edge and is never shown, so its box ends one texel short there.
void texel_box(PrimVertex* v, int n, bool far_edge_exclusive) {
    std::uint8_t ulo = v[0].u, uhi = v[0].u, vlo = v[0].v, vhi = v[0].v;
    for (int i = 1; i < n; i++) {
        if (v[i].u < ulo) ulo = v[i].u;
        if (v[i].u > uhi) uhi = v[i].u;
        if (v[i].v < vlo) vlo = v[i].v;
        if (v[i].v > vhi) vhi = v[i].v;
    }
    if (far_edge_exclusive) {
        if (uhi > ulo) uhi--;
        if (vhi > vlo) vhi--;
    }
    for (int i = 0; i < n; i++) {
        v[i].ulo = ulo;
        v[i].vlo = vlo;
        v[i].uhi = uhi;
        v[i].vhi = vhi;
    }
}

// Corners B0 B1 B2 B3 (B2 and B3 already swapped into hardware order) become triangles B0 B1 B3, B1 B3 B2.
void split_quad(PrimVertex* v) {
    v[4] = v[3];
    v[5] = v[2];
    v[2] = v[3];
    v[3] = v[1];
}

// The GPU does not draw a polygon triangle wider than 1023 or taller than 511 pixels.
int apply_size_rule(PrimVertex* v, int count, const StreamFrame& f) {
    if (!f.size_rule) return count;
    int kept = 0;
    for (int t = 0; t + 2 < count; t += 3) {
        int x0 = v[t].x, x1 = v[t].x, y0 = v[t].y, y1 = v[t].y;
        for (int i = 1; i < 3; i++) {
            if (v[t + i].x < x0) x0 = v[t + i].x;
            if (v[t + i].x > x1) x1 = v[t + i].x;
            if (v[t + i].y < y0) y0 = v[t + i].y;
            if (v[t + i].y > y1) y1 = v[t + i].y;
        }
        if (static_cast<float>(x1 - x0) > f.max_poly_width || (y1 - y0) > 511) continue;
        if (kept != t) std::memcpy(static_cast<void*>(&v[kept]), &v[t], 3 * sizeof(PrimVertex));
        kept += 3;
    }
    return kept;
}

class Emitter {
public:
    explicit Emitter(StreamTarget& target) : t_(target) {}

    void operator()(const Polygon& p) {
        const int n = p.count == 3 ? 3 : 4;
        if (p.textured) t_.latch_page(p.page.word());
        const StreamFrame f = t_.frame();

        // A textured flat triangle with every coordinate -1 is the SDK's way of changing the page without a
        // draw-mode command: it draws nothing.
        if (p.textured && !p.gouraud && n == 3) {
            bool null = true;
            for (int i = 0; i < 3; i++) null = null && p.xy[i].x == -1 && p.xy[i].y == -1;
            if (null) return;
        }

        StreamRun run;
        run.shape = RunShape::Polygon;
        run.textured = p.textured;
        run.translucent = p.shade.translucent;
        run.page = p.page.word();
        run.palette = p.palette;
        run.color = p.color[0];
        PrimVertex* v = t_.begin(run, n == 4 ? 6 : 3);
        if (v == nullptr) return;

        // Corner k of the run is hardware vertex order[k]: a quad's last two corners are swapped so that the
        // split below yields (0 1 2) (1 2 3).
        static constexpr int kQuad[4] = {0, 1, 3, 2};
        static constexpr int kTri[3] = {0, 1, 2};
        const int* order = n == 4 ? kQuad : kTri;
        clear(v, n);
        const bool shaded = p.gouraud || !p.shade.raw_texture;
        const std::uint16_t page = p.page.word();
        const std::uint8_t dither = p.textured ? (((f.page >> 9) & 1) != 0 || f.dither ? 1 : 0)
                                               : (p.gouraud ? 1 : 0);
        for (int k = 0; k < n; k++) {
            const int i = order[k];
            PrimVertex& c = v[k];
            place(c, p.xy[i], f);
            if (f.mark_geometry) c.geom3d = 1.f;
            if (f.precise) {
                c.ppx = c.x;
                c.ppy = c.y;
            }
            if (p.textured) {
                c.u = p.uv[i].u;
                c.v = p.uv[i].v;
                c.bright = kBrightTextured;
                c.dither = dither;
                c.page = vertex_page(page);
                c.clut = static_cast<std::int16_t>(p.palette.word());
            }
            paint(c, shaded ? (p.gouraud ? p.color[i] : p.color[0]) : Rgb{128, 128, 128});
        }
        if (!p.textured) untextured(v, n, dither);
        else texel_box(v, n, true);
        if (p.gouraud) {
            // Vertex 0 has no fog byte of its own: it takes vertex 1's, except on a textured quad, whose vertex 0
            // amount lives in a spare half the command leaves clear.
            const bool textured_quad = p.textured && n == 4;
            for (int k = 0; k < n; k++) {
                const int i = order[k];
                v[k].fog = static_cast<std::int8_t>(i == 0 ? (textured_quad ? 0 : p.fog.amount[1]) : p.fog.amount[i]);
            }
        }
        t_.shape(run, v, n);
        int count = n;
        if (n == 4) {
            split_quad(v);
            count = 6;
        }
        t_.end(apply_size_rule(v, count, f));
    }

    void operator()(const Sprite& s) {
        const StreamFrame f = t_.frame();
        StreamRun run;
        run.shape = RunShape::Sprite;
        run.textured = true;
        run.translucent = s.shade.translucent;
        run.page = f.page;
        run.palette = s.palette;
        run.color = s.color;
        run.size = s.size;
        run.at = s.at;
        run.w = rect_w(s.size, static_cast<std::int16_t>(s.w));
        run.h = rect_w(s.size, static_cast<std::int16_t>(s.h));
        run.uv = s.uv;
        PrimVertex* v = t_.begin(run, 6);
        if (v == nullptr) return;
        clear(v, 4);
        rect_corners(v, run, f);

        // The far edge of a 1:1 rectangle is exclusive, so its last texel is u + w - 1; only an edge exactly on
        // the page edge (256) does not fit and is carried as an offset.
        int w = run.w, h = run.h;
        const int u0 = s.uv.u, v0 = s.uv.v;
        const int last_u = (u0 + w <= 255 && w > 0) ? u0 + w - 1 : -1;
        const int last_v = (v0 + h <= 255 && h > 0) ? v0 + h - 1 : -1;
        bool extend_u = false, extend_v = false;
        if (u0 + w == 256) {
            w = 255 - u0;
            extend_u = true;
        } else if (u0 + w > 255) {
            w = 255 - u0;
        }
        if (v0 + h == 256) {
            h = 255 - v0;
            extend_v = true;
        } else if (v0 + h > 255) {
            h = 255 - v0;
        }
        const std::uint8_t us[4] = {static_cast<std::uint8_t>(u0), static_cast<std::uint8_t>(u0),
                                    static_cast<std::uint8_t>(u0 + w), static_cast<std::uint8_t>(u0 + w)};
        const std::uint8_t vs[4] = {static_cast<std::uint8_t>(v0), static_cast<std::uint8_t>(v0 + h),
                                    static_cast<std::uint8_t>(v0 + h), static_cast<std::uint8_t>(v0)};
        const Rgb color = s.shade.raw_texture ? Rgb{128, 128, 128} : s.color;
        for (int k = 0; k < 4; k++) {
            v[k].u = us[k];
            v[k].v = vs[k];
            v[k].bright = kBrightTextured;
            v[k].dither = 0;
            v[k].page = vertex_page(f.page);
            v[k].clut = static_cast<std::int16_t>(s.palette.word());
            paint(v[k], color);
        }
        if (extend_u) v[2].tcx = v[3].tcx = 2;
        if (extend_v) v[1].tcy = v[2].tcy = 2;
        texel_box(v, 4, false);
        for (int k = 0; k < 4; k++) {
            if (last_u >= 0) v[k].uhi = static_cast<std::uint8_t>(last_u);
            if (last_v >= 0) v[k].vhi = static_cast<std::uint8_t>(last_v);
        }
        t_.shape(run, v, 4);
        split_quad(v);
        t_.end(6);
    }

    void operator()(const Fill& fill) {
        const StreamFrame f = t_.frame();
        StreamRun run;
        run.shape = RunShape::Fill;
        run.translucent = fill.translucent;
        run.page = f.page;
        run.color = fill.color;
        run.size = fill.size;
        run.at = fill.at;
        run.w = rect_w(fill.size, static_cast<std::int16_t>(fill.w));
        run.h = rect_w(fill.size, static_cast<std::int16_t>(fill.h));
        PrimVertex* v = t_.begin(run, 6);
        if (v == nullptr) return;
        clear(v, 4);
        rect_corners(v, run, f);
        untextured(v, 4, 0);
        for (int k = 0; k < 4; k++) paint(v[k], fill.color);
        t_.shape(run, v, 4);
        split_quad(v);
        t_.end(6);
    }

    void operator()(const Line& l) {
        const int n = l.count < 2 ? 2 : (l.count > 4 ? 4 : l.count);
        const StreamFrame f = t_.frame();
        StreamRun run;
        run.shape = RunShape::Line;
        run.translucent = l.translucent;
        run.page = f.page;
        run.color = l.color[0];
        PrimVertex* v = t_.begin(run, 6 * (n - 1));
        if (v == nullptr) return;
        PrimVertex* seg = v;
        for (int i = 0; i + 1 < n; i++, seg += 6) {
            Point p0 = l.xy[i], p1 = l.xy[i + 1];
            Rgb c0 = l.gouraud ? l.color[i] : l.color[0];
            Rgb c1 = l.gouraud ? l.color[i + 1] : l.color[0];
            // Drawn left to right (top to bottom when vertical), one pixel thick across its major axis.
            if (p0.x > p1.x || (p0.y > p1.y && p0.x == p1.x)) {
                std::swap(p0, p1);
                std::swap(c0, c1);
            }
            const std::int16_t dx = s16(p1.x - p0.x), dy = s16(p1.y - p0.y);
            clear(seg, 4);
            place(seg[0], p0, f);
            if (dx > std::abs(static_cast<int>(dy))) {
                seg[1].x = s16(p1.x + f.offset_x + 1);
                seg[1].y = s16(p1.y + f.offset_y);
                seg[2].x = seg[1].x;
                seg[2].y = s16(seg[1].y + 1);
                seg[3].x = seg[0].x;
                seg[3].y = s16(seg[0].y + 1);
            } else {
                seg[1].x = s16(p1.x + f.offset_x);
                seg[1].y = s16(p1.y + f.offset_y + 1);
                seg[2].x = s16(seg[1].x + 1);
                seg[2].y = seg[1].y;
                seg[3].x = s16(seg[0].x + 1);
                seg[3].y = seg[0].y;
            }
            for (int k = 1; k < 4; k++) seg[k].z = f.depth;
            untextured(seg, 4, 0);
            paint(seg[0], c0);
            paint(seg[1], c1);
            paint(seg[2], c1);
            paint(seg[3], c0);
            t_.shape(run, seg, 4);
            split_quad(seg);
        }
        t_.end(6 * (n - 1));
    }

    void operator()(const DrawMode& m) {
        const std::uint16_t word = m.page.word();
        t_.latch_mode(static_cast<std::uint16_t>(word & 0x1FF), m.dither || (word & 0x200) != 0);
    }

    void operator()(const DrawArea& a) { t_.latch_area(a); }
    void operator()(const Mask& m) { t_.latch_mask(m); }

private:
    static std::int16_t rect_w(RectSize size, std::int16_t free) {
        switch (size) {
            case RectSize::Free: return free;
            case RectSize::Px1: return 1;
            case RectSize::Px8: return 8;
            case RectSize::Px16: return 16;
        }
        return free;
    }

    static void rect_corners(PrimVertex* v, const StreamRun& run, const StreamFrame& f) {
        place(v[0], run.at, f);
        v[1].x = v[0].x;
        v[1].y = s16(v[0].y + run.h);
        v[2].x = s16(v[0].x + run.w);
        v[2].y = s16(v[0].y + run.h);
        v[3].x = s16(v[0].x + run.w);
        v[3].y = v[0].y;
        for (int k = 1; k < 4; k++) v[k].z = f.depth;
    }

    StreamTarget& t_;
};

}  // namespace

void emit_stream(const Command& command, StreamTarget& target) {
    std::visit(Emitter(target), command);
}

}  // namespace sh::draw2d
