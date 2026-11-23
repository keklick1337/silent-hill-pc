// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/screens/paper_map.h"

#include <algorithm>
#include <climits>

namespace sh::screens {

namespace {

constexpr int kScreenW = 320;
constexpr int kScreenH = 240;
constexpr std::int32_t kOne = 4096;
constexpr std::int32_t kCellSize = 40 * kOne;  // a world cell, Q12 metres
constexpr int kTile = 128;                     // texels per page tile side
constexpr std::uint8_t kTexturedTranslucentQuad = 0x2E;

// Q12 product with a 64-bit intermediate, as the original's precise multiply.
std::int32_t mul_q12(std::int32_t a, std::int32_t b) {
    return static_cast<std::int32_t>((static_cast<std::int64_t>(a) * b) >> 12);
}

// Texture coordinates packed as the game's 32-bit stores: u + v * 256 + extra * 65536, added with carries.
struct PackedTexel {
    draw2d::TexCoord uv;
    std::uint16_t extra = 0;
};
PackedTexel unpack(std::uint32_t word) {
    return {{static_cast<std::uint8_t>(word), static_cast<std::uint8_t>(word >> 8)},
            static_cast<std::uint16_t>(word >> 16)};
}

std::uint32_t palette_word(std::int16_t x, std::int16_t y) {
    return (static_cast<std::uint32_t>(y) << 6) | ((static_cast<std::uint32_t>(x) >> 4) & 0x3F);
}

// The texture page word of a 4-bit/8-bit page at texel column x, row y (both in texture memory pixels).
std::uint32_t page_word(std::uint32_t depth, std::uint32_t x, std::uint32_t y) {
    return ((depth & 3) << 7) | ((y & 0x100) >> 4) | ((x & 0x3FF) >> 6) | ((y & 0x200) << 2);
}

draw2d::Rgb rgb(const MapRgb& c) {
    return {static_cast<std::uint8_t>(c.r), static_cast<std::uint8_t>(c.g), static_cast<std::uint8_t>(c.b)};
}

draw2d::Point xy(std::int32_t x, std::int32_t y) {
    return {static_cast<std::int16_t>(x), static_cast<std::int16_t>(y)};
}

// World cell of a position: cell -1 starts at 0 (not below it), so 0 itself is in cell -1.
std::int32_t cell_of(std::int32_t p) { return p <= 0 ? (p - kCellSize) / kCellSize : p / kCellSize; }

// Chunk of a position, counted from 1 above 0 and from -1 at 0 and below.
std::int32_t chunk_of(std::int32_t p) { return p > 0 ? p / kCellSize + 1 : p / kCellSize - 1; }

std::int16_t turn_q12(int degrees) {
    return static_cast<std::int16_t>(static_cast<std::int32_t>((static_cast<float>(degrees) / 360.0f) * 4096.0f));
}

std::int32_t grid_coord(const MapPlacementCoord& c, const PaperMapPlayer& p) {
    if (!c.from_position) {
        return c.fixed;
    }
    std::int32_t v = c.axis == 'x' ? p.x : p.z;
    if (c.negate) {
        v = -v;
    }
    v += c.add_metres * kOne;
    // Division by 2^shift toward zero.
    if (v < 0) {
        v += (1 << c.shift) - 1;
    }
    return (v >> c.shift) + c.offset;
}

bool rule_holds(const MapPlacementRule& r, const PaperMapPlayer& p, std::int32_t cx, std::int32_t cz) {
    if (!r.maps.empty() && std::find(r.maps.begin(), r.maps.end(), p.map) == r.maps.end()) {
        return false;
    }
    if (!r.cells.empty()) {
        if (std::none_of(r.cells.begin(), r.cells.end(),
                         [&](const std::array<int, 2>& c) { return c[0] == cx && c[1] == cz; })) {
            return false;
        }
    }
    if (!r.chunks.empty()) {
        const std::int32_t hx = chunk_of(p.x), hz = chunk_of(p.z);
        if (std::none_of(r.chunks.begin(), r.chunks.end(),
                         [&](const std::array<int, 2>& c) { return c[0] == hx && c[1] == hz; })) {
            return false;
        }
    }
    if (r.cell_z_below && !(cz < *r.cell_z_below)) {
        return false;
    }
    if (r.cell_x_in && !(cx >= (*r.cell_x_in)[0] && cx <= (*r.cell_x_in)[1])) {
        return false;
    }
    return true;
}

}  // namespace

PaperMapBook PaperMapBook::build(MapScreenData data, const PaperMapNames& names) {
    PaperMapBook book;
    book.data_ = std::move(data);
    const MapScreenData& d = book.data_;
    auto report = [&](const std::string& what) {
        if (names.report) names.report(what);
    };
    auto page_of = [&](const std::string& name, const char* what) {
        const int n = names.page ? names.page(name) : -1;
        if (n < 0 || n >= kPaperMapPages) {
            report(std::string(what) + ": unknown paper map \"" + name + "\"");
            return -1;
        }
        return n;
    };
    auto file_of = [&](const std::string& name) {
        const int n = names.file ? names.file(name) : -1;
        if (n < 0) report("page_files: unknown disc file \"" + name + "\"");
        return n;
    };

    for (const MapPageFiles& f : d.page_files) {
        const int n = page_of(f.page, "page_files");
        if (n < 0) continue;
        book.pages_[n].image_file = file_of(f.image);
        book.pages_[n].marking_file = f.markings.empty() ? -1 : file_of(f.markings);
    }
    for (const MapPageLink& link : d.links) {
        const int n = page_of(link.page, "links");
        if (n < 0) continue;
        if (!link.up.empty()) book.pages_[n].up = std::max(page_of(link.up, "links"), 0);
        if (!link.down.empty()) book.pages_[n].down = std::max(page_of(link.down, "links"), 0);
    }
    for (std::size_t i = 0; i < d.markings.size(); ++i) {
        const MapPageMarks& pm = d.markings[i];
        const int n = page_of(pm.page, "markings");
        if (n < 0) continue;
        std::vector<std::array<int, 2>> flags;
        for (const MapMark& m : pm.marks) {
            std::array<int, 2> pair{-1, -1};
            for (std::size_t half = 0; half < 2; ++half) {
                pair[half] = names.flag ? names.flag(m.flags[half]) : -1;
                if (pair[half] < 0) {
                    report("markings of " + pm.page + ": unknown event flag \"" + m.flags[half] + "\"");
                }
            }
            flags.push_back(pair);
        }
        for (std::size_t a = 0; a < d.atlases.size(); ++a) {
            if (d.atlases[a].name == pm.atlas) book.pages_[n].atlas = static_cast<int>(a);
        }
        book.pages_[n].marks = static_cast<int>(i);
        book.pages_[n].mark_flags = std::move(flags);
    }
    for (std::size_t g = 0; g < d.placement.size(); ++g) {
        for (const std::string& name : d.placement[g].pages) {
            const int n = page_of(name, "player_placement");
            if (n >= 0) book.pages_[n].placement = static_cast<int>(g);
        }
    }
    return book;
}

const PaperMapPage* PaperMapBook::page(int n) const {
    return n >= 0 && n < kPaperMapPages ? &pages_[static_cast<std::size_t>(n)] : nullptr;
}

namespace {

// The view of a pan point at a zoom, without the "no pan point" test (the close step skips it).
PaperMapView view_through(std::int32_t pan_x, std::int32_t pan_y, std::int32_t zoom) {
    PaperMapView v;
    v.left = static_cast<std::uint16_t>(static_cast<std::uint32_t>(pan_x - mul_q12(zoom, pan_x)) >> 12);
    v.top = static_cast<std::uint16_t>(static_cast<std::uint32_t>(pan_y - mul_q12(zoom, pan_y)) >> 12);
    v.scale = static_cast<std::uint16_t>((zoom >> 1) + kOne / 2);
    return v;
}

}  // namespace

PaperMapView paper_map_view(std::int32_t pan_x, std::int32_t pan_y, std::int32_t zoom) {
    PaperMapView v = view_through(pan_x, pan_y, zoom);
    if (pan_x < 0) {
        v.left = 0;
        v.top = 0;
    }
    return v;
}

PaperMapWideView paper_map_wide_view(std::uint16_t left, std::uint16_t scale, float wide) {
    PaperMapWideView out;
    out.left = left;
    if (wide <= 1.0f) {
        return out;
    }
    const std::int32_t half43 = mul_q12(scale, kScreenW / 2);
    const std::int32_t view_half = static_cast<std::int32_t>(static_cast<float>(half43) * wide);
    std::int32_t centre = left + half43;
    if (view_half * 2 >= kScreenW) {
        centre = kScreenW / 2;
        out.span = {true, 0, kScreenW};
    } else {
        centre = std::max(std::min(centre, kScreenW - view_half), view_half);
        out.span = {true, centre - view_half, centre + view_half};
    }
    out.left = static_cast<std::uint16_t>(centre - half43);
    return out;
}

PaperMapWideSpan paper_map_scene_span(float wide) {
    PaperMapWideSpan out;
    if (wide > 1.0f) {
        out = {true, 0, kScreenW};
    }
    return out;
}

std::vector<draw2d::Polygon> paper_map_tiles(const PaperMapView& view, const PaperMapImage& image, int shade,
                                             const PaperMapWideSpan* wide) {
    std::vector<draw2d::Polygon> out;
    const int scale = view.scale;
    // Screen x of each tile column edge and y of each row edge; the page spans -160..160 by -240..240 (the screen
    // is interlaced, 480 lines).
    std::int16_t xs[4] = {-160, -160, -160, -160};
    std::int16_t ys[3] = {-240, -240, -240};
    int left = view.left;
    int right = static_cast<std::uint16_t>(mul_q12(scale, 320) + view.left - 1);
    const int bottom = static_cast<std::uint16_t>(mul_q12(scale, 240) + view.top - 1);
    if (wide != nullptr && wide->on) {
        // The tiles start left of the 4:3 view by its distance in texels, scaled to the screen.
        const std::int16_t start =
            static_cast<std::int16_t>(-160 + static_cast<std::int16_t>(((wide->left - left) << 12) / scale));
        std::fill(std::begin(xs), std::end(xs), start);
        left = static_cast<std::uint16_t>(wide->left);
        right = static_cast<std::uint16_t>(wide->right - 1);
    }
    // The picture starts v / 2 texels into its rows of tiles.
    const int top = view.top + (image.v >> 1);
    const int low = bottom + (image.v >> 1);
    const std::uint32_t palette = palette_word(image.clut_x, image.clut_y);
    const std::uint16_t rg = static_cast<std::uint16_t>(shade + (shade << 8));

    for (int tx = 0; tx < 3; ++tx) {
        for (int ty = 0; ty < 2; ++ty) {
            if ((tx + 1) * kTile < left || right < tx * kTile) continue;
            if ((ty + 1) * kTile < top || low < ty * kTile) continue;

            const std::int16_t cut_left = static_cast<std::int16_t>(left < tx * kTile ? tx * kTile : left);
            const std::int16_t cut_w = static_cast<std::int16_t>((tx + 1) * kTile < right ? (tx + 1) * kTile - cut_left
                                                                                          : right - cut_left);
            const std::int16_t cut_top = static_cast<std::int16_t>(top >= ty * kTile ? top : ty * kTile);
            const std::int16_t cut_h =
                static_cast<std::int16_t>((ty + 1) * kTile < low ? (ty + 1) * kTile - cut_top : low - cut_top);
            const std::int32_t u = cut_left - tx * kTile;
            const std::int32_t v = cut_top - ty * kTile;

            xs[tx + 1] = static_cast<std::int16_t>(xs[tx] + ((cut_w << 12) / scale));
            ys[ty + 1] = static_cast<std::int16_t>(ys[ty] + ((cut_h << 13) / scale));

            const std::uint32_t column = image.column + static_cast<std::uint32_t>(ty * 16);
            const std::uint32_t page =
                page_word(image.depth, (column + static_cast<std::uint32_t>(tx)) << 6, ((column >> 4) & 1) << 8);
            // The tile's texels are 2 rows per screen row: the picture is stored at half height in each tile.
            const PackedTexel t0 = unpack(static_cast<std::uint32_t>(u + ((v << 1) << 8)) + (palette << 16));
            const PackedTexel t1 = unpack(static_cast<std::uint32_t>((u + cut_w) + ((v << 1) << 8)) + (page << 16));
            const std::int16_t bottom_v = static_cast<std::int16_t>((static_cast<std::int16_t>(v) << 1) - 1);
            const std::int32_t row = (bottom_v + (cut_h << 1)) << 8;
            const PackedTexel t2 = unpack(static_cast<std::uint16_t>(u + row));
            const PackedTexel t3 = unpack(static_cast<std::uint16_t>((cut_w + u) + row));

            draw2d::Polygon tile;
            tile.count = 4;
            tile.textured = true;
            tile.color[0] = {static_cast<std::uint8_t>(rg), static_cast<std::uint8_t>(rg >> 8),
                             static_cast<std::uint8_t>(shade)};
            tile.xy[0] = {xs[tx], ys[ty]};
            tile.xy[1] = {xs[tx + 1], ys[ty]};
            tile.xy[2] = {xs[tx], ys[ty + 1]};
            tile.xy[3] = {xs[tx + 1], ys[ty + 1]};
            tile.uv[0] = t0.uv;
            tile.uv[1] = t1.uv;
            tile.uv[2] = t2.uv;
            tile.uv[3] = t3.uv;
            tile.palette = draw2d::Palette::from_word(t0.extra);
            tile.page = draw2d::TexturePage::from_word(t1.extra);
            out.push_back(tile);
        }
    }
    return out;
}

std::optional<std::vector<draw2d::Polygon>> paper_map_marks(const PaperMapBook& book, int page, int mode, int pick,
                                                            int shade, const PaperMapView& view, std::int16_t clut_x,
                                                            std::int16_t clut_y,
                                                            const std::function<bool(int)>& flag) {
    const PaperMapPage* p = book.page(page);
    if (p == nullptr || p->marking_file < 0 || p->atlas < 0 || p->marks < 0) {
        return std::nullopt;
    }
    const MapScreenData& d = book.data();
    const std::vector<MapSprite>& sprites = d.atlases[static_cast<std::size_t>(p->atlas)].sprites;
    const std::vector<MapMark>& marks = d.markings[static_cast<std::size_t>(p->marks)].marks;
    const int total = static_cast<int>(marks.size());

    std::vector<draw2d::Polygon> out;
    int first = 0;
    int end = total;
    int pick_half = 0;
    if (mode == static_cast<int>(PaperMapMarks::One)) {
        // `pick` is the flag of one mark half. The original finds the mark by halving the flag's distance from the
        // page's first flag, rounding up: the second half of mark i picks mark i + 1, whose second half is drawn.
        first = total;
        for (int i = 0; i < total && first == total; ++i) {
            for (int half = 0; half < 2; ++half) {
                if (p->mark_flags[static_cast<std::size_t>(i)][static_cast<std::size_t>(half)] == pick && pick >= 0) {
                    first = i + half;
                    pick_half = half;
                    break;
                }
            }
        }
        end = first + 1;
        if (first >= total) {
            return out;
        }
    } else {
        shade = d.marking_shade;
    }
    const int scale = view.scale;
    const std::uint32_t palette = palette_word(clut_x, clut_y);
    const std::uint32_t colour = static_cast<std::uint32_t>(shade) + (static_cast<std::uint32_t>(shade) << 8) +
                                 (static_cast<std::uint32_t>(shade) << 16) +
                                 (static_cast<std::uint32_t>(kTexturedTranslucentQuad) << 24);

    for (int i = first; i < end; ++i) {
        for (int half = 0; half < 2; ++half) {
            if (mode == static_cast<int>(PaperMapMarks::Found)) {
                const int bit = p->mark_flags[static_cast<std::size_t>(i)][static_cast<std::size_t>(half)];
                if (bit < 0 || !flag(bit)) continue;
            }
            if (mode == static_cast<int>(PaperMapMarks::One) && pick_half != half) continue;
            const MapMark& m = marks[static_cast<std::size_t>(i)];
            const int number = m.sprites[static_cast<std::size_t>(half)];
            if (number == 0) continue;
            const MapSprite& s = sprites[static_cast<std::size_t>(number - 1)];

            // Marks are placed in half texels from the page centre.
            const std::int32_t x0 = (((m.x << 1) - (view.left - 160)) * kOne) / scale - 160;
            const std::int16_t y0 =
                static_cast<std::int16_t>((((m.y << 1) - (view.top * 2 - 240)) * kOne) / scale - 240);
            const std::int32_t x1 = x0 + (s.w * kOne) / scale;
            const std::int32_t y1 = y0 + static_cast<std::int16_t>((s.h * kOne) / scale);

            // The marking texture sits 128 texels into its page.
            const std::uint32_t u = static_cast<std::uint32_t>(s.u), v = static_cast<std::uint32_t>(s.v);
            const std::uint32_t w = static_cast<std::uint32_t>(s.w), h = static_cast<std::uint32_t>(s.h);
            const PackedTexel t0 = unpack(u + ((v << 8) + 0x80) + (palette << 16));
            const PackedTexel t1 = unpack(u + (w + 0x80) + (v << 8) + 0x470000);
            const PackedTexel t2 = unpack(static_cast<std::uint16_t>(u + (((v + h) << 8) + 0x80)));
            const PackedTexel t3 = unpack(static_cast<std::uint16_t>(w + (u + 0x80) + ((v + h) << 8)));

            draw2d::Polygon q;
            q.count = 4;
            q.textured = true;
            q.shade = {((colour >> 24) & 2) != 0, ((colour >> 24) & 1) != 0};
            q.color[0] = {static_cast<std::uint8_t>(colour), static_cast<std::uint8_t>(colour >> 8),
                          static_cast<std::uint8_t>(colour >> 16)};
            q.xy[0] = xy(x0, y0);
            q.xy[1] = xy(x1, y0);
            q.xy[2] = xy(x0, y1);
            q.xy[3] = xy(x1, y1);
            q.uv[0] = t0.uv;
            q.uv[1] = t1.uv;
            q.uv[2] = t2.uv;
            q.uv[3] = t3.uv;
            q.palette = draw2d::Palette::from_word(t0.extra);
            q.page = draw2d::TexturePage::from_word(t1.extra);
            out.push_back(q);
        }
    }
    return out;
}

std::optional<PaperMapPoint> paper_map_player_point(const PaperMapBook& book, int page, const PaperMapPlayer& player) {
    const PaperMapPage* p = book.page(page);
    if (p == nullptr || p->placement < 0) {
        return std::nullopt;
    }
    const MapScreenData& d = book.data();
    const std::int32_t cx = cell_of(player.x);
    const std::int32_t cz = cell_of(player.z);
    for (const MapPlacementRule& r : d.placement[static_cast<std::size_t>(p->placement)].rules) {
        if (!rule_holds(r, player, cx, cz)) continue;
        PaperMapPoint out;
        out.heading = static_cast<std::int16_t>(player.heading + turn_q12(r.turn_degrees));
        if (r.anchor) {
            std::array<int, 2> at{0, 0};
            if (const MapAnchorGrid* g = d.anchor_grid(r.anchor->grid)) {
                if (const std::optional<std::array<int, 2>> a = g->at(cx, cz)) at = *a;
            }
            // The position inside the cell, from its centre.
            const std::int32_t in_x = player.x - cx * kCellSize - kCellSize / 2;
            const std::int32_t in_z = player.z - cz * kCellSize - kCellSize / 2;
            out.x = static_cast<std::int16_t>(at[0] + (in_x * r.anchor->x[0]) / r.anchor->x[1]);
            out.z = static_cast<std::int16_t>(at[1] + (in_z * r.anchor->z[0]) / r.anchor->z[1]);
        } else {
            out.x = static_cast<std::int16_t>(grid_coord(r.x, player));
            out.z = static_cast<std::int16_t>(grid_coord(r.z, player));
        }
        // The original's "no point" value.
        if (out.x == SHRT_MAX) return std::nullopt;
        return out;
    }
    return std::nullopt;
}

std::int32_t paper_map_packed_point(const std::optional<PaperMapPoint>& point) {
    if (!point) return 0;
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(static_cast<std::int32_t>(point->z)) << 16) +
           point->x;
}

