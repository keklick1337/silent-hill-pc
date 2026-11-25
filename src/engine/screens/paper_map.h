// SPDX-License-Identifier: GPL-3.0-or-later
//
// The paper map screen: the page picture as tiles of texture memory, the
// player arrow, the marks of the page, the page arrows, zoom and pan, page turning, and the steps of the screen's
// game state. Numbers and tables come from menu/map_screen.json (map_screen_data.h); the names in it are resolved
// once to the game's numbers (PaperMapBook). Everything the screen needs from the running game goes through
// PaperMapHost, in the same order the original did it.
#pragma once

#include "engine/screens/map_screen_data.h"
#include "engine/screens/map_touch_view.h"
#include "render/draw2d/draw2d.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sh::screens {

inline constexpr int kPaperMapPages = 25;

// One page with its names resolved to numbers.
struct PaperMapPage {
    int image_file = -1;
    int marking_file = -1;  // -1: the page draws no marks
    // Page turned to by Up and Down. 0 where the data names none: the original table holds 0 there too and still
    // asks whether the player has page 0.
    int up = 0;
    int down = 0;
    int atlas = -1;  // index into MapScreenData::atlases, -1 = no marks
    int marks = -1;  // index into MapScreenData::markings
    std::vector<std::array<int, 2>> mark_flags;  // the event flag of each mark's halves (-1: the name is unknown)
    int placement = -1;  // index into MapScreenData::placement
};

// Names to the game's numbers (paper map pages, disc files, event flags); -1 when unknown.
struct PaperMapNames {
    std::function<int(const std::string&)> page;
    std::function<int(const std::string&)> file;
    std::function<int(const std::string&)> flag;
    std::function<void(const std::string&)> report;  // a name that did not resolve
};

class PaperMapBook {
public:
    static PaperMapBook build(MapScreenData data, const PaperMapNames& names);

    const MapScreenData& data() const { return data_; }
    // nullptr outside 0..kPaperMapPages-1.
    const PaperMapPage* page(int n) const;

private:
    MapScreenData data_;
    std::array<PaperMapPage, kPaperMapPages> pages_{};
    std::array<std::string, kPaperMapPages> names_{};
};

// Where the view of the page starts (texels from the page's left and top edge) and its size (Q12: 4096 shows the
// 320x240 page across the 4:3 screen, 2048 half of it). The original kept them as 16-bit values.
struct PaperMapView {
    std::uint16_t left = 0;
    std::uint16_t top = 0;
    std::uint16_t scale = 4096;
};

// PC widescreen: in a window wider than 4:3 the page fills the whole width. The view widens about the centre of the
// 4:3 view, slides back inside the 320-texel page at its edges and is centred once the whole page width fits; the
// pan and the zoom stay the original's. `span` is the texel range the page tiles cover in this frame.
struct PaperMapWideSpan {
    bool on = false;
    int left = 0;
    int right = 0;
};
struct PaperMapWideView {
    std::uint16_t left = 0;  // the view's left texel for every draw of the frame
    PaperMapWideSpan span;
};
PaperMapWideView paper_map_wide_view(std::uint16_t left, std::uint16_t scale, float wide);

// The span of a map scene (cafe exit, water works, church bell): the scene keeps its own view, so its marks and
// outlines stay where they were placed, and in a window wider than 4:3 the rest of the page fills the sides.
PaperMapWideSpan paper_map_scene_span(float wide);

// The view of the zoom and pan: no pan yet (pan_x < 0) is the page's corner, else the pan point minus its share
// at this zoom.
PaperMapView paper_map_view(std::int32_t pan_x, std::int32_t pan_y, std::int32_t zoom);

// Where the page picture lies in texture memory (the game's image descriptor of the loaded page).
struct PaperMapImage {
    std::uint8_t depth = 0;   // texel depth code of the page
    std::uint8_t column = 0;  // first texture page column; bit 4 the 256-pixel row
    std::uint8_t v = 0;
    std::int16_t clut_x = 0, clut_y = 0;
};

// The page picture: a 3x2 grid of 128x128 texture tiles cut to the view, each a textured quad, grey `shade`.
std::vector<draw2d::Polygon> paper_map_tiles(const PaperMapView& view, const PaperMapImage& image, int shade,
                                             const PaperMapWideSpan* wide);

// Which marks: All draws every mark half that has a sprite, Found only those whose event flag is set, One the half
// whose flag number is `pick` (shade `shade`; the others use the data's marking_shade).
enum class PaperMapMarks { All = 0, Found = 1, One = 2 };
// The marks of page `page` as textured quads from the marking texture (palette at clut_x, clut_y). nullopt when the
// page has no marks; `flag` answers whether an event flag is set. `mode` is the script's number: 1 Found, 2 One,
// anything else All.
std::optional<std::vector<draw2d::Polygon>> paper_map_marks(const PaperMapBook& book, int page, int mode, int pick,
                                                            int shade, const PaperMapView& view, std::int16_t clut_x,
                                                            std::int16_t clut_y,
                                                            const std::function<bool(int)>& flag);

