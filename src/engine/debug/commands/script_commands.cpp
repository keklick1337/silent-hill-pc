// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Console commands of scripted checks (-consolecmd files, the replay harness, tests/smoke): a held pad, a
// fixed random seed, updates that stall as on a loaded machine, a mouse and a right stick that turn the view
// at a steady rate, and a record of the camera of every presented frame.

#include "engine/debug/command_services.h"
#include "engine/debug/commands/command_groups.h"
#include "engine/debug/commands/command_kit.h"
#include "engine/debug/commands/scripted_run.h"
#include "engine/game_state/session.h"
#include "engine/loop/frame_clock.h"
#include "engine/loop/frame_view.h"
#include "engine/loop/game_random.h"
#include "engine/log/sh_log.h"
#include "platform/sh_platform.h"

#include <SDL3/SDL_timer.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace sh::debug {

namespace {

std::uint16_t s_held_buttons = 0;  // pad word bits the script keeps pressed

StallDice& stall_dice() {
    static StallDice dice;
    return dice;
}

// `lookinput`: mouse counts a second, and the right stick's deflection (-127..127).
struct ScriptedLook {
    double mouse_rate[2] = {0.0, 0.0};
    double mouse_owed[2] = {0.0, 0.0};
    std::uint64_t polled_at = 0;
    int stick[2] = {0, 0};
};
ScriptedLook s_look;

// `looktrace`: kept in memory while it runs, because a log line per frame would disturb the frame times it measures.
struct LookSample {
    double ms;
    bool between;
    double yaw, pitch;
    double counts[2];
    double eye[3];
};
bool s_trace_on = false;
std::vector<LookSample> s_trace;
std::uint64_t s_trace_start = 0;
double s_counts_sent[2] = {0.0, 0.0};

}  // namespace

void script_look_poll() {
    const std::uint64_t now = SDL_GetPerformanceCounter();
    if (s_look.mouse_rate[0] == 0.0 && s_look.mouse_rate[1] == 0.0) {
        s_look.polled_at = 0;
        return;
    }
    if (s_look.polled_at != 0) {
        const double passed =
            static_cast<double>(now - s_look.polled_at) / static_cast<double>(SDL_GetPerformanceFrequency());
        // A mouse reports whole counts: the fraction waits for the next poll.
        float whole[2];
        for (int i = 0; i < 2; ++i) {
            s_look.mouse_owed[i] += s_look.mouse_rate[i] * passed;
            whole[i] = static_cast<float>(std::trunc(s_look.mouse_owed[i]));
            s_look.mouse_owed[i] -= whole[i];
            s_counts_sent[i] += whole[i];
        }
        Sh_InjectRelativeMouse(whole[0], whole[1]);
    }
    s_look.polled_at = now;
}

bool script_stick_held() { return s_look.stick[0] != 0 || s_look.stick[1] != 0; }

void script_stick_apply(std::uint8_t* right_x, std::uint8_t* right_y) {
    *right_x = static_cast<std::uint8_t>(128 + s_look.stick[0]);
    *right_y = static_cast<std::uint8_t>(128 + s_look.stick[1]);
}

bool look_trace_on() { return s_trace_on; }

void look_trace_frame(bool between, double yaw_deg, double pitch_deg, const double eye_metres[3]) {
    if (!s_trace_on || s_trace.size() >= 100000) return;
    const double ms = static_cast<double>(SDL_GetPerformanceCounter() - s_trace_start) * 1000.0 /
                      static_cast<double>(SDL_GetPerformanceFrequency());
    s_trace.push_back({ms, between, yaw_deg, pitch_deg, {s_counts_sent[0], s_counts_sent[1]},
                       {eye_metres[0], eye_metres[1], eye_metres[2]}});
}

bool script_pad_held() { return s_held_buttons != 0; }

void script_pad_apply(std::uint16_t* buttons_up) {
    *buttons_up = static_cast<std::uint16_t>(*buttons_up & ~s_held_buttons);
}

void stall_test_frame() {
    if (const std::uint32_t ms = stall_dice().roll(); ms != 0) SDL_Delay(ms);
}

