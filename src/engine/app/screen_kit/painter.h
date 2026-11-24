// SPDX-License-Identifier: GPL-3.0-or-later
// Drawing for the screens shown before the game (the quick setup and the disc import) with an SDL renderer, in the
// look of the game's menus: the widget colours of gamedata/menu/menu_style.json (bullets, the gold underline, value
// arrows, the dark edge bands) and text from text_raster.h, rasterised at the window's own size. Positions are canvas
// units (text_layout.h View); in a right-to-left language the canvas is mirrored, so a layout is written once.
#pragma once

#include "engine/app/screen_kit/text_layout.h"
#include "engine/app/screen_kit/text_raster.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

struct SDL_Renderer;
struct SDL_Texture;

namespace sh::screen_kit {

struct Rgb {
    std::uint8_t r = 0, g = 0, b = 0;
};

// The widget colours of gamedata/menu/menu_style.json; these values stand in when the file cannot be read.
struct Style {
    std::array<Rgb, 2> underline{{{176, 176, 176}, {160, 128, 64}}};
    std::array<Rgb, 3> bullet_front{{{160, 128, 64}, {255, 255, 255}, {255, 255, 255}}};
    std::array<Rgb, 3> bullet_front_inactive{{{80, 64, 32}, {160, 160, 160}, {160, 160, 160}}};
    std::array<Rgb, 3> bullet_border{{{255, 255, 255}, {160, 128, 64}, {64, 64, 64}}};
    std::array<Rgb, 3> bullet_border_inactive{{{128, 128, 128}, {40, 32, 16}, {16, 16, 16}}};
    Rgb arrow{0, 240, 240};
    Rgb vignette_edge{96, 96, 96};
};

Style load_style(const std::filesystem::path& menu_style_json);

// The text colours both screens use.
namespace colors {
inline constexpr Rgb kWhite{232, 232, 232};
inline constexpr Rgb kGold{200, 168, 96};
inline constexpr Rgb kGrey{150, 150, 150};
inline constexpr Rgb kDark{90, 90, 90};
}  // namespace colors

// A box in pixels.
struct Rect {
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    bool contains(float x, float y) const { return x >= x0 && x < x1 && y >= y0 && y < y1; }
    float width() const { return x1 - x0; }
    float height() const { return y1 - y0; }
};

enum class Align { Left, Center, Right };

// The fonts of a language, best first, and its direction.
struct TextFace {
    std::vector<std::string> families;
    bool rtl = false;
};

// The base face of both screens: the Latin of the menus, then the language's own fonts.
TextFace face_for(const std::vector<std::string>& language_fonts, bool rtl);

struct Texture {
    SDL_Texture* texture = nullptr;
    int width = 0;
    int height = 0;
};

class Painter {
public:
    // `base_data` is the gamedata/ beside the executable: font/ and menu/menu_style.json.
    explicit Painter(const std::filesystem::path& base_data);
    ~Painter();
    Painter(const Painter&) = delete;
    Painter& operator=(const Painter&) = delete;

    // The renderer the textures are made for; the textures of an earlier one are dropped.
    void attach(SDL_Renderer* renderer);
    SDL_Renderer* renderer() const { return renderer_; }
    const Style& style() const { return style_; }

    // A new scale drops the rasterised text (it is drawn at the window's own size).
    void set_view(const View& view, bool mirrored);
    const View& view() const { return view_; }
    float scale() const { return view_.scale; }
    bool mirrored() const { return mirrored_; }

    // Canvas to pixels (mirrored in a right-to-left language).
    float sx(float x) const { return view_.origin_x + (mirrored_ ? view_.width - x : x) * view_.scale; }
    float sy(float y) const { return view_.origin_y + y * view_.scale; }
    Rect box(float x0, float y0, float x1, float y1) const;
    Align flip(Align a) const;

    // The interface language's face, used where no other is given.
    void set_face(TextFace face);
    const TextFace& face() const { return face_; }

    // A line rasterised at `size` canvas units.
    const Texture& text(const std::string& s, float size, const TextFace& face);
    // Its width in canvas units.
    float width(const std::string& s, float size, const TextFace* face = nullptr);
    // Draws a line centred on canvas y; returns its width in canvas units. `max_w` shrinks a line too wide for it.
    float draw_text(const std::string& s, float x, float y, float size, Rgb color, Align align, float max_w = 0,
                    const TextFace* face = nullptr, bool shadow = true, std::uint8_t alpha = 255);
    // Draws a rasterised line with its top left corner at pixel (left, top).
    void blit(const Texture& t, float left, float top, Rgb color, std::uint8_t alpha = 255, bool shadow = true);
    // `s` broken into lines no wider than `max_w` canvas units (text_layout.h wrap_lines); kept until the scale
    // or the face changes.
    const std::vector<std::string>& wrap(const std::string& s, float size, float max_w, const TextFace* face = nullptr);
    void clear_text();

    // Pixel boxes.
    void fill(Rect r, Rgb color, std::uint8_t alpha = 255);
    void outline(Rect r, float thickness, Rgb color, std::uint8_t alpha = 255);
    void fill_gradient(Rect r, Rgb a, std::uint8_t a_alpha, Rgb b, std::uint8_t b_alpha, bool horizontal);

    // The menus' widgets, at canvas positions.
    void bullet(float x, float y, bool active);
    void arrow(float x, float y, bool right, float size);
    // The selected row's band and the two-colour underline of the original options, from x0 to x1 at y.
    void underline(float x0, float x1, float y);
    // The dark bands at the top and the bottom of the menus, over the whole output.
    void edge_bands(int pixel_w, int pixel_h);

private:
    std::string key(const std::string& s, int px, const TextFace& face) const;

    Style style_;
    TextRaster raster_;
    SDL_Renderer* renderer_ = nullptr;
    View view_;
    bool mirrored_ = false;
    TextFace face_;
    std::unordered_map<std::string, Texture> textures_;
    std::unordered_map<std::string, float> widths_;
    std::unordered_map<std::string, std::vector<std::string>> wrapped_;
};

}  // namespace sh::screen_kit