// The player as the arrow placement sees him.
struct PaperMapPlayer {
    std::int32_t x = 0, z = 0;  // world position, Q12 metres
    std::int16_t heading = 0;   // Q12 turns
    std::string_view map;       // current map name ("map0_s00")
};
// The player's point on page `page` (map cells from the page's centre) and the heading the arrow shows; nullopt when
// the page does not place him there.
struct PaperMapPoint {
    std::int16_t x = 0, z = 0;
    std::int16_t heading = 0;
};
std::optional<PaperMapPoint> paper_map_player_point(const PaperMapBook& book, int page, const PaperMapPlayer& player);
// The point packed as the original returned it: z << 16 plus x, so a negative x borrows from z.
std::int32_t paper_map_packed_point(const std::optional<PaperMapPoint>& point);

// The arrow at `point`: its outline (a closed line) and the gouraud triangle under it. `sin`/`cos` are the game's
// Q12 sine and cosine of the heading folded to -2047..2048.
struct PaperMapArrow {
    draw2d::Line outline;
    draw2d::Polygon fill;
};
PaperMapArrow paper_map_arrow(const MapPlayerArrow& style, const PaperMapPoint& point, const PaperMapView& view,
                              std::int32_t sin, std::int32_t cos);
// The heading folded to -2047..2048 (the angle whose sine and cosine the arrow takes).
std::int16_t paper_map_fold_heading(std::int32_t heading);

// The page arrows at the top (Up) and bottom (Down) edge, in that order, for the ones the player can turn to.
std::vector<draw2d::Polygon> paper_map_page_arrows(const MapPageArrows& style, bool up, bool down);

// One frame of the dimming that runs when the map closes back into the inventory: the draw mode and the tile.
std::array<draw2d::Command, 2> paper_map_close_dim();
inline constexpr int kPaperMapCloseDimFrames = 63;

// The 2D layers the screen draws into, front to back.
enum class PaperMapLayer { Arrows, Marks, Page };

struct PaperMapInput {
    bool cancel = false;
    bool map = false;
    bool enter = false;
    bool up = false;
    bool down = false;
    bool hold_arrow = false;  // L1 or R1 held: the arrow is located but not drawn
    std::int8_t stick_x = 0, stick_y = 0;
};

class PaperMapHost {
public:
    virtual ~PaperMapHost() = default;

    // Steps of the screen's game state.
    virtual int step() = 0;
    virtual void set_step(int step) = 0;  // also restarts the sub-steps and the step counter
    virtual bool opened_from_inventory() = 0;
    virtual bool opened_from_game() = 0;  // from the game or the map load screen
    virtual void set_return_to_game() = 0;
    virtual void return_to_previous() = 0;
    virtual void frame_upkeep() = 0;  // music track and play timer, every frame

    // The screen.
    virtual void enter_display() = 0;  // 320 wide, interlaced, black clear colour
    virtual void leave_display() = 0;  // back to the 320 wide progressive game screen
    virtual void set_vblank_interval(int vblanks) = 0;
    virtual void store_frame() = 0;    // what the page picture covers in texture memory, kept for later
    virtual void restore_frame() = 0;
    virtual void flush_frame_lists() = 0;  // draw the frame's lists, wait a vblank, draw them again
    virtual void reload_map_item_textures() = 0;
    virtual void draw_now(const draw2d::Command& command) = 0;
    virtual void wait_vblank() = 0;
    virtual void submit(PaperMapLayer layer, const draw2d::Command& command) = 0;
    virtual float wide_factor() = 0;  // window width over 4:3 width, <= 1 for 4:3
    virtual void note_wide_frame() = 0;
    virtual void pause_music() = 0;   // the page picture covers the screen this frame

    // Files.
    virtual int saved_page() = 0;
    virtual void load_page_image(int file) = 0;
    virtual void load_marking_texture(int file) = 0;
    virtual void seek_file(int file) = 0;
    virtual void finish_loads() = 0;  // start the queue now and wait until it is empty
    virtual void pump_file_queue() = 0;
    virtual void wait_file_queue() = 0;
    virtual PaperMapImage page_image() = 0;
    virtual std::array<std::int16_t, 2> marking_palette() = 0;

    // Sound and fade.
    virtual void stop_radio() = 0;
    virtual void play_map_sound() = 0;
    virtual void fade_in() = 0;
    virtual void fade_out() = 0;
    virtual bool fade_idle() = 0;
    virtual bool fade_done() = 0;
    virtual void reset_fade_step() = 0;

