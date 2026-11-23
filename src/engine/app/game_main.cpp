// SPDX-License-Identifier: GPL-3.0-or-later
//
// The game's entry point: reads the command line and config.cfg, imports the disc image on the first run, mounts
// the game data, opens the window and the renderer, hands the settings to the subsystems, loads the game's data
// tables and the start map, and runs the main loop. The steps of main() run in the order they are written in; each
// says what it needs to have happened before it.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <SDL3/SDL.h>

#include "audio/bridge/sound_bridge.h"
#include "audio/voice/voice_line_player.h"
#include "engine/app/command_line.h"
#include "engine/app/command_script.h"
#include "engine/app/control_scheme.h"
#include "engine/app/dialog.h"
#include "engine/app/first_run/first_run.h"
#include "engine/app/quick_setup/hardware_probe.h"
#include "engine/app/quick_setup/quick_setup.h"
#include "engine/app/texture_upscale_run.h"
#include "engine/app/game_data_dir.h"
#include "engine/app/loader/game_files.h"
#include "engine/app/map_info_tables.h"
#include "engine/app/mods_data_reload.h"
#include "engine/app/pc_paths.h"
#include "engine/app/restart.h"
#include "engine/audio/sound_api.h"
#include "engine/config/gameplay_config.h"
#include "engine/config/pc_config.h"
#include "engine/crash/crash_handler.h"
#include "engine/debug/overlay/debug_overlay.h"
#include "engine/desktop/desktop_entry.h"
#include "engine/desktop/window_icon.h"
#include "engine/game_state/game_state_app.h"
#include "engine/ids/game_numbers.h"
#include "engine/loader/map_list.h"
#include "engine/log/sh_log.h"
#include "engine/manifest/map_manifest.h"
#include "engine/memory/work_buffers.h"
#include "engine/mods/data_merge.h"
#include "engine/mods/mods_admin.h"
#include "engine/mods/registry.h"
#include "engine/platform/disc.h"
#include "engine/platform/host_api.h"
#include "engine/platform/window.h"
#include "engine/runtime/game_hooks.h"
#include "engine/script/sh_script.h"
#include "engine/timing/vblank.h"
#include "engine/version/engine_version.h"
#include "engine/vfs/vfs_c.h"
#include "i18n/localization.h"
#include "i18n/text_bridge.h"
#include "input/camera_style.h"
#include "input/pad.h"
#include "media/fmv/movie_player.h"
#include "render/geometry/hero_fill_light.h"
#include "render/gpu/device_host.h"
#include "render/gpu/page_resolve.h"
#include "render/gpu/atmosphere_pass.h"
#include "render/gpu/render_state.h"
#include "render/gpu/renderer.h"
#include "render/post/hdr.h"
#include "render/post/temporal.h"
#include "render/rhi/shader_overrides.h"
#include "render/settings/render_preset.h"

extern "C" {
void MainLoop(void);
void PcPort_InitCharaAnimInfo(void);
void ClipTables_Init(void);
void PuppetNurseVariants_Load(void);
void Inventory_GamedataInit(const char* gamedataDir, const char* modsDir);
void CharaFiles_Load(const char* gamedataDir, const char* modsDir);
void SoundTables_GamedataInit(const char* gamedataDir, const char* modsDir);
void ControllerLayouts_GamedataInit(const char* gamedataDir, const char* modsDir);
void ShScript_InstallGameApi(void);
void ShScript_Boot(const char* gamedataDir, const char* modsDir);
void ShScript_SetModConfigPath(const char* path);
int BootScreen_IsLogoPrim(int clut);
#ifdef _WIN32
__declspec(dllimport) int __stdcall AllocConsole(void);
#endif
}

// Loads that block until they are done instead of spreading over frames (config cd_fast_load).
extern int g_PcFastBlockingLoads;
// The game's window, made by sh::platform::start.
extern SDL_Window* g_window;

