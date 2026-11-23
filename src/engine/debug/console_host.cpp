// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/debug/console_host.h"
#include "input/pad.h"

#include "engine/app/pc_paths.h"
#include "engine/config/pc_config.h"
#include "engine/console/console.h"
#include "engine/console/console_view.h"
#include "engine/console/font_atlas.h"
#include "engine/console/instance.h"
#include "engine/debug/console_commands.h"
#include "engine/debug/console_render.h"
#include "engine/log/sh_log.h"
#include "input/object_pick.h"
#include "platform/sh_platform.h"
#include "render/overlay/overlay.h"

#include "engine/platform/host_api.h"
#include <SDL3/SDL_clipboard.h>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_scancode.h>
#include <SDL3/SDL_timer.h>
#include <SDL3/SDL_video.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include "render/gpu/render_queries.h"

extern int g_PcAllowDebugControls;
extern SDL_Window* g_window;

int g_PcConsoleInputActive = 0;
// Set when input mode ends (Enter or the toggle) and held until the keys are physically released:
// The debug overlay's update runs before the game's input parse, so without this the submitting Enter reached the game
// the same frame as Start. game_main releases it next to the pad suppression.
int g_PcConsoleSwallowInput = 0;

namespace {

namespace con = sh::console;

constexpr float kSlideStep = 0.18f;
constexpr unsigned kApplyMs = 175u;       // unfrozen window after Enter so the command's effect shows
constexpr unsigned kRepeatDelayMs = 350u;
constexpr unsigned kRepeatRateMs = 40u;
constexpr int kWheelRows = 3;
constexpr unsigned kMultiClickMs = 400;

struct Host {
    std::unique_ptr<con::Console> console;
    con::ConsoleView view;
    con::Renderer renderer;
    std::optional<con::FontAtlas> atlas;
    std::vector<std::uint8_t> font_bytes;
    bool font_tried = false;
    float atlas_em = 0;

    bool open = false;
    float slide = 0.0f;
    unsigned apply_until = 0;
    bool prev_toggle = false;
    std::array<bool, SDL_SCANCODE_COUNT> prev_keys{};
    std::array<unsigned, SDL_SCANCODE_COUNT> press_ms{};
    std::array<unsigned, SDL_SCANCODE_COUNT> last_ms{};

