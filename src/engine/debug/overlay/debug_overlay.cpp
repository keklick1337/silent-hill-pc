// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/debug/overlay/debug_overlay.h"

#include "engine/config/pc_config.h"
#include "engine/console/instance.h"
#include "engine/debug/command_services.h"
#include "engine/debug/console_host.h"
#include "engine/debug/dbg_trace.h"
#include "engine/debug/overlay/bitmap_text.h"
#include "engine/debug/overlay/pointer_sprite.h"
#include "engine/debug/overlay/toast_queue.h"
#include "engine/devtools/tool_mode.h"
#include "engine/game_state/game_state.h"
#include "engine/log/sh_log.h"
#include "features/dump/game_dump.h"
#include "features/freecam/controls_panel.h"
#include "features/hud/fps_counter.h"
#include "input/bind/console_binds.h"
#include "input/mouse_pointer.h"
#include "input/object_pick.h"
#include "input/pad.h"
#include "input/pc_quick_actions.h"
#include "platform/sh_platform.h"
#include "render/gpu/render_queries.h"
#include "render/gpu/render_state.h"
#include "render/overlay/overlay.h"
#include "ui/menu/pc_debug_menu.h"
#include "ui/screens/bindings_screen.h"

#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_scancode.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_timer.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>


// engine/app/loop/main_loop.cpp: the keyframe inspector (the console's `keyframe`, the , and . keys).
extern int g_DebugAnimKfView;
extern int g_DebugAnimKf;
extern int g_DebugAnimKfMax;

namespace sh::debug {

namespace {

namespace gs = sh::game_state;

constexpr int kPanelScale = 2;  // screen pixels per panel pixel
constexpr int kInspectorColumns = 28;
constexpr int kInspectorRows = 8;
constexpr Ink kInspectorInk{255, 210, 110, 255};  // amber, apart from the white toast
constexpr int kPointerRetryFrames = 30;

// A key of the config that acts on its press, not while held.
class PressEdge {
public:
    bool pressed(bool down) {
        const bool edge = down && !was_down_;
        was_down_ = down;
        return edge;
    }

private:
    bool was_down_ = false;
};

// Lays panels out over the picture the way they were first measured: in a -1..1 square with y up, turned into
// picture pixels at the end.
struct PictureSquare {
    float width = 1.0f, height = 1.0f;

    sh::overlay::Rect rect(float left, float top, float right, float bottom) const {
        const float x = (left + 1.0f) * 0.5f * width;
        const float y = (1.0f - top) * 0.5f * height;
        return {x, y, (right + 1.0f) * 0.5f * width - x, (1.0f - bottom) * 0.5f * height - y};
    }
    float span_x(int pixels) const { return 2.0f * static_cast<float>(pixels) / width; }
    float span_y(int pixels) const { return 2.0f * static_cast<float>(pixels) / height; }
};

class DebugOverlay {
public:
    void update();
    void draw();
    void notice(const char* line);
    void flush_log();

private:
    void gather_inspector();
    void debug_menu_key(const bool* keys);
    void exit_key(const bool* keys);
    bool exit_bind_down(const bool* keys) const;
    sh::overlay::TextureId pointer_texture();
    void panel(sh::overlay::TextureId texture, const sh::overlay::Rect& rect) const;

    ToastQueue toast_;
    // The boot's own log lines are no feedback: toasts start with the first frame of the main loop.
    bool toast_armed_ = false;
    TextPanel toast_panel_{static_cast<int>(ToastQueue::kColumns), static_cast<int>(ToastQueue::kMaxLines)};
    sh::overlay::TextureId toast_texture_ = 0;

    std::vector<std::string> inspector_lines_;
    TextPanel inspector_panel_{kInspectorColumns, kInspectorRows};
    sh::overlay::TextureId inspector_texture_ = 0;

    sh::overlay::TextureId pointer_texture_ = 0;
    bool pointer_is_game_art_ = false;
    int pointer_retry_ = 0;

