// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Console commands of scripted checks (-consolecmd files, the replay harness, tests/smoke): a held pad, a
// fixed random seed, updates that stall as on a loaded machine.

#include "engine/debug/command_services.h"
#include "engine/debug/commands/command_groups.h"
#include "engine/debug/commands/command_kit.h"
#include "engine/debug/commands/scripted_run.h"
#include "engine/loop/frame_view.h"
#include "engine/loop/game_random.h"

#include <SDL3/SDL_timer.h>

#include <cstdio>
#include <cstdlib>

namespace sh::debug {

namespace {

std::uint16_t s_held_buttons = 0;  // pad word bits the script keeps pressed

StallDice& stall_dice() {
    static StallDice dice;
    return dice;
}

}  // namespace

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
                // The 30 Hz gate restarts with the seed: a scripted check then rolls on the same frames every run.
                sh::loop::frame_timing().tick30_accumulator = 0;
            }
            say(ctx, "rngseed: 0x%08x (update %d, step %d)", sh::loop::game_random().seed(),
                static_cast<int>(sh::loop::frame_ticks()), static_cast<int>(sh::loop::frame_delta()));
            return done();
        });
}

}  // namespace commands
}  // namespace sh::debug