namespace commands {

void register_script_commands(Registry& registry) {
    add(registry, "pad", "debug", "hold pad buttons down for scripted checks (names as in replay routes; - releases)",
        {arg("buttons", ArgType::String, true, "button names, or -")}, [](Context& ctx, const Args& args) {
            const Result<std::uint16_t> held = parse_pad_buttons(args.joined(0));
            if (!held) {
                say(ctx, "pad: no button named %s", held.error().c_str());
                return done();
            }
            s_held_buttons = held.value();
            say(ctx, "pad: holding 0x%04x", s_held_buttons);
            return done();
        });

    add(registry, "lookinput", "debug",
        "scripted look input: mouse <x> <y> moves the mouse by so many counts a second, stick <x> <y> holds the "
        "right stick (-127..127), off stops both",
        {arg("device", ArgType::Enum, false, "what to drive", {"mouse", "stick", "off"}),
         opt_number("x", "to the right"), opt_number("y", "down")},
        [](Context& ctx, const Args& args) {
            const std::string& device = args.str(0);
            if (device == "mouse") {
                s_look.mouse_rate[0] = args.number(1);
                s_look.mouse_rate[1] = args.number(2);
                s_look.mouse_owed[0] = s_look.mouse_owed[1] = 0.0;
            } else if (device == "stick") {
                s_look.stick[0] = std::clamp(static_cast<int>(args.number(1)), -127, 127);
                s_look.stick[1] = std::clamp(static_cast<int>(args.number(2)), -127, 127);
            } else {
                s_look = ScriptedLook{};
            }
            say(ctx, "lookinput: mouse %.1f %.1f counts/s, stick %d %d", s_look.mouse_rate[0], s_look.mouse_rate[1],
                s_look.stick[0], s_look.stick[1]);
            return done();
        });

    add(registry, "looktrace", "debug",
        "record the camera's heading, pitch and place on every presented frame; off writes the records to the log",
        {arg("state", ArgType::Enum, false, "start or stop", {"on", "off"})}, [](Context& ctx, const Args& args) {
            if (args.str(0) == "on") {
                s_trace.clear();
                s_trace_on = true;
                s_trace_start = SDL_GetPerformanceCounter();
                s_counts_sent[0] = s_counts_sent[1] = 0.0;
                return done();
            }
            s_trace_on = false;
            // frame, milliseconds when its view was settled, update or between, yaw and pitch (degrees), mouse counts
            // sent, the eye (metres)
            for (std::size_t i = 0; i < s_trace.size(); ++i) {
                const LookSample& r = s_trace[i];
                SH_LOG("[LOOKTRACE] %zu %.3f %c %.5f %.5f %.0f %.0f %.5f %.5f %.5f", i, r.ms, r.between ? 'b' : 'u',
                       r.yaw, r.pitch, r.counts[0], r.counts[1], r.eye[0], r.eye[1], r.eye[2]);
            }
            say(ctx, "looktrace: %zu frames", s_trace.size());
            s_trace.clear();
            return done();
        });

    add(registry, "stall", "debug",
        "sleep 40-100 ms on a share of the updates, as a loaded machine stalls them (0 stops)",
        {arg("percent", ArgType::String, true, "share of the updates, 0..100"),
         arg("seed", ArgType::String, true, "seed of the choice")},
        [](Context& ctx, const Args& args) {
            int percent = 0;
            unsigned seed = 1;
            std::sscanf(args.joined(0).c_str(), "%d %u", &percent, &seed);
            stall_dice().configure(percent, seed);
            say(ctx, "stall: %d%% of the updates sleep 40-100 ms", stall_dice().percent());
            return done();
        });

    add(registry, "rngseed", "debug", "show or set the game's random seed",
        {arg("seed", ArgType::String, true, "new seed")}, [](Context& ctx, const Args& args) {
            if (args.has(0) && !args.str(0).empty()) {
                sh::loop::game_random().set_seed(
                    static_cast<std::uint32_t>(std::strtoul(args.str(0).c_str(), nullptr, 0)));
                // The 30 Hz gates restart with the seed: a scripted check then rolls on the same frames every run.
                // The update under way had its gate taken from the old phase (the weather steps and draws on it),
                // so it is taken again as the first update of the new one.
                sh::loop::FrameTiming& timing = sh::loop::frame_timing();
                timing.tick30_accumulator = 0;
                timing.tick30 = sh::loop::tick30_step(timing.tick30_accumulator, timing.delta) ? 1 : 0;
                sh::game::session().player.tick30_forward = 0;
                sh::game::session().player.tick30_attack = 0;
            }
            say(ctx, "rngseed: 0x%08x (update %d, step %d)", sh::loop::game_random().seed(),
                static_cast<int>(sh::loop::frame_ticks()), static_cast<int>(sh::loop::frame_delta()));
            return done();
        });
}

}  // namespace commands
}  // namespace sh::debug
