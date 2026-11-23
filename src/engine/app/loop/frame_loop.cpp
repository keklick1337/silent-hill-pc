// SPDX-License-Identifier: GPL-3.0-or-later
//
// The game's frame loop services (frame_loop.h): the console timing of the game and its answers to the loop
// classes' questions (who is in control, is a scene playing, is the game held by an overlay) read from the session.

#include "engine/app/loop/frame_loop.h"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>

#include <SDL3/SDL_scancode.h>
#include <SDL3/SDL_timer.h>

#include "audio/voice/voice_line_player.h"
#include "engine/app/pc_paths.h"
#include "engine/config/pc_config.h"
#include "engine/game_state/game_state.h"
#include "engine/game_state/session.h"
#include "engine/app/gameflow/demo_host.h"
#include "engine/gameflow/flow_data.h"
#include "engine/gameflow/soft_reset.h"
#include "engine/log/sh_log.h"
#include "engine/loop/frame_clock.h"
#include "engine/loop/frame_pacer.h"
#include "engine/loop/frame_view.h"
#include "engine/loop/input_gate.h"
#include "engine/loop/pad_tracker.h"
#include "engine/loop/reset_watch.h"
#include "engine/loop/vblank_clock.h"
#include "engine/screens/screens_view.h"
#include "engine/timing/line_counter.h"
#include "engine/timing/vblank.h"
#include "input/sdl_pad.h"
#include "render/gpu/render_state.h"
#include "engine/mods/data_reload.h"
#include "engine/devtools/tool_mode.h"
#include "input/mouse_pointer.h"

extern "C" {
const char* PcPort_GetGameDataPath(void);
void Pc_DebugMenu_Close(void);
void Pc_FreeCam_Set(int on);
extern int g_PcFastForward;
extern int g_DebugUnlockFps;
extern int g_DebugCamEnabled;
extern int g_PcFmvClockDiscard;
extern int g_PcConsoleInputActive;
extern int g_PcConsoleSwallowInput;
extern int g_PcDebugMenuActive;
extern const unsigned char* g_sdlKeyboardState;
}