namespace {

namespace fs = std::filesystem;

#ifdef _WIN32
constexpr const char* kNullDevice = "NUL";
#else
constexpr const char* kNullDevice = "/dev/null";
#endif

// A file beside the executable. Empty when the path does not fit.
std::string beside_exe(const char* name) {
    char path[1024];
    PcPaths_ExeFile(path, sizeof(path), name);
    return path;
}

// The mods folder, as the data loaders take it.
const char* mods_dir() {
    static const std::string dir = beside_exe("mods");
    return dir.c_str();
}

// Copies a C string setting, cutting what does not fit.
template <std::size_t N>
void set_text(char (&setting)[N], const char* value) {
    std::snprintf(setting, N, "%s", value);
}

// ---- before the game data ----

// The applications-menu entry of a Linux desktop: written or removed, then the program ends.
int desktop_entry(bool install, const char* argv0) {
#if defined(__linux__)
    std::error_code ec;
    fs::path exe = fs::read_symlink("/proc/self/exe", ec);
    if (ec || exe.empty()) {
        exe = fs::absolute(argv0, ec);
    }
    const fs::path data_home = sh::desktop::user_data_home();
    const auto files = install ? sh::desktop::install(exe, exe.parent_path() / "assets" / "icon", data_home)
                               : sh::desktop::uninstall(data_home);
    if (!files) {
        std::fprintf(stderr, "desktop entry: %s\n", files.error().c_str());
        return 1;
    }
    for (const fs::path& file : files.value()) {
        std::printf("%s %s\n", install ? "wrote" : "removed", file.string().c_str());
    }
    if (files.value().empty()) {
        std::printf("nothing to remove\n");
    }
    return 0;
#else
    (void)install;
    (void)argv0;
    std::fprintf(stderr, "desktop entry: only for Linux desktops; on this system the exe carries its icon itself\n");
    return 1;
#endif
}

// config.cfg lives beside the executable. A config left in the directory the game is started from, where an
// earlier layout kept it, is copied there once so its settings carry over.
std::string find_config() {
    std::string path = beside_exe("config.cfg");
    if (path.empty()) {
        return "config.cfg";
    }
    std::error_code ec;
    if (!fs::exists(path, ec) && fs::is_regular_file("config.cfg", ec) &&
        fs::copy_file("config.cfg", path, fs::copy_options::skip_existing, ec)) {
        std::fprintf(stderr, "[CONFIG] copied ./config.cfg to %s\n", path.c_str());
    }
    return path;
}

// Mods patch game.json like any JSON data file (maps by "name"). The journal's name and the disc names were taken
// from the base file when the manifest root was set; this is for the maps, read before the start map is chosen.
void merge_mods_into_manifest() {
    std::vector<std::string> warnings;
    const auto merged = sh::mods::read_data_file(beside_exe("gamedata"), mods_dir(), "game.json", warnings);
    // What the mods folder itself has to say ("mods: ...") is the registry's to report, once, when it starts.
    std::erase_if(warnings, [](const std::string& w) { return w.rfind("mods: ", 0) == 0; });
    sh::mods::emit_merge_warnings(warnings);
    if (merged && merged.value().from_mods()) {
        const int maps = MapManifest_LoadText(merged.value().text.c_str());
        SH_LOG_INFO(GAME, "game.json from %s: %d maps", merged.value().source().c_str(), maps);
    }
}

// The map a start leads to. A config without `map` gets the manifest's new_game_map; -skiptogame goes straight
// into play, and from that default map on to the manifest's start_map, which has a save point.
void choose_start_map(bool skip_to_game) {
    const char* new_game = MapManifest_String("new_game_map");
    if (new_game == nullptr) {
        new_game = MapManifest_Name(0);
        SH_LOG_ERROR(GAME, "game.json has no \"new_game_map\"; the first map %s starts a new game",
                     new_game != nullptr ? new_game : "(none)");
    }
    if (g_PcConfig.mapName[0] == '\0' && new_game != nullptr) {
        set_text(g_PcConfig.mapName, new_game);
    }
    if (!skip_to_game) {
        return;
    }
    g_PcConfig.skipIntros = 2;
    const char* start = MapManifest_String("start_map");
    if (start == nullptr) {
        SH_LOG_ERROR(GAME, "game.json has no \"start_map\"; -skiptogame keeps the configured map");
    } else if (new_game != nullptr && std::strcmp(g_PcConfig.mapName, new_game) == 0) {
        set_text(g_PcConfig.mapName, start);
    }
}

// The quick setup: on the first start, with --setup, or after Options >
// Run Quick Setup restarted the game. It runs before the disc import and before anything of the game exists, in a
// window of its own, so the config it writes is simply read again: the start goes on as a fresh one would. Returns
// the exit code when the player closed the window.
std::optional<int> quick_setup(const sh::app::CommandLine& args, const std::string& config) {
    namespace qs = sh::quick_setup;
    std::error_code ec;
    const bool exists = fs::is_regular_file(config, ec);
    const bool scripted = args.command_file.has_value() || args.quit_after_import;
    const qs::StartState state{args.setup || args.setup_script.has_value(), exists, g_PcConfig.quickSetup, scripted};
    if (!qs::should_show(state)) {
        return std::nullopt;
    }
    qs::Request request;
    request.base_data = beside_exe("gamedata");
    char icons[1024];
    request.icons = PcPaths_Asset(icons, sizeof(icons), "icon");
    request.controller_db = beside_exe("gamecontrollerdb.txt");
    if (args.setup_script) {
        request.script = *args.setup_script;
    }
    request.language = g_PcConfig.languageCode;
    request.window = {g_PcConfig.windowWidth, g_PcConfig.windowHeight};
    for (int preset = 0; preset < RENDER_PRESET_COUNT; ++preset) {
        if (preset == RENDER_PRESET_CUSTOM) {
            continue;
        }
        s_PcConfig values = g_PcConfig;
        RenderPreset_Apply(preset, &values);
        request.presets.push_back({RenderPreset_Name(preset), values.rayTracing != 0});
    }
    const qs::Answer answer = qs::run(request);
    switch (answer.outcome) {
        case qs::Outcome::NoDisplay:
            SH_LOG_INFO(GAME, "quick setup: no display to ask on; the settings of %s stay", config.c_str());
            return std::nullopt;
        case qs::Outcome::Quit:
            SH_LOG_INFO(GAME, "quick setup: closed; the game ends");
            return 0;
        case qs::Outcome::Skipped:
        case qs::Outcome::Applied:
            break;
    }
    SH_LOG_INFO(GAME, "quick setup: %s", qs::describe(answer.hardware).c_str());
    const qs::KeyValues values = answer.outcome == qs::Outcome::Applied ? qs::config_values(answer.choices)
                                                                        : qs::KeyValues{{"quick_setup", "0"}};
    if (exists) {
        std::vector<const char*> keys, texts;
        for (const auto& [key, value] : values) {
            keys.push_back(key.c_str());
            texts.push_back(value.c_str());
        }
        PcConfig_SaveKeyValues(keys.data(), texts.data(), static_cast<int>(keys.size()));
    } else if (FILE* f = std::fopen(config.c_str(), "w")) {
        const std::string text = qs::new_config_text(values, PC_CONFIG_VERSION);
        std::fwrite(text.data(), 1, text.size(), f);
        std::fclose(f);
    } else {
        SH_LOG_WARN(GAME, "quick setup: %s cannot be written; the choices hold for this run only", config.c_str());
    }
    for (const auto& [key, value] : values) {
        SH_LOG_INFO(GAME, "quick setup: %s = %s", key.c_str(), value.c_str());
    }
    PcConfig_Load(config.c_str());
    return std::nullopt;
}

// The first run: without gamedata/version.cfg the disc image is imported before anything of the game starts.
// Its progress and errors go to the terminal, so it runs before the output streams are redirected. Returns the
// exit code when the program has to end here, nullopt when the game may start on `gamedata`.
std::optional<int> first_run(const sh::app::CommandLine& args, const std::string& config, std::string& gamedata) {
    // The import names flags and items in the map data it writes, from the base data beside the executable.
    sh::ids::configure(beside_exe("gamedata").c_str(), "");

    PcFirstRunArgs request{};
    request.exe_dir = PcPaths_ExeDir();
    request.config_path = config.c_str();
    request.gamedata_arg = args.data_dir ? args.data_dir->c_str() : nullptr;
    request.image_arg = args.image ? args.image->c_str() : nullptr;
    request.reimport = args.reimport ? 1 : 0;
    request.language = g_PcConfig.languageCode;
    PcFirstRunResult result;
    if (const int code = PcFirstRun_Run(&request, &result); code != 0) {
        return code;
    }
    if (args.quit_after_import) {
        std::fprintf(stderr, "first run: gamedata is ready in %s; --quit-after-import ends here\n",
                     result.gamedata_dir);
        return 0;
    }
    gamedata = result.gamedata_dir;
    return std::nullopt;
}

// ---- the game data is known ----

// Mods' shaders/: the renderer asks here for every shader it compiles; the last mod in load order wins.
void connect_shader_overrides() {
    sh::rhi::set_shader_log([](int level, const std::string& message) {
        if (level != 0) {
            SH_LOG_WARN(RENDER, "[RHI] %s", message.c_str());
        } else {
            SH_LOG_INFO(RENDER, "[RHI] %s", message.c_str());
        }
    });
    sh::rhi::set_shader_file_reader([](std::string_view relative) -> std::optional<std::string> {
        const std::optional<fs::path> file = sh::mods::registry().find(relative);
        if (!file) {
            return std::nullopt;
        }
        std::ifstream in(*file, std::ios::binary);
        if (!in) {
            return std::nullopt;
        }
        return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    });
}

// The numbers of named things, then items, weapons and the attack table. All of it is data - gamedata/items/,
// weapons/, config/attacks.json, with the mods' files over them; nothing of it is compiled in. The numbering
// comes first: the item library numbers the mods' items and weapons into it, and the mods' event flags
// (flags.json, flags/<area>.json) get their save bits before any script runs.
void load_names_and_items(const char* gamedata) {
    sh::ids::configure(gamedata, mods_dir());
    Inventory_GamedataInit(gamedata, mods_dir());
}

// The game script (gamedata/scripts/game.lua, or a mod's) and what `mods reload` reads again.
void start_scripts(const char* gamedata) {
    // Mod settings (sh.config) are kept beside config.cfg, in a file of their own.
    static std::string mod_config = beside_exe("mods_config.cfg");
    if (mod_config.empty()) {
        mod_config = "mods_config.cfg";
    }
    ShScript_SetModConfigPath(mod_config.c_str());
    ShScript_InstallGameApi();
    ShScript_Boot(gamedata, mods_dir());
    sh::hooks::game_boot();

    // After the file index: the texts at once, the menus and the scripts at the next frame (the request may come
    // from inside a script), the game data between frames.
    sh::mods::add_reloader("text", [] {
        sh::i18n::reload();
        return std::string("read again (language ") + Pc_TextLang() + ")";
    });
    sh::mods::add_reloader("menus and scripts", [] {
        ShScript_RequestReload();
        return std::string("defined and run again at the next frame (on_boot runs again)");
    });
    // Items, weapons, flags, game.json, config/, characters, maps and their scripts, models and textures: read
    // again when a data file changed; in play the current map then loads again in place.
    sh::mods::add_reloader("game data", [] { return sh::app::request_data_reload(); });
}

// The log file (<exe>/logs/, with log_file = 1 or the older enable_debug_log = 1), and what a report needs first:
// which build this is and, with the debug log, the settings as the user has them.
void start_log_file(const std::string& config) {
    ShLog_SetLegacyDebugLog(g_PcConfig.enableDebugLog);
    if (ShLog_FileWanted() && ShLog_StartFile(PcPaths_ExeDir())) {
        SH_LOG_INFO(GAME, "log file %s", ShLog_FilePath());
    }
    SH_LOG_INFO(GAME, "Silent Hill PC engine %s (build %d, commit %s, %s)", std::string(sh::version::kText).c_str(),
                sh::version::build_number(), sh::version::commit(), sh::version::build_stamp());
    if (g_PcConfig.enableDebugLog != 0) {
        PcConfig_LogEffective(config.c_str());
        SH_LOG_DEBUG(CONFIG, "graphics_preset=%s flashlight_mode=%d use_pgxp=%d resident_textures=%d",
                     RenderPreset_Name(g_PcConfig.graphicsPreset), g_PcConfig.flashlightMode, g_PcConfig.usePgxp,
                     g_PcConfig.residentTextures);
    }
}

// The shape of the picture. pixel_aspect: the console showed its 320x224 picture inside the 350x240 visible area
// of an NTSC frame, so a pixel is (350/240)/(4/3) = 35/32 as tall as wide and the picture a little narrower than
// 4:3; display_aspect = raw uses it. The world scales and shifts are the view's field of view settings.
void apply_picture_settings() {
    auto& picture = sh::render::picture();
    picture.pixel_aspect = g_PcConfig.pixelAspect;
    picture.world_h_scale = g_PcConfig.worldHScale;
    picture.world_v_scale = g_PcConfig.worldVScale;
    picture.world_v_shift = g_PcConfig.worldVShift;
    picture.cutscene_v_shift = g_PcConfig.cutsceneVShift;
    // The first-person camera's eye position as the player left it.
    sh::game_state::set_fps_head_offset(g_PcConfig.fpsHeadX, g_PcConfig.fpsHeadY, g_PcConfig.fpsHeadZ);
    picture.widescreen_mode = g_PcConfig.widescreenMode;
    picture.menu_pillarbox = g_PcConfig.menuPillarbox;
}

// Points a standard stream at a file or device. One that cannot be reopened is lost: only stray output went there.
bool redirect(std::FILE* stream, const char* path, const char* mode, bool unbuffered = true) {
    if (std::freopen(path, mode, stream) == nullptr) {
        return false;
    }
    if (unbuffered) {
        std::setvbuf(stream, nullptr, _IONBF, 0);
    }
    return true;
}

// Where stray printf output goes. show_console = 1 or 3 wants a terminal: on Windows the game has none of its
// own and opens one. Otherwise the output joins the log file, or goes nowhere - the journal reaches the terminal
// through its own descriptor either way. (The in-game console is another thing: console_enabled.)
void route_standard_output() {
    const bool terminal = g_PcConfig.showConsole == 1 || g_PcConfig.showConsole == 3;
    if (terminal) {
#ifdef _WIN32
        if (!sh::app::take_over_console()) {
            AllocConsole();
        }
        redirect(stdout, "CONOUT$", "w");
        redirect(stderr, "CONOUT$", "w");
        ShLog_SetTerminalStream(stderr);
#endif
        std::setvbuf(stdout, nullptr, _IONBF, 0);
        std::setvbuf(stderr, nullptr, _IONBF, 0);
    } else if (g_ShDebugLog != nullptr) {
        redirect(stdout, ShLog_FilePath(), "a");
        redirect(stderr, ShLog_FilePath(), "a");
    } else {
        redirect(stdout, kNullDevice, "w", false);
        redirect(stderr, kNullDevice, "w", false);
    }
}

// The memory the game loads its files into, and the tables that point into it; the character animation tables
// (gamedata/tables/anim/, written by the import) and the Puppet Nurse's variants come with them.
void prepare_work_buffers() {
    sh::memory::work_buffers().clear();
    SH_LOG_INFO(GAME, "work buffers: %zu KB", sh::memory::WorkBuffers::total_bytes() / 1024);
    PcPort_InitCharaAnimInfo();
    ClipTables_Init();
    PuppetNurseVariants_Load();
    // The map's data and the demo's recording live in the buffers cleared above.
    sh::game_state::attach_work_buffers();
}

// The boot screens' logo pictures are drawn unfiltered whatever the menus' filter; text keeps that filter.
int unfiltered_prim(int /*tpage*/, int clut) { return BootScreen_IsLogoPrim(clut); }

// What has to be decided before the window exists: multisampling and the choice of device go into its creation.
void apply_settings_before_window() {
    auto& graphics = sh::render::graphics();
    // If the driver refuses the sample count the window is made again without, and this goes back to 0.
    graphics.msaa_samples = g_PcConfig.msaaSamples;
    SH_LOG_INFO(RENDER, "MSAA: %dx", graphics.msaa_samples);

    // renderer = vulkan presents through the engine's own Vulkan device, sdlgpu through an SDL3 GPU device
    // (RENDER.md section 2); each falls back towards OpenGL when it cannot start. There is no OpenGL ES path: any
    // other name means gl.
    const std::optional<sh::gpu::BackendKind> backend = sh::gpu::backend_from_name(g_PcConfig.renderer);
    if (backend) {
        graphics.backend = static_cast<int>(*backend);
        set_text(g_PcConfig.renderer, std::string(sh::gpu::backend_name(*backend)).c_str());
    } else {
        SH_LOG_WARN(RENDER, "renderer '%s' is not supported (gl, vulkan or sdlgpu); using gl", g_PcConfig.renderer);
        set_text(g_PcConfig.renderer, "gl");
        graphics.backend = static_cast<int>(sh::gpu::BackendKind::Gl);
    }
    graphics.vulkan_validation = g_PcConfig.vulkanValidation;
    graphics.software_gl = g_PcConfig.softwareGl;
    graphics.ray_tracing = g_PcConfig.rayTracing;
    graphics.rt_shadows = g_PcConfig.rtShadows;
    graphics.rt_reflections = g_PcConfig.rtReflections;
    graphics.rt_gi = g_PcConfig.rtGi;
    graphics.rt_lights = g_PcConfig.rtLights;
    graphics.extra_lights = g_PcConfig.extraLights;
    SH_LOG_INFO(RENDER, "renderer: %s", g_PcConfig.renderer);
    // The props left in the world pass test against the scene's own depth.
    sh::render::precise().world_far_bias = 0;

    // The window is confined from the moment it is created.
    sh::platform::set_mouse_confinement(g_PcConfig.confineCursor != 0);
    sh::gpu::page_hooks().glyph_test = unfiltered_prim;

    // Decides whether the window itself may be multisampled: the native-size picture is scaled onto it.
    sh::render::picture().native_render = g_PcConfig.nativeResolution;
    if (sh::render::picture().native_render) {
        SH_LOG_INFO(RENDER, "picture at the console's resolution in a %dx%d window", g_PcConfig.windowWidth,
                    g_PcConfig.windowHeight);
    }
    RenderSettings_Push();

    // Before the pads are opened. Over Bluetooth SDL keeps DualShock 4 and DualSense pads in their simple report
    // mode unless asked, and that mode has no touchpad (the puzzles' cursor) and no rumble. Once switched, a pad
    // stays so until it is powered off.
    SDL_SetHint(SDL_HINT_JOYSTICK_ENHANCED_REPORTS, "1");
}

// A window the user resized is the resolution from then on: Options shows it and config.cfg keeps it.
void on_window_resized(int width, int height, bool settled) {
    // A size Options set arrives here too, already in the config: nothing to save then.
    static bool unsaved = false;
    if (g_PcConfig.windowWidth != width || g_PcConfig.windowHeight != height) {
        g_PcConfig.windowWidth = width;
        g_PcConfig.windowHeight = height;
        unsaved = true;
    }
    if (settled && unsaved) {
        const std::string w = std::to_string(width);
        const std::string h = std::to_string(height);
        const char* const keys[] = {"width", "height"};
        const char* const values[] = {w.c_str(), h.c_str()};
        PcConfig_SaveKeyValues(keys, values, 2);
        unsaved = false;
        SH_LOG_INFO(RENDER, "window resized to %dx%d (saved as the resolution)", width, height);
    }
}

// A renderer that draws on the CPU starts at the lightest settings, unless the player chose the graphics: the
// original preset (no MSAA, no post-processing, Classic flashlight and lighting) at render scale 1. Its
// native_resolution is left as configured: the scene target took its size when the window opened, and that key
// applies only at start. Only this run's values change; config.cfg keeps what it has.
void lighten_for_cpu_renderer(const sh::gpu::HostCaps& caps) {
    if (!caps.software || g_PcConfig.softwareGl == static_cast<int>(sh::gpu::SoftwareGl::Off)) return;
    if (PcConfig_GraphicsChosen()) {
        SH_LOG_INFO(RENDER, "%s draws on the CPU; the graphics settings of config.cfg are kept", caps.adapter.c_str());
        return;
    }
    const int native = g_PcConfig.nativeResolution;
    RenderPreset_Apply(RENDER_PRESET_ORIGINAL, &g_PcConfig);
    g_PcConfig.nativeResolution = native;
    sh::render::graphics().msaa_samples = g_PcConfig.msaaSamples;
    RenderSettings_Push();
    SH_LOG_INFO(RENDER, "%s draws on the CPU: graphics_preset %s for this run (set graphics_preset to keep another)",
                caps.adapter.c_str(), RenderPreset_Name(RENDER_PRESET_ORIGINAL));
}

// The window and the renderer's device. False when neither could be made.
bool open_window() {
    static char title[] = "Silent Hill";
    sh::platform::start(title, g_PcConfig.windowWidth, g_PcConfig.windowHeight,
                        sh::platform::window_mode_from_config(g_PcConfig.fullscreen));
    if (!sh::gpu::renderer()) {
        SH_LOG_ERROR(RENDER, "no renderer: the window or the graphics device could not be created");
        return false;
    }
    sh::platform::set_window_resized_hook(on_window_resized);
    const auto& host = sh::gpu::renderer()->host();
    if (const auto asked = sh::gpu::backend_from_name(g_PcConfig.renderer); asked && *asked != host.backend()) {
        SH_LOG_WARN(RENDER, "renderer: %s is not available here; running %s", g_PcConfig.renderer,
                    std::string(sh::gpu::backend_name(host.backend())).c_str());
    }
    const sh::gpu::HostCaps& caps = host.caps();
    SH_LOG_INFO(RENDER, "window %dx%d; GPU %s (%s), driver %s", g_PcConfig.windowWidth, g_PcConfig.windowHeight,
                caps.adapter.c_str(), caps.vendor.c_str(), caps.version.c_str());
    lighten_for_cpu_renderer(caps);

    if (g_window != nullptr) {
        // Text input attaches the system's input method to the window, and a CJK one then swallows every key
        // press before the game sees it. The console turns text input on for itself while it is open.
        SDL_StopTextInput(g_window);
        char icons[1024];
        PcPaths_Asset(icons, sizeof(icons), "icon");
        if (!sh::desktop::set_window_icon(g_window, icons)) {
            SH_LOG_INFO(GAME, "window icon: none in %s", icons);
        }
    }
    return true;
}

// The config's bindings over the stock ones the platform start gave, the camera style, and the window in front.
void apply_input_settings() {
    sh::app::apply_camera_control_scheme();
    // Applies the saved style (and with it that camera's bindings) and writes this build's style names into
    // config.cfg, for a player who sets control_style by hand.
    sh::input::camera_styles().init();
    // On Windows the terminal the game may have opened took the focus before the window existed.
    if (g_window != nullptr) {
        SDL_RaiseWindow(g_window);
    }
    // The d-pad's part in movement is decided every frame by the state of play, so it still drives the menus.
    sh::input::bindings().dpadMovementDisabled = false;
}

// refresh_rate applies to exclusive full screen only: a borderless window runs at the desktop's mode. SDL offers
// a fixed list of modes; the one nearest the wish is taken. Vertical sync goes through the platform's own switch,
// which the frame start reads every frame - the way Options sets it too.
void apply_display_settings() {
    if (g_PcConfig.refreshRate > 0 && g_PcConfig.fullscreen == 1 && g_window != nullptr) {
        const SDL_DisplayMode* current = SDL_GetWindowFullscreenMode(g_window);
        SDL_DisplayMode nearest;
        if (current != nullptr &&
            SDL_GetClosestFullscreenDisplayMode(current->displayID, current->w, current->h,
                                                static_cast<float>(g_PcConfig.refreshRate), false, &nearest)) {
            if (SDL_SetWindowFullscreenMode(g_window, &nearest)) {
                SH_LOG_INFO(RENDER, "display mode: %d Hz asked, %.2f Hz set", g_PcConfig.refreshRate,
                            static_cast<double>(nearest.refresh_rate));
            } else {
                SH_LOG_WARN(RENDER, "display mode: %d Hz could not be set: %s", g_PcConfig.refreshRate,
                            SDL_GetError());
            }
        }
    }
    sh::platform::set_vsync(g_PcConfig.vsync != 0);
}

// texture_filter: 0 off, 1 bilinear, 2 trilinear, 3..6 anisotropic with 2, 4, 8, 16 taps.
void apply_texture_filter() {
    auto& graphics = sh::render::graphics();
    constexpr int kAnisotropic = 3;
    const int setting = g_PcConfig.textureFilter;
    if (setting >= kAnisotropic && setting <= 6) {
        graphics.texture_filter = kAnisotropic;
        graphics.aniso_level = 2 << (setting - kAnisotropic);
    } else {
        graphics.texture_filter = setting == 1 || setting == 2 ? setting : 0;
    }
    graphics.bilinear_filtering = graphics.texture_filter > 0;
    // Dithering and filtering are separate settings; the menus and other 2D frames have a filter of their own.
    graphics.dither = g_PcConfig.dithering != 0 ? 1 : 0;
    graphics.menu_filter = g_PcConfig.menuFilter != 0 ? 1 : 0;
}

// The rest of the picture's look, all of it changeable later from Options and the console.
void apply_graphics_settings() {
    auto& graphics = sh::render::graphics();
    auto& picture = sh::render::picture();
    auto& precise = sh::render::precise();
    auto& flashlight = sh::render::flashlight();

    apply_texture_filter();
    picture.present_scale = g_PcConfig.scaling;

    // use_pgxp: vertices keep their fractions and textures are perspective-correct; off is the original's whole
    // pixels and affine textures.
    precise.enabled = g_PcConfig.usePgxp != 0 ? 1 : 0;
    // The console's GPU never drew a triangle wider than 1023 or taller than 511 pixels; drawing those shows as
    // wedges across the screen when the camera sits inside geometry.
    precise.poly_size_cull = g_PcConfig.polySizeCull != 0 ? 1 : 0;
    const float aspect = PcConfig_RenderHeight() > 0
                             ? static_cast<float>(PcConfig_RenderWidth()) / static_cast<float>(PcConfig_RenderHeight())
                             : 4.0f / 3.0f;
    const bool widened = g_PcConfig.wideEdgeFix != 0 && g_PcConfig.widescreenMode == 1 && aspect > 4.0f / 3.0f;
    precise.poly_size_wide_scale = widened ? aspect / (4.0f / 3.0f) : 1.0f;

    graphics.post_process = g_PcConfig.postProcess;
    graphics.fxaa = g_PcConfig.fxaa;
    graphics.tonemap = g_PcConfig.tonemap;

    // The flashlight mode sets the per-pixel light, its style, its shadows and the defaults of its strength and
    // size; the settings below then put the player's own values over those.
    Pc_FlashlightModeApply(g_PcConfig.flashlightMode, 0);
    sh::render::apply_hero_fill_light();

    flashlight.intensity = g_PcConfig.flashlightIntensity;
    graphics.post_process_intensity = g_PcConfig.postProcessIntensity;
    graphics.tonemap_intensity = g_PcConfig.tonemapIntensity;
    sh::post::HdrSettings& hdr = sh::post::hdr();
    hdr.frame = g_PcConfig.hdrFrame != 0;
    hdr.tonemap = g_PcConfig.hdrTonemap;
    hdr.bloom = g_PcConfig.bloom;
    hdr.bloom_threshold = g_PcConfig.bloomThreshold;
    hdr.output = g_PcConfig.hdrOutput != 0;
    hdr.paper_white = g_PcConfig.hdrPaperWhite;
    hdr.peak = g_PcConfig.hdrPeak;
    sh::render::fog().strength = g_PcConfig.fogStrength;
    sh::render::fog().soft = g_PcConfig.softFog;
    sh::render::AtmosphereSettings& atmosphere = sh::render::atmosphere();
    atmosphere.ambient_occlusion = g_PcConfig.ambientOcclusion;
    atmosphere.ao_strength = g_PcConfig.aoStrength;
    atmosphere.volumetric_fog = g_PcConfig.volumetricFog;
    atmosphere.fog_scatter = g_PcConfig.fogScatter;
    atmosphere.fog_anisotropy = g_PcConfig.fogAnisotropy;
    atmosphere.map_lights = g_PcConfig.mapLights;
    atmosphere.sun_shadows = g_PcConfig.sunShadows;
    atmosphere.god_rays = g_PcConfig.godRays;
    atmosphere.particle_shadows = g_PcConfig.particleShadows;
    sh::post::TemporalSettings& temporal = sh::post::temporal();
    temporal.upscaler = g_PcConfig.upscaler;
    temporal.quality = g_PcConfig.upscaleQuality;
    temporal.sharpness = g_PcConfig.upscaleSharpness;
    temporal.debug = g_PcConfig.temporalDebug;
    temporal.present_mode = g_PcConfig.presentMode;
    temporal.frames_in_flight = g_PcConfig.framesInFlight;
    temporal.present_wait = g_PcConfig.presentWait;
    graphics.brightness = g_PcConfig.brightness;
    graphics.contrast = g_PcConfig.contrast;
    graphics.saturation = g_PcConfig.saturation;
    graphics.image_adjust_scope = g_PcConfig.imageAdjustScope;
    flashlight.size = g_PcConfig.flashlightSize;
    flashlight.shadow_map_size = g_PcConfig.shadowMapSize;
    flashlight.intensity_fps = g_PcConfig.flashlightIntensityFps;
    flashlight.size_fps = g_PcConfig.flashlightSizeFps;

    SH_LOG_INFO(RENDER,
                "vsync %s, dithering %s, texture filter %d, scaling %d, precise vertices %s, oversize cull %s, "
                "post-process %d, tone map %d, flashlight %s",
                g_PcConfig.vsync != 0 ? "on" : "off", graphics.dither != 0 ? "on" : "off", g_PcConfig.textureFilter,
                g_PcConfig.scaling, precise.enabled != 0 ? "on" : "off", precise.poly_size_cull != 0 ? "on" : "off",
                graphics.post_process, graphics.tonemap, Pc_FlashlightModeLabel(g_PcConfig.flashlightMode));
}

// Volumes, then the engine's sound system: converted banks, songs and voice streams on one output.
void start_sound(const char* config_path) {
    sh::fmv::MovieSound& movies = sh::fmv::movie_sound();
    sh::audio::voice_lines().set_volume(g_PcConfig.xaVolume);
    movies.volume = g_PcConfig.fmvVolume;
    movies.console_level = g_PcConfig.fmvOriginalVolume != 0;
    // Nothing is called at the vertical blank until the game asks for it.
    sh::timing::set_vblank_callback(nullptr);
    if (!sh::audio::start_sound_output(sh::audio::load_output_settings(config_path))) {
        SH_LOG_ERROR(AUDIO, "the sound output did not start; the game runs silent");
    }
    ShSound_SetLineGain(g_PcConfig.xaVolume);
}

// The game's tables, each a file of gamedata/config/ (or a directory of gamedata/) with the mods' files over it.
void load_game_tables(const char* gamedata) {
    // The languages of the text directories, the configured one, and the game font.
    sh::i18n::init();
    CharaFiles_Load(gamedata, mods_dir());                     // chara/: the characters' files
    sh::app::load_map_info(gamedata, mods_dir());              // map_info.json: map types, zones, environments
    SoundTables_GamedataInit(gamedata, mods_dir());            // sound_tables.json
    ControllerLayouts_GamedataInit(gamedata, mods_dir());      // controller_layouts.json
    // gameplay.json (health, healing, grab escapes) and difficulties.json (the New Game picker). The engine's
    // built-in values, which stand in for a missing file, are the original's - the same the shipped files hold.
    Gameplay_GamedataInit(gamedata, mods_dir());
    Difficulty_GamedataInit(gamedata, mods_dir());
}

}  // namespace