std::int16_t paper_map_fold_heading(std::int32_t heading) {
    const std::int32_t folded = heading & 0xFFF;
    return static_cast<std::int16_t>(folded > 2048 ? (folded | 0xF000) : folded);
}

PaperMapArrow paper_map_arrow(const MapPlayerArrow& style, const PaperMapPoint& point, const PaperMapView& view,
                              std::int32_t sin, std::int32_t cos) {
    const std::int16_t sx = static_cast<std::int16_t>(((point.x - (view.left - kScreenW / 2)) * kScreenW) /
                                                          mul_q12(view.scale, kScreenW) -
                                                      kScreenW / 2);
    const std::int16_t sy = static_cast<std::int16_t>(((point.z - (view.top - kScreenH / 2)) * kScreenH) /
                                                          mul_q12(view.scale, kScreenH) -
                                                      kScreenH / 2);
    const int tip = style.tip, back = style.back, hw = style.half_width;
    // The screen is interlaced: y doubles.
    const draw2d::Point p_tip = xy(sx + ((sin * tip) >> 12), (sy + ((cos * -tip) >> 12)) * 2);
    const draw2d::Point p_left =
        xy(sx + ((cos * hw - sin * back) >> 12), (sy + ((sin * hw + cos * back) >> 12)) * 2);
    const draw2d::Point p_right =
        xy(sx + ((cos * -hw - sin * back) >> 12), (sy + ((sin * -hw + cos * back) >> 12)) * 2);

    PaperMapArrow a;
    a.outline.count = 4;
    a.outline.xy[0] = p_tip;
    a.outline.xy[1] = p_left;
    a.outline.xy[2] = p_right;
    a.outline.xy[3] = p_tip;
    a.outline.color[0] = rgb(style.outline);
    a.fill.count = 3;
    a.fill.gouraud = true;
    a.fill.xy[0] = p_tip;
    a.fill.xy[1] = p_left;
    a.fill.xy[2] = p_right;
    a.fill.color[0] = rgb(style.tip_color);
    a.fill.color[1] = rgb(style.back_color);
    a.fill.color[2] = a.fill.color[1];
    return a;
}

