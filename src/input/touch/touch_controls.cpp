// SPDX-License-Identifier: GPL-3.0-or-later
// The touch screen on the running game; see touch_controls.h. The fingers arrive through the window's touch hook
// (engine/platform/window.h) and are sorted at once by what the screen is (the mode of the last pad update); the
// pad update then turns what they hold into input actions, the stick bytes, the pointer and the wheel.
#include "input/touch/touch_controls.h"

#include "engine/app/game_data_dir.h"
#include "engine/app/pc_paths.h"
#include "engine/config/pc_config.h"
#include "engine/debug/console_host.h"
#include "engine/game_state/game_state.h"
#include "engine/game_state/session.h"
#include "engine/gameflow/flow_data.h"
#include "engine/gameflow/game/action_target.h"
#include "engine/gameflow/state_names.h"
#include "engine/game_state/game_state_inventory.h"
#include "engine/game_state/game_state_ui.h"
#include "engine/inventory/item_catalog.h"
#include "engine/input/input_actions.h"
#include "engine/input/skip_watch.h"
#include "engine/input/touch_prompts.h"
#include "engine/log/sh_log.h"
#include "engine/platform/host_state.h"
#include "engine/platform/window.h"
#include "engine/screens/paper_map.h"
#include "engine/text/message_box.h"
#include "input/camera_style.h"
#include "input/mouse_pointer.h"
#include "input/pad.h"
#include "input/pc_actions.h"
#include "input/touch/action_kinds.h"
#include "input/touch/touch_gestures.h"
#include "input/touch/touch_icons.h"
#include "input/touch/touch_layout.h"
#include "input/touch/touch_sprites.h"
#include "platform/sh_platform.h"
#include "render/overlay/overlay.h"
#include "ui/menu/pc_debug_menu.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace sh::input::touch {
namespace {

namespace gs = sh::game_state;

enum class Mode { Off, Idle, Controls, Map, Pointer, Edit };
enum class Owner { None, Stick, Dpad, Control, Look, Pointer, Map, Edit, EditUi, Back, Skip, Delete };
enum class EditUi { Done, Reset };

constexpr int kMaxFingers = 10;
constexpr float kSlop = 0.035f;            // of the screen height: a finger that moved less stayed put
constexpr float kLongPress = 0.45f;        // seconds: the pointer's button goes down and stays
constexpr float kScrollStep = 0.075f;      // of the screen height per notch of the wheel
constexpr float kMapSwipe = 0.12f;         // of the screen height: a swipe that turns the map's page
constexpr float kStickFollow = 1.6f;       // radii before the floating stick's origin follows the thumb
constexpr int kPulseFrames = 3;            // a tap holds its action this many pad updates (over a 30 Hz tick)
constexpr int kClickFrames = 2;
constexpr int kTargetEvery = 4;            // pad updates between two looks for the action button's target
constexpr int kSkipShownMs = 300;
constexpr int kPromptMs = 250;             // a prompt the game stopped renewing for this long is over
constexpr float kDoubleTap = 0.30f;        // seconds between the two taps of a double tap
constexpr float kDpadReach = 1.25f;        // radii: a finger that lands this near the d-pad takes it

// The Back and Skip buttons sit where pause and the map do in play, a menu's Delete where the inventory does (the
// editor moves them along).
constexpr Place kEditDone{0.42f, 0.11f, 0.13f};
constexpr Place kEditReset{0.58f, 0.11f, 0.13f};

struct Finger {
    bool down = false;
    std::uint64_t id = 0;
    bool from_mouse = false;
    Point start, pos, last;  // window pixels
    std::uint64_t start_ns = 0;
    float moved = 0.0f;  // farthest from the start, pixels
    bool looked = false;  // a look finger past the tap distance: it turns the camera
    Owner owner = Owner::None;
    Control control = Control::Count;
    EditUi ui = EditUi::Done;
};

struct State {
    bool installed = false;
    Mode mode = Mode::Off;
    std::array<Finger, kMaxFingers> fingers{};
    std::uint64_t last_ns = 0;
    std::uint32_t frame = 0;

    Layout layout = default_layout();
    std::string layout_text;  // what `layout` was parsed from
    Visibility visibility;
    std::array<int, sh::input::kActionCount> pulses{};

    // The stick.
    int stick_finger = -1;
    Point stick_origin;
    StickOutput stick;

    // The d-pad (`touch_movement = dpad`): its finger, the arrows it presses, those held as the pad's directions on
    // this pad update (none while Harry is held), and the run toggle, kept for the session.
    int dpad_finger = -1;
    std::uint8_t dpad_bits = 0;
    std::uint8_t dpad_held = 0;
    std::uint8_t dpad_before = 0;  // the arrows of the update before, for the struggle's presses
    Toggle run;
    bool run_held = false;

    // The look: a finger that landed on the free screen while an alternate camera plays, and its drag not yet taken
    // by the camera, in screen widths.
    int look_finger = -1;
    double look_x = 0.0, look_y = 0.0;

    // Aim and attack: a weapon in hand, and the picture of its attack.
    bool aim_latched = false;
    bool armed = false;
    std::string attack_icon = "attack";

    // What the game asks for (engine/input/touch_prompts.h): bits of Control, the attack button's picture.
    std::uint32_t prompted = 0;
    std::string attack_prompt_icon;
    // While Harry is held: the struggle button's taps and the stick's flicks, as presses of the action button.
    PressTrain struggle;
    Flicks flicks;

    // The action button's target.
    bool target = false;
    ActionKind target_kind = ActionKind::Act;
    std::optional<ActionKinds> kinds;
    bool kinds_tried = false;

    // The pointer.
    int pointer_finger = -1;
    bool pointer_long = false;   // the button is held (a long press)
    bool pointer_long_taken = false;  // the long press was the menu's Delete: the button stays up
    bool scrolling = false;
    bool scroll_horizontal = false;
    int click_frames = 0;
    Scroller scroller;
    Point pointer_at;  // window pixels
    bool pointer_live = false;
    Point frame_pos;   // the pointer's or the map's finger at the last pad update, for its speed
    // A list drawn by the menu view follows the finger itself (claim_list_drag): its drags move the pointer
    // instead of turning the wheel, and a finger that stops its glide picks nothing.
    std::uint32_t list_claim_frame = 0;
    bool list_flinging = false;
    bool pointer_stop_only = false;

    // The paper map: the fingers go to its touch view (engine/screens/paper_map.h).
    bool map_multi = false;   // two fingers took part in this touch
    bool map_swiped = false;
    Point map_velocity;       // pixels a second of the panning finger
    int map_finger = -1;
    std::uint64_t map_tap_ns = 0;  // the last single tap, for a double tap
    Point map_tap_at;

