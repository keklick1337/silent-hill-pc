// Host event pump: drains SDL's queue once per call and turns it into pad hot-plug, window changes,
// mouse-wheel latches, text input and the game's debug key hook.
#include "engine/platform/host_state.h"

#include <cstdlib>

#include "engine/log/sh_log.h"
#include "engine/mods/mod_import.h"
#include "input/pad.h"
#include "platform/sh_platform.h"
#include "render/gpu/frame_driver.h"
#include "render/gpu/render_state.h"
#include "render/gpu/renderer.h"
#include "render/gpu/vram.h"

#include <cstdio>
#include <vector>

// The video memory as a 24-bit TGA (top row first), the debugger's view of the PS1 GPU's pages.
bool sh::platform::save_vram_tga(const char* path)
{
    sh::gpu::Renderer* renderer = sh::gpu::renderer();
    if (!renderer || path == nullptr)
        return false;
    const auto words = renderer->vram().words();
    const int w = sh::gpu::Vram::kWidth, h = sh::gpu::Vram::kHeight;
    std::vector<unsigned char> out(18 + static_cast<size_t>(w) * h * 3, 0);
    out[2] = 2; // uncompressed true colour
    out[12] = static_cast<unsigned char>(w & 0xFF);
    out[13] = static_cast<unsigned char>(w >> 8);
    out[14] = static_cast<unsigned char>(h & 0xFF);
    out[15] = static_cast<unsigned char>(h >> 8);
    out[16] = 24;
    out[17] = 0x20; // top-left origin
    unsigned char* p = out.data() + 18;
    for (const unsigned short word : words)
    {
        *p++ = static_cast<unsigned char>(((word >> 10) & 31) << 3); // B
        *p++ = static_cast<unsigned char>(((word >> 5) & 31) << 3);  // G
        *p++ = static_cast<unsigned char>((word & 31) << 3);         // R
    }
    FILE* file = std::fopen(path, "wb");
    if (file == nullptr)
        return false;
    const bool written = std::fwrite(out.data(), 1, out.size(), file) == out.size();
    return std::fclose(file) == 0 && written;
}

namespace {

using sh::input::LastDevice;

constexpr Sint16 kStickDeadZone = 12000; // past the resting noise of a worn stick

void toggle_fullscreen()
{
    const bool fullscreen = (SDL_GetWindowFlags(g_window) & SDL_WINDOW_FULLSCREEN) != 0;
    // Through the same path as the menu: writing the window size straight into the render size
    // would leave the internal target at the old size.
    sh::platform::apply_window_mode(sh::render::graphics().render_width, sh::render::graphics().render_height,
                                    fullscreen ? sh::platform::WindowMode::Windowed
                                               : sh::platform::WindowMode::Borderless);
}

void handle_key(const SDL_KeyboardEvent& key)
{
    const bool down = key.type == SDL_EVENT_KEY_DOWN;
    int code = key.scancode;

    if (code == SDL_SCANCODE_F11)
    {
        if (down && !key.repeat)
            toggle_fullscreen();
        return;
    }

    if (code == SDL_SCANCODE_RETURN)
    {
        // Alt+Enter goes by SDL's live modifier state: a hand-tracked Alt flag missed the key-up
        // after an alt-tab and made a later plain Enter flip the window mode.
        if (down && (SDL_GetModState() & SDL_KMOD_ALT))
            toggle_fullscreen();
        return;
    }

    // Both sides of a modifier act as the left one.
    if (code == SDL_SCANCODE_RSHIFT)
        code = SDL_SCANCODE_LSHIFT;
    else if (code == SDL_SCANCODE_RCTRL)
        code = SDL_SCANCODE_LCTRL;
    else if (code == SDL_SCANCODE_RALT)
        code = SDL_SCANCODE_LALT;

    if (const auto onText = sh::input::text_input_handler(); onText && down && code == SDL_SCANCODE_BACKSPACE)
        onText(nullptr);

    sh::platform::debug_key(code, down);
}

void handle_wheel(const SDL_MouseWheelEvent& wheel)
{
    // Whole notches: the float y of a smooth-scrolling touchpad carries fractions of one.
    int notches = wheel.integer_y;
    if (wheel.direction == SDL_MOUSEWHEEL_FLIPPED)
        notches = -notches;

    sh::input::wheel().scroll(notches);
    sh::input::set_last_device(LastDevice::Mouse);
}

void handle_mouse_motion(const SDL_MouseMotionEvent& motion)
{
    // A one-pixel nudge is the pointer settling or a warp, not the player reaching for the mouse.
    if (motion.which != SDL_TOUCH_MOUSEID &&
        (motion.xrel > 1 || motion.xrel < -1 || motion.yrel > 1 || motion.yrel < -1))
    {
        sh::input::set_last_device(LastDevice::Mouse);
        Sh_ReleaseTouchPointer();  // the real mouse takes the pointer back from the touch screen
    }

    sh::platform::debug_mouse_motion(static_cast<int>(motion.x), static_cast<int>(motion.y));
}

sh::platform::WindowResizedHook g_resized_hook = nullptr;
bool g_resize_pending = false;
Uint64 g_resize_at = 0;

// A drag sends a size every frame: the hook hears each one, and "settled" once after this long without another.
constexpr Uint64 kResizeSettleMs = 400;

void window_resized()
{
    // A fullscreen window has the display's size, not one the player chose. The size is read now, not from the
    // event: an event queued while fullscreen may be handled after the window left it.
    if (!g_window || (SDL_GetWindowFlags(g_window) & SDL_WINDOW_FULLSCREEN) != 0)
        return;
    int w = 0, h = 0;
    SDL_GetWindowSize(g_window, &w, &h);
    if (w <= 0 || h <= 0)
        return;
    sh::render::graphics().render_width = w;
    sh::render::graphics().render_height = h;
    if (g_resized_hook)
        g_resized_hook(w, h, false);
    g_resize_pending = true;
    g_resize_at = SDL_GetTicks();
}

void settle_resize(bool now)
{
    if (!g_resize_pending || (!now && SDL_GetTicks() - g_resize_at < kResizeSettleMs))
        return;
    g_resize_pending = false;
    if (!g_window || (SDL_GetWindowFlags(g_window) & SDL_WINDOW_FULLSCREEN) != 0)
        return;
    int w = 0, h = 0;
    SDL_GetWindowSize(g_window, &w, &h);
    if (g_resized_hook && w > 0 && h > 0)
        g_resized_hook(w, h, true);
}

} // namespace

