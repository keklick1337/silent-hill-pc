#pragma once

// The engine's 2D draw list (docs/RENDER.md section 5): screen-space sprites, fills, polygons and
// draw-mode changes described with engine types, so drawers build no GPU packets themselves.
//
// A drawer records a Command and gets an id back; the frame's ordering (which layer, before or after what)
// is the caller's business and travels separately with the id. The backend (draw2d_stream.h) looks the command up
// by id when the frame reaches it and draws it into the frame's vertex stream.
//
// Field semantics follow the console's 2D rasteriser, which the game's art and layout depend on:
// - coordinates are pixels relative to the draw offset, 16-bit and wrapping;
// - texture coordinates are texels inside the texture page, 8-bit and wrapping;
// - colour 128 means "texture unchanged" when a texture is modulated;
// - a translucent command blends only the texels with the semi-transparency bit, by the page's blend mode.

#include "render/frame/frame.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <variant>
#include <vector>

namespace sh::draw2d {

struct Rgb {
    std::uint8_t r = 0, g = 0, b = 0;
    friend bool operator==(const Rgb&, const Rgb&) = default;
};

// How a translucent command mixes with what is under it (back, front).
enum class Blend : std::uint8_t {
    Average,      // B/2 + F/2
    Add,          // B + F
    Subtract,     // B - F
    AddQuarter,   // B + F/4
};

// Texel format of a texture page.
enum class Depth : std::uint8_t {
    Indexed4,  // 4-bit palette indices
    Indexed8,  // 8-bit palette indices
    Direct15,  // 15-bit colour plus the semi-transparency bit
    Reserved,  // the fourth hardware value; kept so a page round-trips
};

// A 256x256 window of texture memory, addressed in 64-pixel columns and 256-pixel rows. The word form is the
// one the game's data and the hardware use; `from_word(word()).word()` is the identity for every 16-bit value.
struct TexturePage {
    std::uint8_t column = 0;  // x / 64, 0..15
    std::uint8_t row = 0;     // y / 256, 0..3
    Blend blend = Blend::Average;
    Depth depth = Depth::Indexed4;
    std::uint16_t other_bits = 0;  // bits outside the fields above, carried unchanged

    static TexturePage from_word(std::uint16_t word);
    std::uint16_t word() const;
    friend bool operator==(const TexturePage&, const TexturePage&) = default;
};

// Where an indexed texture's palette lives: a row of 16 (4-bit) or 256 (8-bit) colours at x = 16 * column.
struct Palette {
    std::uint8_t column = 0;  // x / 16, 0..63
    std::uint16_t row = 0;    // 0..1023 (rows past 511 name virtual pages, outside the 1024x512 texture memory)

