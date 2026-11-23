// SPDX-License-Identifier: GPL-3.0-or-later
// The crosshair of the aimed cameras (config.cfg `crosshair`, `crosshair_style`, `crosshair_size`,
// `crosshair_fps_aim_only`, `crosshair_hide_melee`): a small white half-translucent mark where a shot goes, drawn
// on the 2D layer over the world. The shapes are built around the middle of the screen in its 320 x 224 space.
#pragma once

#include <array>
#include <cstdint>

namespace sh::ui {

// A filled quad, corners in the order top left, top right, bottom left, bottom right.
struct CrosshairQuad {
    std::int16_t x[4] = {0, 0, 0, 0};
    std::int16_t y[4] = {0, 0, 0, 0};
};

// The values of `crosshair_style`; anything else is the cross.
enum class CrosshairStyle { Cross = 0, Dot = 1, Circle = 2, Dashes = 3 };

class CrosshairShape {
public:
    static constexpr int kMaxQuads = 8;      // the circle's ring
    static constexpr int kSmallestSize = 25;  // percent
    static constexpr int kLargestSize = 125;

    // `size` is `crosshair_size`, a percentage of the shape as designed; `drop` moves the mark down by that many
    // lines after sizing.
    CrosshairShape(int style, float size, int drop) {
        switch (static_cast<CrosshairStyle>(style)) {
            case CrosshairStyle::Dot:
                bar(-2, -2, 2, 2);
                break;
            case CrosshairStyle::Circle:
                // An octagon ring between the radii 4 and 6.
                for (int corner = 0; corner < 8; corner++) {
                    ring_segment(corner, (corner + 1) % 8);
                }
                break;
            case CrosshairStyle::Dashes: {
                // Four dashes 4 long, 2 thick, 3 from the middle: the target shows between them.
                const int near = 3, far = 7, half = 1;
                bar(-far, -half, -near, half);
                bar(near, -half, far, half);
                bar(-half, -far, half, -near);
                bar(-half, near, half, far);
                break;
            }
            case CrosshairStyle::Cross:
            default: {
                const int arm = 6, half = 1;
                bar(-arm, -half, arm, half);
                bar(-half, -arm, half, arm);
                break;
            }
        }

        int percent = static_cast<int>(size + 0.5f);
        percent = percent < kSmallestSize ? kSmallestSize : percent > kLargestSize ? kLargestSize : percent;
        if (percent != 100) {
            resize(percent);
        }
        for (int i = 0; i < count_; i++) {
            for (std::int16_t& y : quads_[static_cast<std::size_t>(i)].y) {
                y = static_cast<std::int16_t>(y + drop);
            }
        }
    }

    int count() const { return count_; }
    const CrosshairQuad* quads() const { return quads_.data(); }
    const CrosshairQuad& quad(int index) const { return quads_[static_cast<std::size_t>(index)]; }

private:
    CrosshairQuad& next() { return quads_[static_cast<std::size_t>(count_++)]; }

    void bar(int left, int top, int right, int bottom) {
        CrosshairQuad& q = next();
        const int xs[4] = {left, right, left, right};
        const int ys[4] = {top, top, bottom, bottom};
        for (int corner = 0; corner < 4; corner++) {
            q.x[corner] = static_cast<std::int16_t>(xs[corner]);
            q.y[corner] = static_cast<std::int16_t>(ys[corner]);
        }
    }

    // Corner `corner` (0..7, clockwise on screen from the right) of an octagon whose corners on the axes are
    // `axis` from the middle and whose diagonal corners are `diagonal` along each axis.
    static void octagon_corner(int corner, int axis, int diagonal, std::int16_t& x, std::int16_t& y) {
        static constexpr int kSignX[8] = {1, 1, 0, -1, -1, -1, 0, 1};
        static constexpr int kSignY[8] = {0, 1, 1, 1, 0, -1, -1, -1};
        const int reach = corner % 2 == 0 ? axis : diagonal;
        x = static_cast<std::int16_t>(kSignX[corner] * reach);
        y = static_cast<std::int16_t>(kSignY[corner] * reach);
    }

    // The piece of the ring between two neighbouring corners: outer edge first, inner edge second.
    void ring_segment(int from, int to) {
        CrosshairQuad& q = next();
        octagon_corner(from, 6, 4, q.x[0], q.y[0]);
        octagon_corner(to, 6, 4, q.x[1], q.y[1]);
        octagon_corner(from, 4, 3, q.x[2], q.y[2]);
        octagon_corner(to, 4, 3, q.x[3], q.y[3]);
    }

    // A coordinate at `percent` of its distance from the middle, rounded to the nearest line. The shapes are a
    // line or two thick, so nothing that was off the middle may land on it: a shrunk bar would vanish.
    static std::int16_t resized(int v, int percent) {
        int scaled = (v * percent + (v >= 0 ? 50 : -50)) / 100;
        if (scaled == 0 && v != 0) {
            scaled = v > 0 ? 1 : -1;
        }
        return static_cast<std::int16_t>(scaled);
    }

    void resize(int percent) {
        for (int i = 0; i < count_; i++) {
            CrosshairQuad& q = quads_[static_cast<std::size_t>(i)];
            for (int corner = 0; corner < 4; corner++) {
                q.x[corner] = resized(q.x[corner], percent);
                q.y[corner] = resized(q.y[corner], percent);
            }
        }
    }

    std::array<CrosshairQuad, kMaxQuads> quads_{};
    int count_ = 0;
};

// What decides whether the mark is on screen this frame.
struct CrosshairView {
    bool enabled = false;       // `crosshair`
    bool aimed_camera = false;  // third person, over the shoulder or first person: the view's middle is the aim
    bool in_play = false;       // the player has control: no mark over cutscenes, menus or the map
    bool aiming = false;        // the weapon is raised
    bool only_while_aiming = false;  // `crosshair_fps_aim_only`
    bool hide_without_gun = false;   // `crosshair_hide_melee`
    bool gun_in_hand = false;
};

constexpr bool crosshair_visible(const CrosshairView& v) {
    if (!v.enabled || !v.aimed_camera || !v.in_play) {
        return false;
    }
    if (v.only_while_aiming && !v.aiming) {
        return false;
    }
    return v.gun_in_hand || !v.hide_without_gun;
}

// Draws the mark for this frame of the game when it is to be seen.
void draw_crosshair();

}  // namespace sh::ui