    // The editor.
    bool editor = false;
    bool editor_keys_held = false;
    Layout edit_start;  // places when the finger took the control
    float pinch_start = 0.0f;
    Control pinch_control = Control::Count;
    float pinch_base = 0.0f;
};

// A finger that went down on the Skip button, seen by an event watch: the loops that only pump SDL's events
// (movies, the boot screens) do not reach the touch hook, and a quick tap is gone before their next poll.
std::atomic<std::uint64_t> g_skip_tap_ns{0};

// The phone's Back key (Escape, see SilentHillActivity) is down for a moment only: a press that comes and goes
// between two pad updates never shows in the keyboard state. Counted here and held as Back by the next update.
std::atomic<int> g_back_keys{0};
constexpr std::uint64_t kSkipTapNs = 400'000'000;

State& st() {
    static State s;
    return s;
}

bool device_active() { return Sh_IsMobile() != 0 || g_PcConfig.touchControls == 1; }

bool dpad_mode() { return g_PcConfig.touchMovement == 1; }

// The controls of the chosen movement: the stick, or the d-pad with its run toggle (also in the layout editor).
bool in_use(Control c) {
    // The View button is the original camera's search view; the alternate cameras turn by a drag instead.
    if (c == Control::Camera) return !sh::input::is_alternate_camera(sh::input::camera_styles().style());
    if (c == Control::Stick) return !dpad_mode();
    if (c == Control::Dpad || c == Control::Run) return dpad_mode();
    return true;
}

// ---- geometry ----

struct Screen {
    float w = 0.0f, h = 0.0f;  // window pixels
    float points_w = 0.0f, points_h = 0.0f;
    Area safe;
};

Screen screen() {
    Screen s;
    if (g_window == nullptr) return s;
    int pw = 0, ph = 0, ww = 0, wh = 0;
    SDL_GetWindowSizeInPixels(g_window, &pw, &ph);
    SDL_GetWindowSize(g_window, &ww, &wh);
    s.w = static_cast<float>(pw);
    s.h = static_cast<float>(ph);
    s.points_w = static_cast<float>(ww);
    s.points_h = static_cast<float>(wh);
    s.safe = {0.0f, 0.0f, s.w, s.h};
    SDL_Rect r{};
    if (ww > 0 && wh > 0 && SDL_GetWindowSafeArea(g_window, &r) && r.w > 0 && r.h > 0) {
        const float sx = s.w / s.points_w, sy = s.h / s.points_h;
        s.safe = {static_cast<float>(r.x) * sx, static_cast<float>(r.y) * sy, static_cast<float>(r.w) * sx,
                  static_cast<float>(r.h) * sy};
    }
    return s;
}

float scale() { return std::clamp(g_PcConfig.touchScale, 0.6f, 1.6f); }

Circle circle(const Place& p, const Screen& s) { return circle_of(p, s.safe, scale()); }

bool inside(const Circle& c, Point p, float slop) { return std::hypot(p.x - c.x, p.y - c.y) <= c.r + slop; }

Place back_place() { return st().layout[Control::Pause]; }
Place skip_place() { return st().layout[Control::Map]; }
Place delete_place() { return st().layout[Control::Inventory]; }

// ---- what the screen is ----

// A menu view's list was drawn on one of the last pad updates (the claim is renewed every frame it shows).
bool list_claimed() {
    const State& s = st();
    return s.list_claim_frame != 0 && s.frame - s.list_claim_frame <= 2;
}

bool in_game(gs::GameState g) { return g == gs::GameState::InGame || g == gs::GameState::MapEvent; }

bool free_gameplay() {
    if (!in_game(gs::game_state()) || gs::sys_state() != gs::SysState::Gameplay) return false;
    if (sh::game::session().cutscene_live() || sh::game::session().sys_flag(sh::game::sys_flag::kDemo)) return false;
    return Pc_ConsoleIsOpen() == 0 && Pc_DebugMenu_IsOpen() == 0;
}

bool skip_shown() { return skip_asked_within(kSkipShownMs); }

// A menu asks for its Delete (the save list while a save is selected): the button, and a long press deletes.
bool delete_shown(Mode mode) {
    return mode == Mode::Pointer && sh::input::touch_prompt(sh::input::kTouchDelete, kPromptMs).has_value();
}

// The message on screen waits for an answer (its options are listed; text/message_box.h).
bool question_open() {
    const sh::text::MessageBox& box = sh::text::message_box();
    return box.selection().max_idx != box.spec().no_value && box.selection().max_idx > 0;
}

bool back_shown(Mode mode) {
    if (mode == Mode::Map) return true;
    if (mode != Mode::Pointer) return false;
    const gs::GameState g = gs::game_state();
    // The title's own menu has nothing to go back to; boot screens and movies take no Back.
    if (g == gs::GameState::MainMenu || g < gs::GameState::MainMenu || g == gs::GameState::MovieOpening ||
        g == gs::GameState::ExitMovie) {
        return false;
    }
    // A message box or a dialogue line goes on with Cross (a tap anywhere); Back does nothing there. Only a question
    // ("Take it?") answers Back: with its Cancel answer.
    if (in_game(g) && (gs::sys_state() == gs::SysState::ReadMessage || sh::game::session().system.flow.message_shown != 0))
        return gs::sys_state() == gs::SysState::ReadMessage && question_open();
    return sh::input::mouse_pointer().requested();
}

Mode compute_mode() {
    State& s = st();
    if (!device_active()) return Mode::Off;
    if (s.editor) return Mode::Edit;
    const gs::GameState g = gs::game_state();
    if (g == gs::GameState::PaperMapScreen || (in_game(g) && gs::sys_state() == gs::SysState::MapScreen))
        return Mode::Map;
    if (free_gameplay()) {
        const bool pad = g_PcConfig.touchControls == 0 && last_device() == LastDevice::Pad;
        return controls_enabled() && !pad ? Mode::Controls : Mode::Idle;
    }
    return Mode::Pointer;
}

// ---- actions ----

void pulse(Action a) { st().pulses[static_cast<std::size_t>(a)] = kPulseFrames; }

bool aim_hold_mode() { return sh::game::session().game.settings.weapon_control_press != 0; }

void load_icon(const std::string& name);

// ---- the action button's picture ----

void load_kinds() {
    State& s = st();
    if (s.kinds_tried) return;
    s.kinds_tried = true;
    char mods[1024];
    PcPaths_ExeFile(mods, sizeof(mods), "mods");
    std::vector<std::string> warnings;
    std::string source;
    const Result<std::string> text =
        sh::gameflow::read_config_file(PcPort_GetGameDataPath(), mods, "touch_actions.json", &source, warnings);
    if (!text) {
        SH_LOG_WARN(GAME, "touch: config/touch_actions.json: %s (the action button shows no kinds)",
                    text.error().c_str());
        return;
    }
    Result<ActionKinds> parsed = ActionKinds::parse(text.value());
    if (!parsed) {
        SH_LOG_WARN(GAME, "touch: config/touch_actions.json: %s", parsed.error().c_str());
        return;
    }
    s.kinds = std::move(parsed.value());
}

void look_for_target() {
    State& s = st();
    const sh::gameflow::game::ActionTarget t = sh::gameflow::game::action_target();
    const bool was = s.target;
    s.target = t.found;
    if (!t.found) {
        if (was) SH_LOG_DEBUG(GAME, "touch: nothing in reach of the action button");
        return;
    }
    load_kinds();
    const ActionKind kind =
        s.kinds ? s.kinds->classify(sh::gameflow::sys_state_name(t.state), t.handler) : ActionKind::Act;
    if (!was || kind != s.target_kind) {
        SH_LOG_DEBUG(GAME, "touch: action button for state %d param %d %s: kind %d", t.state, t.param,
                     t.handler.c_str(), static_cast<int>(kind));
    }
    s.target_kind = kind;
}

void look_at_prompts() {
    State& s = st();
    s.prompted = 0;
    for (const Control c : {Control::Struggle, Control::Attack}) {
        const std::optional<std::string> icon = sh::input::touch_prompt(control_name(c), kPromptMs);
        if (!icon) continue;
        s.prompted |= bit(c);
        if (c == Control::Attack) {
            s.attack_prompt_icon = *icon;
            load_icon(*icon);
        }
    }
}

void look_at_weapon() {
    State& s = st();
    const int slot = gs::inventory_equipped_item();
    int item = sh::inventory::kNoItem;
    if (slot >= 0 && slot < static_cast<int>(sh::inventory::kSlotCapacity)) item = gs::inventory_slot(slot).item;
    const sh::items::ItemLibrary* library = sh::inventory::item_catalog().library();
    const sh::items::WeaponDef* weapon =
        library != nullptr && item != sh::inventory::kNoItem ? library->weapon_of_item(item) : nullptr;
    s.armed = weapon != nullptr;
    if (weapon == nullptr) {
        s.attack_icon = "attack";
    } else if (!weapon->touch_icon.empty()) {
        s.attack_icon = weapon->touch_icon;
    } else {
        s.attack_icon = weapon->is_gun() ? "attack_pistol" : "attack_blunt";
    }
    load_icon(s.attack_icon);
}

// ---- fingers ----

Finger* find_finger(std::uint64_t id) {
    for (Finger& f : st().fingers)
        if (f.down && f.id == id) return &f;
    return nullptr;
}

int index_of(const Finger& f) { return static_cast<int>(&f - st().fingers.data()); }

Finger* new_finger(std::uint64_t id) {
    for (Finger& f : st().fingers) {
        if (!f.down) {
            f = Finger{};
            f.down = true;
            f.id = id;
            return &f;
        }
    }
    return nullptr;
}

float seconds_since(std::uint64_t ns) { return static_cast<float>(SDL_GetTicksNS() - ns) * 1e-9f; }

// Lets go of every finger's job (the screen changed under them): they do nothing more until lifted.
void drop_owners() {
    State& s = st();
    for (Finger& f : s.fingers) f.owner = Owner::None;
    s.stick_finger = -1;
    s.stick = {};
    s.dpad_finger = -1;
    s.dpad_bits = 0;
    s.dpad_held = 0;
    s.run.release_all();
    s.look_finger = -1;
    s.look_x = s.look_y = 0.0;
    s.pointer_finger = -1;
    s.pointer_long = false;
    s.pointer_long_taken = false;
    s.pointer_stop_only = false;
    s.scrolling = false;
    s.scroller.stop();
    s.map_finger = -1;
    s.map_multi = false;
    s.pinch_control = Control::Count;
}

std::optional<Control> control_at(const Screen& scr, Point p, std::uint32_t visible) {
    return hit_test(st().layout, scr.safe, scale(), visible, p.x, p.y, kSlop * scr.h * 0.5f);
}

void down_dpad(Finger& f, const Screen& scr) {
    State& s = st();
    if (s.dpad_finger >= 0) return;  // one finger steers; another one landing there does nothing
    f.owner = Owner::Dpad;
    s.dpad_finger = index_of(f);
    const Circle k = circle(s.layout[Control::Dpad], scr);
    s.dpad_bits = dpad_bits(f.pos.x - k.x, f.pos.y - k.y, k.r, 0);
    // A tap shorter than a frame still steps or turns, as a button's tap still presses. Held, Harry does not walk.
    if ((s.prompted & bit(Control::Struggle)) != 0) return;
    if (s.dpad_bits & kDpadUp) pulse(Action::MoveForward);
    if (s.dpad_bits & kDpadDown) pulse(Action::MoveBack);
    if (s.dpad_bits & kDpadLeft) pulse(Action::TurnLeft);
    if (s.dpad_bits & kDpadRight) pulse(Action::TurnRight);
}

void down_controls(Finger& f, const Screen& scr) {
    State& s = st();
    const std::uint32_t visible = s.visibility.visible();
    if (const std::optional<Control> c = control_at(scr, f.pos, visible)) {
        if (*c == Control::Dpad) {
            down_dpad(f, scr);
            return;
        }
        f.owner = Owner::Control;
        f.control = *c;
        // A tap shorter than a frame still presses: the button holds at least a pulse.
        switch (*c) {
            case Control::Action: pulse(Action::Interact); break;
            case Control::Attack: pulse(Action::Attack); break;
            case Control::Map: pulse(Action::Map); break;
            case Control::Inventory: pulse(Action::Inventory); break;
            case Control::Pause: pulse(Action::Back); break;
            case Control::Camera: pulse(Action::View); break;
            case Control::Struggle: s.struggle.add(); break;
            case Control::Run: s.run.press(); break;
            default: break;
        }
        if (*c == Control::Aim && g_PcConfig.touchAimToggle != 0) {
            if (aim_hold_mode()) {
                s.aim_latched = !s.aim_latched;
            } else {
                pulse(Action::Aim);  // the game's own Switch mode toggles on each press
            }
        }
        return;
    }
    // The d-pad takes a finger that lands a little beside it too: the thumb aims at an arrow, not at the circle.
    if ((visible & bit(Control::Dpad)) != 0) {
        const Circle k = circle(s.layout[Control::Dpad], scr);
        if (std::hypot(f.pos.x - k.x, f.pos.y - k.y) <= k.r * kDpadReach) down_dpad(f, scr);
        return;
    }
    if ((visible & bit(Control::Stick)) != 0 && s.stick_finger < 0 && in_stick_zone(scr.safe, f.pos.x, f.pos.y)) {
        f.owner = Owner::Stick;
        s.stick_finger = index_of(f);
        s.stick_origin = f.pos;
        return;
    }
    // The rest of the screen turns the alternate cameras, as the mouse does on a desktop (whose mouse looks already).
    if (s.look_finger < 0 && !f.from_mouse && sh::input::is_alternate_camera(sh::input::camera_styles().style())) {
        f.owner = Owner::Look;
        s.look_finger = index_of(f);
    }
}

void down_pointer(Finger& f, const Screen& scr, Mode mode) {
    State& s = st();
    if (back_shown(mode) && inside(circle(back_place(), scr), f.pos, kSlop * scr.h * 0.5f)) {
        f.owner = Owner::Back;
        pulse(Action::Back);
        return;
    }
    if (skip_shown() && inside(circle(skip_place(), scr), f.pos, kSlop * scr.h * 0.5f)) {
        f.owner = Owner::Skip;
        pulse(Action::Confirm);
        g_skip_tap_ns.store(0);  // taken here, by the game's frame: a movie starting next must not take it too
        return;
    }
    if (delete_shown(mode) && inside(circle(delete_place(), scr), f.pos, kSlop * scr.h * 0.5f)) {
        f.owner = Owner::Delete;
        sh::input::note_touch_prompt_press(sh::input::kTouchDelete);
        return;
    }
    if (mode != Mode::Pointer || s.pointer_finger >= 0 || f.from_mouse) return;
    f.owner = Owner::Pointer;
    s.pointer_finger = index_of(f);
    s.pointer_long = false;
    s.pointer_long_taken = false;
    // A finger that lands on a gliding list stops it, as on a phone; lifting it picks nothing.
    s.pointer_stop_only = (list_claimed() && s.list_flinging) || s.scroller.gliding();
    s.scrolling = false;
    s.scroller.press();
    s.pointer_at = f.pos;
    s.pointer_live = true;
    s.frame_pos = f.pos;
}

void down_map(Finger& f, const Screen& scr) {
    State& s = st();
    if (inside(circle(back_place(), scr), f.pos, kSlop * scr.h * 0.5f)) {
        f.owner = Owner::Back;
        pulse(Action::Back);
        return;
    }
    f.owner = Owner::Map;
    int count = 0;
    Finger* other = nullptr;
    for (Finger& g : s.fingers) {
        if (g.down && g.owner == Owner::Map) {
            ++count;
            if (&g != &f) other = &g;
        }
    }
    if (count == 1) {
        s.map_finger = index_of(f);
        s.frame_pos = f.pos;
        s.map_multi = false;
        s.map_swiped = false;
        s.map_velocity = {};
        sh::screens::paper_map_touch_begin(f.pos.x, f.pos.y);
    } else if (count == 2 && other != nullptr) {
        s.map_multi = true;
        s.map_finger = -1;
        s.map_tap_ns = 0;
        sh::screens::paper_map_touch_pinch(0.5f * (f.pos.x + other->pos.x), 0.5f * (f.pos.y + other->pos.y),
                                           std::hypot(f.pos.x - other->pos.x, f.pos.y - other->pos.y));
    }
}

// True while exactly two fingers hold the map: the pinch, in *a and *b.
bool map_pair(const Finger** a, const Finger** b) {
    *a = *b = nullptr;
    int count = 0;
    for (const Finger& g : st().fingers) {
        if (!g.down || g.owner != Owner::Map) continue;
        (*a == nullptr ? *a : *b) = &g;
        ++count;
    }
    return count == 2;
}

void down_edit(Finger& f, const Screen& scr) {
    State& s = st();
    const float slop = kSlop * scr.h * 0.5f;
    if (inside(circle(kEditDone, scr), f.pos, slop)) {
        f.owner = Owner::EditUi;
        f.ui = EditUi::Done;
        return;
    }
    if (inside(circle(kEditReset, scr), f.pos, slop)) {
        f.owner = Owner::EditUi;
        f.ui = EditUi::Reset;
        return;
    }
    // A second finger while one holds a control sizes that control.
    for (Finger& g : s.fingers) {
        if (&g != &f && g.down && g.owner == Owner::Edit) {
            f.owner = Owner::Edit;
            f.control = Control::Count;
            s.pinch_control = g.control;
            s.pinch_start = std::max(std::hypot(f.pos.x - g.pos.x, f.pos.y - g.pos.y), 1.0f);
            s.pinch_base = s.layout[g.control].size;
            return;
        }
    }
    std::optional<Control> hit;
    float best = 0.0f;
    for (int i = 0; i < kControlCount; ++i) {
        const Control c = static_cast<Control>(i);
        if (!in_use(c)) continue;
        const Circle k = circle(s.layout[c], scr);
        const float d = std::hypot(f.pos.x - k.x, f.pos.y - k.y) - k.r;
        if (d <= slop && (!hit || d < best)) {
            hit = c;
            best = d;
        }
    }
    if (hit) {
        f.owner = Owner::Edit;
        f.control = *hit;
        s.edit_start = s.layout;
    }
}

void move_edit(Finger& f, const Screen& scr) {
    State& s = st();
    if (f.owner != Owner::Edit) return;
    if (f.control == Control::Count) {
        // The sizing finger: the distance to the holding one against where it started.
        for (const Finger& g : s.fingers) {
            if (&g != &f && g.down && g.owner == Owner::Edit && g.control == s.pinch_control &&
                s.pinch_control != Control::Count) {
                const float d = std::hypot(f.pos.x - g.pos.x, f.pos.y - g.pos.y);
                Place p = s.layout[s.pinch_control];
                p.size = s.pinch_base * d / s.pinch_start;
                s.layout[s.pinch_control] = clamp_place(s.pinch_control, p);
            }
        }
        return;
    }
    if (scr.safe.w <= 0.0f || scr.safe.h <= 0.0f) return;
    Place p = s.edit_start[f.control];
    p.x += (f.pos.x - f.start.x) / scr.safe.w;
    p.y += (f.pos.y - f.start.y) / scr.safe.h;
    p.size = s.layout[f.control].size;
    s.layout[f.control] = clamp_place(f.control, p);
}

void save_layout() {
    State& s = st();
    const std::string text = s.layout == default_layout() ? std::string() : format_layout(s.layout);
    std::snprintf(g_PcConfig.touchLayout, sizeof(g_PcConfig.touchLayout), "%s", text.c_str());
    s.layout_text = g_PcConfig.touchLayout;
    PcConfig_SaveKeyValue("touch_layout", g_PcConfig.touchLayout);
}

void close_editor() {
    State& s = st();
    if (!s.editor) return;
    s.editor = false;
    save_layout();
    drop_owners();
}

void up_finger(Finger& f, const Screen& scr) {
    State& s = st();
    const float slop = kSlop * scr.h;
    const bool tap = is_tap(seconds_since(f.start_ns), f.moved, slop);
    switch (f.owner) {
        case Owner::Stick:
            s.stick_finger = -1;
            s.stick = {};
            break;
        case Owner::Dpad:
            s.dpad_finger = -1;
            s.dpad_bits = 0;
            break;
        case Owner::Control:
            if (f.control == Control::Run) s.run.release();
            break;
        case Owner::Look:
            s.look_finger = -1;
            break;
        case Owner::Pointer:
            s.pointer_finger = -1;
            if (s.pointer_long) {
                s.pointer_long = false;
                s.pointer_long_taken = false;
            } else if (!s.scrolling && pointer_tap(f.moved, slop, s.pointer_stop_only)) {
                s.pointer_at = f.pos;
                s.click_frames = kClickFrames;
                // A screen that does not follow the pointer takes a tap as Cross: the next line of a message.
                // Not in free play, even while a scene's borders still fade: a tap there must not act.
                if (sh::game::session().sys_flag(sh::game::sys_flag::kDemo)) {
                    pulse(Action::Confirm);  // a tap ends the attract demo, as any button does
                } else if (!sh::input::mouse_pointer().requested() && in_game(gs::game_state()) &&
                           gs::sys_state() != gs::SysState::Gameplay) {
                    pulse(Action::Interact);
                }
            }
            // A claimed list glides by itself.
            if (list_claimed()) {
                s.scroller.stop();
            } else {
                s.scroller.release();
            }
            s.scrolling = false;
            s.pointer_stop_only = false;
            break;
        case Owner::Map: {
            if (index_of(f) == s.map_finger) s.map_finger = -1;
            Finger* other = nullptr;
            int others = 0;
            for (Finger& g : s.fingers) {
                if (&g != &f && g.down && g.owner == Owner::Map) {
                    other = &g;
                    ++others;
                }
            }
            if (others == 1 && other != nullptr) {
                // Out of a pinch the finger left drags on from where it is.
                s.map_finger = index_of(*other);
                s.frame_pos = other->pos;
                s.map_velocity = {};
                sh::screens::paper_map_touch_move(other->pos.x, other->pos.y);
                break;
            }
            if (others != 0) break;
            const bool single = tap && !s.map_multi && !s.map_swiped;
            sh::screens::paper_map_touch_end(single || s.map_multi ? 0.0f : s.map_velocity.x,
                                             single || s.map_multi ? 0.0f : s.map_velocity.y);
            if (single) {
                const std::uint64_t now = SDL_GetTicksNS();
                if (s.map_tap_ns != 0 && static_cast<float>(now - s.map_tap_ns) * 1e-9f <= kDoubleTap &&
                    std::hypot(f.pos.x - s.map_tap_at.x, f.pos.y - s.map_tap_at.y) <= 2.0f * slop) {
                    sh::screens::paper_map_touch_double_tap(f.pos.x, f.pos.y);
                    s.map_tap_ns = 0;
                } else {
                    s.map_tap_ns = now;
                    s.map_tap_at = f.pos;
                }
            }
            s.map_multi = false;
            break;
        }
        case Owner::EditUi: {
            const Circle c = circle(f.ui == EditUi::Done ? kEditDone : kEditReset, scr);
            if (inside(c, f.pos, slop)) {
                if (f.ui == EditUi::Done) {
                    close_editor();
                } else {
                    s.layout = default_layout();
                }
            }
            break;
        }
        case Owner::Edit:
            if (f.control == Control::Count) s.pinch_control = Control::Count;
            break;
        default:
            break;
    }
    f = Finger{};
}

void move_finger(Finger& f, const Screen& scr) {
    State& s = st();
    switch (f.owner) {
        case Owner::Stick:
            s.stick_origin = follow_origin(s.stick_origin, f.pos, circle(s.layout[Control::Stick], scr).r,
                                           kStickFollow);
            break;
        case Owner::Look:
            // A finger that has not yet moved past the tap distance turns nothing: a tap stays a tap. Then the whole
            // way from where it landed counts, so the view does not jump when it starts to follow.
            if (f.moved > kSlop * scr.h && scr.w > 0.0f) {
                const Point from = f.looked ? f.last : f.start;
                s.look_x += (f.pos.x - from.x) / scr.w;
                s.look_y += (f.pos.y - from.y) / scr.w;
                f.looked = true;
            }
            break;
        case Owner::Pointer: {
            if (s.pointer_long) {
                s.pointer_at = f.pos;  // a held button drags (a slider)
                break;
            }
            if (!s.scrolling && f.moved > kSlop * scr.h) {
                s.scrolling = true;
                s.scroll_horizontal = std::fabs(f.pos.x - f.start.x) > std::fabs(f.pos.y - f.start.y) * 1.5f;
            }
            if (s.scrolling && list_claimed()) {
                s.pointer_at = f.pos;  // the list follows the pointer
            } else if (s.scrolling) {
                const float delta = s.scroll_horizontal ? f.pos.x - f.last.x : f.pos.y - f.last.y;
                const int notches = s.scroller.drag(delta, 0.0f, kScrollStep * scr.h);
                if (notches != 0) sh::input::wheel().scroll(s.scroll_horizontal ? -notches : notches);
            }
            break;
        }
        case Owner::Map: {
            const Finger* a = nullptr;
            const Finger* b = nullptr;
            if (map_pair(&a, &b)) {
                sh::screens::paper_map_touch_pinch(0.5f * (a->pos.x + b->pos.x), 0.5f * (a->pos.y + b->pos.y),
                                                   std::hypot(a->pos.x - b->pos.x, a->pos.y - b->pos.y));
            } else if (index_of(f) == s.map_finger) {
                sh::screens::paper_map_touch_move(f.pos.x, f.pos.y);
                // The whole page shown: a vertical swipe turns it, as the pad's Up and Down do.
                if (!s.map_multi && !s.map_swiped && sh::screens::paper_map_touch_at_fit()) {
                    const float dy = f.pos.y - f.start.y;
                    if (std::fabs(dy) > kMapSwipe * scr.h && std::fabs(dy) > std::fabs(f.pos.x - f.start.x)) {
                        pulse(dy < 0.0f ? Action::MoveBack : Action::MoveForward);
                        s.map_swiped = true;
                    }
                }
            }
            break;
        }
        case Owner::Edit:
            move_edit(f, scr);
            break;
        default:
            break;
    }
}

void touch_hook(sh::platform::TouchPhase phase, std::uint64_t id, float x, float y, bool from_mouse) {
    State& s = st();
    if (!device_active()) return;
    const Screen scr = screen();
    const Point p{x * scr.w, y * scr.h};
    // A desktop's mouse already is the pointer: only the controls, the map and the editor take it as a finger.
    const bool mouse_ignored = from_mouse && (s.mode == Mode::Pointer || s.mode == Mode::Idle || s.mode == Mode::Off);
    using sh::platform::TouchPhase;
    if (phase == TouchPhase::Down) {
        if (mouse_ignored) return;
        Finger* f = new_finger(id);
        if (f == nullptr) return;
        f->from_mouse = from_mouse;
        f->start = f->pos = f->last = p;
        f->start_ns = SDL_GetTicksNS();
        if (!from_mouse) set_last_device(LastDevice::Mouse);
        switch (s.mode) {
            case Mode::Controls: down_controls(*f, scr); break;
            case Mode::Map: down_map(*f, scr); break;
            case Mode::Edit: down_edit(*f, scr); break;
            case Mode::Pointer:
            case Mode::Idle: down_pointer(*f, scr, s.mode); break;
            default: break;
        }
        return;
    }
    Finger* f = find_finger(id);
    if (f == nullptr) return;
    f->last = f->pos;
    f->pos = p;
    f->moved = std::max(f->moved, std::hypot(p.x - f->start.x, p.y - f->start.y));
    if (phase == TouchPhase::Move) {
        move_finger(*f, scr);
    } else if (phase == TouchPhase::Cancel) {
        if (f->owner == Owner::Control && f->control == Control::Run) s.run.release();
        f->owner = Owner::None;
        if (index_of(*f) == s.stick_finger) {
            s.stick_finger = -1;
            s.stick = {};
        }
        if (index_of(*f) == s.dpad_finger) {
            s.dpad_finger = -1;
            s.dpad_bits = 0;
        }
        if (index_of(*f) == s.pointer_finger) s.pointer_finger = -1;
        if (index_of(*f) == s.look_finger) s.look_finger = -1;
        *f = Finger{};
    } else {
        up_finger(*f, scr);
    }
}

// ---- the pad update ----

std::uint32_t held_actions(const Screen& scr) {
    State& s = st();
    std::uint32_t mask = 0;
    const auto add = [&mask](Action a) { mask |= 1u << static_cast<int>(a); };
    for (std::size_t i = 0; i < s.pulses.size(); ++i) {
        if (s.pulses[i] > 0) {
            --s.pulses[i];
            mask |= 1u << i;
        }
    }
    if (s.mode != Mode::Controls) return mask;
    for (const Finger& f : s.fingers) {
        if (!f.down || f.owner != Owner::Control || (s.visibility.visible() & bit(f.control)) == 0) continue;
        switch (f.control) {
            case Control::Action: add(Action::Interact); break;
            case Control::Attack: add(Action::Attack); break;
            case Control::Aim:
                if (g_PcConfig.touchAimToggle == 0) add(Action::Aim);
                break;
            case Control::Map: add(Action::Map); break;
            case Control::Inventory: add(Action::Inventory); break;
            case Control::Pause: add(Action::Back); break;
            case Control::Camera: add(Action::View); break;
            default: break;
        }
    }
    if (s.struggle.step()) add(Action::Interact);
    if (s.aim_latched && aim_hold_mode()) add(Action::Aim);
    if (s.stick.run) add(Action::Run);
    // The d-pad holds the pad's digital directions, as the keyboard's arrows do: the game's own d-pad movement.
    if (s.dpad_held & kDpadUp) add(Action::MoveForward);
    if (s.dpad_held & kDpadDown) add(Action::MoveBack);
    if (s.dpad_held & kDpadLeft) add(Action::TurnLeft);
    if (s.dpad_held & kDpadRight) add(Action::TurnRight);
    if (s.run_held) add(Action::Run);
    (void)scr;
    return mask;
}

void enter_mode(Mode from, Mode to) {
    State& s = st();
    drop_owners();
    if (to != Mode::Controls) {
        s.aim_latched = false;
        s.visibility.hide();
        s.struggle.clear();
        s.prompted = 0;
    }
    if (from == Mode::Pointer || to != Mode::Pointer) s.click_frames = 0;
}

void refresh_layout() {
    State& s = st();
    if (s.editor || s.layout_text == g_PcConfig.touchLayout) return;
    s.layout_text = g_PcConfig.touchLayout;
    std::vector<std::string> warnings;
    s.layout = parse_layout(s.layout_text, &warnings);
    for (const std::string& w : warnings) SH_LOG_WARN(CONFIG, "touch_layout: %s", w.c_str());
}

TouchStick frame() {
    State& s = st();
    if (!device_active()) {
        if (s.mode != Mode::Off) {
            enter_mode(s.mode, Mode::Off);
            s.mode = Mode::Off;
            PcActions_SetTouchActions(0);
            Sh_ReleaseTouchPointer();
        }
        return {};
    }
    const std::uint64_t now = SDL_GetTicksNS();
    const float dt = s.last_ns == 0 ? 0.0f : std::min(static_cast<float>(now - s.last_ns) * 1e-9f, 0.1f);
    ++s.frame;
    refresh_layout();
    if (s.frame == 1) {
        for (const char* name : {"back", "skip", "pause", "map", "inventory", "aim", "act", "take", "read", "open", "use",
                                 "check", "done", "reset", "attack", "struggle", "camera", "kick", "run", "delete",
                                 "dpad_up", "dpad_right", "dpad_down", "dpad_left"}) {
            load_icon(name);
        }
    }
    const Screen scr = screen();

    const Mode mode = compute_mode();
    if (mode != s.mode) {
        enter_mode(s.mode, mode);
        s.mode = mode;
    }

    // The editor's Back: Escape or the phone's back key closes it; the keys stay away from the menu under it
    // until they are let go.
    if (s.mode == Mode::Edit || s.editor_keys_held) {
        const bool* keys = SDL_GetKeyboardState(nullptr);
        const bool back = keys != nullptr && (keys[SDL_SCANCODE_ESCAPE] || keys[SDL_SCANCODE_AC_BACK]);
        if (s.mode == Mode::Edit && back) close_editor();
        s.editor_keys_held = back;
    }

    if (s.mode == Mode::Controls) {
        if (s.frame % kTargetEvery == 0) {
            look_for_target();
            look_at_weapon();
        }
        Facts facts;
        facts.gameplay = true;
        bool aim_finger = false;
        for (const Finger& f : s.fingers)
            if (f.down && f.owner == Owner::Control && f.control == Control::Aim) aim_finger = true;
        facts.aiming = gs::player_aiming() || s.aim_latched || aim_finger;
        facts.action_target = s.target;
        facts.action_always = g_PcConfig.touchActionButton != 0;
        facts.dpad = dpad_mode();
        facts.look_drag = !in_use(Control::Camera);
        facts.armed = s.armed;
        look_at_prompts();
        facts.prompted = s.prompted;
        s.visibility.update(facts, dt);
        if (s.stick_finger >= 0) {
            const Finger& f = s.fingers[static_cast<std::size_t>(s.stick_finger)];
            StickTuning tuning;
            tuning.run = g_PcConfig.touchStickRun != 0;
            tuning.running = s.stick.run;
            s.stick = stick_output(f.pos.x - s.stick_origin.x, f.pos.y - s.stick_origin.y,
                                   circle(s.layout[Control::Stick], scr).r, tuning);
        }
        if (s.dpad_finger >= 0) {
            const Finger& f = s.fingers[static_cast<std::size_t>(s.dpad_finger)];
            const Circle k = circle(s.layout[Control::Dpad], scr);
            s.dpad_bits = dpad_bits(f.pos.x - k.x, f.pos.y - k.y, k.r, s.dpad_bits);
        }
    }
    const bool struggling = s.mode == Mode::Controls && (s.prompted & bit(Control::Struggle)) != 0;
    if (!struggling) s.flicks.reset();
    // Held, Harry does not walk: each arrow newly pressed is a press to break free, as the stick's flicks are.
    const bool dpad_shown = s.mode == Mode::Controls && (s.visibility.visible() & bit(Control::Dpad)) != 0;
    const std::uint8_t dpad_now = dpad_shown ? s.dpad_bits : std::uint8_t{0};
    if (struggling && (dpad_now & ~s.dpad_before) != 0) s.struggle.add();
    s.dpad_before = dpad_now;
    s.dpad_held = struggling ? std::uint8_t{0} : dpad_now;
    s.run_held = s.mode == Mode::Controls && !struggling && s.run.on() &&
                 (s.visibility.visible() & bit(Control::Run)) != 0;

    TouchStick out;
    if (s.mode == Mode::Controls && s.stick_finger >= 0) {
        out.active = true;
        out.x = s.stick.axis_x;
        out.y = s.stick.axis_y;
        if (struggling) {
            // Held, Harry does not walk: the stick's flicks are presses, and the stick itself stays centred so a
            // flick through the centre is not counted twice by the game's own stick presses.
            if (s.flicks.update(s.stick.x, s.stick.y)) s.struggle.add();
            out.x = out.y = 128;
            s.stick.run = false;
        }
    }
    if (s.mode == Mode::Map && s.map_finger >= 0 && dt > 0.0f) {
        // The finger's speed, for the glide after it lifts (the map follows the finger itself).
        const Finger& f = s.fingers[static_cast<std::size_t>(s.map_finger)];
        const Point v{(f.pos.x - s.frame_pos.x) / dt, (f.pos.y - s.frame_pos.y) / dt};
        s.map_velocity = {s.map_velocity.x * 0.5f + v.x * 0.5f, s.map_velocity.y * 0.5f + v.y * 0.5f};
        s.frame_pos = f.pos;
    }

    // The editor's Back closes it; elsewhere it is the Back action.
    if (g_back_keys.exchange(0) > 0) {
        if (s.mode == Mode::Edit) {
            close_editor();
        } else {
            pulse(Action::Back);
        }
    }
    PcActions_SetTouchActions(held_actions(scr));
    const bool mouse_is_finger = Sh_IsMobile() == 0 && (s.mode == Mode::Controls || s.mode == Mode::Map);
    PcActions_SetSuspended((s.mode == Mode::Edit || s.editor_keys_held ? 3 : 0) | (mouse_is_finger ? 2 : 0));

    // The pointer: a long press holds its button, a tap clicks where it lifted, a drag's glide keeps scrolling.
    if (s.mode == Mode::Pointer) {
        if (s.pointer_finger >= 0) {
            const Finger& f = s.fingers[static_cast<std::size_t>(s.pointer_finger)];
            // A list of the menu view has no long press: a finger that rests and then drags scrolls it.
            if (!s.scrolling && !s.pointer_long && !list_claimed() && seconds_since(f.start_ns) >= kLongPress &&
                f.moved <= kSlop * scr.h) {
                s.pointer_long = true;
                // Over a menu that asks for its Delete the long press is that, on the row under the finger (the
                // menu followed the pointer there); the button is not held, so the row is not chosen as well.
                if (delete_shown(s.mode)) {
                    s.pointer_long_taken = true;
                    sh::input::note_touch_prompt_press(sh::input::kTouchDelete);
                }
            }
            if (s.scrolling) {
                s.scroller.sample(s.scroll_horizontal ? f.pos.x - s.frame_pos.x : f.pos.y - s.frame_pos.y, dt);
            }
            s.frame_pos = f.pos;
        }
        if (s.pointer_finger < 0) {
            const int notches = s.scroller.glide(dt, kScrollStep * scr.h);
            if (notches != 0) sh::input::wheel().scroll(s.scroll_horizontal ? -notches : notches);
        }
    }
    if (s.pointer_live && Sh_IsMobile() != 0 && scr.w > 0.0f && scr.h > 0.0f) {
        std::uint32_t buttons = 0;
        if (s.click_frames > 0) {
            --s.click_frames;
            buttons = SDL_BUTTON_LMASK;
        }
        if (s.pointer_long && !s.pointer_long_taken) buttons = SDL_BUTTON_LMASK;
        // Window points, which the mouse is measured in.
        Sh_SetTouchPointer(static_cast<int>(s.pointer_at.x * scr.points_w / scr.w),
                           static_cast<int>(s.pointer_at.y * scr.points_h / scr.h), buttons);
    }
    s.last_ns = now;
    return out;
}


bool SDLCALL back_watch(void*, SDL_Event* event) {
    if (event->type == SDL_EVENT_KEY_DOWN && !event->key.repeat && Sh_IsMobile() != 0 &&
        (event->key.scancode == SDL_SCANCODE_ESCAPE || event->key.scancode == SDL_SCANCODE_AC_BACK)) {
        g_back_keys.fetch_add(1);
    }
    return true;
}

bool SDLCALL skip_watch(void*, SDL_Event* event) {
    if (event->type != SDL_EVENT_FINGER_DOWN || !device_active() || !skip_shown()) return true;
    const Screen scr = screen();
    if (scr.h > 0.0f && inside(circle(skip_place(), scr), {event->tfinger.x * scr.w, event->tfinger.y * scr.h},
                               kSlop * scr.h * 0.5f)) {
        g_skip_tap_ns.store(SDL_GetTicksNS());
    }
    return true;
}

// Polled by the blocking loops (movies, the boot screens) through skip_held(): a finger on the Skip button.
bool skip_pressed() {
    if (!device_active()) return false;
    note_skip_asked();
    // Taken once, so one tap skips one thing, not the movie that follows the logo it skipped.
    const std::uint64_t tap = g_skip_tap_ns.exchange(0);
    return tap != 0 && SDL_GetTicksNS() - tap < kSkipTapNs;
}

// ---- drawing ----

// Every shape is a coverage mask (touch_sprites.h) made at the size it shows, kept while it is drawn, and drawn as
// one quad of window pixels into overlay::window_overlay(): on the window after the present, so neither the render
// scale nor the post-processing touches it. A mask carries one colour, so the source-over blend of straight alpha
// composes the layers exactly as premultiplied ones would, and a texel-for-pixel quad has no fringe to bleed.
struct Sprite {
    sh::overlay::TextureId id = 0;
    int size = 0;
    std::uint64_t used = 0;
};

constexpr std::uint64_t kSpriteKeepFrames = 120;  // a size not drawn for two seconds (the editor resizing) goes

struct SpriteCache {
    std::map<std::string, Sprite> sprites;
    std::uint64_t frame = 0;
};

SpriteCache& sprite_cache() {
    static SpriteCache cache;
    return cache;
}

// Quarter pixels: a mask is made again only when its size changes visibly.
std::string size_key(float v) { return std::to_string(std::lround(v * 4.0f)); }

class Painter {
public:
    Painter(sh::overlay::Overlay& o, const Screen& scr)
        : o_(o),
          kx_(scr.w > 0.0f ? static_cast<float>(o.frame().target_w) / scr.w : 1.0f),
          ky_(scr.h > 0.0f ? static_cast<float>(o.frame().target_h) / scr.h : 1.0f) {
        ++sprite_cache().frame;
    }
    ~Painter() { collect(); }

