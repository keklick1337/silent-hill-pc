// Host window: SDL start-up and shutdown, the window mode switch, the pointer confinement and the process exit.
// The engine renderer creates the window with its device (sh::gpu::start); this module owns the order everything
// starts and stops in, and what a window-mode change implies.
#include "engine/platform/host_state.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <string>

#define SDL_MAIN_HANDLED // main() is the game's own; only SDL_SetMainReady is wanted
#include <SDL3/SDL_main.h>

#include "engine/log/sh_log.h"
#include "engine/timing/vblank.h"
#include "engine/version/engine_version.h"
#include "render/gpu/device_host.h"
#include "render/gpu/frame_driver.h"
#include "render/gpu/render_state.h"
#include "render/gpu/renderer.h"
#include "render/textures/replacements.h"

extern "C" const char* PcPort_GetGameDataPath(void);

SDL_Window* g_window = nullptr;

namespace sh::platform {

namespace {

// Keep the pointer inside the window while the game has focus, so a multi-monitor borderless setup does not let
// it walk onto the next screen and a click there tab the game out. Mouse grab only: the keyboard grab would
// swallow Alt+Tab. SDL clips only while focused, so alt-tabbing releases it.
bool g_confineCursor = true;

std::atomic<bool> g_normalExit{false};

[[noreturn]] void on_terminate()
{
    // abort() skips stdio teardown; without this the tail of the buffered log is lost.
    if (g_ShDebugLog)
        std::fflush(g_ShDebugLog);

    if (g_normalExit.load())
        std::_Exit(0);

    std::fprintf(stderr, "[host] FATAL: std::terminate() called!\n");
    std::fflush(stderr);
    std::abort();
}

void shutdown_at_exit()
{
    shutdown();
}

// Exit status when the teardown does not finish in time.
constexpr int kShutdownHungStatus = 70;
constexpr Uint32 kShutdownDeadlineMs = 10000;

// The last teardown step reached, for the watchdog's message (string literals only).
std::atomic<const char*> g_shutdownStage{"start"};
// shutdown() has returned: the rest of the exit (later atexit handlers, static destructors) may already have
// destroyed the logger, so the watchdog then writes to stderr only.
std::atomic<bool> g_shutdownReturned{false};

void stage(const char* name)
{
    g_shutdownStage.store(name);
    // stderr is unbuffered: the last stage printed before a crash names the step that threw. Without a terminal
    // stderr goes to the log file or nowhere, so the journal gets the step too.
    std::fprintf(stderr, "[host] shutdown: %s\n", name);
    std::fflush(stderr);
    SH_LOG_INFO(GAME, "shutdown: %s", name);
    ShLog_Flush();
}

// The process is exiting either way, so a teardown that never returns must not keep it alive.
// Seen when the X server goes away under a running game: Xlib's I/O error handler calls exit(),
// and the window and GL teardown then spin on the dead connection.
int SDLCALL shutdown_watchdog(void*)
{
    SDL_Delay(kShutdownDeadlineMs);
    const bool returned = g_shutdownReturned.load();
    const char* last = returned ? "after the teardown (atexit handlers, static destructors)" : g_shutdownStage.load();
    std::fprintf(stderr, "[host] shutdown: did not finish in %u ms (last step: %s), exiting with %d\n",
                 static_cast<unsigned>(kShutdownDeadlineMs), last, kShutdownHungStatus);
    std::fflush(stderr);
    if (!returned) {
        SH_LOG_ERROR(GAME, "shutdown did not finish in %u ms (last step: %s), exiting with %d",
                     static_cast<unsigned>(kShutdownDeadlineMs), last, kShutdownHungStatus);
        ShLog_Flush();
    }
    std::_Exit(kShutdownHungStatus);
    return 0;
}

} // namespace

void mark_normal_exit()
{
    g_normalExit.store(true);
}

void install_terminate_handler()
{
    std::set_terminate(on_terminate);
}

void set_mouse_confinement(bool on)
{
    g_confineCursor = on;
}

void update_mouse_confinement()
{
    if (!g_window)
        return;

    // Never in a window the player can see past: trapping the pointer there is hostile. The
    // fullscreen flag covers both exclusive and desktop fullscreen.
    const bool want = g_confineCursor && (SDL_GetWindowFlags(g_window) & SDL_WINDOW_FULLSCREEN) != 0;

    // Relative mode (mouse-look) already confines the pointer and owns the grab state.
    if (SDL_GetWindowRelativeMouseMode(g_window))
        return;

    if (SDL_GetWindowMouseGrab(g_window) != want)
        SDL_SetWindowMouseGrab(g_window, want);
}

void window_size(int* width, int* height)
{
    SDL_GetWindowSize(g_window, width, height);
}

void display_size(int* width, int* height)
{
    *width = 0;
    *height = 0;
    if (!g_window)
        return;
    SDL_DisplayID display = SDL_GetDisplayForWindow(g_window);
    if (display == 0)
        display = SDL_GetPrimaryDisplay();
    if (const SDL_DisplayMode* mode = SDL_GetDesktopDisplayMode(display))
    {
        *width = mode->w;
        *height = mode->h;
    }
}

void resize_window(int width, int height)
{
    if (!g_window || width <= 0 || height <= 0)
        return;
    SDL_SetWindowSize(g_window, width, height);
    // The size event goes through the queue like a window manager's; this frame's pump delivers it.
    SDL_SyncWindow(g_window);
}

// The size the scene is drawn at: the chosen resolution, which a borderless window honours (Targets::resize). A phone's
// window is the whole screen whatever config.cfg says, and its presets set the picture's share of it (render_scale):
// a width and height from the file would draw a 768-line picture on a 1440-line screen there.
static void set_render_size(int width, int height, WindowMode mode)
{
#ifdef __ANDROID__
    width = height = 0;
#endif
    sh::render::graphics().render_width = width;
    sh::render::graphics().render_height = height;
    sh::render::graphics().fullscreen_mode = static_cast<int>(mode);
}

void apply_window_mode(int width, int height, WindowMode mode)
{
    if (!g_window)
        return;

    // Leave fullscreen first: SDL will not switch directly between exclusive and desktop
    // fullscreen, nor re-apply a mode while fullscreen, so a bare SetWindowFullscreen no-ops.
    SDL_SetWindowFullscreen(g_window, false);
    SDL_SyncWindow(g_window);

    if (mode == WindowMode::Exclusive)
    {
        SDL_DisplayID display = SDL_GetDisplayForWindow(g_window);
        if (display == 0)
            display = SDL_GetPrimaryDisplay();
        SDL_DisplayMode closest;
        if (SDL_GetClosestFullscreenDisplayMode(display, width, height, 0.0f, false, &closest))
            SDL_SetWindowFullscreenMode(g_window, &closest);
        SDL_SetWindowSize(g_window, width, height);
        SDL_SetWindowFullscreen(g_window, true);
    }
    else if (mode == WindowMode::Borderless)
    {
        SDL_SetWindowFullscreenMode(g_window, nullptr);
        SDL_SetWindowFullscreen(g_window, true);
    }
    else
    {
        SDL_SetWindowSize(g_window, width, height);
        SDL_SetWindowPosition(g_window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    }
    // SDL3 applies window changes asynchronously; the size read below has to be the new one.
    SDL_SyncWindow(g_window);

    // The window is the PRESENT size now. The renderer keeps its scene target at the RENDER size (the chosen
    // resolution) and stretches it to the window, so a borderless window honours the chosen resolution; it falls
    // back to the desktop size if no target can be made.
    set_render_size(width, height, mode);
    sh::gpu::window_changed();
    update_mouse_confinement();
}

void start(const char* title, int width, int height, WindowMode mode)
{
    install_terminate_handler();

#ifdef _DEBUG
    const std::string window_title = std::string(title ? title : "") + " | Debug";
#else
    const std::string window_title = title ? title : "";
#endif

    // main() is the game's own, so SDL is told it may initialise.
    SDL_SetMainReady();
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        SH_LOG_ERROR(GAME, "SDL did not initialise: %s", SDL_GetError());
        shutdown();
        return;
    }

    sh::gpu::DriverParams params;
    params.window_name = window_title.c_str();
    params.width = width;
    params.height = height;
    params.fullscreen = static_cast<int>(mode);
    params.msaa = sh::render::graphics().msaa_samples;
    params.backend = static_cast<sh::gpu::BackendKind>(sh::render::graphics().backend);
    params.vulkan_validation = sh::render::graphics().vulkan_validation != 0;
    params.software_gl = sh::render::graphics().software_gl;
    params.pipeline_cache_dir = (std::filesystem::path(PcPort_GetGameDataPath()) / "cache" / "pipelines").string();
    params.game_version = sh::version::full();
    params.shader_prewarm = sh::render::graphics().shader_prewarm != 0;
    params.lookup_override = [](int tpage, int clut, int* w, int* h, int* off_x, int* off_y, int* hires_w,
                                int* hires_h) -> std::uint32_t {
        const std::optional<sh::textures::Replacement> found = sh::textures::replacements().find(
            static_cast<std::uint16_t>(tpage), static_cast<std::uint16_t>(clut));
        if (!found) return 0;
        // A caller that only asks whether a replacement exists passes no outputs.
        if (w != nullptr) *w = found->native_w;
        if (h != nullptr) *h = found->native_h;
        if (off_x != nullptr) *off_x = found->offset_x;
        if (off_y != nullptr) *off_y = found->offset_y;
        if (hires_w != nullptr) *hires_w = found->width;
        if (hires_h != nullptr) *hires_h = found->height;
        return found->texture;
    };
    if (!sh::gpu::start(params))
    {
        SH_LOG_ERROR(GAME, "the window and the renderer did not start");
        shutdown();
        return;
    }
    g_window = sh::gpu::renderer()->host().window();
    // What the device granted (MSAA dropped when the window format could not carry it).
    sh::render::graphics().msaa_samples = sh::gpu::renderer()->host().msaa_samples();
    set_render_size(width, height, mode);

    update_mouse_confinement();

    if (!sh::timing::start_vblank())
    {
        shutdown();
        return;
    }

    sh_host_default_input();

    // The game never returns from its loop on purpose, so teardown hangs off exit.
    std::atexit(shutdown_at_exit);

    SDL_HideCursor();
}

void exit_game()
{
    mark_normal_exit();
    std::fprintf(stderr, "[host] normal game exit.\n");
    std::fflush(stderr);
    std::exit(0);
}

void shutdown()
{
    std::fprintf(stderr, "[host] shutdown (window=%p).\n", static_cast<void*>(g_window));
    std::fflush(stderr);

    if (g_window)
    {
        if (SDL_Thread* watchdog = SDL_CreateThread(shutdown_watchdog, "shutdown watchdog", nullptr))
            SDL_DetachThread(watchdog);
    }

    // Every way out of the main loop gets here, including the warm reboot that skips exit_game;
    // the benign teardown exception must take the quiet exit on all of them.
    mark_normal_exit();
    if (!g_window)
        return;

    sh::timing::stop_vblank();
    stage("vblank thread joined");

    // The renderer owns the window: its device objects go first, then the window.
    sh::gpu::stop();
    g_window = nullptr;
    stage("renderer and window destroyed");

    SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
    SDL_Quit();
    stage("SDL_Quit done");
    g_shutdownReturned.store(true);
}

} // namespace sh::platform