    SDL_Scancode menu_key_ = SDL_SCANCODE_UNKNOWN;
    int menu_pad_button_ = -1;
    int menu_binds_seen_ = 0;  // g_PcBindsGen + 1 of the resolved binds; 0 = never resolved
    PressEdge menu_edge_;
    PressEdge exit_edge_;
};

DebugOverlay& instance() {
    static DebugOverlay overlay;
    return overlay;
}

void toast_from_log(const char* line) { instance().notice(line); }

void DebugOverlay::notice(const char* line) {
    if (!toast_armed_ || Pc_ConsoleIsOpen()) return;  // the open console already shows the line
    toast_.push(line, static_cast<std::uint32_t>(SDL_GetTicks()));
}

void DebugOverlay::flush_log() { ConsoleHost_DrainLog(toast_from_log); }

void DebugOverlay::update() {
    sh::input::object_pick().frame_end();  // a click parked last frame has had a frame of drawing to resolve

    if (sh::devtools::tool_mode().update) sh::devtools::tool_mode().update();

    DbgTrace_Frame();
    tick_command_services();
    sh::dump::tick();
    flush_log();

    const bool* keys = SDL_GetKeyboardState(nullptr);
    if (keys == nullptr) return;
    toast_armed_ = true;

    // The console first: the binds and the keys below must see it as open on the frame it opened.
    ConsoleHost_Update();

    // Typing a command or moving in the debug menu must not fire what the letters are bound to.
    if (!Pc_ConsoleIsOpen() && !g_PcDebugMenuActive) {
        sh::input::console_binds().update(
            {reinterpret_cast<const std::uint8_t*>(keys), static_cast<std::size_t>(SDL_SCANCODE_COUNT)});
    }

    if (g_DebugAnimKfView) gather_inspector();

    debug_menu_key(keys);
    exit_key(keys);
}

void DebugOverlay::gather_inspector() {
    std::vector<std::string>& lines = inspector_lines_;
    lines.clear();
    const auto line = [&lines](const char* format, auto... values) {
        char text[kInspectorColumns];
        std::snprintf(text, sizeof(text), format, values...);
        lines.emplace_back(text);
    };

    const gs::PlayerAnimDebug anim = gs::player_anim_debug();
    const unsigned status = static_cast<unsigned>(anim.status) & 0xFFu;  // animation index << 1 | active
    const int frame = g_DebugAnimKf;

    line("%s", "== ANIM (K) ,. / step ==");
    line("KF %d / %d", frame, g_DebugAnimKfMax > 0 ? g_DebugAnimKfMax - 1 : 0);

    // The base animation whose keyframes hold the inspected frame (blend entries have no range).
    int holder = -1, first = 0, last = 0;
    for (int index = 0; index < gs::kHarryBaseAnims && holder < 0; ++index) {
        if (gs::harry_anim_range(index, &first, &last) && frame >= first && frame <= last) holder = index;
    }
    if (holder >= 0) {
        line("in anim %d [%d-%d]", holder, first, last);
    } else {
        line("%s", "in anim --  (/ next)");
    }

    line("anim %d  active %d", static_cast<int>(status >> 1), static_cast<int>(status & 1u));
    line("status 0x%02X flg 0x%X", status, static_cast<unsigned>(anim.flags));
    line("ctrl %d  step %d", anim.control, anim.step);
    line("upperBody %d", anim.upper_body);
}

// The debug menu key (key_debug_menu, F10 by default; pad_debug_menu is unbound by default) opens DEBUG
// OPTIONS or closes it. It is not a debug control: allow_debug_controls does not gate it.
void DebugOverlay::debug_menu_key(const bool* keys) {
    if (menu_binds_seen_ != g_PcBindsGen + 1) {  // the controls panel rebinds it
        menu_key_ = SDL_GetScancodeFromName(g_PcConfig.keyDebugMenu);
        menu_pad_button_ = g_PcConfig.padDebugMenu[0] != '\0'
                               ? sh::input::lookup_controller_bind(g_PcConfig.padDebugMenu, -1)
                               : -1;
        menu_binds_seen_ = g_PcBindsGen + 1;
    }

    // Ctrl chords belong to the console and the editors.
    const bool chord = (SDL_GetModState() & SDL_KMOD_CTRL) != 0;
    const bool key = menu_edge_.pressed(menu_key_ != SDL_SCANCODE_UNKNOWN && keys[menu_key_]) && !chord;
    // The pad query is already a click, so it is asked every frame, apart from the key's own edge.
    const bool pad = menu_pad_button_ >= 0 && PC_RawControllerButtonClicked(menu_pad_button_) != 0;
    if (!key && !pad) return;

    const gs::GameState state = gs::game_state();
    const bool reachable = state == gs::GameState::InGame || state == gs::GameState::MainMenu;
    // An attract demo plays recorded input into the same controller the menu reads.
    if (g_PcConsoleInputActive || !reachable || gs::demo_active() || sh::ui::bindings_screen().is_open()) return;

    // The player turns DEBUG OPTIONS on in Options > Interface; an open menu always closes.
    if (g_PcConfig.debugOptions != 0 || Pc_DebugMenu_IsOpen()) {
        Pc_DebugMenu_Request();
    } else {
        SH_DBG_ECHO("Debug options are off: Options > Interface > Debug Options");
    }
}

bool DebugOverlay::exit_bind_down(const bool* keys) const {
    const char* name = g_PcConfig.keyExitGame;
    if (name == nullptr || name[0] == '\0') return false;
    // An open console owns the keyboard and the wheel (the wheel scrolls its text).
    if (Pc_ConsoleIsOpen()) return false;
    if (SDL_strcasecmp(name, "MouseWheelUp") == 0) return sh::input::wheel().upFrames != 0;
    if (SDL_strcasecmp(name, "MouseWheelDown") == 0) return sh::input::wheel().downFrames != 0;
    const SDL_Scancode key = SDL_GetScancodeFromName(name);
    return key != SDL_SCANCODE_UNKNOWN && keys[key];
}

// The exit key (key_exit_game, Escape by default; unbound disables it) works in every state: on the title it
// quits, in play it returns to the title. The screens that use Escape themselves keep it.
void DebugOverlay::exit_key(const bool* keys) {
    if (!exit_edge_.pressed(exit_bind_down(keys))) return;
    if (g_PcConsoleInputActive || g_PcDebugMenuActive || sh::ui::bindings_screen().is_open()) return;

    if (gs::game_state() == gs::GameState::OptionScreen && gs::options_brightness_open()) {
        // The brightness page draws its calibration bar outside the menu: back out as its own cancel does,
        // or the bar would stay over the title.
        sh::render::graphics().calibration_bar = 0;
        gs::sfx_play(gs::sfx::kMenuCancel, 64);
        gs::options_brightness_leave();
    } else if (gs::game_state() == gs::GameState::MainMenu) {
        std::exit(0);
    } else {
        gs::request_warm_reset();
        SH_DBG_ECHO("Exiting to title...");
    }
}

sh::overlay::TextureId DebugOverlay::pointer_texture() {
    sh::overlay::TextureStore& textures = sh::overlay::overlay().textures();
    if (pointer_texture_ == 0) {
        const PointerImage plain = plain_pointer();
        pointer_texture_ = textures.create(kPointerSize, kPointerSize, sh::overlay::Format::Rgba8,
                                           sh::overlay::Filter::Nearest, plain.data());
    }
    if (!pointer_is_game_art_ && --pointer_retry_ < 0) {
        pointer_retry_ = kPointerRetryFrames;
        std::array<std::uint16_t, kPointerTexelWords * kPointerSize> texels{};
        std::array<std::uint16_t, 16> colours{};
        sh::gpu::read_video_memory(texels.data(), kPointerTexelsX, kPointerTexelsY, kPointerTexelWords, kPointerSize);
        sh::gpu::read_video_memory(colours.data(), kPointerColoursX, kPointerColoursY, 16, 1);
        const PointerImage image = decode_pointer(texels, colours);
        if (is_game_pointer(image)) {
            textures.update(pointer_texture_, kPointerSize, kPointerSize, image.data());
            pointer_is_game_art_ = true;
        }
    }
    return pointer_texture_;
}

void DebugOverlay::panel(sh::overlay::TextureId texture, const sh::overlay::Rect& rect) const {
    sh::overlay::overlay().quad(sh::overlay::Space::Picture, rect, {1.0f, 1.0f, 1.0f, 1.0f}, texture);
}

void DebugOverlay::draw() {
    DbgTrace_FramePresented();
    // The post-process runs right after this hook and grades only frames that draw the 3D world.
    sh::render::graphics().scene_frame =
        gs::game_state() == gs::GameState::InGame || gs::game_state() == gs::GameState::MapEvent;

    // Bottom to top: the game's own panels, the tools, the pointers, the console.
    sh::freecam::draw_controls_panel();
    sh::hud::draw_fps_counter();
    if (sh::devtools::tool_mode().draw) sh::devtools::tool_mode().draw();
    sh::input::mouse_pointer().draw();
    ConsoleHost_Render();

    // The menus' pointer is off while the console is open: the console's own is drawn here.
    const bool pointer = ConsoleHost_PointerVisible() != 0;
    const bool inspector = g_DebugAnimKfView != 0 && !inspector_lines_.empty();
    const int toast_alpha =
        Pc_ConsoleIsOpen() ? 0 : toast_.alpha(static_cast<std::uint32_t>(SDL_GetTicks()));
    if (!pointer && !inspector && toast_alpha <= 0) return;

    sh::overlay::Overlay& overlay = sh::overlay::overlay();
    const sh::overlay::Frame& frame = overlay.frame();
    if (!frame.valid()) return;
    const PictureSquare square{frame.picture.w, frame.picture.h};

    if (inspector_texture_ == 0) {
        inspector_texture_ = overlay.textures().create(inspector_panel_.width(), inspector_panel_.height(),
                                                       sh::overlay::Format::Rgba8, sh::overlay::Filter::Nearest,
                                                       nullptr);
        toast_texture_ = overlay.textures().create(toast_panel_.width(), toast_panel_.height(),
                                                   sh::overlay::Format::Rgba8, sh::overlay::Filter::Nearest, nullptr);
    }

    if (pointer) {
        // The same mapping as the console's hit test, so the arrow's tip is the point that gets picked.
        int window_x = 0, window_y = 0;
        float x = 0.0f, y = 0.0f;
        Sh_MouseState(&window_x, &window_y);
        sh::gpu::window_to_overlay(window_x, window_y, &x, &y);
        const float left = -1.0f + 2.0f * (x - frame.picture.x) / square.width;
        const float top = 1.0f - 2.0f * (y - frame.picture.y) / square.height;
        panel(pointer_texture(), square.rect(left, top, left + square.span_x(kPointerSize),
                                             top - square.span_y(kPointerSize)));
    }

    if (inspector) {  // bottom-right corner
        inspector_panel_.draw(inspector_lines_, kInspectorInk);
        overlay.textures().update(inspector_texture_, inspector_panel_.width(), inspector_panel_.height(),
                                  inspector_panel_.rgba());
        const float left = 1.0f - square.span_x(inspector_panel_.width() * kPanelScale);
        const float top = -1.0f + square.span_y(inspector_panel_.height() * kPanelScale);
        panel(inspector_texture_, square.rect(left, top, 1.0f, -1.0f));
    }

    if (toast_alpha > 0) {  // top-left corner, where the closed console rests
        toast_panel_.draw(toast_.lines(), Ink{255, 255, 255, static_cast<std::uint8_t>(toast_alpha)});
        overlay.textures().update(toast_texture_, toast_panel_.width(), toast_panel_.height(), toast_panel_.rgba());
        const float right = -1.0f + square.span_x(toast_panel_.width() * kPanelScale);
        const float bottom = 1.0f - square.span_y(toast_panel_.height() * kPanelScale);
        panel(toast_texture_, square.rect(-1.0f, 1.0f, right, bottom));
    }
}

}  // namespace

void overlay_update() { instance().update(); }

void overlay_draw() { instance().draw(); }

void print_command_line(sh::console::Output& out, std::string_view line) {
    instance().flush_log();
    out.print(sh::console::Level::Plain, line);
}

void console_print(const char* line) {
    instance().flush_log();
    ConsoleHost_PushLine(line, 0);
}

void console_notice(const char* line) {
    instance().flush_log();
    ConsoleHost_PushLine(line, SH_LOGLV_INFO);
    instance().notice(line);
}

}  // namespace sh::debug