    // Window pixels to target pixels (the same on every renderer; kept for a drawable of another size).
    Point at(Point p) const { return {p.x * kx_, p.y * ky_}; }
    float len(float l) const { return l * 0.5f * (kx_ + ky_); }

    void rect(Point a, Point b, const sh::overlay::Color& color) {
        o_.rect(sh::overlay::Space::Target, {a.x, a.y, b.x - a.x, b.y - a.y}, color);
    }
    void disc(Point c, float r, const sh::overlay::Color& color) {
        draw("disc/" + size_key(r), c, color, [r] { return disc_mask(r); });
    }
    void ring(Point c, float r, float width, const sh::overlay::Color& color) {
        draw("ring/" + size_key(r) + "/" + size_key(width), c, color, [r, width] { return ring_mask(r, width); });
    }
    void halo(Point c, float r, float spread, const sh::overlay::Color& color) {
        draw("halo/" + size_key(r) + "/" + size_key(spread), c, color, [r, spread] { return halo_mask(r, spread); });
    }
    void icon(const std::string& name, const VectorIcon& icon, Point c, float side, const sh::overlay::Color& color) {
        draw("icon/" + name + "/" + size_key(side), c, color, [&icon, side] { return icon_mask(icon, side); });
    }

private:
    template <typename Make>
    void draw(const std::string& key, Point c, const sh::overlay::Color& color, Make make) {
        if (color.a <= 0.002f) return;
        SpriteCache& cache = sprite_cache();
        Sprite& sprite = cache.sprites[key];
        if (sprite.id == 0) {
            const Mask m = make();
            sprite.size = m.size;
            sprite.id = o_.textures().create(m.size, m.size, sh::overlay::Format::Alpha8, sh::overlay::Filter::Linear,
                                             m.alpha.data());
            if (sprite.id == 0) {
                cache.sprites.erase(key);
                return;
            }
        }
        sprite.used = cache.frame;
        // The mask's centre is a pixel corner: a whole-pixel quad keeps its edges where they were rasterised.
        const float half = 0.5f * static_cast<float>(sprite.size);
        const float x = std::round(c.x) - half, y = std::round(c.y) - half;
        o_.quad(sh::overlay::Space::Target, {x, y, static_cast<float>(sprite.size), static_cast<float>(sprite.size)},
                color, sprite.id);
    }

