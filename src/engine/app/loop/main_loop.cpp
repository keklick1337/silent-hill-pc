// SPDX-License-Identifier: GPL-3.0-or-later
//
// The game's main loop on the engine: the host of
// sh::gameflow::MainFrame (the frame's hooks by name, the framing gate, the scene owner, the freeze frame, the clear
// colour and the order-table passes), the boot state machine's host, the game state dispatch by data names and the
// debug camera chain. The flags the developer tools and the cheats set live here.

#include "engine/effects/map_lights.h"
#include "engine/loader/map_list.h"
#include "engine/save/game_saves.h"
#include "input/pad.h"
#include "engine/runtime/game_hooks.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_scancode.h>
#include <SDL3/SDL_timer.h>

#include "audio/voice/game_message_voice.h"
#include "audio/voice/voice_line_player.h"
#include "engine/app/game_view.h"
#include "engine/app/gameflow/demo_host.h"
#include "engine/app/gameflow/game_flow.h"
#include "engine/app/texture_upscale_run.h"
#include "engine/app/loader/game_files.h"
#include "engine/app/loop/frame_loop.h"
#include "engine/app/loop/scene_aim.h"
#include "engine/app/pc_paths.h"
#include "engine/camera/game_camera.h"
#include "engine/cutscene/cutscene_c.h"
#include "engine/config/pc_config.h"
#include "engine/debug/command_services.h"
#include "engine/debug/console_host.h"
#include "engine/debug/overlay/debug_overlay.h"
#include "engine/devtools/replay.h"
#include "engine/devtools/tool_mode.h"
#include "engine/game_state/game_state.h"
#include "engine/game_state/session.h"
#include "engine/gameflow/boot_machine.h"
#include "engine/gameflow/clear_color.h"
#include "engine/gameflow/debug_camera.h"
#include "engine/gameflow/flow_data.h"
#include "engine/gameflow/frame_cheats.h"
#include "engine/gameflow/frame_gate.h"
#include "engine/gameflow/freeze_latch.h"
#include "engine/gameflow/main_frame.h"
#include "engine/gameflow/packet_arena.h"
#include "engine/gameflow/scene_owner.h"
#include "engine/memory/work_buffers.h"
#include "engine/timing/line_counter.h"
#include "engine/app/command_script.h"
#include "features/hud/text_panel.h"
#include "features/quicksave/quick_save.h"
#include "render/geometry/hero_fill_light.h"
#include "engine/loader/queue/disc_files.h"
#include "engine/log/sh_log.h"
#include "engine/loop/frame_view.h"
#include "engine/loop/vblank_clock.h"
#include "engine/platform/window.h"
#include "engine/screens/health_cues.h"
#include "engine/screens/screens_view.h"
#include "ui/pause/pause_menu.h"
#include "ui/screens/boot_warning.h"
#include "render/gpu/render_state.h"
#include "render/frame/frame.h"
#include "render/gpu/game_display.h"
#include "render/scene/frame_blend.h"
#include "render/scene/scene.h"
#include "render/scene/scene_c.h"
#include "engine/math/precise_capture.h"
#include "engine/devtools/perf_monitor.h"
#include "input/alt_camera_view.h"
#include "input/camera_style.h"
#include "input/mouse_pointer.h"

// --- the developer tools' and cheats' flags (read by name across the engine) ---
int g_DebugCamEnabled = 0;        // the free camera flies
int g_DebugFogDisabled = 0;       // fog forced off (free camera only)
int g_DebugNoWallCollision = 0;   // walk through walls
// God mode: no combat damage and health held full. One flag for the `god` command and debug key 7, so either way
// turns it fully off.
int g_PcGodMode = 0;
// Firing spends no ammo: the two decrements are skipped and an empty clip fires; the inventory is never written.
int g_PcInfiniteAmmo = 0;
int g_DebugNoFloorCollision = 0;  // always 0 (the toggle is gone)
int g_DebugThirdPersonCam = 0;    // a control style's alternate camera instead of the game camera
int g_DebugNoTarget = 0;          // enemies ignore Harry
// The fast-forward toggle of the quick options and the console's `ff`; the main loop mirrors it into
// g_skipSwapInterval (the swap skip). The game clock speeds up when either is set.
int g_PcFastForward = 0;
int g_DebugAnimKfView = 0;        // Harry's skeleton frozen on g_DebugAnimKf (keyframe inspector)
int g_DebugAnimKf = 588;          // absolute keyframe posed while inspecting (588: gun forward)
int g_DebugAnimKfMax = 0;         // keyframes of Harry's active animation, published by the player update
int g_DebugAnimPlaying = 0;       // loop the selected keyframe range instead of freezing on one frame
int g_DebugAnimKfStart = 0;
int g_DebugAnimKfEnd = 0;
std::int32_t g_DebugAnimRate = 15 * 4096;  // loop rate, Q12 keyframes per second at 30 fps
int g_DebugViewNpcSlot = -1;      // keyframe viewer target: -1 Harry, else an NPC slot
int g_DebugAnimPlayGen = 0;       // bumped on each loop start so the loop cursor re-seeds
int g_SH_PostFireTrace = 0;       // frames left of verbose post-fire tracing
int g_SH_AlwaysMlTrace = 0;
int g_DebugUnlockFps = 0;         // uncapped frame rate (debug toggle)

// --- frame state the renderer and the HUD read ---
int g_PcMapScreenActive = 0;  // the paper map overlay is shown
// This frame submitted the world: the fog-coloured clear is right only behind a drawn world; on a frame that drew
// nothing it is the visible image (the grey flash).
int g_PcWorldDrawnThisFrame = 0;
// The framing the world was last drawn with: HUD parts laid out before the world is submitted (the minimap) follow
// this rather than the live Hor+ flag, which menus drive to 0 too.
int g_PcWorldHorPlus = 0;
// The framing the gate decided for the UI pass: the world asserts Hor+ for itself at submission, which must not
// leak into the 320-wide UI drawn in the same frame.
int g_PcHorPlusGate = 0;
// A freeze-frame state (pause, map messages) hands control back through this instead of dropping the freeze on
// the spot.
int g_PcFreezeReleasePending = 0;
// A blocking movie returned: its whole runtime lands in the next frame's step, which must not become cutscene
// catch-up debt.
int g_PcFmvClockDiscard = 0;

