#pragma once

// The light glow over the screen (docs/RENDER.md section 5.3): an additive fan around the light and a
// subtractive gradient in three rings out to a fill that reaches the screen border, as 2D commands into the world's
// order table. Built once per pair of frame buffers and only moved each frame. The numbers are screen_glow.h's.

#include "engine/world/screen_glow.h"
#include "render/draw2d/draw2d.h"

#include <array>
#include <cstdint>

namespace sh::world {

// Parts of one buffer's overlay, in the order the original's packet block laid them out: the two draw modes, 16 fan
// triangles, 48 ring quads, 16 fill quads.
inline constexpr int kGlowPartSubtract = 0;
inline constexpr int kGlowPartAdd = 1;
inline constexpr int kGlowPartFan = 2;
inline constexpr int kGlowPartRing = kGlowPartFan + 16;
inline constexpr int kGlowPartFill = kGlowPartRing + 48;
inline constexpr int kGlowParts = kGlowPartFill + 16;

// The screen the overlay covers.
struct GlowScreen {
    std::int32_t width = 320;       // the game's frame (4:3)
    std::int32_t height = 240;
    std::int32_t ring_half_w = 160;  // half width the border ring reaches (wider than width / 2 in a wide view)
    float v_scale = 1.0f;           // vertical stretch of the world view (at least 1 counts)
    float v_shift = 0.0f;           // vertical shift of the world view
};

class GlowOverlayHost {
public:
    virtual ~GlowOverlayHost() = default;
    // Links part `part` of buffer `buffer` at entry `entry` (0 or 1) of the world order table being built.
    virtual void link(int entry, int buffer, int part, const draw2d::Command& command) = 0;
    virtual int buffer() = 0;
    virtual GlowScreen screen() = 0;
    // The per-pixel flashlight lights the centre: the additive fan is not drawn.
    virtual bool flashlight_lit() = 0;
};

class ScreenGlowOverlay {
public:
    // Sets the shapes' kinds (the tint stays) and lays the border ring out.
    void init(GlowOverlayHost& host);
    // Colours of the centre fan and of the ring's edge.
    void set_tint(std::uint8_t fan_r, std::uint8_t fan_g, std::uint8_t fan_b, std::uint8_t edge_r, std::uint8_t edge_g,
                  std::uint8_t edge_b);
    // The glow of a light at screen position `pos` (x, y; z the depth) of `size`, seen under two angles (Q12).
    void draw(GlowOverlayHost& host, const Vec3& pos, std::int32_t size, std::int32_t angle0, std::int32_t angle1);

private:
    struct Shapes {
        draw2d::DrawMode subtract;
        draw2d::DrawMode add;
        std::array<draw2d::Polygon, 16> fan;
        std::array<draw2d::Polygon, 48> ring;
        std::array<draw2d::Polygon, 16> fill;
    };
    void build_ring(const GlowScreen& screen);

    std::array<Shapes, 2> shapes_{};
    bool built_ = false;
    std::int32_t ring_half_w_ = 0;
};

ScreenGlowOverlay& screen_glow_overlay();

// Lays the glow out on the running game's screen (the game's glow host, engine/world/game/screen_glow_host.cpp).
void screen_glow_init();

}  // namespace sh::world