std::vector<draw2d::Polygon> paper_map_page_arrows(const MapPageArrows& style, bool up, bool down) {
    std::vector<draw2d::Polygon> out;
    const bool shown[2] = {up, down};
    for (int i = 0; i < 2; ++i) {
        if (!shown[i]) continue;
        const int sign = i != 0 ? 1 : -1;
        draw2d::Polygon p;
        p.count = 3;
        p.gouraud = true;
        p.xy[0] = xy(0, sign * style.tip_y);
        p.xy[1] = xy(-style.half_width, sign * style.base_y);
        p.xy[2] = xy(style.half_width, sign * style.base_y);
        p.color[0] = rgb(style.tip_color);
        p.color[1] = rgb(style.base_color);
        p.color[2] = p.color[1];
        out.push_back(p);
    }
    return out;
}

std::array<draw2d::Command, 2> paper_map_close_dim() {
    draw2d::DrawMode mode;
    mode.page.blend = draw2d::Blend::Subtract;
    mode.dither = true;
    draw2d::Fill tile;
    tile.at = {-160, -224};
    tile.w = 320;
    tile.h = 448;
    tile.color = {8, 8, 8};
    tile.translucent = true;
    return {mode, tile};
}

void paper_map_draw_page(PaperMapHost& host, const PaperMapBook& book, const PaperMapView& view,
                         const PaperMapWideSpan* wide) {
    for (const draw2d::Polygon& tile : paper_map_tiles(view, host.page_image(), book.data().image_shade, wide)) {
        host.submit(PaperMapLayer::Page, tile);
    }
    host.pause_music();
}