extern int g_PcAllowDebugControls;
extern int g_skipSwapInterval;
extern int g_PcPuzzleItemDepth;
extern int g_Pc2dBackgroundActive;
extern int g_PcPickupItemActive;
extern int g_PcConsoleInputActive;
extern int g_PcDebugMenuActive;
extern "C" const unsigned char* g_sdlKeyboardState;

extern "C" {
const char* PcPort_GetGameDataPath(void);
int Pc_MapWarp_Due(void);
void Pc_MapWarp_Run(void);
int Pc_DataReload_Due(void);
void Pc_DataReload_Run(void);
std::int32_t Gameplay_MaxHealthQ12(void);
int Pc_AltCamStateOk(void);
void Pc_TpsCamera_Apply(void);
void Pc_FreeCam_Set(int on);
void Pc_FreeCam_Fly(void);
void Pc_FreeCam_Apply(void);
void Pc_DevKeys_Update(void);
void Pc_DebugMenu_Init(void);
void ShScript_OnUpdate(void);
void Pc_ExtraActionsUpdate(void);
void Pc_RearLookUpdate(void);
void Sh_LogPeriodicFlush(void);
void Pc_FogFloorFrameEnd(void);
}

namespace {

namespace gf = sh::gameflow;
namespace gs = sh::game_state;
using sh::game::session;

constexpr int kFadeOutStart = 2;
constexpr int kFadeOutComplete = 5;
constexpr int kFadeInStart = 6;
constexpr int kFadeResetTimestep = 4;
constexpr std::uint32_t kCameraUserFlags = (1u << 0) | (1u << 1);  // a script drives the camera or its target
void* const kResidentBuffer = sh::memory::work_buffers().data(sh::memory::WorkBuffer::PlayerBaseAnim);
void* const kOverlayBuffer = sh::memory::work_buffers().data(sh::memory::WorkBuffer::FileStaging);

int game_state() { return session().game.game_state; }
int sys_state() { return session().system.flow.sys_state; }
bool state_is(gs::GameState s) { return game_state() == static_cast<int>(s); }
bool sys_is(gs::SysState s) { return sys_state() == static_cast<int>(s); }
bool border_active() { return session().system.flow.cutscene_border != sh::game::kCutsceneBorderNone; }
int fade_state() { return sh::screens::fade_status() & 0x7; }
std::int32_t file(const char* path) { return sh::loader::disc_files().find(path); }

// gamedata/config/<file> (or the highest-priority mod's copy); a parse failure is journalled under `tag` and keeps
// the built-ins.
template <class T>
T load_rules(const char* name, const char* tag, const char* what)
{
    char mods[1024];
    PcPaths_ExeFile(mods, sizeof(mods), "mods");
    std::string err;
    T p = gf::load_config<T>(PcPort_GetGameDataPath(), mods, name, &err);
    if (!err.empty()) SH_ERR("[%s] config/%s: %s (built-in %s)", tag, name, err.c_str(), what);
    return p;
}

// --- the draw-packet arenas (sizes and canary: gamedata/config/packet_arena.json) ---

gf::PacketArena& packet_arena()
{
    static gf::PacketArena* s_arena = new gf::PacketArena(load_rules<gf::PacketArenaParams>("packet_arena.json", "PKTARENA", "numbers"));
    return *s_arena;
}

gf::PacketArenaMonitor& packet_monitor()
{
    static gf::PacketArenaMonitor* s_monitor =
        new gf::PacketArenaMonitor(load_rules<gf::PacketArenaParams>("packet_arena.json", "PKTARENA", "numbers"));
    return *s_monitor;
}

int s_buffer = 0;  // the display buffer this frame builds

// --- the debug camera chain ---

class SessionDebugCameraHost final : public gf::DebugCameraHost {
public:
    bool keyboard_ready() const override { return g_sdlKeyboardState != nullptr; }
    bool dev_controls_allowed() const override { return g_PcAllowDebugControls != 0; }
    bool console_or_debug_menu_open() const override { return g_PcConsoleInputActive || g_PcDebugMenuActive; }
    bool free_camera_on() const override { return g_DebugCamEnabled != 0; }
    bool third_person_on() const override { return g_DebugThirdPersonCam != 0; }
    bool alt_camera_state_ok() const override { return Pc_AltCamStateOk() != 0; }
    void free_camera_fly() override { Pc_FreeCam_Fly(); }
    void free_camera_apply() override { Pc_FreeCam_Apply(); }
    void free_camera_set(bool on) override { Pc_FreeCam_Set(on ? 1 : 0); }
    void third_person_apply() override { Pc_TpsCamera_Apply(); }
    void dev_keys_update() override {
        // The editor's UI may be taking the keys the dev table binds.
        if (!sh::devtools::tool_mode().keyboard_captured) Pc_DevKeys_Update();
    }
};

// --- boot ---

class SessionBootHost final : public gf::BootHost {
public:
    bool skip_intros() const override { return g_PcConfig.skipIntros != 0; }
    void screen_init() override { sh::app::view::screen_init(); }
    bool audio_streaming() const override { return sh::app::view::audio_streaming(); }
    void audio_call(int task) override { gs::sound_command(task); }
    void graphics_queue() override
    {
        // The 8x8 debug font (Text_Debug_Draw: the controller screen's action names), queued here because this
        // state runs even with skip_intros. It must stay ahead of FONT16: the atlas covers VRAM (256..319,
        // 496..511) and FONT16's palette sits at (304, 511) inside its last row (glyph cells the debug font never
        // indexes); queued after it, this atlas overwrote the menu font's palette (solid magenta text).
        static const sh::loader::ImagePlace kFont8 = {{0, 20}, 0, 240, 0, 0};
        sh::loader::queue_read_image(file("1ST/FONT8NOC.TIM"), kOverlayBuffer, &kFont8);
        sh::loader::queue_read_image(file("1ST/FONT16.TIM"), kOverlayBuffer,
                                     sh::app::view::picture(sh::app::view::Picture::Font16Atlas));
        sh::loader::queue_read_image(file("1ST/KONAMI.TIM"), kOverlayBuffer,
                                     sh::app::view::picture(sh::app::view::Picture::KonamiLogo));
    }
    void intro_loads_run() override
    {
        // What the logo screens load while they show, when they are skipped.
        sh::app::view::intro_world_loads();
        sh::loader::queue_read(file("ANIM/HB_BASE.ANM"), kResidentBuffer);
        sh::app::title_graphics_load();
        // The title's attract mode starts from the setup file of the first demo. A start straight into the game
        // shows no title; the reset that later leads to one reads the next demo's setup itself.
        if (g_PcConfig.skipIntros < 2) {
            sh::gameflow::demo_player().advance(0);
            sh::gameflow::demo_player().read_setup(sh::gameflow::demo_host());
        }
    }
    // A fade out from the start (reset, fade out, black): the boot waits for it to complete.
    void fade_in_start() override { sh::screens::set_fade_status(kFadeOutStart); }
    bool fade_finished() const override { return fade_state() == kFadeOutComplete; }
    void queue_wait_empty() override { sh::loader::queue_wait_empty(); }
    void settings_restore_defaults() override { sh::app::settings_restore_defaults(); }
    void pad_hook(int arg) override { sh::app::view::vibration_mode_request(arg); }
};

gf::BootMachine& boot_machine()
{
    static SessionBootHost host;
    static gf::BootMachine* s_machine = new gf::BootMachine(host, load_rules<gf::BootParams>("boot.json", "BOOT", "numbers"));
    return *s_machine;
}

// The update of the current game state, by the data table's names.
void game_state_dispatch()
{
    static sh::app::view::StateUpdate s_table[64];
    static int s_count = -1;
    if (s_count < 0) {
        s_count = 0;
        for (const std::string& name : boot_machine().params().state_handlers) {
            const sh::app::view::StateUpdate fn = sh::app::view::game_state_update(name);
            if (fn == nullptr) SH_ERR("[BOOT] config/boot.json: no handler named %s", name.c_str());
            if (s_count < 64) s_table[s_count++] = fn;
        }
    }
    const int state = game_state();
    if (state >= 0 && state < s_count && s_table[state] != nullptr) s_table[state]();
}

// --- scene ownership ---

gf::SceneOwner& scene_owner()
{
    static gf::SceneOwner* s_owner =
        new gf::SceneOwner(load_rules<gf::SceneOwnerParams>("scene_owner.json", "SCENEOWNER", "numbers"));
    return *s_owner;
}

gf::SceneSnapshot scene_snapshot()
{
    gf::SceneSnapshot s;
    s.cutscene_flag = session().sys_flag(sh::game::sys_flag::kCutscene);
    s.border_active = border_active();
    s.sys_state = sys_state();
    s.game_state = game_state();
    s.fade_in_started = fade_state() >= kFadeInStart;
    s.camera_user_flags = (sh::camera::game_camera().state().flags & kCameraUserFlags) != 0;
    s.control_disabled = sh::app::view::player_control_disabled();
    return s;
}

gf::FrameGate& frame_gate()
{
    static gf::FrameGate* s_gate = new gf::FrameGate(load_rules<gf::FrameGateParams>("frame_gate.json", "FRAMEGATE", "numbers"));
    return *s_gate;
}

class SessionFreezeHost final : public gf::FreezeHost {
public:
    bool pause_return_pending() const override { return sh::ui::pause_menu().return_pending(); }
    bool pause_return_take() override { return sh::ui::pause_menu().take_return(); }
    void pause_state_enter() override { session().set_sys_state_next(sh::game::sys_state::kPaused); }
    void pause_reset() override { sh::ui::pause_menu().reset(); }
    void pause_away_update() override { sh::ui::pause_menu().away_update(); }
};

// The fade is opaque between its reset and the start of its fade-in: it is the image.
bool fade_opaque()
{
    const int f = fade_state();
    return f >= kFadeResetTimestep && f <= kFadeInStart;
}

// --- frame hooks ---

void hook_warning_screen()
{
    // The warning screen's quads span the whole framebuffer, so it draws with the 4:3 projection; the frame's
    // framing gate takes over once the loop starts.
    const int previous = sh::render::picture().hor_plus;
    sh::render::picture().hor_plus = 0;
    sh::ui::play_boot_warning();
    sh::render::picture().hor_plus = previous;
}

void hook_frame_cheats()
{
    gf::FrameCheatIn ci;
    ci.god_mode = g_PcGodMode != 0;
    ci.no_target = g_DebugNoTarget != 0;
    ci.fast_forward = g_PcFastForward != 0;
    ci.in_game = state_is(gs::GameState::InGame);
    ci.game_over = sys_is(gs::SysState::GameOver);
    ci.player_dead = gs::chara_health(0) <= 0;
    const gf::FrameCheatOut co = gf::frame_cheats_apply(ci);
    g_PcFastForward = co.fast_forward ? 1 : 0;
    if (co.refill_health) gs::set_chara_health(0, Gameplay_MaxHealthQ12());
    if (co.write_skip_swap) g_skipSwapInterval = co.skip_swap ? 1 : 0;
    if (co.hold_no_target) sh::app::view::hold_no_target();
}

void hook_dpad_movement_gate()
{
    sh::input::bindings().dpadMovementDisabled =
        gf::dpad_movement_disabled(g_PcConfig.disableDpadMovement != 0, state_is(gs::GameState::InGame),
                                   sys_is(gs::SysState::Gameplay), g_PcDebugMenuActive != 0);
}

void hook_packet_arena_check()
{
    if (!state_is(gs::GameState::InGame)) return;
    gf::PacketArena& arena = packet_arena();
    const long used = static_cast<long>(sh::app::view::packets_used(arena.base(s_buffer)));
    const bool ok[2] = {arena.canary_intact(0), arena.canary_intact(1)};
    const gf::PacketArenaReport rep = packet_monitor().observe(used, ok);
    const int size = static_cast<int>(arena.params().size_bytes);
    const int map = gs::save_map_index();

    if (rep.cull_stat) {
        SH_DBG("[CULLSTAT] culling=%s avg %ld packet bytes/frame over %d frames, peak %ld, map %d",
               g_PcConfig.culling ? "modern" : "original", rep.cull_avg, static_cast<int>(rep.cull_frames), rep.peak, map);
    }
    for (int b = 0; b < 2; b++) {
        if (rep.overrun_new[b]) {
            SH_WARN("[PKTARENA] buffer %d OVERRUN — packets past the %d-byte arena "
                    "(this frame used %ld, peak %ld). Expect garbage or missing "
                    "geometry; map %d",
                    b, size, rep.used, rep.peak, map);
        }
    }
    if (rep.high_water_new) SH_LOG("[PKTARENA] high-water %ld of %d bytes (map %d)", rep.peak, size, map);
}

void hook_freeze_frame()
{
    gf::FreezeLatch latch;
    latch.present_last_frame = sh::render::feedback_control().present_last_frame != 0;
    latch.release_pending = g_PcFreezeReleasePending != 0;

    gf::FreezeInputs in;
    in.pickup_active = g_PcPickupItemActive != 0;
    in.in_game = state_is(gs::GameState::InGame);
    in.at_or_below_load_screen = game_state() <= static_cast<int>(gs::GameState::MainLoadScreen);
    in.load_screen_or_menu = state_is(gs::GameState::MainLoadScreen) || state_is(gs::GameState::MainMenu);
    in.sys_gameplay = sys_is(gs::SysState::Gameplay);
    in.pause_flag = session().music_status(sh::game::music_status::kPause);

    static SessionFreezeHost host;
    static gf::FreezeRules rules;
    rules.update(in, latch, host);
    sh::render::feedback_control().present_last_frame = latch.present_last_frame ? 1 : 0;
    g_PcFreezeReleasePending = latch.release_pending ? 1 : 0;
}

void hook_position_key()
{
    static gf::PositionKey s_key;
    gf::PositionKeyIn in;
    in.enabled = g_PcAllowDebugControls && g_sdlKeyboardState && state_is(gs::GameState::InGame);
    in.key_down = in.enabled && g_sdlKeyboardState[SDL_SCANCODE_KP_PERIOD] != 0;
    in.free_camera = g_DebugCamEnabled != 0;
    in.fog_disabled = g_DebugFogDisabled != 0;
    const gf::PositionKeyOut out = s_key.update(in);
    // Marks spots where Harry falls through the floor (pairs with Numpad 3's fall position, where he lands).
    if (out.log_position) sh::app::view::log_player_position(g_DebugCamEnabled != 0);
    g_DebugFogDisabled = out.fog_disabled ? 1 : 0;
    if (out.fog_toggled) SH_DBG_ECHO("[DEBUG] Numpad . (debug cam) Fog: %s", g_DebugFogDisabled ? "OFF" : "ON");
    if (out.force_fog_off) sh::app::view::set_fog_enabled(false);
}

struct FrameHook {
    const char* name;
    void (*fn)();
};

const FrameHook kFrameHooks[] = {
    {"vcount_init", [] { sh::timing::start_line_counter(); }},
    {"save_init", [] { sh::save::game_saves().open(PcPaths_ExeDir(), g_PcConfig.savegameDir, PcPort_GetGameDataPath()); }},
    {"pad_init", [] { sh::loop::pad_init(); }},
    // The sound driver's init clears the vertical-blank callback, so it is registered again after it.
    {"vsync_callback", [] { sh::loop::install_vblank_tick(); }},
    {"geometry_init", [] { sh::app::view::geometry_init(); }},
    // The retail hook fills the item models' drawer table; the scene renderer draws them, so nothing is set up.
    {"item_screen_init", [] {}},
    {"audio_init", [] { sh::app::view::audio_init(); }},
    {"warning_screen", hook_warning_screen},
    {"replay_init", [] { sh::devtools::replay_init(); }},

    {"debug_menu_init", [] { Pc_DebugMenu_Init(); }},
    {"debug_overlay", [] { sh::debug::overlay_update(); }},
    {"console_command_file", [] {
         sh::app::command_script_frame();
         sh::debug::stall_test_frame();
     }},
    {"game_script", [] { ShScript_OnUpdate(); }},
    {"mod_hooks", [] { sh::hooks::game_frame_tick(); }},

    {"pad_read", [] { sh::loop::pad_read(); }},
    {"replay_pad", [] {
         sh::devtools::replay_pad(session().game.pads[0].raw);
         // The console's `pad` command holds buttons for scripted checks.
         if (sh::debug::script_pad_held()) {
             sh::game::RawPad& raw = session().game.pads[0].raw;
             raw.status = 0;
             sh::debug::script_pad_apply(&raw.buttons);
         }
         if (sh::debug::script_stick_held()) {
             sh::game::RawPad& raw = session().game.pads[0].raw;
             raw.status = 0;
             sh::debug::script_stick_apply(&raw.right_x, &raw.right_y);
         }
     }},
    {"demo_pad", [] { sh::gameflow::demo_player().feed_pad(sh::gameflow::demo_host()); }},
    {"pad_build", [] { sh::loop::pad_build(); }},
    {"frame_cheats", hook_frame_cheats},
    {"input_gate_after_pad", [] { sh::loop::input_gate_after_pad(); }},
    {"quick_save_load", [] { sh::quicksave::poll_keys(); }},
    {"control_style", [] { sh::input::camera_styles().frame_update(); }},
    {"extra_actions", [] { Pc_ExtraActionsUpdate(); }},
    {"rear_look", [] { Pc_RearLookUpdate(); }},
    {"hero_light", [] { sh::render::apply_hero_fill_light(); }},
    {"dpad_movement_gate", hook_dpad_movement_gate},
    {"mouse_cursor", [] { sh::input::mouse_pointer().frame_update(); }},
    {"console_fmv", [] { sh::debug::play_queued_movie(); }},
    {"console_gpu_bench", [] { sh::debug::gpu_bench_frame(); }},
    {"texture_upscale", [] { sh::app::poll_texture_upscale(); }},

    {"packet_arena_check", hook_packet_arena_check},
    // The light's depth pre-pass runs on exactly the frames that submitted the world.
    {"shadow_gate", [] { sh::render::flashlight().shadows_allowed = g_PcWorldDrawnThisFrame; }},
    {"freeze_frame", hook_freeze_frame},
    {"demo_update", [] { sh::gameflow::demo_player().update(sh::gameflow::demo_host()); }},
    {"demo_random_seed", [] { sh::gameflow::demo_player().apply_frame_seed(sh::gameflow::demo_host()); }},

    {"screen_fade", [] { sh::screens::run_screen_fade(); }},
    {"heal_flash", [] { sh::screens::run_heal_flash(); }},
    {"low_health_glow", [] { sh::screens::run_low_health_glow(); }},
    {"log_flush", [] { Sh_LogPeriodicFlush(); }},
    {"audio_tasks", [] { sh::app::view::audio_tasks_run(); }},
    {"audio_tasks_drain", [] { sh::app::view::audio_tasks_drain(); }},
    {"xa_player", [] { sh::audio::voice_lines().update(); }},
    {"message_voice", [] { sh::audio::message_voice().frame_ended(); }},
    {"fog_floor_frame_end", [] { Pc_FogFloorFrameEnd(); }},
    // The counter's font is read here, never from the draw hook.
    {"fps_counter_preload", [] { if (g_PcConfig.fpsDisplay || g_PcConfig.fpsProfiler) sh::hud::load_shared_font(); }},
    // Ticked every frame: on PC reads and XA streams do not share one laser, and gating the queue on audio
    // streaming let any audio-task stall block inventory opening and room loads.
    {"file_queue", [] { sh::loader::queue_update(); }},

    {"vibration", [] { sh::app::view::vibration_update(); }},
    {"water", [] { sh::app::view::water_update(); }},
    {"map_lights", [] { sh::effects::describe_map_lights(); }},
    {"draw_sync", [] { sh::screens::wait_frame_done(false); }},

    {"position_key", hook_position_key},

    {"post_fire_trace", [] { if (g_SH_PostFireTrace > 0) g_SH_PostFireTrace--; }},
};

class SessionMainFrameHost final : public gf::MainFrameHost {
public:
    int hook_id(std::string_view name) override
    {
        for (int i = 0; i < static_cast<int>(sizeof(kFrameHooks) / sizeof(kFrameHooks[0])); i++) {
            if (name == kFrameHooks[i].name) return i;
        }
        return -1;
    }
    void run_hook(int id) override
    {
        // One section of the performance history per hook, named after it.
        static int sections[sizeof(kFrameHooks) / sizeof(kFrameHooks[0])];
        static bool named = false;
        if (!named) {
            named = true;
            for (std::size_t i = 0; i < sizeof(kFrameHooks) / sizeof(kFrameHooks[0]); i++)
                sections[i] = sh::devtools::perf_monitor().section(std::string("hook_") + kFrameHooks[i].name);
        }
        const sh::devtools::PerfScope scope(sections[id]);
        kFrameHooks[id].fn();
    }

