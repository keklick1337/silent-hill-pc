// SPDX-License-Identifier: GPL-3.0-or-later
//
// The pictures of the touch controls (gamedata/ui/touch/<name>.svg) as plain shapes the controls rasterise
// (touch_sprites.h). The files are SVG in a small subset, enough for flat one-colour icons:
//   - <path d>: M/m L/l H/h V/v Z/z (straight segments only),
//   - <line x1 y1 x2 y2>, <polyline points>, <polygon points>, <circle cx cy r>, <rect x y width height>;
//   - fill="none" or any colour (filled), stroke="none" or any colour with stroke-width (outlined).
// Colours do not matter: the controls tint every icon alike. The viewBox's square is fitted into the button.
// No SDL, no renderer, no game.
#pragma once

#include <array>
#include <string>
#include <string_view>
#include <vector>

#include "formats/common/status.h"
#include "input/touch/touch_gestures.h"

namespace sh::input::touch {

struct VectorIcon {
    struct Stroke {
        std::vector<Point> points;
        bool closed = false;
        float width = 1.0f;
    };
    struct Ring {
        Point centre;
        float radius = 0.0f;
        float width = 0.0f;  // 0: a filled disc
    };
    std::vector<Stroke> strokes;
    std::vector<std::array<Point, 3>> triangles;  // the filled areas, triangulated
    std::vector<Ring> rings;
    float box_x = 0.0f, box_y = 0.0f, box_size = 64.0f;  // the viewBox's square (the larger side)
};

Result<VectorIcon> parse_svg_icon(std::string_view svg);

// Triangles covering a simple polygon (either winding); empty for fewer than three points.
std::vector<std::array<Point, 3>> triangulate(const std::vector<Point>& polygon);

}  // namespace sh::input::touch
