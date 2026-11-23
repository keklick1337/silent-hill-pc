// SPDX-License-Identifier: GPL-3.0-or-later
// The menu screens' shared selection widgets: the underline under the selected row
// (with its optional shadow band), the flashing arrows, the gradient bullet points and
// the screen-edge vignette. Colors come from gamedata/menu/menu_style.json; the module
// emits draw records and the game bridge turns them into primitives.

#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

#include "formats/common/status.h"

namespace sh::screens {

struct MenuPoint {
    std::int32_t x = 0;
    std::int32_t y = 0;
};

// One record. Vertex colors are r | g << 8 | b << 16.
//  - Line: a two-color line from point 0 to point 1.
//  - Triangle: a three-color triangle.
//  - Quad: a four-color quad, corners in x/y order 0..3.
//  - Blend: the blend setup that follows semi-transparent records of the layer.
//  - FlatQuad: a one-color quad (rgb[0]), corners in x/y order 0..3.
struct MenuPrim {
    enum class Kind : std::uint8_t { Line, Triangle, Quad, Blend, FlatQuad };
    // Where the record goes in the frame (the bridge maps each to its draw layer).
    enum class Layer : std::uint8_t { Underline, Shadow, Arrow, Bullet, Vignette, Bar };
    Kind kind = Kind::Line;
    Layer layer = Layer::Underline;
    std::int32_t x[4] = {};
    std::int32_t y[4] = {};
    std::uint32_t rgb[4] = {};
    bool semi_transparent = false;
};

// The menus' sounds and directions, shared by the screens' hosts.
enum class MenuSfx : std::uint8_t { Move, Confirm, Cancel, Error };
enum class MenuDir : std::uint8_t { Up, Down, Left, Right };

class MenuSink {
public:
    virtual ~MenuSink() = default;
    virtual void prim(const MenuPrim& record) = 0;
};

struct MenuStyle {
    std::array<std::uint32_t, 2> underline{};         // left, right
    std::int32_t shadow_height = 16;                   // the band above the underline
    std::array<std::uint32_t, 2> shadow{};             // at the top edge, at the underline
    std::uint32_t arrow_base = 0;                      // flashing arrows: green is added
    std::int32_t arrow_flash_step = 7;                 // green per step of the flash ramp
    std::uint32_t arrow_solid = 0;
    std::array<std::uint32_t, 3> bullet_front{};
    std::array<std::uint32_t, 3> bullet_front_inactive{};
    std::array<std::uint32_t, 3> bullet_border{};
    std::array<std::uint32_t, 3> bullet_border_inactive{};
    std::uint32_t vignette_edge = 0;
    std::uint32_t vignette_inner = 0;
    std::int32_t vignette_left = -160;
    std::int32_t vignette_right = 160;
    // Two bands: y of the screen edge and y where the band fades out.
    std::array<std::int32_t, 2> vignette_edge_y{};
    std::array<std::int32_t, 2> vignette_inner_y{};
};

class MenuWidgets {
public:
    explicit MenuWidgets(const MenuStyle& style) : style_(style) {}

    // The underline from a to b; with a shadow, a semi-transparent band above it and
    // the shadow's blend setup, all on the shadow layer.
    void underline(MenuPoint a, MenuPoint b, bool shadow, MenuSink& sink) const;
    // A selection arrow. Flashing arrows ramp their green with `timer` (a frame counter,
    // period 128); others are solid.
    void arrow(const MenuPoint (&v)[3], bool flashing, std::int32_t timer, MenuSink& sink) const;
    // A bullet point as two triangles (diagonal gradient); the border is drawn as a
    // second, slightly larger bullet.
    void bullet(const MenuPoint (&v)[4], bool border, bool inactive, MenuSink& sink) const;
    // The dark bands at the top and bottom of the screen and their blend setup.
    void vignette(MenuSink& sink) const;

private:
    const MenuStyle& style_;
};

Result<MenuStyle> parse_menu_style(std::string_view json_text);
// menu/menu_style.json from the last mod that has it, else from the gamedata directory.
Result<MenuStyle> load_menu_style(const std::filesystem::path& gamedata_dir, const std::filesystem::path& mods_dir,
                                  std::string* source = nullptr);

}  // namespace sh::screens