    void tick() override
    {
        sh::loop::frame_ticks()++;
        sh::loop::frame_timing().render_only = 0;
        update_step_ = sh::loop::frame_delta();
        sh::input::alt_view_update_begin();
        // The update starts from what the last update left, not from what the frames in between showed.
        sh::gpu::display::set_presenting_again(false);
        if (beam_shown_) set_flashlight_beam(beam_updated_);
        beam_shown_ = false;
    }

    bool update_only_due() override { return sh::loop::update_owed(); }
    void update_only_end() override
    {
        sh::frame::frame().discard_pending();
        ShScene_FrameEnd();
        sh::math::capture::end_frame();
        sh::input::wheel().decay();
        sh::loop::update_owed_done();
    }
    bool render_only_due() override { return sh::loop::frame_timing().render_only_next != 0; }
    void render_only_begin() override
    {
        sh::loop::frame_timing().render_only = 1;
        sh::input::poll();
        sh::debug::script_look_poll();
        sh::loop::pad_latch();
    }
    void updated_present() override
    {
        // What the update asked of the frame store is asked again on the frames that present it again.
        feedback_asked_ = sh::render::feedback_control();
        sh::scene::TickFrames& ticks = sh::scene::tick_frames();
        // A cutscene's change of shot is a cut even when the camera hardly moves. The view may take the new shot's
        // pose an update after the script sampled it, so the update after the change is not blended either.
        const std::uint32_t cuts = ShCutscene_CameraCuts();
        if (cuts != camera_cuts_) {
            camera_cuts_ = cuts;
            cut_updates_ = 2;
        }
        if (cut_updates_ > 0) {
            --cut_updates_;
            ticks.reset();
        }
        sh::scene::Frame& scene = sh::scene::current_frame();
        beam_updated_ = beam_of_flashlight();
        sh::scene::Beam beam = beam_updated_;
        if (!sh::loop::frame_rate_free()) {
            ticks.reset();
            note_alt_view(false, scene);
            update_due_ = 0.0;
            if (aim_alt_camera(false, 2, 1.0, 0.0, scene, beam)) {
                ++scene.serial;
                set_flashlight_beam(beam);
                beam_shown_ = true;
            }
            trace_look(false);
            return;
        }
        // Above the update rate the frames show the way from one update's scene to the next.
        ticks.record(scene, beam_updated_);
        note_alt_view(ticks.has_pair(), scene);
        // The mouse is read again now that the update is done, as on the frames between two updates: the view is
        // then as old on every presented frame.
        late_mouse();
        update_presented_ = SDL_GetPerformanceCounter();
        // An update runs on the first frame after its vertical blank, up to a frame late and never by the same
        // time: the moment it was due is the steady one the picture's time is counted from. An update is never
        // early, so the least late of the last few tells where that moment is.
        const double now = seconds_of(update_presented_);
        const double period = update_period();
        const double due = update_due_ + period;
        if (update_due_ == 0.0 || period <= 0.0 || std::fabs(now - due) > 2.0 * period) {
            update_due_ = now;
            lateness_.fill(0.0);
        } else {
            lateness_[lateness_at_++ % lateness_.size()] = now - due;
            const double least = *std::min_element(lateness_.begin(), lateness_.end());
            update_due_ = due + least;
            for (double& late : lateness_) late -= least;
        }
        if (show_moment(now, false, scene, beam)) {
            ++scene.serial;
            set_flashlight_beam(beam);
            beam_shown_ = true;
        }
        trace_look(false);
    }
    void render_only_present() override
    {
        // After the frame's wait, so that the picture shows the mouse as it is when the frame goes out.
        late_mouse();
        sh::render::feedback_control() = feedback_asked_;
        sh::gpu::display::set_presenting_again(true);
        sh::frame::frame().redraw_last();
        if (!sh::scene::tick_frames().has_last()) return;
        sh::scene::Frame& scene = sh::scene::current_frame();
        sh::scene::Beam beam = beam_updated_;
        show_moment(seconds_of(SDL_GetPerformanceCounter()), true, scene, beam);
        ++scene.serial;
        set_flashlight_beam(beam);
        beam_shown_ = true;
        trace_look(true);
    }
    void mark(gf::FrameMarkPoint point) override { sh::loop::profile_mark(static_cast<sh::loop::FrameMark>(point)); }
    void poll_input() override
    {
        SH_PERF_SCOPE("input_poll", "");
        sh::input::poll();
        sh::debug::script_look_poll();
    }
    // A map warp is an exit to the title and a New Game in one step, so it takes the warm reset's place in the frame.
    // `mods reload` of the data runs at the same point, between frames: in play it also loads the map again.
    // The reset watch counts held buttons per call, so it is asked once and its answer kept for warm_boot().
    bool warm_reset_due() override
    {
        data_reload_ = false;
        if (Pc_MapWarp_Due() || sh::loop::warm_reset_due())
            return true;
        data_reload_ = Pc_DataReload_Due() != 0;
        return data_reload_;
    }
    void warm_boot() override
    {
        if (Pc_MapWarp_Due())
            Pc_MapWarp_Run();
        else if (data_reload_)
            Pc_DataReload_Run();
        else
            sh::app::warm_boot();
    }
    void begin_buffers() override
    {
        s_buffer = sh::app::view::frame_buffer_begin();
        packet_arena().ensure_allocated();
        sh::app::view::frame_lists_clear(packet_arena().base(s_buffer));
    }
    void clear_frame_flags() override
    {
        session().system.process.music_status = 0;
        g_PcPickupItemActive = 0;
        g_PcPuzzleItemDepth = 0;
        g_PcWorldDrawnThisFrame = 0;
    }
    void replay_tick() override { sh::devtools::replay_tick(); }
    void update_game_state() override
    {
        // A long frame is made up by several updates before one present: the count tells them apart.
        static const int updates = sh::devtools::perf_monitor().counter("logic_updates");
        sh::devtools::perf_monitor().count(updates, 1.0);
        SH_PERF_SCOPE("game_update", "");
        game_state_dispatch();
    }