int main(int argc, char* argv[]) {
    // The restart hook first, so it runs last of all at exit. The manifest root before anything logs: the log
    // file's name and the disc names come from gamedata/game.json.
    sh::app::remember_command_line(argc, argv);
    sh::mods::set_restart_handler(sh::app::restart);
    MapManifest_SetRoot(PcPaths_ExeDir());
    ShLog_Init();
    std::atexit(ShLog_Shutdown);
    sh::crash::install(beside_exe("logs/crash.log"), std::string("Silent Hill PC ") + sh::version::full());

    std::fputs("==============================================\n"
               "  Silent Hill PC\n"
               "  Game data is imported from your own disc image\n"
               "==============================================\n\n",
               stdout);
    const sh::app::CommandLine args = sh::app::parse_command_line(argc, argv);
    using Action = sh::app::CommandLine::Action;
    switch (args.action) {
        case Action::ShowHelp:
            std::printf("Silent Hill PC %s\n%s", sh::version::full(), sh::app::usage_text());
            return 0;
        case Action::ShowVersion:
            std::printf("Silent Hill PC %s\n", sh::version::full());
            return 0;
        case Action::InstallDesktop: return desktop_entry(true, argv[0]);
        case Action::UninstallDesktop: return desktop_entry(false, argv[0]);
        case Action::Run: break;
    }
    if (args.dialogs_to_console) {
        // Tests: the text of a dialog goes to stderr and no window waits for a click.
        ShDialog_SetConsole(1);
    }
    if (args.command_file) {
        sh::app::set_command_script(*args.command_file);
    }
    // Before any window: the window class and the Wayland app id the desktop entry is matched by.
    sh::desktop::set_app_identity(std::string(sh::version::kText).c_str());

    const std::string config = find_config();
    PcConfig_Load(config.c_str());
    if (const std::optional<int> exit_code = quick_setup(args, config)) {
        return *exit_code;
    }
    g_PcFastBlockingLoads = g_PcConfig.cdFastLoad;
    sh::disc::drive_pacing().console_speed = g_PcConfig.cdConsolePace != 0;
    // -renderer overrides config.cfg for this run only; the Options row is then locked, and a restart keeps it.
    if (args.renderer) {
        set_text(g_PcConfig.renderer, args.renderer->c_str());
        g_PcRendererFromCommandLine = 1;
    }

    merge_mods_into_manifest();
    choose_start_map(args.skip_to_game);

    std::string gamedata_dir;
    if (const std::optional<int> exit_code = first_run(args, config, gamedata_dir)) {
        return *exit_code;
    }
    sh::app::set_game_data_dir(gamedata_dir);
    const char* const gamedata = PcPort_GetGameDataPath();
    ShVfs_InitGame(gamedata, PcPaths_ExeDir(), config.c_str());
    sh::app::apply_texture_upscale(g_PcConfig.textureUpscale);

    connect_shader_overrides();
    load_names_and_items(gamedata);
    start_scripts(gamedata);
    start_log_file(config);
    apply_picture_settings();
    route_standard_output();
    // The debug overlay (console, panels) is drawn after a frame has been captured, so it is never part of a
    // frozen picture such as the pause screen's.
    sh::platform::set_post_capture_hook(sh::debug::overlay_draw);
    SH_LOG_INFO(GAME, "game data: %s", gamedata);

    prepare_work_buffers();
    apply_settings_before_window();
    if (!open_window()) {
        return 1;
    }
    apply_input_settings();
    apply_display_settings();
    apply_graphics_settings();
    start_sound(config.c_str());

    // The disc's file table, from the imported game data: every later read names a file through it.
    sh::game_state::init_file_table();
    SH_LOG_INFO(FS, "the disc's files are read from the game data in %s", ShVfs_GameDataDir());
    load_game_tables(gamedata);
    sh::loader::queue_clear();
    // Needs the animation tables and the work buffers; the main loop needs a loaded map.
    sh::loader::load_start_map();

    MainLoop();

    SH_LOG_DEBUG(GAME, "the main loop ended; shutting down");
    sh::platform::shutdown();
    return 0;
}