    void collect() {
        SpriteCache& cache = sprite_cache();
        for (auto it = cache.sprites.begin(); it != cache.sprites.end();) {
            if (cache.frame - it->second.used > kSpriteKeepFrames) {
                o_.textures().destroy(it->second.id);
                it = cache.sprites.erase(it);
            } else {
                ++it;
            }
        }
    }

    sh::overlay::Overlay& o_;
    float kx_, ky_;
};

// The pictures, gamedata/ui/touch/<name>.svg (a mod's file of the same path wins), read in the pad update and
// drawn from memory.
std::map<std::string, std::optional<VectorIcon>>& icons() {
    static std::map<std::string, std::optional<VectorIcon>> cache;
    return cache;
}

void load_icon(const std::string& name) {
    auto& cache = icons();
    if (name.empty() || cache.count(name) != 0) return;
    char mods[1024];
    PcPaths_ExeFile(mods, sizeof(mods), "mods");
    std::vector<std::string> warnings;
    std::string source;
    const Result<std::string> text = sh::gameflow::read_data_file(PcPort_GetGameDataPath(), mods, "ui/touch",
                                                                   name + ".svg", &source, warnings);
    std::optional<VectorIcon>& slot = cache[name];
    if (!text) {
        SH_LOG_WARN(GAME, "touch: ui/touch/%s.svg: %s", name.c_str(), text.error().c_str());
        return;
    }
    Result<VectorIcon> icon = parse_svg_icon(text.value());
    if (!icon) {
        SH_LOG_WARN(GAME, "touch: ui/touch/%s.svg: %s", name.c_str(), icon.error().c_str());
        return;
    }
    slot = std::move(icon.value());
}

const char* kind_icon(ActionKind k) {
    switch (k) {
        case ActionKind::Take: return "take";
        case ActionKind::Read: return "read";
        case ActionKind::Open: return "open";
        case ActionKind::Use: return "use";
        case ActionKind::Check: return "check";
        default: return "act";
    }
}

// The icon inside a button of radius r (target pixels) at c: the viewBox's square fills most of the circle.
void draw_icon(Painter& p, const std::string& name, Point c, float r, const sh::overlay::Color& ink) {
    const auto found = icons().find(name);
    if (found == icons().end() || !found->second) {
        p.disc(c, 0.18f * r, ink);
        return;
    }
    p.icon(name, *found->second, c, 1.22f * r, ink);
}

// A round control at c with a radius of r (target pixels): a soft shadow under it, the dark glass (light while
// pressed, with a glow), the rim, the icon.
void draw_disc_button(Painter& p, Point c, float r, const std::string& icon, float alpha, bool pressed) {
    const float rim = std::max(1.5f, 0.055f * r);
    p.halo({c.x, c.y + 0.04f * r}, r, 0.16f * r, {0.0f, 0.0f, 0.0f, 0.30f * alpha});
    if (pressed) p.halo(c, r, 0.22f * r, {1.0f, 1.0f, 1.0f, 0.22f * alpha});
    p.disc(c, r, pressed ? sh::overlay::Color{1.0f, 1.0f, 1.0f, 0.32f * alpha}
                         : sh::overlay::Color{0.0f, 0.0f, 0.0f, 0.40f * alpha});
    p.ring(c, r - 0.5f * rim, rim, {1.0f, 1.0f, 1.0f, 0.68f * alpha});
    draw_icon(p, icon, c, r, {1.0f, 1.0f, 1.0f, 0.92f * alpha});
}

void draw_button(Painter& p, const Screen& scr, const Place& place, const std::string& icon, float alpha,
                 bool pressed) {
    if (alpha <= 0.01f) return;
    const Circle k = circle(place, scr);
    draw_disc_button(p, p.at({k.x, k.y}), p.len(k.r), icon, alpha, pressed);
}

// The d-pad: the stick's dark base and on it a cross of four round arrow pads, each lit while it is pressed.
void draw_dpad(Painter& p, const Screen& scr, float alpha, std::uint8_t pressed) {
    if (alpha <= 0.01f) return;
    const Circle k = circle(st().layout[Control::Dpad], scr);
    const Point c = p.at({k.x, k.y});
    const float r = p.len(k.r);
    const float rim = std::max(1.5f, 0.04f * r);
    p.halo({c.x, c.y + 0.03f * r}, r, 0.12f * r, {0.0f, 0.0f, 0.0f, 0.22f * alpha});
    p.disc(c, r, {0.0f, 0.0f, 0.0f, 0.25f * alpha});
    p.ring(c, r - 0.5f * rim, rim, {1.0f, 1.0f, 1.0f, 0.5f * alpha});
    struct Arm {
        std::uint8_t bit;
        float dx, dy;
        const char* icon;
    };
    static constexpr std::array<Arm, 4> kArms = {{
        {kDpadUp, 0.0f, -1.0f, "dpad_up"},
        {kDpadRight, 1.0f, 0.0f, "dpad_right"},
        {kDpadDown, 0.0f, 1.0f, "dpad_down"},
        {kDpadLeft, -1.0f, 0.0f, "dpad_left"},
    }};
    // Four pads of 0.36 radii, 0.56 radii out: a small gap between neighbours, all inside the base's rim.
    for (const Arm& arm : kArms) {
        const Point at{c.x + arm.dx * 0.56f * r, c.y + arm.dy * 0.56f * r};
        draw_disc_button(p, at, 0.36f * r, arm.icon, alpha, (pressed & arm.bit) != 0);
    }
}

bool finger_on(Control c) {
    for (const Finger& f : st().fingers)
        if (f.down && f.owner == Owner::Control && f.control == c) return true;
    return false;
}

bool finger_on(Owner o) {
    for (const Finger& f : st().fingers)
        if (f.down && f.owner == o) return true;
    return false;
}

std::string control_icon(Control c) {
    switch (c) {
        case Control::Action: return kind_icon(st().target ? st().target_kind : ActionKind::Act);
        case Control::Aim: return "aim";
        case Control::Attack:
            // With the weapon down the button is there for what the game asked (a kick at a downed enemy).
            if ((st().prompted & bit(Control::Attack)) != 0 && !st().attack_prompt_icon.empty() &&
                !gs::player_aiming()) {
                return st().attack_prompt_icon;
            }
            return st().attack_icon;
        case Control::Map: return "map";
        case Control::Inventory: return "inventory";
        case Control::Pause: return "pause";
        case Control::Struggle: return "struggle";
        case Control::Camera: return "camera";
        case Control::Run: return "run";
        default: return "";
    }
}

void draw_stick(Painter& p, const Screen& scr, float alpha, bool editing) {
    State& s = st();
    const Circle k = circle(s.layout[Control::Stick], scr);
    Point base{k.x, k.y};
    Point knob = base;
    if (!editing && s.stick_finger >= 0) {
        base = s.stick_origin;
        knob = {base.x + s.stick.x * k.r, base.y + s.stick.y * k.r};
    }
    const Point c = p.at(base);
    const float r = p.len(k.r);
    const float rim = std::max(1.5f, 0.04f * r);
    p.halo({c.x, c.y + 0.03f * r}, r, 0.12f * r, {0.0f, 0.0f, 0.0f, 0.22f * alpha});
    p.disc(c, r, {0.0f, 0.0f, 0.0f, 0.25f * alpha});
    p.ring(c, r - 0.5f * rim, rim, {1.0f, 1.0f, 1.0f, (s.stick.run ? 0.9f : 0.5f) * alpha});
    const Point kc = p.at(knob);
    const float kr = 0.42f * r;
    p.halo({kc.x, kc.y + 0.06f * kr}, kr, 0.25f * kr, {0.0f, 0.0f, 0.0f, 0.30f * alpha});
    p.disc(kc, kr, {1.0f, 1.0f, 1.0f, 0.45f * alpha});
    p.ring(kc, kr - 0.5f * rim, rim, {1.0f, 1.0f, 1.0f, 0.35f * alpha});
}

}  // namespace