    void pace_demo_frame() override
    {
        SH_PERF_SCOPE("frame_pacing_wait", "");
        sh::loop::pace_demo_frame();
    }
    void clock_demo_frame() override { sh::loop::clock_demo_frame(); }
    void pace_frame() override
    {
        // The wait for the frame cap or the next vertical blank: idle time, not work.
        SH_PERF_SCOPE("frame_pacing_wait", "");
        sh::loop::pace_frame();
    }
    void clock_credit_vblanks() override { sh::loop::clock_credit_vblanks(); }
    void profile_sample() override { sh::loop::profile_sample(); }
    void clock_line_frame() override { sh::loop::clock_line_frame(); }
    void clock_commit() override { sh::loop::clock_commit(); }
    void swap_buffers() override
    {
        SH_PERF_SCOPE("swap_buffers", "");
        sh::app::view::swap_buffers();
    }

    bool framing_gate() override
    {
        gf::FrameGateInputs in;
        in.in_game = state_is(gs::GameState::InGame);
        in.skip_framebuffer_store = sh::render::feedback_control().skip_store != 0;  // the paper map's tick
        in.cutscene_live = session().sys_flag(sh::game::sys_flag::kCutscene) || border_active();
        in.map_screen_active = g_PcMapScreenActive != 0;
        in.world_drawn = g_PcWorldDrawnThisFrame != 0;
        in.present_last_frame = sh::render::feedback_control().present_last_frame != 0;
        in.fade_holds_framing = fade_opaque();
        in.bg2d_active = g_Pc2dBackgroundActive;
        in.now_ms = SDL_GetTicks();

        const gf::FrameGateOutputs out = frame_gate().update(in);
        g_Pc2dBackgroundActive = out.bg2d_active;
        sh::render::frame_state().world_3d = out.frame_is_3d_world ? 1 : 0;
        sh::render::picture().hor_plus = out.wide ? 1 : 0;
        // The 2D UI pass uses this, not whatever the world asserted later.
        g_PcHorPlusGate = sh::render::picture().hor_plus;
        return out.bg2d_held;
    }
    gf::FrameSignals signals() override
    {
        gf::FrameSignals s;
        s.game_state = game_state();
        s.sys_state = sys_state();
        s.cutscene_flag = session().sys_flag(sh::game::sys_flag::kCutscene);
        s.border_active = border_active();
        s.menu_flag = session().sys_flag(sh::game::sys_flag::kMenu);
        s.pickup_item = g_PcPickupItemActive;
        s.puzzle_depth = g_PcPuzzleItemDepth != 0;
        s.world_drawn = g_PcWorldDrawnThisFrame != 0;
        s.present_last_frame = sh::render::feedback_control().present_last_frame != 0;
        s.fade_opaque = fade_opaque();
        s.map_screen = g_PcMapScreenActive != 0;
        s.fog_enabled = sh::app::view::fog_enabled();
        s.has_save = gs::save_active();
        if (s.has_save) {
            const char* name = sh::loader::map_name(gs::save_map_index());
            s.map = name != nullptr ? name : "";
            s.room = gs::save_room();
        }
        return s;
    }
    void publish_cutscene(bool live) override { sh::render::frame_state().cutscene = live ? 1 : 0; }
    void publish_item_take(std::int32_t pickup_item) override { sh::render::frame_state().item_take = pickup_item; }
    std::int32_t world_anchor() override;
    void set_projection_offset(std::int32_t x, std::int32_t y) override { sh::app::view::set_projection_offset(x, y); }
    void set_dither_suppressed(bool on) override { sh::render::frame_state().dither_suppressed = on ? 1 : 0; }
    void apply_clear_color(const gf::ClearColorOut& co) override
    {
        std::uint8_t fog[3];
        sh::app::view::fog_color(fog);
        sh::game::Rgbx& clear = session().game.clear_color;
        if (co.color == gf::ClearColor::Black) {
            clear.r = clear.g = clear.b = 0;
        } else if (co.color == gf::ClearColor::Fog) {
            clear.r = fog[0];
            clear.g = fog[1];
            clear.b = fog[2];
        }
        if (co.set_fog_uniform) {
            sh::render::fog().color[0] = fog[0] / 255.0f;
            sh::render::fog().color[1] = fog[1] / 255.0f;
            sh::render::fog().color[2] = fog[2] / 255.0f;
        }
    }
    void sort_clear() override
    {
        const sh::game::Rgbx& c = session().game.clear_color;
        sh::app::view::sort_clear(c.r, c.g, c.b);
    }
    void prepare_world_list() override
    {
        SH_PERF_SCOPE("world_list_prepare", "");
        sh::app::view::prepare_world_list();
    }
    void item_depth_begin() override { sh::app::view::item_depth_begin(); }
    // The engine's scene owns the world's depth, so the opaque-depth request has nothing left to switch.
    void draw_world_list(bool) override
    {
        // What the game's drawers queued for this picture: the world, the overlay and the inset together.
        static const int entries = sh::devtools::perf_monitor().counter("frame_list_entries");
        sh::devtools::perf_monitor().count(entries, static_cast<double>(sh::frame::frame().pending()));
        SH_PERF_SCOPE("world_list_draw", "");
        sh::app::view::draw_world_list();
    }
    void item_depth_end() override { sh::app::view::item_depth_end(); }
    void draw_ui_list(bool menu_framing) override
    {
        // The UI pass draws with the gate's framing, not what the world pass asserted for itself, and a 4:3 menu
        // narrows it; restored so nothing after it sees the swap.
        const int saved = sh::render::picture().hor_plus;
        sh::render::picture().hor_plus = menu_framing ? 0 : g_PcHorPlusGate;
        sh::render::frame_state().ui_ortho_pass = 1;
        {
            SH_PERF_SCOPE("ui_list_draw", "");
            sh::app::view::draw_ui_list();
        }
        sh::render::frame_state().ui_ortho_pass = 0;
        sh::render::picture().hor_plus = saved;
    }
    void end_scene() override
    {
        SH_PERF_SCOPE("end_scene", "");
        sh::platform::end_frame();
    }

private:
    static double seconds_of(std::uint64_t counter)
    {
        return static_cast<double>(counter) / static_cast<double>(SDL_GetPerformanceFrequency());
    }
    // Brings the mouse's travel up to date without handling events: a frame is under way, and a resize or a quit
    // waits for the next poll.
    static void late_mouse()
    {
        SDL_PumpEvents();
        sh::debug::script_look_poll();
    }
    // The real time one update stands for: its step in whole vertical blanks (the step itself is rounded to Q12).
    double update_period() const
    {
        const double blanks = std::round(static_cast<double>(update_step_) * kVBlankRate / 4096.0);
        return blanks / kVBlankRate;
    }
    // Game time from the moment the last update was due to `now`.
    double game_seconds_since_due(double now) const
    {
        const double period = update_period();
        return period > 0.0 ? (now - update_due_) * (static_cast<double>(update_step_) / 4096.0) / period : 0.0;
    }