    // Top-left corner of the picture in the overlay target of the last drawn frame: the origin the view is laid
    // out from, for mapping the pointer to the panel.
    float picture_x = 0.0f, picture_y = 0.0f;
    bool vp_valid = false;
    bool selecting = false;
    // Consecutive presses on one spot: 2 picks the word, 3 the line.
    int clicks = 0;
    unsigned click_ms = 0;
    con::TextPos click_pos;
    bool prev_lmb = false;
    bool prev_rmb = false;
    bool text_input_on = false;
    bool had_focus = true;
};

Host& host() {
    static Host* h = new Host;  // leaked: statics of other modules log through it while exiting
    return *h;
}

ConsoleHostToastFn s_toast_fn = nullptr;

bool run_logged(Host& h, const std::string& text);

con::Level level_from_log(int sh_level) {
    switch (sh_level) {
        case SH_LOGLV_ERROR: return con::Level::Error;
        case SH_LOGLV_WARN: return con::Level::Warn;
        case SH_LOGLV_INFO: return con::Level::Info;
        case SH_LOGLV_DEBUG:
        case SH_LOGLV_TRACE: return con::Level::Debug;
        default: return con::Level::Plain;
    }
}

void on_log_line(const char* text, int level, int toast) {
    Host& h = host();
    h.console->print(level_from_log(level), text);
    if (toast && s_toast_fn) s_toast_fn(text);
}

void on_text(const char* s) {
    // NULL is the text pump's Backspace notification; the console handles Backspace through the key pump.
    if (!s || !g_PcConsoleInputActive) return;
    host().console->on_text(s);
}

Host& ensure() {
    Host& h = host();
    if (h.console) return h;
    con::Registry& registry = con::global_registry();
    h.console = std::make_unique<con::Console>(registry);
    con::register_builtin_commands(registry);
    h.console->register_console_commands();
    sh::debug::register_game_commands(registry);
    con::set_active_output(h.console.get());
    h.console->set_history_file(std::filesystem::path(PcPaths_ExeDir()) / "console_history.txt");
    h.console->clipboard_get = [] {
        char* clip = SDL_GetClipboardText();
        std::string out = clip ? clip : "";
        if (clip) SDL_free(clip);
        return out;
    };
    h.console->clipboard_set = [](const std::string& text) {
        if (!SDL_SetClipboardText(text.c_str())) SH_WARN("[CONSOLE] clipboard: %s", SDL_GetError());
    };
    h.console->run_line = [](std::string_view line) { return run_logged(host(), std::string(line)); };
    h.console->on_executed = [](std::string_view, bool) {
        // Unfreeze briefly so the command's effect renders, then input mode resumes (see ConsoleHost_Update).
        g_PcConsoleInputActive = 0;
        g_PcConsoleSwallowInput = 1;
        host().apply_until = SDL_GetTicks() + kApplyMs;
    };
    return h;
}

// Em pixel height: console_scale = N fixes it (N * 8), 0 follows the window height.
float wanted_em(int viewport_height) {
    if (g_PcConfig.consoleScale > 0) return static_cast<float>(g_PcConfig.consoleScale * 8);
    const float em = std::round(static_cast<float>(viewport_height) / 45.0f);
    return std::clamp(em, 12.0f, 28.0f);
}

// Returns false when no font could be loaded (the console still toggles, nothing is drawn).
bool ensure_font(Host& h, int viewport_height) {
    if (!h.font_tried) {
        h.font_tried = true;
        const std::filesystem::path bundled = std::filesystem::path(PcPaths_ExeDir()) / "fonts" / "JetBrainsMonoNL-Regular.ttf";
        const auto candidates = con::font_candidates(g_PcConfig.consoleFont, bundled);
        std::filesystem::path used;
        auto bytes = con::load_first_font(candidates, &used);
        if (bytes) {
            h.font_bytes = std::move(bytes.value());
        } else {
            SH_WARN("[console] no console font found (looked for console_font, system monospace and fonts/JetBrainsMonoNL-Regular.ttf); the console draws nothing");
        }
    }
    if (h.font_bytes.empty()) return false;
    const float em = wanted_em(viewport_height);
    if (!h.atlas || em != h.atlas_em) {
        auto built = con::FontAtlas::build(h.font_bytes, em);
        if (!built) {
            SH_WARN("[console] font atlas build failed: %s", built.error().c_str());
            h.font_bytes.clear();
            return false;
        }
        h.atlas.emplace(std::move(built.value()));
        h.atlas_em = em;
        h.renderer.upload_atlas(*h.atlas);
    }
    return true;
}

bool key_repeat(Host& h, const bool* ks, int sc) {
    if (!ks[sc]) return false;
    const unsigned now = SDL_GetTicks();
    if (!h.prev_keys[sc]) {
        h.press_ms[sc] = h.last_ms[sc] = now;
        return true;
    }
    if (now - h.press_ms[sc] >= kRepeatDelayMs && now - h.last_ms[sc] >= kRepeatRateMs) {
        h.last_ms[sc] = now;
        return true;
    }
    return false;
}

// Text events only while the console takes input. SDL3 ties them to text input (and with it the OS input
// method), so it is on exactly while the console is open; outside it stays off (game_main.cpp) so a CJK IME never
// swallows game keys. On focus regain the state is re-applied.
void sync_text_input(Host& h) {
    const bool focused = SDL_GetKeyboardFocus() != nullptr;
    const bool want = g_PcConsoleInputActive != 0;
    if (want != h.text_input_on || (h.text_input_on && focused && !h.had_focus)) {
        sh::input::set_text_input_handler(on_text);
        if (g_window) {
            if (want) SDL_StartTextInput(g_window);
            else SDL_StopTextInput(g_window);
        }
        h.text_input_on = want;
    }
    h.had_focus = focused;
}

struct KeyMap {
    SDL_Scancode sc;
    con::Key key;
    bool repeats;
};
constexpr KeyMap kKeys[] = {
    {SDL_SCANCODE_RETURN, con::Key::Enter, false},   {SDL_SCANCODE_KP_ENTER, con::Key::Enter, false},
    {SDL_SCANCODE_TAB, con::Key::Tab, false},        {SDL_SCANCODE_UP, con::Key::Up, true},
    {SDL_SCANCODE_DOWN, con::Key::Down, true},       {SDL_SCANCODE_LEFT, con::Key::Left, true},
    {SDL_SCANCODE_RIGHT, con::Key::Right, true},     {SDL_SCANCODE_HOME, con::Key::Home, false},
    {SDL_SCANCODE_END, con::Key::End, false},        {SDL_SCANCODE_PAGEUP, con::Key::PageUp, true},
    {SDL_SCANCODE_PAGEDOWN, con::Key::PageDown, true}, {SDL_SCANCODE_BACKSPACE, con::Key::Backspace, true},
    {SDL_SCANCODE_DELETE, con::Key::Delete, true},   {SDL_SCANCODE_ESCAPE, con::Key::Escape, false},
};

void pump_keys(Host& h, const bool* ks) {
    con::Mods mods;
    // Windows sends AltGr as Left Ctrl + Right Alt, and AltGr chords type symbols on most non-US layouts,
    // so they are not Ctrl chords.
    mods.ctrl = (ks[SDL_SCANCODE_LCTRL] || ks[SDL_SCANCODE_RCTRL]) && !ks[SDL_SCANCODE_RALT];
    mods.shift = ks[SDL_SCANCODE_LSHIFT] || ks[SDL_SCANCODE_RSHIFT];
    mods.alt = ks[SDL_SCANCODE_LALT];

    if (mods.ctrl) {
        for (int i = 0; i < 26; ++i) {
            const int sc = SDL_SCANCODE_A + i;
            if (ks[sc] && !h.prev_keys[sc]) h.console->on_ctrl_char(static_cast<char>('a' + i));
        }
    }
    for (const KeyMap& m : kKeys) {
        const int sc = m.sc;
        const bool fire = m.repeats ? key_repeat(h, ks, sc) : (ks[sc] && !h.prev_keys[sc]);
        if (fire) h.console->on_key(m.key, mods);
    }
}

void pump_mouse(Host& h) {
    int wx = 0, wy = 0;
    const std::uint32_t buttons = Sh_MouseState(&wx, &wy);
    const bool lmb = (buttons & SDL_BUTTON_MASK(SDL_BUTTON_LEFT)) != 0;
    const bool rmb = (buttons & SDL_BUTTON_MASK(SDL_BUTTON_RIGHT)) != 0;

    if (h.vp_valid && h.slide >= 1.0f) {
        // The view is laid out in the pixels of the overlay's target, which is not the window's size when the
        // scene is supersampled or the display is high-density.
        float ox = 0.0f, oy = 0.0f;
        sh::gpu::window_to_overlay(wx, wy, &ox, &oy);
        const float px = ox - h.picture_x;
        const float py = oy - h.picture_y;
        const auto hit = h.view.hit_test(px, py);
        if (lmb && !h.prev_lmb) {
            if (hit) {
                const unsigned now = static_cast<unsigned>(SDL_GetTicks());
                // The hand shifts a little between presses: a neighbouring character is still the same spot.
                const std::size_t apart = hit->offset > h.click_pos.offset ? hit->offset - h.click_pos.offset
                                                                           : h.click_pos.offset - hit->offset;
                const bool again = h.clicks > 0 && now - h.click_ms <= kMultiClickMs &&
                                   hit->line == h.click_pos.line && apart <= 2;
                h.clicks = again ? h.clicks % 3 + 1 : 1;
                h.click_ms = now;
                h.click_pos = *hit;
                if (h.clicks == 2) h.console->select_word(*hit);
                else if (h.clicks == 3) h.console->select_line(*hit);
                else h.console->select_begin(*hit);
                // A word or line stays as picked; only a plain press starts a drag.
                h.selecting = h.clicks == 1;
            } else {
                h.clicks = 0;
                h.console->select_clear();
                // Below the panel is the scene: pick whatever is there.
                if (!h.view.covers(px, py)) sh::input::object_pick().click_at(wx, wy);
            }
        } else if (lmb && h.selecting) {
            if (const auto drag = h.view.drag_test(px, py)) h.console->select_update(*drag);
        } else if (!lmb) {
            h.selecting = false;
        }
        if (rmb && !h.prev_rmb) {
            // Over the console the right button copies the selection; over the scene it drops the picked character.
            if (h.view.covers(px, py)) (void)h.console->copy_selection();
            else sh::input::object_pick().clear(true);
        }
    }
    h.prev_lmb = lmb;
    h.prev_rmb = rmb;
}

// While a command runs, what it prints also goes to the log (and so to the terminal) as console-category info
// lines after its "> line": a terminal session reads as a transcript, and automated runs read the output there. The journal
// keeps console-category lines out of the scrollback, so nothing shows twice in the console.
int s_commands_running = 0;

// The console, with every printed line also logged.
class LoggedOutput final : public con::Output {
public:
    explicit LoggedOutput(con::Console& console) : console_(console) {}
    void print(con::Level level, std::string_view line) override {
        console_.print(level, line);
        SH_LOG_INFO(CONSOLE, "%.*s", static_cast<int>(line.size()), line.data());
    }

private:
    con::Console& console_;
};

// Runs one line already shown in the scrollback. Typed lines, binds, -consolecmd files and the replay harness
// all come here. Output printed through con::active_output() (scripts, tools) goes through the same tee while
// the command runs; a nested line (a bind or script running a command) tees itself.
bool run_logged(Host& h, const std::string& text) {
    SH_LOG_INFO(CONSOLE, "> %s", text.c_str());
    LoggedOutput out(*h.console);
    con::Output* const outer = con::active_output();
    con::set_active_output(&out);
    ++s_commands_running;
    const bool ok = con::global_registry().execute(text, out);
    --s_commands_running;
    con::set_active_output(outer);
    return ok;
}
}  // namespace