bool paper_map_draw_marks(PaperMapHost& host, const PaperMapBook& book, int mode, int page, int pick, int shade,
                          const PaperMapView& view) {
    const std::array<std::int16_t, 2> palette = host.marking_palette();
    const std::optional<std::vector<draw2d::Polygon>> quads = paper_map_marks(
        book, page, mode, pick, shade, view, palette[0], palette[1], [&host](int n) { return host.flag(n); });
    if (!quads) return false;
    for (const draw2d::Polygon& q : *quads) host.submit(PaperMapLayer::Marks, q);
    return true;
}

std::int32_t paper_map_draw_player(PaperMapHost& host, const PaperMapBook& book, int page, const PaperMapView& view) {
    if (host.saved_page() != page) return 0;
    const std::optional<PaperMapPoint> at = paper_map_player_point(book, page, host.player());
    const std::int32_t packed = paper_map_packed_point(at);
    if (!at || host.input().hold_arrow) return packed;
    // A view this small would divide by zero (the original crashed); nothing to draw then.
    if (mul_q12(view.scale, kScreenH) == 0) return packed;
    const std::int16_t heading = paper_map_fold_heading(at->heading);
    const PaperMapArrow arrow =
        paper_map_arrow(book.data().player_arrow, *at, view, host.sin(heading), host.cos(heading));
    host.submit(PaperMapLayer::Arrows, arrow.outline);
    host.submit(PaperMapLayer::Arrows, arrow.fill);
    return packed;
}