    // The scene of the moment a frame presented at `now` shows, and the alternate camera's view of it. The moment
    // runs evenly with the clock, behind the last update by what an update can be late: a frame, and a little for
    // the clock. False when the scene stays as the update made it.
    bool show_moment(double now, bool between, sh::scene::Frame& scene, sh::scene::Beam& beam)
    {
        sh::scene::TickFrames& ticks = sh::scene::tick_frames();
        const double period = update_period();
        if (last_present_ > 0.0 && period > 0.0) {
            const double frame = std::min(now - last_present_, period);
            frame_share_ += (frame / period - frame_share_) * kFrameShareFollow;
        }
        last_present_ = now;
        double shown = 1.0;
        if (ticks.has_pair() && period > 0.0) {
            shown = (now - update_due_) / period - (frame_share_ + kClockMargin);
            shown = std::clamp(shown, ticks.has_three() ? -1.0 : 0.0, 1.0);
        }
        const bool back = shown < 0.0;
        const int to = back ? 1 : 2;
        bool changed = false;
        if (ticks.has_pair() || between) {
            ticks.show(shown, scene, beam);
            changed = true;
        }
        if (aim_alt_camera(true, to, back ? 1.0 + shown : shown, game_seconds_since_due(now), scene, beam)) {
            changed = true;
        }
        return changed;
    }

