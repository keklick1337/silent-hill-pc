// Host frame: the begin and end of a rendered frame, the swap interval choice and screenshots.
#include "engine/app/command_script.h"
#include "engine/loop/frame_view.h"
#include "engine/debug/command_services.h"
#include "engine/platform/host_state.h"

#include <vector>

#include "engine/timing/vblank.h"
#include "input/pad.h"
#include "render/gpu/frame_driver.h"
#include "render/gpu/renderer.h"


namespace {

bool g_sceneOpen = false;
bool g_vsync = false;

// The swap interval the frame should present with: 1 with vsync on, 0 while fast-forward runs. When the screen
// refreshes slower than the video mode, one interval is dropped to hold the frame rate (60 fps on a 50 Hz screen
// tears, 30 fps on 50 Hz presents at 60 Hz, 30 fps on 60 Hz is clean).
int wanted_swap_interval()
{
    int interval = (g_vsync && !g_skipSwapInterval) ? 1 : 0;

    // The exclusive mode when one is set, otherwise the desktop mode of the window's display.
    const SDL_DisplayMode* mode = SDL_GetWindowFullscreenMode(g_window);
    if (!mode)
        mode = SDL_GetDesktopDisplayMode(SDL_GetDisplayForWindow(g_window));
    if (mode)
    {
        // 0 means "unknown" (common for a window on some drivers); that is not a slow screen,
        // or a requested vsync of 1 would drop to 0 and look stuck off.
        const int refresh = static_cast<int>(mode->refresh_rate + 0.5f);
        if (refresh > 0 && refresh < sh::timing::blank_rate())
            --interval;
    }
    return interval < 0 ? 0 : interval;
}

} // namespace

void sh::platform::set_vsync(bool on)
{
    // Through the per-frame swap interval (the frame begin): a direct SDL_GL_SetSwapInterval would be overwritten
    // on the next frame.
    g_vsync = on;
}

void sh::platform::set_post_capture_hook(void (*hook)())
{
    sh::gpu::set_overlay(hook);
}

// ---------------------------------------------------------------------------------------------
// Frame

bool sh::platform::begin_frame()
{
    sh::platform::pump_events();

    if (g_sceneOpen)
        return false;

    sh::gpu::set_swap_interval(wanted_swap_interval());
    // Clears the frame and, while the game is frozen, draws the captured frame again under this
    // frame's UI (render/gpu/frame_driver.h).
    sh::gpu::frame_begin();

    g_sceneOpen = true;
    return true;
}

void sh::platform::end_frame()
{
    if (!g_sceneOpen)
        return;
    g_sceneOpen = false;

    // Decay the wheel latches once per updated frame, so the pad word and the tuning keys both see a scroll; a
    // frame presented between two updates read nothing.
    if (sh::loop::frame_timing().render_only == 0)
        sh::input::wheel().decay();

    // Stores the frame for the feedback effects (the loading-screen motion blur, the dream
    // overlays read the previous frame back), keeps a copy for freeze frames, then the console and
    // the other overlays (never in those copies), the full-screen look and the present.
    sh::gpu::frame_end();

    // -consolecmd: a due SHOT captures the frame just presented, also on the boot screens that loop outside the
    // main loop.
    sh::app::command_script_present();

    char note[96];
    const sh::loop::FrameTiming& t = sh::loop::frame_timing();
    SDL_snprintf(note, sizeof(note), "ms=%.3f %s", static_cast<double>(SDL_GetTicksNS()) / 1e6,
                 t.render_only != 0 ? "between" : "update");
    sh::debug::shot_sequence_frame(note);
}

void sh::input::poll()
{
    sh::platform::pump_events();

    // The cursor is hidden again every frame: alt-tabbing into fullscreen or a window-mode change
    // can bring the OS pointer back, and the visibility query only reads SDL's own state.
    if (SDL_CursorVisible())
        SDL_HideCursor();

    // No pad polling while Alt is held (Alt+Tab, Alt+Enter); SDL's live modifier state cannot stick.
    if (!(SDL_GetModState() & SDL_KMOD_ALT))
        update_pads();
}

void sh::platform::save_screenshot()
{
    sh::gpu::Renderer* renderer = sh::gpu::renderer();
    int w = 0, h = 0;
    if (!renderer || !renderer->frame_size(&w, &h))
        return;
    std::vector<unsigned char> pixels(static_cast<size_t>(w) * h * 4);
    if (!renderer->read_frame_rgba(pixels.data(), static_cast<int>(pixels.size())))
        return;

    SDL_Surface* surface = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_RGBA32, pixels.data(), w * 4);
    if (surface)
    {
        SDL_SaveBMP(surface, "SCREENSHOT.BMP");
        SDL_DestroySurface(surface);
    }
}
