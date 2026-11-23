// The parts of the debug layer (src/engine/debug) that need no game: the toast queue, the 8x8 text panel, the
// console pointer's picture, the scripted-run aids, the remembered flags, the capture pictures and the kit the
// console command files share. tests/CMakeLists.txt builds those sources into the runner.

#include "framework/sh_test.h"

#include "engine/console/registry.h"
#include "engine/debug/commands/command_kit.h"
#include "engine/debug/commands/frame_image.h"
#include "engine/debug/commands/remembered_flags.h"
#include "engine/debug/commands/scripted_run.h"
#include "engine/debug/overlay/bitmap_text.h"
#include "engine/debug/overlay/debug_overlay.h"
#include "engine/debug/overlay/pointer_sprite.h"
#include "engine/debug/overlay/toast_queue.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

// The command kit prints through the debug overlay, which drains the log into the console first; the test has
// neither, so the line goes straight to the output.
namespace sh::debug {
static int g_printed_lines = 0;
void print_command_line(sh::console::Output& out, std::string_view line) {
    ++g_printed_lines;
    out.print(sh::console::Level::Plain, line);
}
}  // namespace sh::debug

using namespace sh::debug;

namespace {

struct Lines final : sh::console::Output {
    std::vector<std::string> text;
    std::vector<sh::console::Level> levels;
    void print(sh::console::Level level, std::string_view line) override {
        levels.push_back(level);
        text.emplace_back(line);
    }
};

}  // namespace

SH_TEST(debug_toast_stacks_and_restarts) {
    ToastQueue toast;
    SH_CHECK(toast.alpha(0) == 0);
    toast.push("one", 1000);
    toast.push("two", 1500);
    SH_CHECK(toast.lines().size() == 2);
    // Past the hold time a new line starts a new stack.
    toast.push("three", 1500 + ToastQueue::kHoldMs + 1);
    SH_CHECK(toast.lines().size() == 1 && toast.lines()[0] == "three");
    // Exactly at the hold time it still stacks.
    toast.push("four", 1500 + ToastQueue::kHoldMs + 1 + ToastQueue::kHoldMs);
    SH_CHECK(toast.lines().size() == 2);
}

SH_TEST(debug_toast_keeps_the_newest_lines) {
    ToastQueue toast;
    for (int i = 0; i < 6; ++i) toast.push("line" + std::to_string(i), 100 + i);
    SH_CHECK(toast.lines().size() == ToastQueue::kMaxLines);
    SH_CHECK(toast.lines().front() == "line2" && toast.lines().back() == "line5");
    toast.push(std::string(200, 'x'), 110);
    SH_CHECK(toast.lines().back().size() == ToastQueue::kColumns - 1);
}

SH_TEST(debug_toast_fades_in_holds_and_fades_out) {
    ToastQueue toast;
    toast.push("hello", 5000);
    SH_CHECK(toast.alpha(5000) == 0);
    SH_CHECK(toast.alpha(5060) == 127);  // half of the fade-in
    SH_CHECK(toast.alpha(5000 + ToastQueue::kFadeInMs) == 255);
    SH_CHECK(toast.alpha(5000 + ToastQueue::kHoldMs - 1) == 255);
    SH_CHECK(toast.alpha(5000 + ToastQueue::kHoldMs + 300) == 128);  // half of the fade-out
    SH_CHECK(toast.alpha(5000 + ToastQueue::kHoldMs + ToastQueue::kFadeOutMs) == 0);
}

SH_TEST(debug_bitmap_glyphs) {
    // 'A': the top row is two pixels in the middle, the fifth row is the bar.
    SH_CHECK(!glyph_pixel('A', 0, 0) && !glyph_pixel('A', 1, 0) && glyph_pixel('A', 2, 0) && glyph_pixel('A', 3, 0));
    for (int x = 0; x < 6; ++x) SH_CHECK(glyph_pixel('A', x, 4));
    SH_CHECK(!glyph_pixel('A', 6, 4));
    // '_' is the bottom row only.
    for (int x = 0; x < kGlyphSize; ++x) SH_CHECK(glyph_pixel('_', x, 7) && !glyph_pixel('_', x, 6));
    // Blank: the space, control characters, bytes past ASCII, anything outside the cell.
    for (int y = 0; y < kGlyphSize; ++y) {
        for (int x = 0; x < kGlyphSize; ++x) {
            SH_CHECK(!glyph_pixel(' ', x, y) && !glyph_pixel('\n', x, y) && !glyph_pixel(static_cast<char>(0xC3), x, y));
        }
    }
    SH_CHECK(!glyph_pixel('A', 8, 0) && !glyph_pixel('A', 0, -1));
}