void install() {
    State& s = st();
    if (s.installed) return;
    s.installed = true;
    sh::platform::set_touch_hook(&touch_hook);
    set_touch_source(&frame);
    set_touch_skip(&skip_pressed);
    SDL_AddEventWatch(&skip_watch, nullptr);
    SDL_AddEventWatch(&back_watch, nullptr);
    if (Sh_IsMobile() != 0) {
        // The touch screen drives the pointer itself (a tap clicks, a drag scrolls): SDL's mouse made of the first
        // finger would click at every touch.
        SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    } else if (g_PcConfig.touchControls == 1) {
        // Testing on a desktop: the mouse becomes a finger for the controls.
        SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "1");
    }
}

bool controls_enabled() {
    return (Sh_IsMobile() != 0 && g_PcConfig.touchControls != 2) || g_PcConfig.touchControls == 1;
}

bool pointer_pressed() {
    const State& s = st();
    return s.installed && s.mode == Mode::Pointer && s.pointer_finger >= 0 && !s.scrolling && !s.pointer_stop_only;
}

void claim_list_drag(bool flinging) {
    State& s = st();
    s.list_claim_frame = std::max<std::uint32_t>(s.frame, 1);
    s.list_flinging = flinging;
}

ListFinger list_finger() {
    const State& s = st();
    ListFinger out;
    if (!s.installed || s.mode != Mode::Pointer || s.pointer_finger < 0) return out;
    out.down = true;
    out.dragging = s.scrolling;
    out.horizontal = s.scrolling && s.scroll_horizontal;
    out.stop_only = s.pointer_stop_only;
    return out;
}