void paper_map_draw_page_arrows(PaperMapHost& host, const PaperMapBook& book, int page) {
    const PaperMapPage* p = book.page(page);
    if (p == nullptr) return;
    for (const draw2d::Polygon& a :
         paper_map_page_arrows(book.data().page_arrows, host.has_page(p->up), host.has_page(p->down))) {
        host.submit(PaperMapLayer::Arrows, a);
    }
}

namespace {

int image_file(const PaperMapBook& book, int page) {
    const PaperMapPage* p = book.page(page);
    return p != nullptr ? p->image_file : -1;
}

int marking_file(const PaperMapBook& book, int page) {
    const PaperMapPage* p = book.page(page);
    return p != nullptr ? p->marking_file : -1;
}

// The step numbers of the game state (scripts and the replay dump see them).
enum Step { Open = 0, Load = 1, Show = 2, Turn = 3, Close = 4 };

}  // namespace

void PaperMapScreen::update(PaperMapHost& host, const PaperMapBook& book) {
    host.frame_upkeep();
    switch (host.step()) {
        case Open:
            host.enter_display();
            loaded_marking_file_ = marking_file(book, host.saved_page());
            page_ = host.saved_page();
            pan_x_ = -1;
            zoom_ = kOne;
            zoomed_ = false;
            host.stop_radio();
            host.play_map_sound();
            host.store_frame();
            host.load_page_image(image_file(book, page_));
            host.finish_loads();
            host.set_vblank_interval(1);
            host.fade_in();
            host.set_step(Show);
            break;

        case Show:
            show(host, book);
            break;

        case Turn: {
            if (host.fade_done()) {
                page_ = turn_to_;
                host.play_map_sound();
                host.set_step(Load);
                break;
            }
            const PaperMapWideView wide = paper_map_wide_view(0, kOne, host.wide_factor());
            if (wide.span.on) host.note_wide_frame();
            const PaperMapView view{wide.left, 0, kOne};
            paper_map_draw_player(host, book, page_, view);
            paper_map_draw_marks(host, book, static_cast<int>(PaperMapMarks::Found), page_, 0, 0, view);
            paper_map_draw_page(host, book, view, &wide.span);
            break;
        }

        case Load: {
            host.load_page_image(image_file(book, page_));
            const int marks = marking_file(book, page_);
            if (marks != loaded_marking_file_ && marks >= 0) {
                loaded_marking_file_ = marks;
                host.load_marking_texture(marks);
            }
            host.finish_loads();
            host.fade_in();
            host.set_step(Show);
            break;
        }

        case Close: {
            PaperMapView view = view_through(pan_x_, pan_y_, zoom_);
            const PaperMapWideView wide = paper_map_wide_view(view.left, view.scale, host.wide_factor());
            if (wide.span.on) host.note_wide_frame();
            view.left = wide.left;
            paper_map_draw_player(host, book, page_, view);
            paper_map_draw_marks(host, book, static_cast<int>(PaperMapMarks::Found), page_, 0, 0, view);
            paper_map_draw_page(host, book, view, &wide.span);
            if (host.fade_done()) {
                if (host.opened_from_game()) {
                    host.restore_frame();
                    host.leave_display();
                    host.set_return_to_game();
                }
                host.return_to_previous();
            }
            break;
        }

        default:
            break;
    }
}