SH_TEST(debug_text_panel_draws_rows_in_one_ink) {
    TextPanel panel(4, 2);
    SH_CHECK(panel.width() == 32 && panel.height() == 16);
    const std::vector<std::string> lines = {"A_", "!", "dropped: past the rows"};
    panel.draw(lines, Ink{10, 20, 30, 40});
    const auto pixel = [&panel](int x, int y) { return panel.rgba() + (static_cast<std::size_t>(y) * 32 + x) * 4; };
    SH_CHECK(pixel(2, 0)[0] == 10 && pixel(2, 0)[1] == 20 && pixel(2, 0)[2] == 30 && pixel(2, 0)[3] == 40);
    SH_CHECK(pixel(0, 0)[3] == 0);
    SH_CHECK(pixel(8, 7)[3] == 40);   // the underscore in the second cell
    SH_CHECK(pixel(3, 8)[3] == 40);   // the stem of '!' on the second row
    // A longer line is cut at the panel's width and a redraw clears what was there.
    const std::vector<std::string> wide = {"     A"};
    panel.draw(wide, Ink{});
    SH_CHECK(pixel(2, 0)[3] == 0 && pixel(8, 7)[3] == 0);
}

SH_TEST(debug_pointer_decodes_video_memory_words) {
    std::array<std::uint16_t, kPointerTexelWords * kPointerSize> texels{};
    std::array<std::uint16_t, 16> colours{};
    colours[1] = 0x001F;  // red
    colours[2] = 0x7C00;  // blue
    colours[3] = 0x8000;  // black with the mask bit: opaque
    texels[0] = 0x3021;   // pixels 0..3 of row 0: colours 1, 2, 0, 3
    texels[kPointerTexelWords * 5 + 7] = 0x1000;  // pixel 31 of row 5: colour 1
    const PointerImage image = decode_pointer(texels, colours);
    const auto pixel = [&image](int x, int y) { return image.data() + (y * kPointerSize + x) * 4; };
    SH_CHECK(pixel(0, 0)[0] == 248 && pixel(0, 0)[1] == 0 && pixel(0, 0)[2] == 0 && pixel(0, 0)[3] == 255);
    SH_CHECK(pixel(1, 0)[0] == 0 && pixel(1, 0)[2] == 248 && pixel(1, 0)[3] == 255);
    SH_CHECK(pixel(2, 0)[3] == 0);
    SH_CHECK(pixel(3, 0)[0] == 0 && pixel(3, 0)[3] == 255);
    SH_CHECK(pixel(31, 5)[0] == 248 && pixel(30, 5)[3] == 0);
    SH_CHECK(!is_game_pointer(image));
}

SH_TEST(debug_pointer_plain_arrow) {
    const PointerImage arrow = plain_pointer();
    const auto alpha = [&arrow](int x, int y) { return arrow[static_cast<std::size_t>(y * kPointerSize + x) * 4 + 3]; };
    SH_CHECK(alpha(0, 0) == 255);            // the tip is the hot spot
    SH_CHECK(alpha(1, 8) == 255 && arrow[static_cast<std::size_t>(8 * kPointerSize + 1) * 4] > 128);  // light inside
    SH_CHECK(arrow[0] < 128);                // dark rim
    SH_CHECK(alpha(31, 31) == 0 && alpha(20, 2) == 0);
    SH_CHECK(!is_game_pointer(arrow));
}

SH_TEST(debug_pad_button_names) {
    SH_CHECK(parse_pad_buttons("").value() == 0);
    SH_CHECK(parse_pad_buttons("-").value() == 0);
    SH_CHECK(parse_pad_buttons("Up+Cross").value() == ((1u << 4) | (1u << 14)));
    SH_CHECK(parse_pad_buttons("select+SQUARE+l3").value() == ((1u << 0) | (1u << 15) | (1u << 1)));
    SH_CHECK(parse_pad_buttons("Up++Down").value() == ((1u << 4) | (1u << 6)));
    SH_CHECK(parse_pad_buttons("R1 Start").value() == ((1u << 11) | (1u << 3)));
    // A '+' with no name before it ends the list.
    SH_CHECK(parse_pad_buttons("+Up").value() == 0);
    const auto unknown = parse_pad_buttons("Up+Jump+Down");
    SH_CHECK(!unknown && unknown.error() == "Jump");
}