void draw() {
    State& s = st();
    if (!s.installed || !device_active()) return;
    sh::overlay::Overlay& o = sh::overlay::window_overlay();
    if (!o.frame().valid()) return;
    const Screen scr = screen();
    if (scr.w <= 0.0f || scr.h <= 0.0f) return;
    Painter p(o, scr);
    const float opacity = std::clamp(g_PcConfig.touchOpacity, 0.2f, 1.0f);

    switch (s.mode) {
        case Mode::Controls:
            for (int i = kControlCount - 1; i >= 0; --i) {
                const Control c = static_cast<Control>(i);
                const float a = s.visibility.alpha(c) * opacity;
                if (c == Control::Stick) {
                    if (a > 0.01f) draw_stick(p, scr, a, false);
                    continue;
                }
                if (c == Control::Dpad) {
                    draw_dpad(p, scr, a, s.dpad_bits);
                    continue;
                }
                // The run toggle is lit while it runs.
                const bool pressed = finger_on(c) || (c == Control::Aim && (s.aim_latched || gs::player_aiming())) ||
                                     (c == Control::Run && s.run.on());
                draw_button(p, scr, s.layout[c], control_icon(c), a, pressed);
            }
            break;
        case Mode::Edit: {
            p.rect(p.at({0.0f, 0.0f}), p.at({scr.w, scr.h}), {0.0f, 0.0f, 0.0f, 0.7f});
            const float a = std::max(opacity, 0.5f);
            for (int i = 0; i < kControlCount; ++i) {
                const Control c = static_cast<Control>(i);
                if (!in_use(c)) continue;
                bool held = false;
                for (const Finger& f : s.fingers)
                    if (f.down && f.owner == Owner::Edit && f.control == c) held = true;
                if (c == Control::Stick) {
                    draw_stick(p, scr, a, true);
                } else if (c == Control::Dpad) {
                    draw_dpad(p, scr, a,
                              static_cast<std::uint8_t>(held ? kDpadUp | kDpadRight | kDpadDown | kDpadLeft : 0));
                } else {
                    draw_button(p, scr, s.layout[c], control_icon(c), a, held);
                }
            }
            draw_button(p, scr, kEditDone, "done", 1.0f, false);
            draw_button(p, scr, kEditReset, "reset", 1.0f, false);
            break;
        }
        case Mode::Map:
        case Mode::Pointer:
        case Mode::Idle:
            if (back_shown(s.mode)) draw_button(p, scr, back_place(), "back", opacity, finger_on(Owner::Back));
            if (s.mode != Mode::Map && skip_shown())
                draw_button(p, scr, skip_place(), "skip", opacity, finger_on(Owner::Skip));
            if (delete_shown(s.mode))
                draw_button(p, scr, delete_place(), "delete", opacity, finger_on(Owner::Delete));
            break;
        default:
            break;
    }
}

