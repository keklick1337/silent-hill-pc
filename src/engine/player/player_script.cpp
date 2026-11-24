// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/player/player_script.h"

#include "engine/game_state/session.h"
#include "engine/input/input_primitives.h"
#include "engine/input/touch_prompts.h"
#include "engine/log/sh_log.h"
#include "engine/player/player.h"

#include <cstdio>
#include <cstring>
#include <iterator>
#include <string_view>
#include <vector>

namespace sh::game {

namespace {

constexpr int kHarry = 1;  // Harry's character id (character.json "id")

constexpr const char* kNames[] = {
    "entity.harry",       "entity.set_harry",        "entity.harry_weapon_clip", "entity.weapon_number",
    "entity.clip_link_set", "input.control",         "input.move_stick",         "input.set_control",
    "input.controls_clear", "input.pad_mask",        "input.pad_direction",      "input.pad_button",
    "input.stick_zone",   "input.tick30",            "input.player_flag",        "input.set_player_flag",
    "input.hyper_blaster_mode", "input.touch_prompt",
};

constexpr int kAnswerMax = 8;
constexpr int kNotMine = -2;

// Pad bits (the game's pad word): buttons, the d-pad-or-stick directions (24..27) and the stick alone (16..19).
namespace pad {
constexpr std::uint32_t kSelect = 1u << 0, kL3 = 1u << 1, kR3 = 1u << 2, kStart = 1u << 3;
constexpr std::uint32_t kL2 = 1u << 8, kR2 = 1u << 9, kL1 = 1u << 10, kR1 = 1u << 11;
constexpr std::uint32_t kTriangle = 1u << 12, kCircle = 1u << 13, kCross = 1u << 14, kSquare = 1u << 15;
constexpr std::uint32_t kStickUp = 1u << 16, kStickRight = 1u << 17, kStickDown = 1u << 18, kStickLeft = 1u << 19;
constexpr std::uint32_t kUp = 1u << 24, kRight = 1u << 25, kDown = 1u << 26, kLeft = 1u << 27;
}  // namespace pad

struct Args {
    int argc;
    const ShScriptArg* argv;
    long long integer(int i, long long fallback = 0) const {
        return i < argc && (argv[i].kind == 1 || argv[i].kind == 3) ? argv[i].i : fallback;
    }
    const char* text(int i) const { return i < argc && argv[i].kind == 2 ? argv[i].s : nullptr; }
    std::string_view name(int i) const { return text(i) != nullptr ? text(i) : ""; }
    const char* shown(int i) const { return text(i) != nullptr ? text(i) : "?"; }
};

int fail(char* err, int cap, const char* msg, const char* what = "") {
    std::snprintf(err, static_cast<std::size_t>(cap), msg, what);
    return -1;
}

PlayerControls& controls() { return session().player.controls; }

// The 16-bit controls by name (the controller's flags; the upper body clears and refills some of them).
std::uint16_t* control(std::string_view n) {
    PlayerControls& c = controls();
    if (n == "forward") return &c.forward;
    if (n == "back") return &c.back;
    if (n == "turn_left") return &c.turn_left;
    if (n == "turn_right") return &c.turn_right;
    if (n == "step_left") return &c.step_left_hold;
    if (n == "step_right") return &c.step_right_hold;
    if (n == "step_left_tap") return &c.step_left_tap;
    if (n == "step_right_tap") return &c.step_right_tap;
    if (n == "run") return &c.run;
    if (n == "aim") return &c.aim;
    if (n == "attack") return &c.attack;
    if (n == "shoot") return &c.shoot;
    if (n == "hold_attack") return &c.hold_attack;
    return nullptr;
}

std::int32_t* control_word(std::string_view n) {
    if (n == "has_move") return &controls().has_move;
    if (n == "has_action") return &controls().has_action;
    return nullptr;
}

// The pad word of a history: 0 held, 1 clicked, 2 released, 3 pulsed.
bool pad_word(const PadState& p, long long mode, std::uint32_t* bits) {
    switch (mode) {
        case 0: *bits = p.held; return true;
        case 1: *bits = p.clicked; return true;
        case 2: *bits = p.released; return true;
        case 3: *bits = p.pulsed; return true;
        default: return false;
    }
}

bool bind_bits(std::string_view action, std::uint32_t* bits) {
    const PadBinds& b = session().game.settings.binds;
    if (action == "enter") *bits = b.enter;
    else if (action == "cancel") *bits = b.cancel;
    else if (action == "skip") *bits = b.skip;
    else if (action == "action") *bits = b.action;
    else if (action == "aim") *bits = b.aim;
    else if (action == "light") *bits = b.light;
    else if (action == "run") *bits = b.run;
    else if (action == "view") *bits = b.view;
    else if (action == "stepLeft") *bits = b.step_left;
    else if (action == "stepRight") *bits = b.step_right;
    else if (action == "pause") *bits = b.pause;
    else if (action == "item") *bits = b.item;
    else if (action == "map") *bits = b.map;
    else if (action == "option") *bits = b.option;
    else return false;
    return true;
}

bool direction_bits(std::string_view dir, std::uint32_t* bits) {
    if (dir == "up") *bits = pad::kUp;
    else if (dir == "right") *bits = pad::kRight;
    else if (dir == "down") *bits = pad::kDown;
    else if (dir == "left") *bits = pad::kLeft;
    else if (dir == "stick_up") *bits = pad::kStickUp;
    else if (dir == "stick_right") *bits = pad::kStickRight;
    else if (dir == "stick_down") *bits = pad::kStickDown;
    else if (dir == "stick_left") *bits = pad::kStickLeft;
    else return false;
    return true;
}

bool button_bits(std::string_view button, std::uint32_t* bits) {
    if (button == "select") *bits = pad::kSelect;
    else if (button == "l3") *bits = pad::kL3;
    else if (button == "r3") *bits = pad::kR3;
    else if (button == "start") *bits = pad::kStart;
    else if (button == "l2") *bits = pad::kL2;
    else if (button == "r2") *bits = pad::kR2;
    else if (button == "l1") *bits = pad::kL1;
    else if (button == "r1") *bits = pad::kR1;
    else if (button == "triangle") *bits = pad::kTriangle;
    else if (button == "circle") *bits = pad::kCircle;
    else if (button == "cross") *bits = pad::kCross;
    else if (button == "square") *bits = pad::kSquare;
    else return false;
    return true;
}

PlayerScriptHost* s_host = nullptr;
int (*s_delta_time)(void) = nullptr;
decltype(ShScriptGameApi::named_call) s_next_call = nullptr;
decltype(ShScriptGameApi::named_list) s_next_list = nullptr;

int entity_call(std::string_view fn, const Args& a, long long* out, char* err, int err_cap) {
    if (fn == "harry" || fn == "set_harry") {
        const PlayerVarName* v = player_var_find(a.name(0));
        if (v == nullptr) {
            return fail(err, err_cap, "unknown Harry variable '%s'", a.shown(0));
        }
        if (fn == "harry") {
            out[0] = player().get(v->var);
            return 1;
        }
        if (!v->writable) {
            return fail(err, err_cap, "Harry variable '%s' is read-only", v->name);
        }
        player().set(v->var, a.integer(1));
        return 0;
    }
    if (fn == "weapon_number") {
        // weapon_number(name) -> the run-time number of a weapon; the packed weapon_attack is number + 10 * input
        // (0 tap, 1 hold, 2 multitap), -1 empty-handed.
        const int n = s_host->weapon_number(a.text(0));
        if (n < 0) {
            return fail(err, err_cap, "unknown weapon '%s'", a.shown(0));
        }
        out[0] = n;
        return 1;
    }
    if (fn == "harry_weapon_clip") {
        // harry_weapon_clip(i) -> first keyframe, the keyframe the clip is ready at (aim up, swing done). A clip past
        // the table (a map's own clip in use) has no keyframes: 0, 0.
        const long long i = a.integer(0, -1);
        const auto& records = session().player.weapon_keyframes;
        const bool known = i >= 0 && i < static_cast<long long>(std::size(records));
        out[0] = known ? records[i].start : 0;
        out[1] = known ? records[i].ready : 0;
        return 2;
    }
    // clip_link_set(slot, status, end_keyframe, link_status): the clip entry ends at `end_keyframe` and hands over to
    // `link_status` (the blend clip carries the legs into the next cycle).
    const char* error = nullptr;
    if (!s_host->set_clip_link(static_cast<int>(a.integer(0, -1)), a.integer(1, -1), static_cast<int>(a.integer(2)),
                               static_cast<int>(a.integer(3)), &error)) {
        return fail(err, err_cap, "%s", error != nullptr ? error : "clip_link_set failed");
    }
    return 0;
}

int input_call(std::string_view fn, const Args& a, long long* out, char* err, int err_cap) {
    const PadState& pad0 = session().game.pads[0];
    if (fn == "move_stick") {
        // move_stick() -> x, y (-128..127, forward is negative y), up held, down held (stick or d-pad).
        out[0] = pad0.sticks.left_x;
        out[1] = pad0.sticks.left_y;
        out[2] = (pad0.held & pad::kUp) != 0 ? 1 : 0;
        out[3] = (pad0.held & pad::kDown) != 0 ? 1 : 0;
        return 4;
    }
    // The raw pad queries return the masked bits (not booleans): `~= 0` in Lua is the original's test.
    if (fn == "pad_mask" || fn == "pad_direction" || fn == "pad_button") {
        const long long which = fn == "pad_button" ? a.integer(2, 0) : 0;
        if (which != 0 && which != 1) {
            return fail(err, err_cap, "unknown pad");
        }
        std::uint32_t word = 0;
        if (!pad_word(session().game.pads[which], a.integer(1), &word)) {
            return fail(err, err_cap, "unknown mode");
        }
        std::uint32_t bits = 0;
        if (fn == "pad_mask" && !bind_bits(a.name(0), &bits)) {
            return fail(err, err_cap, "unknown action '%s'", a.shown(0));
        }
        if (fn == "pad_direction" && !direction_bits(a.name(0), &bits)) {
            return fail(err, err_cap, "unknown direction '%s'", a.shown(0));
        }
        if (fn == "pad_button" && !button_bits(a.name(0), &bits)) {
            return fail(err, err_cap, "unknown button '%s'", a.shown(0));
        }
        out[0] = static_cast<long long>(word & bits);
        return 1;
    }
    if (fn == "stick_zone") {
        // The analog branch of the controller: outside the zone, the turn excess per side, the forward/back zones.
        const int x = pad0.sticks.left_x;
        const int y = pad0.sticks.left_y;
        out[0] = sh::input::stick_outside_deadzone(x, y) ? 1 : 0;
        out[1] = sh::input::axis_negative_excess(x);
        out[2] = sh::input::axis_positive_excess(x);
        out[3] = sh::input::axis_negative(y) ? 1 : 0;
        out[4] = sh::input::axis_positive(y) ? 1 : 0;
        return 5;
    }
    if (fn == "tick30") {
        // The controller's aging gates, one accumulator per site.
        const std::string_view site = a.name(0);
        std::int32_t* acc = site == "forward" ? &session().player.tick30_forward
                            : site == "attack" ? &session().player.tick30_attack
                                               : nullptr;
        if (acc == nullptr) {
            return fail(err, err_cap, "unknown site '%s'", a.shown(0));
        }
        out[0] = sh::input::tick30_ready(*acc, s_delta_time != nullptr ? s_delta_time() : 0) ? 1 : 0;
        return 1;
    }
    if (fn == "player_flag" || fn == "set_player_flag") {
        const std::string_view n = a.name(0);
        const bool set = fn == "set_player_flag";
        const long long v = a.integer(1);
        if (std::uint16_t* c = control(n)) {
            set ? void(*c = static_cast<std::uint16_t>(v)) : void(out[0] = *c);
            return set ? 0 : 1;
        }
        if (std::int32_t* w = control_word(n)) {
            set ? void(*w = static_cast<std::int32_t>(v)) : void(out[0] = *w);
            return set ? 0 : 1;
        }
        if (n == "rock_drill_direction") {
            if (set) {
                player().set(PlayerVar::RockDrillDirection, static_cast<std::int8_t>(v));
                return 0;
            }
            out[0] = static_cast<std::int8_t>(player().get(PlayerVar::RockDrillDirection));
            return 1;
        }
        return fail(err, err_cap, "unknown player flag '%s'", a.shown(0));
    }
    if (fn == "hyper_blaster_mode") {
        out[0] = s_host->hyper_blaster_mode();
        return 1;
    }
    if (fn == "touch_prompt") {
        // touch_prompt(control[, icon]): only the touch screen's controls look at it (engine/input/touch_prompts.h).
        sh::input::note_touch_prompt(a.name(0), a.name(1));
        return 0;
    }
    if (fn == "controls_clear") {
        // No control is pressed for the rest of the tick (the aim lock takes Harry over).
        controls() = PlayerControls{};
        return 0;
    }
    if (fn == "set_control") {
        // set_control(name, v): the control's history (0: not pressed; hold_attack 0x1F: held since long).
        std::uint16_t* c = control(a.name(0));
        if (c == nullptr) {
            return fail(err, err_cap, "unknown control '%s'", a.shown(0));
        }
        *c = static_cast<std::uint16_t>(a.integer(1));
        return 0;
    }
    // control(name): the control's history (non-zero while it counts as pressed); the turns are how far the turn is
    // pushed (64 full); the layout's aim and action buttons as the pad has them.
    const std::string_view n = a.name(0);
    const PadBinds& binds = session().game.settings.binds;
    if (const std::uint16_t* c = control(n)) {
        out[0] = *c;
    } else if (const std::int32_t* w = control_word(n)) {
        out[0] = *w;
    } else if (n == "aim_button") {
        out[0] = (pad0.held & binds.aim) != 0;
    } else if (n == "action_held") {
        out[0] = (pad0.held & binds.action) != 0;
    } else if (n == "action_pressed") {
        out[0] = (pad0.clicked & binds.action) != 0;
    } else {
        return fail(err, err_cap, "unknown control '%s'", a.shown(0));
    }
    return 1;
}

int chained_call(const char* name, int argc, const ShScriptArg* argv, long long* out, int out_cap, char* err,
                 int err_cap) {
    if (name != nullptr && s_host != nullptr && out_cap >= kAnswerMax) {
        for (const char* own : kNames) {
            if (std::strcmp(own, name) != 0) {
                continue;
            }
            const Args a{argc, argv};
            const int n = std::strncmp(name, "input.", 6) == 0 ? input_call(name + 6, a, out, err, err_cap)
                                                                 : entity_call(name + 7, a, out, err, err_cap);
            if (n != kNotMine) {
                return n;
            }
        }
    }
    return s_next_call != nullptr ? s_next_call(name, argc, argv, out, out_cap, err, err_cap)
                                  : fail(err, err_cap, "not available");
}

const char* const* chained_list() {
    static std::vector<const char*> names;
    if (names.empty()) {
        if (s_next_list != nullptr) {
            for (const char* const* n = s_next_list(); n != nullptr && *n != nullptr; ++n) {
                names.push_back(*n);
            }
        }
        names.insert(names.end(), std::begin(kNames), std::end(kNames));
        names.push_back(nullptr);
    }
    return names.data();
}

// One function of Harry's script; `cannot` says what Harry cannot do without it (logged once per part).
long long run(const char* fn, const char* part, const char* cannot, bool* reported) {
    long long result = 0;
    if (ShScript_CharaBehaviorHas(kHarry, "Harry") == 0) {
        if (!*reported) {
            *reported = true;
            std::fprintf(stderr, "[%s] chara/Harry/behavior.lua is missing: Harry cannot %s\n", part, cannot);
        }
        return 0;
    }
    if (std::strcmp(fn, "update") == 0) {
        ShScript_CharaBehaviorUpdate(kHarry, "Harry", 0);
    } else {
        ShScript_CharaBehaviorCall(kHarry, fn, 0, 0, &result);
    }
    return result;
}

bool s_move_reported = false;
bool s_combat_reported = false;
bool s_damage_reported = false;

long long move(const char* fn) { return run(fn, "harry-move", "move", &s_move_reported); }
long long fight(const char* fn) { return run(fn, "harry-combat", "fight", &s_combat_reported); }
long long damage(const char* fn) { return run(fn, "harry-rest", "take damage", &s_damage_reported); }

}  // namespace

void bind_player_script(ShScriptGameApi& api, PlayerScriptHost& host) {
    s_host = &host;
    s_delta_time = api.delta_time_get;
    s_next_call = api.named_call;
    s_next_list = api.named_list;
    api.named_call = chained_call;
    api.named_list = chained_list;
}

namespace harry_script {

// There is no other controller, so a missing behaviour or a failed call is an error (the controls keep last tick's
// values: the script writes them only after computing all of them). The first 20 failures are logged one by one,
// then one line per 600 with the running count.
void controller() {
    static long long failures = 0;
    const char* what = nullptr;
    if (ShScript_CharaBehaviorHas(kHarry, "Harry") == 0) {
        what = "no Harry behaviour (chara/Harry/behavior.lua), Harry ignores the pad";
    } else {
        long long r = 0;
        if (ShScript_CharaBehaviorCall(kHarry, "controller", 0, 0, &r)) {
            return;
        }
        what = "chara/Harry/controller.lua controller() failed, input flags not updated";
    }
    ++failures;
    if (failures <= 20 || failures % 600 == 0) {
        SH_LOG_ERROR(INPUT, "[C1LUA] %s (failure %lld)", what, failures);
    }
}

void legs() { move("update"); }
void footsteps() { move("footsteps"); }
void upper_body() { fight("upper_body"); }
void combat_update() { fight("combat_update"); }
void receive_damage() { damage("receive_damage"); }
void before_tick() { damage("logic_pre"); }
bool idle() { return static_cast<int>(damage("idle")) != 0; }
bool state() { return static_cast<int>(damage("state_update")) != 0; }
void after_tick() { damage("logic_post"); }

}  // namespace harry_script

}  // namespace sh::game