    static Palette from_word(std::uint16_t word);
    std::uint16_t word() const;
    friend bool operator==(const Palette&, const Palette&) = default;
};

struct Point {
    std::int16_t x = 0, y = 0;
    friend bool operator==(const Point&, const Point&) = default;
};

struct TexCoord {
    std::uint8_t u = 0, v = 0;
    friend bool operator==(const TexCoord&, const TexCoord&) = default;
};

// Shading flags shared by every drawing command.
struct Shade {
    bool translucent = false;  // blend by the page's mode where texels (or the whole fill) are translucent
    bool raw_texture = false;  // texels as stored, colour ignored (textured commands only)
    friend bool operator==(const Shade&, const Shade&) = default;
};

// Sprites and fills come in a free size and in the fixed sizes the hardware has separate commands for; the
// fixed ones ignore w and h. Kept apart because the renderer treats them slightly differently (feedback
// detection runs on free-size sprites only), and the image must not change.
enum class RectSize : std::uint8_t { Free, Px1, Px8, Px16 };

// A texture rectangle drawn 1:1 (no scaling), from the current draw-mode page.
struct Sprite {
    Point at;
    std::uint16_t w = 0, h = 0;
    TexCoord uv;
    Palette palette;
    Rgb color{128, 128, 128};
    Shade shade;
    RectSize size = RectSize::Free;
    friend bool operator==(const Sprite&, const Sprite&) = default;
};

// A solid rectangle.
struct Fill {
    Point at;
    std::uint16_t w = 0, h = 0;
    Rgb color;
    bool translucent = false;
    RectSize size = RectSize::Free;
    friend bool operator==(const Fill&, const Fill&) = default;
};

// Per-vertex fog amounts of a gouraud polygon (0 = none). The renderer reads them from the spare high byte
// of the colour words of vertices 1 to 3 (vertex 0's high byte is the command code; the renderer gives vertex 0
// the amount of vertex 1). Drawers that write a colour as one word with the code in its high byte put the code
// there; everything else leaves 0.
struct VertexFog {
    std::uint8_t amount[4] = {};
    friend bool operator==(const VertexFog&, const VertexFog&) = default;
};

// A triangle or a quad (vertex order 0 1 2 3 is the hardware's: two triangles 0-1-2 and 1-2-3). Textured
// polygons carry their own page, which also becomes the current draw-mode page.
struct Polygon {
    std::uint8_t count = 4;  // 3 or 4
    bool textured = false;
    bool gouraud = false;  // per-vertex colour, else color[0] for the whole polygon
    Shade shade;
    Point xy[4];
    TexCoord uv[4];
    Rgb color[4];
    Palette palette;
    TexturePage page;
    VertexFog fog;
    friend bool operator==(const Polygon&, const Polygon&) = default;
};

// A line through 2 to 4 points (a polyline past 2), one pixel wide.
struct Line {
    std::uint8_t count = 2;  // 2..4
    bool gouraud = false;    // per-point colour, else color[0]
    bool translucent = false;
    Point xy[4];
    Rgb color[4];
    friend bool operator==(const Line&, const Line&) = default;
};

// The current texture page of the draw mode (used by sprites), with dithering and the display-area flag.
struct DrawMode {
    TexturePage page;
    bool dither = false;
    bool draw_on_display = false;
    friend bool operator==(const DrawMode&, const DrawMode&) = default;
};

// The rectangle of the target the following commands are clipped to, in target pixels (x, y and the far edge
// x + w, y + h wrap at 1024 as the hardware's 10-bit fields do). An area right of the display buffers is an
// offscreen scratch the frame samples later (the drawn picture is redirected there).
struct DrawArea {
    std::int16_t x = 0, y = 0, w = 0, h = 0;
    friend bool operator==(const DrawArea&, const DrawArea&) = default;
};

// The mask bit of the following commands: `set` marks the pixels they draw, `test` keeps marked pixels from being
// drawn over.
struct Mask {
    bool set = false;
    bool test = false;
    friend bool operator==(const Mask&, const Mask&) = default;
};

using Command = std::variant<Sprite, Fill, Polygon, Line, DrawMode, DrawArea, Mask>;

// Ids are 24-bit and wrap; 0 is never handed out, so a zeroed marker names nothing.
using CommandId = std::uint32_t;
constexpr CommandId kIdMask = 0xFFFFFFu;

// The commands of the last frames. Recording never fails: the oldest commands are overwritten once the ring
// is full, and a lookup of an overwritten (or never issued) id returns nothing. A frame's commands are drawn
// in the frame they are recorded, so a ring a few frames deep is enough.
class CommandRing {
public:
    explicit CommandRing(std::size_t capacity_log2 = 16);

    CommandId record(const Command& command);
    const Command* find(CommandId id) const;

    std::size_t capacity() const { return slots_.size(); }
    std::uint64_t recorded() const { return recorded_; }

private:
    struct Slot {
        CommandId id = 0;
        Command command;
    };
    std::vector<Slot> slots_;
    CommandId next_ = 1;
    std::uint64_t recorded_ = 0;
};

// The process-wide ring the game's drawers record into.
CommandRing& frame_commands();

// Places a command in the engine frame at `key` in the lists being built (render/frame): a draw-area change as an
// area entry, any other command recorded into frame_commands(). `depth_max` feeds the frame's depth maximum (a
// drawer placing a command right after its projections passes the largest of their newest depths); 0 feeds nothing.
void submit(const Command& command, frame::OrderKey key, std::uint32_t depth_max = 0);

}  // namespace sh::draw2d