    // The update's scene, before it is blended with the ones before: was it drawn with the alternate camera's view?
    // The game's rounding stays well inside the limits; beyond them another camera drew the scene (a scene's shot,
    // an item screen), and its picture is not touched.
    void note_alt_view(bool follows, const sh::scene::Frame& scene)
    {
        constexpr double kTurn = 0.02, kMove = 8.0;
        alt_view_[0] = alt_view_[1] && follows;
        alt_view_[1] = alt_view_[2] && follows;
        updated_camera_[0] = updated_camera_[1];
        updated_camera_[1] = updated_camera_[2];
        updated_camera_[2] = scene.camera;
        sh::input::AltCameraView told;
        sh::scene::Camera exact;
        alt_view_[2] = sh::input::alt_view_update_presented(follows, &told) && scene.world &&
                       sh::loop::camera_looking(told.eye, told.at, &exact);
        if (alt_view_[2]) {
            const sh::loop::CameraGap gap = sh::loop::camera_gap(scene.camera, exact);
            alt_view_[2] = gap.turn <= kTurn && gap.move <= kMove;
        }
    }

    // An alternate camera's view takes the place of the camera the scene was drawn with: the same view without the
    // game's rounding, turned as far as the mouse and the stick have turned it by now (input/alt_camera_view.h).
    // The scene stands `shown` of the way from the update before update `to` (1 the one before the last, 2 the
    // last) to that update.
    bool aim_alt_camera(bool read_mouse, int to, double shown, double seconds, sh::scene::Frame& scene,
                        sh::scene::Beam& beam)
    {
        if (!scene.world || !alt_view_[to]) return false;
        // Two updates too far apart to blend are shown as a cut to the newer one, with its camera as it was.
        const sh::scene::Camera& made = updated_camera_[to];
        const bool cut = scene.camera.rot == made.rot && scene.camera.trans == made.trans;
        // Blended with a scene of another camera: neither view is the picture's.
        if (shown < 1.0 && !cut && !alt_view_[to - 1]) return false;
        sh::input::AltViewMoment moment;
        moment.read_mouse = read_mouse;
        moment.before_last = to == 1;
        moment.blend = cut ? 1.0 : shown;
        moment.seconds = seconds;
        sh::input::AltCameraView view;
        sh::input::alt_view_for_present(moment, &view);
        sh::scene::Camera aimed;
        if (!sh::loop::camera_looking(view.eye, view.at, &aimed)) return false;
        sh::loop::aim_scene(scene, beam, aimed);
        return true;
    }