SH_TEST(debug_stall_dice) {
    StallDice off;
    for (int i = 0; i < 50; ++i) SH_CHECK(off.roll() == 0);

    StallDice always;
    always.configure(250, 7);
    SH_CHECK(always.percent() == 100);
    for (int i = 0; i < 200; ++i) {
        const std::uint32_t ms = always.roll();
        SH_CHECK(ms >= 40 && ms <= 100);
    }

    StallDice a, b;
    a.configure(30, 4660);
    b.configure(30, 4660);
    int stalled = 0;
    for (int i = 0; i < 2000; ++i) {
        const std::uint32_t ms = a.roll();
        SH_CHECK(ms == b.roll());
        if (ms != 0) ++stalled;
    }
    SH_CHECK(stalled > 400 && stalled < 800);  // about 30 %
    a.configure(-5, 1);
    SH_CHECK(a.percent() == 0);
}

SH_TEST(debug_remembered_flags) {
    RememberedFlags flags;
    flags.remember(100, true);
    flags.remember(200, false);
    flags.remember(100, false);
    SH_CHECK(flags.entries().size() == 2);
    SH_CHECK(flags.entries()[0].flag == 100 && !flags.entries()[0].value);
    for (int i = 0; i < 40; ++i) flags.remember(1000 + i, true);
    SH_CHECK(flags.entries().size() == RememberedFlags::kCapacity);
    flags.remember(200, true);  // a known flag still takes its new value when the list is full
    SH_CHECK(flags.entries()[1].value);
    flags.forget_all();
    SH_CHECK(flags.entries().empty());
}

SH_TEST(debug_frame_placed_in_window) {
    FrameImage frame;
    frame.width = 2;
    frame.height = 1;
    frame.rgba = {255, 0, 0, 7, 0, 255, 0, 9};
    frame.make_opaque();
    SH_CHECK(frame.rgba[3] == 255 && frame.rgba[7] == 255);

    const FrameImage window = place_in_window(frame, 6, 3, 1, 1, 4, 2);
    SH_CHECK(window.width == 6 && window.height == 3 && window.rgba.size() == 6u * 3 * 4);
    const auto pixel = [&window](int x, int y) { return window.rgba.data() + (static_cast<std::size_t>(y) * 6 + x) * 4; };
    SH_CHECK(pixel(0, 0)[0] == 0 && pixel(0, 0)[1] == 0 && pixel(0, 0)[3] == 255);  // black around, opaque
    SH_CHECK(pixel(1, 1)[0] == 255 && pixel(2, 1)[0] == 255);                         // the left pixel, doubled
    SH_CHECK(pixel(3, 2)[1] == 255 && pixel(4, 2)[1] == 255);                         // the right one
    SH_CHECK(pixel(5, 1)[0] == 0 && pixel(5, 1)[1] == 0);

    // A rectangle partly outside the window is clipped, not wrapped.
    const FrameImage clipped = place_in_window(frame, 2, 2, -2, 0, 4, 2);
    SH_CHECK(clipped.rgba[1] == 255 && clipped.rgba[0] == 0);
    SH_CHECK(place_in_window(frame, 0, 3, 0, 0, 4, 2).empty());
    SH_CHECK(place_in_window(frame, 6, 3, 0, 0, 0, 2).empty());
}

SH_TEST(debug_frame_png_file) {
    FrameImage frame;
    frame.width = 3;
    frame.height = 2;
    frame.rgba.assign(3 * 2 * 4, 200);
    const std::filesystem::path file = std::filesystem::temp_directory_path() / "sh_debug_parts_test.png";
    const auto written = write_png(frame, file.string());
    SH_CHECK(written);
    if (written) {
        SH_CHECK(std::filesystem::file_size(file) == written.value());
        std::filesystem::remove(file);
    }
    const auto refused = write_png(frame, "/no-such-directory-sh/x.png");
    SH_CHECK(!refused && refused.error() == "cannot write /no-such-directory-sh/x.png");
    SH_CHECK(!write_png(FrameImage{}, file.string()));
}

