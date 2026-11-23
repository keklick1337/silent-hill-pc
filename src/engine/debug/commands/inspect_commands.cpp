// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Console commands that only show what the game holds: charas, textures, actions, langcheck.

#include "engine/debug/commands/command_groups.h"
#include "engine/debug/commands/command_kit.h"
#include "engine/debug/dbg_trace.h"
#include "engine/game_state/game_state.h"
#include "i18n/localization.h"
#include "input/pc_actions.h"

#include <cstdarg>
#include <cstdio>
#include <string>

namespace sh::debug::commands {

namespace {

namespace gs = sh::game_state;

void say_character(Context& ctx, const char* label, int slot) {
    const int type = gs::chara_id(slot);
    const sh::entities::Vec3 at = gs::chara_position(slot);
    int state = 0, step = 0;
    gs::chara_control(slot, &state, &step);
    say(ctx, "%-6s %-16s id %2d hp %6.1f pos %8.2f %7.2f %8.2f yaw %3d state %d/%d", label, DbgTrace_CharaName(type),
        type, gs::chara_health(slot) / 4096.0f, at.x / 4096.0f, at.y / 4096.0f, at.z / 4096.0f,
        ((gs::chara_rotation(slot).y & 0xFFF) * 360) >> 12, state, step);
}

// The language report prints through a plain printf-style function, so the command lends it its output.
Context* s_report_output = nullptr;

void report_line(const char* format, ...) {
    if (s_report_output == nullptr) return;
    char line[kLineLimit + 1];
    va_list values;
    va_start(values, format);
    std::vsnprintf(line, sizeof(line), format, values);
    va_end(values);
    say_text(*s_report_output, line);
}

}  // namespace

void register_inspect_commands(Registry& registry) {
    add(registry, "charas", "debug", "spawned characters: slot, type, health, position", {},
        [](Context& ctx, const Args&) {
            say_character(ctx, "player", 0);
            int others = 0;
            for (int npc = 0; npc < gs::kNpcSlots; ++npc) {
                if (gs::chara_id(npc + 1) == gs::chara::kNone) continue;
                say_character(ctx, ("npc" + std::to_string(npc)).c_str(), npc + 1);
                ++others;
            }
            say(ctx, "%d character(s) besides Harry, %d slots", others, gs::kNpcSlots);
            return done();
        },
        {"chars"});

    add(registry, "textures", "debug", "textures uploaded to VRAM",
        {arg("filter", ArgType::String, true, "part of a name")}, [](Context& ctx, const Args& args) {
            const std::string filter = upper(args.joined(0));
            const int total = DbgTrace_TimCount();
            int shown = 0;
            for (int i = 0; i < total; ++i) {
                char line[160];
                if (!DbgTrace_TimGet(i, line, sizeof(line)) || !contains_word(line, filter)) continue;
                say_text(ctx, line);
                ++shown;
            }
            say(ctx, "%d of %d TIM(s) resident in VRAM since boot%s%s", shown, total, filter.empty() ? "" : ", filter ",
                filter.c_str());
            return done();
        },
        {"tex"});

    // Input actions and what they are bound to.
    add(registry, "actions", "debug", "List the input actions", {}, [](Context& ctx, const Args&) {
        char line[96];
        for (int i = 0; PcActions_Describe(i, line, static_cast<int>(sizeof(line))); ++i) say_text(ctx, line);
        return done();
    });

    // The languages found, the text of the one in force and the fonts, in one place.
    add(registry, "langcheck", "debug", "Check every text layer of the current language", {},
        [](Context& ctx, const Args&) {
            s_report_output = &ctx;
            sh::i18n::describe(report_line);
            s_report_output = nullptr;
            return done();
        });
}

}  // namespace sh::debug::commands