    // `looktrace`: where the camera of the frame about to be presented looks.
    static void trace_look(bool between)
    {
        if (!sh::debug::look_trace_on()) return;
        const std::array<double, 9>& rot = sh::scene::current_frame().camera.rot;
        constexpr double kDegrees = 180.0 / 3.14159265358979323846;
        const std::array<double, 3> eye = sh::loop::camera_eye(sh::scene::current_frame().camera);
        const double metres[3] = {eye[0] / 256.0, eye[1] / 256.0, eye[2] / 256.0};
        sh::debug::look_trace_frame(between, std::atan2(rot[6], rot[8]) * kDegrees,
                                    -std::asin(std::clamp(rot[7], -1.0, 1.0)) * kDegrees, metres);
    }
    static sh::scene::Beam beam_of_flashlight()
    {
        const sh::render::Flashlight& light = sh::render::flashlight();
        sh::scene::Beam beam;
        beam.on = light.active != 0;
        for (std::size_t i = 0; i < 3; ++i) {
            beam.position[i] = light.pos[i];
            beam.shadow_position[i] = light.shadow_pos[i];
            beam.direction[i] = light.dir[i];
        }
        return beam;
    }
    static void set_flashlight_beam(const sh::scene::Beam& beam)
    {
        sh::render::Flashlight& light = sh::render::flashlight();
        for (std::size_t i = 0; i < 3; ++i) {
            light.pos[i] = beam.position[i];
            light.shadow_pos[i] = beam.shadow_position[i];
            light.dir[i] = beam.direction[i];
        }
    }