namespace {
sh::platform::EventFilter g_event_filter = nullptr;
sh::platform::TouchHook g_touch_hook = nullptr;

void handle_finger(const SDL_TouchFingerEvent& finger)
{
    using sh::platform::TouchPhase;
    if (!g_touch_hook)
        return;
    const TouchPhase phase = finger.type == SDL_EVENT_FINGER_DOWN     ? TouchPhase::Down
                             : finger.type == SDL_EVENT_FINGER_MOTION ? TouchPhase::Move
                             : finger.type == SDL_EVENT_FINGER_UP     ? TouchPhase::Up
                                                                      : TouchPhase::Cancel;
    g_touch_hook(phase, finger.fingerID, finger.x, finger.y, finger.touchID == SDL_MOUSE_TOUCHID);
}

// Only input may be kept from the game: a window change or a quit always goes through.
bool input_event(Uint32 type)
{
    switch (type)
    {
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
    case SDL_EVENT_TEXT_INPUT:
    case SDL_EVENT_MOUSE_MOTION:
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
    case SDL_EVENT_MOUSE_WHEEL:
        return true;
    default:
        return false;
    }
}
} // namespace

void sh::platform::set_event_filter(EventFilter filter)
{
    g_event_filter = filter;
}

void sh::platform::set_touch_hook(TouchHook hook)
{
    g_touch_hook = hook;
}

void sh::platform::pump_events()
{
    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
        if (g_event_filter && g_event_filter(&event) && input_event(event.type))
            continue;
        switch (event.type)
        {
        case SDL_EVENT_GAMEPAD_ADDED:
            sh::input::on_pad_added(event.gdevice.which);
            break;
        case SDL_EVENT_GAMEPAD_REMOVED:
            sh::input::on_pad_removed(event.gdevice.which);
            break;

        case SDL_EVENT_QUIT:
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            settle_resize(true);
            sh::platform::exit_game();
            break;

        case SDL_EVENT_WINDOW_RESIZED:
            window_resized();
            break;

        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            // A fullscreen-desktop window reports the desktop size right after creation; the
            // renderer decides what is the present size and what stays the chosen one.
            sh::gpu::window_changed();
            break;

        case SDL_EVENT_WINDOW_FOCUS_GAINED:
            // SDL drops the pointer clip on deactivation (which is what lets alt-tab out); put it back.
            sh::platform::update_mouse_confinement();
            break;

        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
            sh::input::set_last_device(LastDevice::Pad);
            break;
        case SDL_EVENT_GAMEPAD_AXIS_MOTION:
            if (event.gaxis.value > kStickDeadZone || event.gaxis.value < -kStickDeadZone)
                sh::input::set_last_device(LastDevice::Pad);
            break;

        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            if (event.button.which != SDL_TOUCH_MOUSEID)
                sh::input::set_last_device(LastDevice::Mouse);
            break;
        case SDL_EVENT_MOUSE_MOTION:
            handle_mouse_motion(event.motion);
            break;
        case SDL_EVENT_MOUSE_WHEEL:
            handle_wheel(event.wheel);
            break;

        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
            handle_key(event.key);
            break;
        case SDL_EVENT_TEXT_INPUT:
            if (const auto onText = sh::input::text_input_handler())
                onText(event.text.text);
            break;

        case SDL_EVENT_DROP_FILE:
            // A folder or .zip dropped on the window while Options > Mods > Import is open is imported.
            if (event.drop.data != nullptr)
                sh::mods::import_session().offer_drop(event.drop.data);
            break;

        case SDL_EVENT_FINGER_DOWN:
        case SDL_EVENT_FINGER_MOTION:
        case SDL_EVENT_FINGER_UP:
        case SDL_EVENT_FINGER_CANCELED:
            handle_finger(event.tfinger);
            break;

        default:
            break;
        }
    }
    settle_resize(false);
}

void sh::platform::set_window_resized_hook(WindowResizedHook hook)
{
    g_resized_hook = hook;
}

void sh::platform::debug_mouse_motion(int x, int y)
{
    if (g_dbg_gameDebugMouse)
        g_dbg_gameDebugMouse(x, y);
}

void sh::platform::debug_key(int key, bool down)
{
    // No function key does a diagnostic by default: fast-forward, the screenshot and the
    // VRAM dump are the console's `ff`, `shot` and `vramdump` and the quick options' rows.
    if (g_dbg_gameDebugKeys)
        g_dbg_gameDebugKeys(key, down ? 1 : 0);
}