void draw_over_movie() {
    State& s = st();
    if (!s.installed || !device_active() || !skip_shown()) return;
    sh::overlay::Overlay& o = sh::overlay::window_overlay();
    const Screen scr = screen();
    if (!o.frame().valid() || scr.w <= 0.0f || scr.h <= 0.0f) return;
    Painter p(o, scr);
    draw_button(p, scr, skip_place(), "skip", std::clamp(g_PcConfig.touchOpacity, 0.2f, 1.0f), false);
}

void open_editor() {
    State& s = st();
    if (!device_active()) return;
    refresh_layout();
    drop_owners();
    s.editor = true;
}

bool editor_open() { return st().editor; }

void take_look(double* yaw, double* pitch) {
    State& s = st();
    *yaw = *pitch = 0.0;
    if (s.mode == Mode::Controls) {
        // Degrees per screen width to angle units (4096 a turn); a finger moved up looks up, as the mouse does.
        const double units = std::clamp(g_PcConfig.touchLookSensitivity, 30.0f, 360.0f) * (4096.0 / 360.0);
        *yaw = s.look_x * units;
        *pitch = -s.look_y * units;
    }
    s.look_x = s.look_y = 0.0;
}

void reset_layout() {
    State& s = st();
    s.layout = default_layout();
    save_layout();
}

}  // namespace sh::input::touch

extern "C" int Pc_Touch_Active(void) { return sh::input::touch::controls_enabled() ? 1 : 0; }