    std::int32_t update_step_ = 0;          // the step of the last update, Q12 seconds
    std::uint64_t update_presented_ = 0;    // performance counter when the last update was presented
    // The last three presented updates, oldest first: each was drawn with an alternate camera's view (and the
    // frames lead to it from the one before), and the camera of its scene.
    bool alt_view_[3] = {false, false, false};
    sh::scene::Camera updated_camera_[3];
    double last_present_ = 0.0;             // when the last frame's moment was taken, seconds
    double frame_share_ = 0.5;              // the time between two frames as a share of an update, followed slowly
    static constexpr double kFrameShareFollow = 0.05;
    static constexpr double kClockMargin = 0.08;  // of an update: what the due moment itself may be off by
    double update_due_ = 0.0;               // the steady moment the last update stands for, seconds; 0 = none yet
    static constexpr double kVBlankRate = 60.0;
    std::array<double, 8> lateness_{};      // how late the last updates were against the due moment, seconds
    std::size_t lateness_at_ = 0;
    sh::render::FeedbackControl feedback_asked_{};
    sh::scene::Beam beam_updated_;          // the flashlight as the last update left it
    bool beam_shown_ = false;               // the renderer's flashlight holds a blended beam
    std::uint32_t camera_cuts_ = 0;         // ShCutscene_CameraCuts() as the last update saw it
    int cut_updates_ = 0;                   // updates still to be shown without a blend from the one before
    bool data_reload_ = false;  // warm_reset_due() answered for `mods reload`, not for a reset
};

}  // namespace

// --- entry points the engine calls by name ---

extern "C" void DebugCamera_Update(void)
{
    SessionDebugCameraHost host;
    gf::debug_camera_update(host);
}

extern "C" void GameState_Boot_Update(void) { boot_machine().update(); }

extern "C" int Pc_ScriptOwnsScene(void) { return scene_owner().owns_scene(scene_snapshot()) ? 1 : 0; }

extern "C" int Pc_ScriptOwnsShot(void) { return scene_owner().owns_shot(scene_snapshot()) ? 1 : 0; }

extern "C" std::int32_t Pc_WorldAnchorOfy(void)
{
    gf::AnchorSnapshot a;
    a.game_state = game_state();
    a.sys_state = sys_state();
    a.pickup_active = g_PcPickupItemActive != 0;
    a.cutscene_active = sh::render::frame_state().cutscene != 0;
    a.border_active = border_active();
    a.third_person = g_DebugThirdPersonCam != 0;
    a.cutscene_shift = static_cast<std::int32_t>(sh::render::picture().cutscene_v_shift);
    a.world_shift = static_cast<std::int32_t>(sh::render::picture().world_v_shift);
    return scene_owner().world_anchor(a);
}

std::int32_t SessionMainFrameHost::world_anchor() { return Pc_WorldAnchorOfy(); }

extern "C" void MainLoop(void)
{
    static SessionMainFrameHost host;
    static gf::MainFrame frame(host, load_rules<gf::MainFrameRules>("main_frame.json", "MAINFRAME", "rules"));
    for (const std::string& name : frame.unknown_hooks())
        SH_ERR("[MAINFRAME] config/main_frame.json: no frame hook named %s (skipped)", name.c_str());

    frame.startup();
    while (true) frame.run();
}