    // The game.
    virtual PaperMapInput input() = 0;
    virtual bool has_page(int page) = 0;
    virtual bool flag(int number) = 0;
    virtual PaperMapPlayer player() = 0;
    virtual std::int32_t sin(std::int32_t angle) = 0;
    virtual std::int32_t cos(std::int32_t angle) = 0;

    // Touch. A window pixel (the touch layer's: SDL_GetWindowSizeInPixels) in the screen's draw coordinates: x from
    // the centre of the 320-wide frame, y in the interlaced lines from the centre (the page spans -240..240). False
    // while the picture's place in the window is unknown.
    virtual bool window_to_screen(float /*window_x*/, float /*window_y*/, float* /*x*/, float* /*y*/) { return false; }
    // The draw x of the window's left and right edge.
    virtual void screen_span(float* left, float* right) {
        const float wide = std::max(1.0f, wide_factor());
        *left = -160.0f * wide;
        *right = 160.0f * wide;
    }
};

// The draws the map scripts use too (sh.map_screen_*): each submits to its layer.
void paper_map_draw_page(PaperMapHost& host, const PaperMapBook& book, const PaperMapView& view,
                         const PaperMapWideSpan* wide);
bool paper_map_draw_marks(PaperMapHost& host, const PaperMapBook& book, int mode, int page, int pick, int shade,
                          const PaperMapView& view);
// Draws the arrow when the page places the player (unless the arrow is held) and returns the packed point (0 when
// the page is not the saved page or does not place him).
std::int32_t paper_map_draw_player(PaperMapHost& host, const PaperMapBook& book, int page, const PaperMapView& view);
void paper_map_draw_page_arrows(PaperMapHost& host, const PaperMapBook& book, int page);

// True while the paper map on screen is zoomed in (or zooming), by the pad's zoom or by touch.
bool paper_map_zoomed();

// The touch screen moves the paper map as a browser moves a page (map_touch_view.h). Positions are window pixels as
// the touch layer has them (SDL_GetWindowSizeInPixels, top-left origin), velocities pixels a second; the screen
// maps them into its picture itself. The calls may come from any thread: they are queued and applied in the
// screen's own frame. Touch owns the view from its first event until the pad zooms (Confirm) or pans (the stick);
// the pad's zoom then continues from what touch showed. Taps are not read here: a single tap stays the touch
// layer's (Back), and its vertical swipe at fit still turns the page through the pad's Up and Down.
//
// The map's Show step is up and takes touch.
bool paper_map_touch_available();
// One finger down / moved. A move after a pinch (one of two fingers lifted) continues as a drag from there.
void paper_map_touch_begin(float x, float y);
void paper_map_touch_move(float x, float y);
// Two fingers: their midpoint and distance. The first call after a one-finger event starts the pinch.
void paper_map_touch_pinch(float mid_x, float mid_y, float distance);
// The last finger lifted, moving at this speed (a drag glides on; a pinch ignores it).
void paper_map_touch_end(float velocity_x, float velocity_y);
// Zoom in on the point (centred), or back out to fit when zoomed.
void paper_map_touch_double_tap(float x, float y);
// The whole page is shown and nothing zooms: a vertical swipe turns the page. True also while the pad owns an
// unzoomed view.
bool paper_map_touch_at_fit();

// The paper map game state: open, show (zoom, pan, page turn, close), turn the page, load the page, close.
class PaperMapScreen {
public:
    void update(PaperMapHost& host, const PaperMapBook& book);

private:
    void show(PaperMapHost& host, const PaperMapBook& book);
    void begin_page_turn(PaperMapHost& host, const PaperMapBook& book, int to);
    // Touch: the queued events and the view's motion of this frame; the drawing in touch's view; the hand-overs.
    void touch_frame(PaperMapHost& host);
    std::int32_t draw_touch(PaperMapHost& host, const PaperMapBook& book);
    void touch_take_over(PaperMapHost& host);
    void touch_hand_back(const MapScreenData& data);
    void touch_reset();

    int page_ = 0;
    int turn_to_ = 0;
    int loaded_marking_file_ = -1;
    std::int32_t pan_x_ = -1;  // Q12 texels, -1 = no pan point yet
    std::int32_t pan_y_ = 0;
    std::int32_t zoom_ = 4096;  // Q12: 4096 = the whole page, 0 = zoomed in fully
    bool zoomed_ = false;

    // Touch's view of the page, in page units: x as draw x from the window's left edge, y as half lines from the
    // top of the page's 480 lines, so the page is 320x240 and fit is the pad's unzoomed view.
    MapTouchView touch_;
    bool touch_mode_ = false;
    bool touch_pinching_ = false;
    bool touch_dragging_ = false;
    MapTouchVec touch_last_{};  // the drag's last point, page units
    std::chrono::steady_clock::time_point touch_clock_{};
};

}  // namespace sh::screens