void PaperMapScreen::begin_page_turn(PaperMapHost& host, const PaperMapBook& book, int to) {
    turn_to_ = to;
    host.seek_file(image_file(book, to));
    host.fade_out();
    pan_x_ = -1;
    host.set_step(Turn);
}

void PaperMapScreen::show(PaperMapHost& host, const PaperMapBook& book) {
    const MapScreenData& d = book.data();
    PaperMapView view = paper_map_view(pan_x_, pan_y_, zoom_);
    const PaperMapWideView wide = paper_map_wide_view(view.left, view.scale, host.wide_factor());
    if (wide.span.on) host.note_wide_frame();
    view.left = wide.left;

    const std::int32_t packed = paper_map_draw_player(host, book, page_, view);
    paper_map_draw_marks(host, book, static_cast<int>(PaperMapMarks::Found), page_, 0, 0, view);
    if (host.fade_idle() && !zoomed_ && zoom_ == kOne) {
        paper_map_draw_page_arrows(host, book, page_);
    }
    paper_map_draw_page(host, book, view, &wide.span);

    const PaperMapInput in = host.input();
    // From the inventory only Cancel closes (the map button there is the inventory's own).
    const bool close = host.opened_from_inventory() ? in.cancel : (in.cancel || in.map);
    if (close) {
        host.play_map_sound();
        if (host.opened_from_inventory()) {
            host.flush_frame_lists();
            host.restore_frame();
            host.reload_map_item_textures();
            for (int i = 0; i < kPaperMapCloseDimFrames; ++i) {
                for (const draw2d::Command& c : paper_map_close_dim()) host.draw_now(c);
                host.pump_file_queue();
                host.wait_vblank();
            }
            host.wait_file_queue();
            host.reset_fade_step();
        } else {
            host.fade_out();
        }
        host.set_step(Close);
        return;
    }

    if (in.enter) {
        if (zoomed_) {
            zoomed_ = false;
        } else {
            zoomed_ = true;
            if (pan_x_ < 0) {
                // Zoom in on the arrow (packed as the original: its y borrows when x is negative).
                const int x = std::min(std::max(static_cast<std::int16_t>(packed) + d.focus_offset[0], 0), d.pan_max[0]);
                const int y = std::min(std::max((packed >> 16) + d.focus_offset[1], 0), d.pan_max[1]);
                pan_x_ = x * kOne;
                pan_y_ = y * kOne;
            }
        }
    }

    if (!zoomed_) {
        if (zoom_ == kOne) {
            const PaperMapPage* p = book.page(page_);
            if (p != nullptr && in.up && host.has_page(p->up)) {
                begin_page_turn(host, book, p->up);
                return;
            }
            if (p != nullptr && in.down && host.has_page(p->down)) {
                begin_page_turn(host, book, p->down);
                return;
            }
            return;
        }
        zoom_ = std::min(zoom_ + d.zoom_step, kOne);
        return;
    }

    zoom_ = std::max(zoom_ - d.zoom_step, 0);
    pan_x_ += (in.stick_x * 16384) / d.cursor_divisor;
    pan_x_ = std::clamp(pan_x_, 0, d.pan_max[0] * kOne);
    pan_y_ += (in.stick_y * 16384) / d.cursor_divisor;
    pan_y_ = std::clamp(pan_y_, 0, d.pan_max[1] * kOne);
}

}  // namespace sh::screens