namespace sh::loop {
namespace {

namespace gs = sh::game_state;
using sh::game::session;

constexpr int kVBlanksPerSecond = 60;
constexpr int kFadeNone = 1;  // the fade's idle status

int game_state() { return session().game.game_state; }
int sys_state() { return session().system.flow.sys_state; }
bool state_is(gs::GameState s) { return game_state() == static_cast<int>(s); }
bool sys_is(gs::SysState s) { return sys_state() == static_cast<int>(s); }

// --- frame clock ---

// The console the game was written for: an NTSC line rate, a 60 Hz frame, the game's tuning of the slowest frame
// (15 fps), the longest step (30 fps) and gravity.
ClockSpec game_clock_spec() {
    ClockSpec spec;
    spec.lines_per_second = 15780;
    spec.vblanks_per_second = kVBlanksPerSecond;
    spec.slowest_frame_rate = kVBlanksPerSecond / 4;
    spec.step_frame_rate = kVBlanksPerSecond / 2;
    spec.gravity = 9.8f;
    spec.max_vblanks_in_control = 4;
    spec.max_vblanks_otherwise = 16;
    spec.scene_debt_limit = 2 * 4096;
    return spec;
}

FrameClock& clock() {
    static FrameClock c(game_clock_spec());
    return c;
}

class EmulatedLineCounter final : public LineCounter {
public:
    int read() override { return sh::timing::line_count(); }
};

// A cutscene runs or the letterbox bars are anywhere but at rest.
bool scene_playing() {
    return session().sys_flag(sh::game::sys_flag::kCutscene) ||
           session().system.flow.cutscene_border != sh::game::kCutsceneBorderNone;
}

bool player_in_control() { return state_is(gs::GameState::InGame) && sys_is(gs::SysState::Gameplay); }

// The console, the quick options or the editor hold the game, unless a pause or a map message already holds it.
bool held_by_overlay() {
    const sh::devtools::ToolMode& tool = sh::devtools::tool_mode();
    return (g_PcConsoleInputActive || g_PcDebugMenuActive || (tool.active && tool.run_frames == 0)) &&
           !session().music_status(sh::game::music_status::kPause) && !session().system.flow.message_shown;
}

FrameStep s_step;
ButtonLatch s_button_latch;
// The frame was paced by the timer or not at all: it may come before the next vertical blank.
bool s_free_rate = false;
// Vertical blanks one frame takes at the chosen rate (0: this frame is not one of the capped ones), and the updates
// still to run, without a present, to make up a frame that took longer.
int s_usual_vblanks = 0;
int s_updates_owed = 0;
// The longest stall made up, in vertical blanks.
constexpr int kMadeUpVBlanks = 8;

void commit_step(const FrameStep& step) {
    frame_delta() = step.delta;
    frame_delta_raw() = step.delta_raw;
    frame_gravity() = step.gravity;
}

// --- frame pacer ---

class PlatformPacerHost final : public PacerHost {
public:
    int vblank_count() override { return sh::timing::vblank_count(); }
    void wait_vblank() override { sh::timing::wait_vblank(); }
    std::uint64_t ticks_ms() override { return SDL_GetTicks(); }
    std::uint64_t perf_counter() override { return SDL_GetPerformanceCounter(); }
    std::uint64_t perf_frequency() override { return SDL_GetPerformanceFrequency(); }
    void sleep_ms(std::uint32_t ms) override { SDL_Delay(ms); }
};

FramePacer& pacer() {
    static FramePacer p(PacerSpec{kVBlanksPerSecond, 50});
    return p;
}

// Screens with nothing running under them (no animation stepping, no world): they may follow the player's frame
// rate cap instead of one vertical blank a frame. The inventory spins live item models on gameplay timing and is
// left out; cursor puzzles scale their cursor by the step.
bool screen_unlocked() {
    if (!g_PcConfig.menuFpsUnlock) {
        return false;
    }
    if (state_is(gs::GameState::MainMenu) || state_is(gs::GameState::OptionScreen) ||
        state_is(gs::GameState::PaperMapScreen)) {
        return true;
    }
    if (state_is(gs::GameState::InGame) && (sys_is(gs::SysState::OptionsMenu) || sys_is(gs::SysState::MapScreen))) {
        return true;
    }
    return sh::input::mouse_pointer().puzzle_active();
}

// --- profiler ---

FrameProfiler s_profiler;

std::uint32_t now_ms() { return static_cast<std::uint32_t>(SDL_GetTicks()); }

// --- soft reset ---

const sh::gameflow::SoftResetRules& reset_rules() {
    static sh::gameflow::SoftResetRules r;
    static std::uint32_t r_epoch = sh::mods::kNeverRead;
    if (sh::mods::data_stale(r_epoch)) {
        r = [] {
            char mods[1024];
            PcPaths_ExeFile(mods, sizeof(mods), "mods");
            std::string err;
            sh::gameflow::SoftResetRules loaded = sh::gameflow::load_config<sh::gameflow::SoftResetRules>(
                PcPort_GetGameDataPath(), mods, "soft_reset.json", &err);
            if (!err.empty()) {
                SH_ERR("[WARMRESET] config/soft_reset.json: %s (built-in rules)", err.c_str());
            }
            return loaded;
        }();
    }
    return r;
}

const ResetWatch& reset_watch() {
    static const ResetWatch w(reset_rules().spec);
    return w;
}

const VBlankTimers& vblank_timers() {
    static const VBlankTimers t = [] {
        VBlankTimers v;
        v.add(&demo_vblanks());
        v.add(&reset_hold_vblanks());
        for (std::int32_t& c : session().system.flow.frame_counters) {
            v.add(&c);
        }
        return v;
    }();
    return t;
}

// --- pads ---

constexpr std::uint32_t kButtonStart = 1u << 3;

PadSpec game_pad_spec() {
    PadSpec spec;
    spec.dpad = {1u << 4, 1u << 5, 1u << 6, 1u << 7};
    spec.left_analog = {1u << 16, 1u << 17, 1u << 18, 1u << 19};
    spec.right_analog = {1u << 20, 1u << 21, 1u << 22, 1u << 23};
    spec.direction = {1u << 24, 1u << 25, 1u << 26, 1u << 27};
    spec.dead_zone = gs::pad::kStickThreshold;
    spec.repeat_delay = kVBlanksPerSecond / 2;
    spec.repeat_interval = kVBlanksPerSecond / 20;
    spec.dpad_stick = 45;
    spec.dpad_steer = 32;
    spec.dpad_steer_back_walk = -32;
    spec.dpad_steer_back_run = -64;
    return spec;
}

const PadTracker& tracker() {
    static const PadTracker t(game_pad_spec());
    return t;
}

// The second byte of the report (type and length) marks a pad in analog mode.
PadInput input_of(const sh::game::RawPad& pad) {
    PadInput in;
    in.connected = pad.status != 0xFF;
    in.analog = (pad.kind & 0x53) == 0x53;
    in.buttons_up = pad.buttons;
    in.sticks = {pad.right_x, pad.right_y, pad.left_x, pad.left_y};
    return in;
}

std::array<std::int8_t, 4> axes(const sh::game::PadSticks& s) { return {s.right_x, s.right_y, s.left_x, s.left_y}; }

sh::game::PadSticks sticks_of(const std::array<std::int8_t, 4>& a) { return {a[0], a[1], a[2], a[3]}; }

PadState state_of(const sh::game::PadState& c) {
    PadState s;
    s.repeat_ticks = c.pulse_ticks;
    s.held = c.held;
    s.pressed = c.clicked;
    s.released = c.released;
    s.pulsed = c.pulsed;
    s.pulsed_menu = c.pulsed_menu;
    s.sticks_raw = axes(c.sticks);
    s.sticks = axes(c.sticks_past_dead_zone);
    s.steer = axes(c.steer);
    return s;
}

void write_back(sh::game::PadState& c, const PadState& s) {
    c.pulse_ticks = s.repeat_ticks;
    c.held = s.held;
    c.clicked = s.pressed;
    c.released = s.released;
    c.pulsed = s.pulsed;
    c.pulsed_menu = s.pulsed_menu;
    c.sticks = sticks_of(s.sticks_raw);
    c.sticks_past_dead_zone = sticks_of(s.sticks);
    c.steer = sticks_of(s.steer);
}

}  // namespace

void pace_demo_frame() {
    PlatformPacerHost host;
    sh::gameflow::demo_player().update_present_interval();
    frame_vblanks() = pacer().demo_frame(host, sh::gameflow::demo_player().present_interval(), frame_interval_vblanks());
}

void pace_frame() {
    PlatformPacerHost host;
    const bool unlocked = screen_unlocked();
    FpsChoice fps;
    fps.locked_refresh = (g_PcConfig.vsync != 0 && g_PcConfig.refreshRate > 0) ? g_PcConfig.refreshRate : 0;
    fps.uncapped = g_DebugUnlockFps || g_PcConfig.fpsCap == 0;
    fps.cap = g_PcConfig.fpsCap;
    fps.menu = unlocked;
    const int target = pacer().target_fps(fps);
    // A map event's scene runs outside Gameplay. Above 60 it presents between its updates like gameplay; at or
    // below 60 it keeps the one-blank frame, so its updates do not change with the cap.
    const bool event_scene = state_is(gs::GameState::InGame) && sys_is(gs::SysState::EventCallback) &&
                             (target <= 0 || target > kVBlanksPerSecond) && !sh::timing::virtual_time::active();
    if (!sys_is(gs::SysState::Gameplay) && !unlocked && !event_scene) {
        frame_vblanks() = pacer().fixed_frame(host, g_PcFastForward != 0);
        s_free_rate = false;
        s_usual_vblanks = 0;
        return;
    }
    const bool apply_cap = state_is(gs::GameState::InGame) || unlocked;
    s_free_rate = apply_cap && (target <= 0 || target > kVBlanksPerSecond || kVBlanksPerSecond % target != 0);
    s_usual_vblanks = !apply_cap ? 0
                      : (target <= 0 || target >= kVBlanksPerSecond) ? 1
                      : (kVBlanksPerSecond + target - 1) / target;
    frame_vblanks() = pacer().capped_frame(host, sh::screens::fade_status() != kFadeNone, apply_cap,
                                           frame_interval_vblanks(), fps);
}

void clock_demo_frame() {
    const int interval = sh::gameflow::demo_player().present_interval();
    frame_uncapped_vblanks() = frame_vblanks();
    frame_vblanks() = interval;
    s_step = clock().demo_step(interval, frame_uncapped_vblanks());
}

void clock_credit_vblanks() {
    frame_uncapped_vblanks() = frame_vblanks();
    frame_vblanks() = clock().credited_vblanks(frame_vblanks(), player_in_control());
}

void clock_line_frame() {
    EmulatedLineCounter counter;
    s_step = clock().line_step(counter);
}

void clock_commit() {
    const bool demo = session().sys_flag(sh::game::sys_flag::kDemo);
    const bool held = held_by_overlay();
    FrameStep step = s_step;
    // In play a frame that took longer than usual becomes several updates of the usual step (the first now, the
    // others before the next present). Replays run on the virtual clock, whose frames keep their recorded steps.
    s_updates_owed = 0;
    if (!demo && !held && s_usual_vblanks > 0 && player_in_control() && !scene_playing() &&
        !sh::timing::virtual_time::active() && !g_skipSwapInterval && !g_PcFastForward &&
        frame_uncapped_vblanks() > s_usual_vblanks) {
        const UpdatePlan plan = plan_updates(frame_uncapped_vblanks(), s_usual_vblanks, kMadeUpVBlanks);
        s_updates_owed = plan.updates - 1;
        frame_vblanks() = plan.vblanks;
        frame_uncapped_vblanks() = plan.vblanks;
        step = clock().demo_step(plan.vblanks, plan.vblanks);
    }
    commit_step(step);
    sh::timing::clear_line_count();

    if (!demo) {
        CatchUpInput in;
        in.wall_clock = sh::timing::wall_clock_q12();
        in.wall_clock_step = g_skipSwapInterval || g_PcFastForward;
        in.scene_playing = scene_playing();
        in.frozen = held;
        in.forget_debt = g_PcFmvClockDiscard != 0;
        in.free_rate = frame_rate_free();
        g_PcFmvClockDiscard = 0;
        // Voices hold with the game clock, or a line runs ahead of the held scene.
        sh::audio::voice_lines().hold(held);
        step = clock().catch_up(step, in);
    }

    if (held) {
        step = FrameStep{};
    }
    commit_step(step);
    FrameTiming& t = frame_timing();
    t.tick30 = tick30_step(t.tick30_accumulator, step.delta) ? 1 : 0;
    // The virtual clock of a replay ticks a blank per frame: it has no frame between two blanks.
    t.render_only_next =
        (s_free_rate && !demo && !held && step.delta == 0 && !sh::timing::virtual_time::active()) ? 1 : 0;
}

bool in_play() { return player_in_control(); }

bool update_owed() { return s_updates_owed > 0; }

void update_owed_done() {
    --s_updates_owed;
    // The next update of the same step: its own 30 Hz gate.
    FrameTiming& t = frame_timing();
    t.tick30 = tick30_step(t.tick30_accumulator, t.delta) ? 1 : 0;
}

bool frame_rate_free() { return s_free_rate && !sh::timing::virtual_time::active(); }

void profile_mark(FrameMark mark) { s_profiler.mark(mark, now_ms()); }

void profile_sample() {
    const ProfileSample s = s_profiler.sample(now_ms(), frame_uncapped_vblanks());
    if (s.slow) {
        const auto& p = s.slow->phase_ms;
        SH_DBG("[SLOWFRAME] total=%ums present=%u tail=%u top=%u update=%u fsq=%u sync=%u | gameState=%d sysState=%d",
               (unsigned)s.slow->total_ms, (unsigned)p[kPresent], (unsigned)p[kTail], (unsigned)p[kTopOfFrame],
               (unsigned)p[kUpdate], (unsigned)p[kFiles], (unsigned)p[kSync], game_state(), sys_state());
    }
    if (s.window) {
        const ProfileWindow& w = *s.window;
        SH_DBG("[PERF] avg=%.1fms (%.1f fps) worst=%lums vblanks/frame=%.2f over %lu frames", w.average_ms, w.fps,
               (unsigned long)w.worst_ms, w.vblanks_per_frame, (unsigned long)w.frames);
        if (w.has_phases) {
            SH_DBG("[PERF]   per frame: present=%.1f tail=%.1f top=%.1f update=%.1f fsq=%.1f sync=%.1f ms | mapIdx %d",
                   w.phase_ms[kPresent], w.phase_ms[kTail], w.phase_ms[kTopOfFrame], w.phase_ms[kUpdate],
                   w.phase_ms[kFiles], w.phase_ms[kSync], gs::save_active() ? gs::save_map_index() : -1);
        }
    }
}

void vblank_tick() { vblank_timers().tick(); }

void install_vblank_tick() {
    vblank_timers();
    static const bool stop_registered = [] {
        std::atexit([] { vblank_clock().stop(); });
        return true;
    }();
    (void)stop_registered;
    vblank_clock().set_callback(&vblank_tick);
}

bool warm_reset_due() {
    const sh::game::Session& s = session();
    ResetInput in;
    // Boot and the intro, and the frames in which a save is being read or written.
    in.blocked = reset_rules().blocked(s.game.game_state, s.game.game_steps[0]);
    in.demo_playing = s.sys_flag(sh::game::sys_flag::kDemo);
    in.at_main_menu = state_is(gs::GameState::MainMenu);
    in.requested = s.sys_flag(sh::game::sys_flag::kWarmReset);
    in.held = s.game.pads[0].held;
    in.pressed = s.game.pads[0].clicked;

    const char* cause = reset_watch().check(in, {&demo_vblanks(), &reset_hold_vblanks()});
    if (cause == nullptr) {
        return false;
    }
    // One line per reset: a reset nobody asked for drops the player to the title mid-game.
    SH_DBG("[WARMRESET] cause=%s held=0x%04X timer=%d sysFlags=0x%X gameState=%d sysState=%d", cause,
           (unsigned int)(s.game.pads[0].held & 0xFFFF), (int)reset_hold_vblanks(),
           (unsigned int)s.system.process.sys_flags, game_state(), sys_state());
    return true;
}

void pad_init() {
    sh::input::PadSystem& pads = sh::input::PadSystem::instance();
    pads.attach(0, &session().game.raw_pad);
    pads.attach(1, &session().game.pads[1].raw);
    pads.enableUpdates();
}

void pad_read() {
    sh::game::RawPad& raw = session().game.pads[0].raw;
    raw = session().game.raw_pad;
    raw.buttons = s_button_latch.take(raw.buttons);
}

void pad_latch() {
    const sh::game::RawPad& raw = session().game.raw_pad;
    if (raw.status == 0) s_button_latch.seen(raw.buttons);
}

void pad_build() {
    const std::uint32_t run = session().game.settings.binds.run;
    for (int i = 0; i < 2; ++i) {
        sh::game::PadState& c = session().game.pads[i];
        PadState s = state_of(c);
        tracker().update(s, input_of(c.raw), frame_vblanks(), i == 0, run);
        write_back(c, s);
    }
}

void pad_update() {
    pad_read();
    pad_build();
}

void input_gate_after_pad() {
    sh::game::PadState& c = session().game.pads[0];
    // A demo replays recorded input, which would drive the menu's rows: it counts as not in game. The title keeps
    // the menu (Go to map). The menu reads the pad itself in its own tick, so the pad is not taken from the game here.
    if (g_PcDebugMenuActive && ((!state_is(gs::GameState::InGame) && !state_is(gs::GameState::MainMenu)) ||
                                session().sys_flag(sh::game::sys_flag::kDemo))) {
        Pc_DebugMenu_Close();
    }
    // A load (title, load screens, map load) ends the free camera; its own off path runs in game only.
    if (g_DebugCamEnabled &&
        (state_is(gs::GameState::MainMenu) || state_is(gs::GameState::LoadSavegameScreen) ||
         state_is(gs::GameState::MainLoadScreen) || state_is(gs::GameState::AutoLoadSavegame) ||
         state_is(gs::GameState::LoadMapScreen))) {
        Pc_FreeCam_Set(0);
    }
    if (g_DebugCamEnabled && !g_PcDebugMenuActive) {
        PadState s = state_of(c);
        InputGate::free_camera_take(s);
        write_back(c, s);
    }
    if (g_PcConsoleInputActive || g_PcConsoleSwallowInput) {
        const unsigned char* ks = g_sdlKeyboardState;
        const bool keys_down = ks == nullptr || ks[SDL_SCANCODE_RETURN] || ks[SDL_SCANCODE_GRAVE];
        PadState s = state_of(c);
        g_PcConsoleSwallowInput =
            InputGate::console_take(s, g_PcConsoleInputActive != 0, g_PcConsoleSwallowInput != 0, keys_down,
                                    kButtonStart)
                ? 1
                : 0;
        write_back(c, s);
    }
}

}  // namespace sh::loop
