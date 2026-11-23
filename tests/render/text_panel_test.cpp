// The text panels of the free camera, the frame-rate counter and the profiler (features/hud/text_panel.h,
// frame_stats.h): the picture of the rows and the frame figures.
#include "features/hud/frame_stats.h"
#include "features/hud/text_panel.h"

#include "framework/sh_test.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;
using sh::hud::PanelBitmap;
using sh::hud::PanelFont;

const fs::path kFont = fs::path(SH_PROJECT_ROOT) / "gamedata" / "font" / "BarlowSemiCondensed-Regular.ttf";

std::uint8_t alpha(const PanelBitmap& b, int x, int y) {
    return b.rgba[(static_cast<std::size_t>(y) * b.width + x) * 4 + 3];
}

// The first and the last column that hold anything, within the rows [top, bottom).
struct Span {
    int first = -1;
    int last = -1;
};
Span ink(const PanelBitmap& b, int top, int bottom) {
    Span span;
    for (int x = 0; x < b.width; ++x) {
        for (int y = top; y < bottom; ++y) {
            if (alpha(b, x, y) != 0) {
                span.first = span.first < 0 ? x : span.first;
                span.last = x;
                break;
            }
        }
    }
    return span;
}

}  // namespace

SH_TEST(text_panel_font_must_be_a_font) {
    PanelFont font;
    SH_CHECK(!font.ready());
    SH_CHECK(!font.load(fs::path(SH_PROJECT_ROOT) / "no-such-font.ttf"));
    SH_CHECK(!font.load(fs::path(SH_PROJECT_ROOT) / "VERSION"));
    SH_CHECK(!font.ready());
    const std::vector<std::string> rows{"A\tb"};
    SH_CHECK(font.render(rows, 20.0f).empty());
}

SH_TEST(text_panel_renders_rows_in_two_columns) {
    PanelFont font;
    SH_CHECK(font.load(kFont));
    if (!font.ready()) {
        return;
    }
    const std::vector<std::string> rows{"HEADING", "W\tmove", "Shift / Ctrl\tfast"};
    const float px = 20.0f;
    const PanelBitmap b = font.render(rows, px);
    SH_CHECK(!b.empty());
    SH_CHECK(b.rgba.size() == static_cast<std::size_t>(b.width) * b.height * 4);
    // Three lines of 1.25 of the text height, a one-pixel outline and two clear pixels on each side.
    SH_CHECK(b.height == static_cast<int>(px * 1.25f * 3) + 2 * 3);

    // The corners are clear; the text is white where it is solid.
    SH_CHECK(alpha(b, 0, 0) == 0 && alpha(b, b.width - 1, b.height - 1) == 0);
    bool solid_white = false;
    for (std::size_t i = 0; i < b.rgba.size(); i += 4) {
        solid_white = solid_white || (b.rgba[i + 3] == 255 && b.rgba[i] == 255 && b.rgba[i + 1] == 255);
    }
    SH_CHECK(solid_white);

    // A heading starts at the left edge; the keys end in one column, the longer one starting further left.
    const int line = static_cast<int>(px * 1.25f);
    const Span heading = ink(b, 3, 3 + line);
    const Span short_key = ink(b, 3 + line, 3 + 2 * line);
    const Span long_key = ink(b, 3 + 2 * line, 3 + 3 * line);
    SH_CHECK(heading.first >= 0 && heading.first <= 4);
    SH_CHECK(long_key.first >= 0 && long_key.first <= 4);
    SH_CHECK(short_key.first > long_key.first + 20);
    // Every row ends inside the picture, with the outline and the clear pixels to spare.
    SH_CHECK(heading.last < b.width - 2 && short_key.last < b.width - 2 && long_key.last < b.width - 2);

    // The same rows give the same picture; a larger text height a larger one with a thicker outline.
    SH_CHECK(font.render(rows, px).rgba == b.rgba);
    const PanelBitmap large = font.render(rows, 24.0f);
    SH_CHECK(large.width > b.width && large.height == static_cast<int>(24.0f * 1.25f * 3) + 2 * 4);
}

SH_TEST(text_panel_refuses_degenerate_pictures) {
    PanelFont font;
    if (!font.load(kFont)) {
        SH_SKIP("the panel font is not in gamedata/font");
    }
    SH_CHECK(font.render({}, 20.0f).empty());
    const std::vector<std::string> wide{std::string(2000, 'W')};
    SH_CHECK(font.render(wide, 40.0f).empty());  // wider than 4096 pixels
}

SH_TEST(frame_times_slowest_percent) {
    sh::hud::FrameTimes times;
    SH_CHECK(times.count() == 0 && times.slowest_percent() == 0.0);
    for (int i = 0; i < 99; ++i) {
        times.add(10.0f);
    }
    times.add(50.0f);
    SH_CHECK(times.count() == 100 && times.slowest_percent() == 50.0);  // one frame of a hundred
    for (int i = 0; i < 100; ++i) {
        times.add(i == 0 ? 30.0f : 10.0f);
    }
    SH_CHECK(times.slowest_percent() == 40.0);  // the two slowest of two hundred
    // Only the newest frames are kept.
    for (std::size_t i = 0; i < sh::hud::FrameTimes::kCapacity; ++i) {
        times.add(5.0f);
    }
    SH_CHECK(times.count() == sh::hud::FrameTimes::kCapacity && times.slowest_percent() == 5.0);
}

SH_TEST(frame_window_counts_half_seconds) {
    sh::hud::FrameWindow window;
    sh::hud::FrameWindow::Closed closed;
    const std::uint64_t frequency = 1000;
    std::uint64_t now = 5000;
    SH_CHECK(!window.frame(now, frequency, closed));  // the first frame starts the window
    int frames = 1;
    while (!window.frame(now += 10, frequency, closed)) {
        ++frames;
    }
    SH_CHECK(now == 5500 && closed.ticks == 500 && closed.frames == frames + 1);
    SH_CHECK(closed.rate() == 102 && closed.seconds() == 0.5);  // 51 frames counted in half a second
    // The next window starts at the frame that closed the last one.
    SH_CHECK(!window.frame(now + 499, frequency, closed));
    SH_CHECK(window.frame(now + 500, frequency, closed) && closed.frames == 2);
    window.reset();
    SH_CHECK(!window.frame(now + 5000, frequency, closed));
}