SH_TEST(debug_command_kit_words_and_numbers) {
    using namespace sh::debug::commands;
    SH_CHECK(upper("aB1+") == "AB1+" && lower("aB1+") == "ab1+");
    SH_CHECK(same_word("Groaner", "GROANER") && !same_word("Groaner", "Groaners"));
    SH_CHECK(contains_word("M6S04_CybilSaved", "cybil") && contains_word("x", "") && !contains_word("ab", "abc"));
    // A quoted word with spaces inside counts as several, as the command line had them.
    const sh::console::Args args({"a b", "", "c"});
    SH_CHECK(spaced_words(args) == (std::vector<std::string>{"a", "b", "c"}));
    SH_CHECK(whole("12abc") == 12 && whole("abc") == 0 && whole("0x10") == 0 && whole("-7") == -7);
    SH_CHECK(real("1.5x") == 1.5 && real("") == 0.0);
    SH_CHECK(switched(sh::console::Args({"off"}), 0, true) == false);
    SH_CHECK(switched(sh::console::Args({"1"}), 0, false) == true);
    SH_CHECK(switched(sh::console::Args(), 0, false) == true && switched(sh::console::Args(), 0, true) == false);
}

SH_TEST(debug_command_kit_output) {
    using namespace sh::debug::commands;
    Lines lines;
    sh::console::Registry registry;
    sh::console::Context ctx{lines, registry};

    const int printed = g_printed_lines;
    say(ctx, "%s is %d", "answer", 42);
    SH_CHECK(lines.text.back() == "answer is 42" && lines.levels.back() == sh::console::Level::Plain);
    SH_CHECK(g_printed_lines == printed + 1);
    say_text(ctx, std::string(400, 'z'));
    SH_CHECK(lines.text.back().size() == kLineLimit);

    // An item that would bring the line to the limit starts a new line.
    lines.text.clear();
    LineWrapper wrap(ctx, 12, 1, " ");
    for (const char* item : {"aaaa", "bbbb", "cccc", "dd"}) wrap.add(item);
    wrap.flush();
    wrap.flush();
    SH_CHECK(lines.text == (std::vector<std::string>{"aaaa bbbb", "cccc dd"}));
}

SH_TEST(debug_command_kit_settings) {
    using namespace sh::debug::commands;
    static float s_number = 1.0f;
    static int s_whole = 3;
    static int s_switch = 0;
    Lines lines;
    sh::console::Registry registry;
    add(registry, "num", "test", "a number", {opt_number("value", "any")},
        number_setting([]() -> float& { return s_number; }, "num: %.2f"), {"n"});
    add(registry, "int", "test", "a whole number", {opt_integer("value", "any")},
        whole_setting([]() -> int& { return s_whole; }, "int: %d"));
    add(registry, "sw", "test", "a switch", {opt_switch("on|off")},
        switch_setting([]() -> int& { return s_switch; }, "sw: %s", "yes", "no"));

    SH_CHECK(registry.execute("num", lines) && lines.text.back() == "num: 1.00");
    SH_CHECK(registry.execute("n 2.5", lines) && lines.text.back() == "num: 2.50" && s_number == 2.5f);
    SH_CHECK(registry.execute("int", lines) && lines.text.back() == "int: 3");
    SH_CHECK(registry.execute("int -8", lines) && lines.text.back() == "int: -8" && s_whole == -8);
    SH_CHECK(registry.execute("sw", lines) && lines.text.back() == "sw: yes" && s_switch == 1);
    SH_CHECK(registry.execute("sw", lines) && lines.text.back() == "sw: no");
    SH_CHECK(registry.execute("sw on", lines) && registry.execute("sw on", lines) && lines.text.back() == "sw: yes");
    SH_CHECK(registry.execute("sw off", lines) && lines.text.back() == "sw: no" && s_switch == 0);
    // The registry refuses what is not a number before the command runs.
    SH_CHECK(!registry.execute("int twelve", lines) && s_whole == -8);

    const sh::console::CommandSpec* spec = registry.find("n");
    SH_CHECK(spec != nullptr && spec->name == "num" && spec->group == "test" && spec->args.size() == 1);
    SH_CHECK(spec != nullptr && spec->args[0].optional && spec->args[0].type == sh::console::ArgType::Float);
}