extern "C" {

int Pc_ConsoleIsOpen(void) { return host().open ? 1 : 0; }

int ConsoleHost_PointerVisible(void) { return host().open && host().slide >= 1.0f ? 1 : 0; }

void ConsoleHost_DrainLog(ConsoleHostToastFn onToast) {
    Host& h = ensure();
    (void)h;
    s_toast_fn = onToast;
    ShLog_ConsoleDrain(on_log_line);
    s_toast_fn = nullptr;
}

void ConsoleHost_PushLine(const char* line, int shLogLevel) {
    if (!line) return;
    ensure().console->print(level_from_log(shLogLevel), line);
    if (s_commands_running > 0) SH_LOG_INFO(CONSOLE, "%s", line);
}


void Pc_ConsoleExec(const char* line) {
    if (!line) return;
    Host& h = ensure();
    const std::string text(line);
    if (text.find_first_not_of(" \t\r\n") == std::string::npos) return;
    ShLog_ConsoleDrain(on_log_line);  // lines logged before the command must land above its output
    h.console->print(con::Level::Command, "> " + text);
    (void)run_logged(h, text);
}

void ConsoleHost_Update(void) {
    Host& h = ensure();
    const bool* ks = SDL_GetKeyboardState(nullptr);
    if (!ks) return;

    // Toggle key is configurable (key_console; default tilde). Resolved once: empty falls back to tilde,
    // "NONE" yields UNKNOWN (keyboard-unbindable).
    static SDL_Scancode toggle_sc = SDL_SCANCODE_GRAVE;
    static bool toggle_resolved = false;
    if (!toggle_resolved) {
        toggle_sc = g_PcConfig.keyConsole[0] == '\0' ? SDL_SCANCODE_GRAVE : SDL_GetScancodeFromName(g_PcConfig.keyConsole);
        toggle_resolved = true;
    }
    const bool toggle = toggle_sc != SDL_SCANCODE_UNKNOWN && ks[toggle_sc];
    if ((g_PcConfig.consoleEnabled || g_PcAllowDebugControls) && toggle && !h.prev_toggle) {
        h.open = !h.open;
        h.apply_until = 0;
        if (h.open) {
            g_PcConsoleInputActive = 1;
            h.console->editor_mut().clear();
            h.console->refresh();
            h.console->close_popup();
            h.console->set_scroll(0);
        } else {
            g_PcConsoleInputActive = 0;
            g_PcConsoleSwallowInput = 1;  // don't leak the toggle press
            h.selecting = false;
        }
    }
    h.prev_toggle = toggle;

    // Apply window: the game runs unfrozen so the last command's effect shows and pad input is suppressed;
    // afterwards input mode resumes if the console is still open.
    if (h.apply_until) {
        if (SDL_GetTicks() < h.apply_until) {
            g_PcConsoleSwallowInput = 1;
        } else {
            h.apply_until = 0;
            if (h.open) g_PcConsoleInputActive = 1;
        }
    }

    if (h.open) {
        if (sh::input::wheel().upFrames) h.console->on_wheel(kWheelRows);
        if (sh::input::wheel().downFrames) h.console->on_wheel(-kWheelRows);
        pump_mouse(h);
    }

    sync_text_input(h);
    if (g_PcConsoleInputActive) pump_keys(h, ks);

    for (int sc = 0; sc < SDL_SCANCODE_COUNT; ++sc) h.prev_keys[sc] = ks[sc];

    const float target = h.open ? 1.0f : 0.0f;
    if (h.slide < target) h.slide = std::min(target, h.slide + kSlideStep);
    else if (h.slide > target) h.slide = std::max(target, h.slide - kSlideStep);
}

void ConsoleHost_Render(void) {
    Host& h = ensure();
    if (h.slide <= 0.0f && !h.open) return;
    const sh::overlay::Frame& frame = sh::overlay::overlay().frame();
    if (!frame.valid()) return;
    // The picture in GL terms (origin bottom-left), as the mouse mapping reads it.
    const int vp[4] = {static_cast<int>(frame.picture.x),
                       static_cast<int>(static_cast<float>(frame.target_h) - frame.picture.y - frame.picture.h),
                       static_cast<int>(frame.picture.w), static_cast<int>(frame.picture.h)};
    if (vp[2] <= 0 || vp[3] <= 0) return;
    if (!ensure_font(h, vp[3])) return;

    con::ViewParams params;
    params.width = frame.picture.w;
    params.height = frame.picture.h;
    params.slide = h.slide * h.slide * (3.0f - 2.0f * h.slide);  // smoothstep over the linear step
    params.panel_fraction = 0.5f;
    params.time_seconds = static_cast<double>(SDL_GetTicks()) / 1000.0;
    params.show_caret = g_PcConsoleInputActive != 0;
    const con::DrawList list = h.view.build(*h.console, *h.atlas, params);
    h.picture_x = frame.picture.x;
    h.picture_y = frame.picture.y;
    h.vp_valid = true;
    h.renderer.draw(list);
}

}  // extern "C"
