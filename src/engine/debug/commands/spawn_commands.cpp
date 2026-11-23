// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Console commands that put characters into the world: spawn, spawnpreview, turntable.

#include "engine/chara/runtime/chara_spawn.h"
#include "engine/debug/commands/command_groups.h"
#include "engine/debug/commands/command_kit.h"
#include "engine/debug/commands/spawn_roster.h"
#include "engine/game_state/game_state.h"
#include "engine/log/sh_log.h"
#include "ui/menu/pc_debug_menu.h"
#include "ui/menu/pc_spawn_turntable.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace sh::debug::commands {

namespace {

namespace gs = sh::game_state;

constexpr std::size_t kLongestWord = 63;
constexpr int kDefaultMetres = 4;
constexpr int kFarthestMetres = 50;
constexpr int kFanArc = 0xC00;  // several spawns spread over this arc in front of Harry (Q12 angle)

// Characters of the library; the list changes only when a mod set is loaded, so it is rebuilt when its size does.
void complete_characters(std::string_view, std::vector<Candidate>& out) {
    static std::vector<Candidate> cache;
    SpawnRoster& roster = spawn_roster();
    const int count = roster.count();
    if (static_cast<int>(cache.size()) != count) {
        cache.clear();
        for (int i = 0; i < count; ++i) {
            cache.push_back({roster.name(i), roster.spawnable(i) ? "" : "not spawnable"});
        }
    }
    out.insert(out.end(), cache.begin(), cache.end());
}

const char* run_name(const SpawnEntry& entry, int ai) {
    return Pc_Spawn_RunName(Pc_Chara_AiResolve(entry.chara, ai));
}

void list_characters(Context& ctx, int ai) {
    SpawnRoster& roster = spawn_roster();
    const std::vector<SpawnEntry>& entries = roster.entries();
    say(ctx, "%d characters (spawn <name> [state|auto] [distance] [count] [ai=auto|full|fallback|none]):",
        static_cast<int>(entries.size()));
    for (const SpawnEntry& entry : entries) {
        const char* load = !entry.spawnable ? " [not spawnable]" : roster.resident(entry) ? "" : " [loads on demand]";
        say(ctx, " %s%s [%s, AI %s]%s", entry.name.c_str(), load, Pc_Chara_AiKind(entry.chara),
            entry.spawnable ? run_name(entry, ai) : "none", entry.id.rfind("silenthill:", 0) == 0 ? "" : " [mod]");
    }
}

// spawn list | spawn <name> [state|auto] [distance] [count], with ai=<kind> anywhere after the name.
Result<void> run_spawn(Context& ctx, const Args& args) {
    std::vector<std::string> words;
    int ai = PcSpawnAi_Auto;
    for (const std::string& typed : spaced_words(args)) {
        const std::string word = upper(typed).substr(0, kLongestWord);
        if (word.rfind("AI=", 0) == 0) {
            ai = Pc_Spawn_AiFind(word.c_str() + 3);
            if (ai < 0) {
                say(ctx, "unknown AI '%s' (auto, full, fallback, none)", word.c_str() + 3);
                return done();
            }
        } else if (words.size() < 4) {
            words.push_back(word);
        }
    }
    if (words.empty() || words[0] == "LIST") {
        list_characters(ctx, ai);
        return done();
    }

    SpawnRoster& roster = spawn_roster();
    const SpawnEntry* found = roster.find(words[0]);
    if (found == nullptr) {
        say(ctx, "unknown: %s (try 'spawn list')", words[0].c_str());
        return done();
    }
    const SpawnEntry entry = *found;
    if (!entry.spawnable) {
        say(ctx, "%s cannot be spawned (no model support for this character number)", entry.name.c_str());
        return done();
    }

    const bool own_state = words.size() < 2 || words[1] == "AUTO" || words[1][0] == '-';
    const int state = own_state ? entry.state : whole(words[1]);
    const int metres = std::clamp(words.size() >= 3 ? whole(words[2]) : kDefaultMetres, 1, kFarthestMetres);
    const int wanted = std::clamp(words.size() >= 4 ? whole(words[3]) : 1, 1, gs::kNpcSlots);

    if (!roster.make_resident(entry)) {
        say(ctx, "%s: loading its model/animations failed", entry.name.c_str());
        return done();
    }
    int made = 0;
    for (; made < wanted; ++made) {
        // One goes straight ahead; several fan out in front.
        const int turn = wanted == 1 ? 0 : static_cast<int>((static_cast<long long>(made - wanted / 2) * kFanArc) / wanted);
        if (roster.spawn(entry, state, metres, turn, ai) < 0) break;
    }
    if (made == 0) {
        say(ctx, "no free NPC slot (max %d) - killall first", gs::kNpcSlots);
        return done();
    }
    say(ctx, "spawned %d x %s (state %d, %d m, AI %s: %s)", made, entry.name.c_str(), state, metres,
        Pc_Spawn_AiName(ai), run_name(entry, ai));
    SH_DBG("[SPAWN] %s id=%d n=%d st=%d dist=%d model=%d animIdx=%d updFn=%d", entry.name.c_str(), entry.chara, made,
           state, metres, gs::chara_model_loaded(entry.chara) ? 1 : 0, gs::chara_anim_slot(entry.chara),
           Pc_Chara_HasAi(entry.chara));
    return done();
}

// spawnpreview <name> [distance] [state|auto] | keep | off
Result<void> run_spawnpreview(Context& ctx, const Args& args) {
    std::string typed = args.str(0);
    if (args.has(1)) typed += " " + args.str(1);
    if (args.has(2)) typed += " " + (lower(args.str(2)) == "auto" ? std::string("-1") : args.str(2));
    char name[kLongestWord + 1] = {0};
    int metres = kDefaultMetres, state = -1;
    const int read = std::sscanf(upper(typed).c_str(), "%63s %d %d", name, &metres, &state);

    SpawnRoster& roster = spawn_roster();
    if (read < 1 || same_word(name, "off")) {
        roster.preview_cancel();
        say(ctx, "spawn preview off");
    } else if (same_word(name, "keep")) {
        say(ctx, roster.preview_keep() ? "kept the previewed character" : "no preview");
    } else if (const SpawnEntry* entry = roster.find(name); entry == nullptr) {
        say(ctx, "unknown: %s (try 'spawn list')", name);
    } else {
        const int index = static_cast<int>(entry - roster.entries().data());
        say(ctx, roster.preview_show(index, metres, state) ? "previewing %s at %d m" : "cannot preview %s (%d m)", name,
            metres);
    }
    return done();
}

}  // namespace

void register_spawn_commands(Registry& registry) {
    add(registry, "spawn", "enemies", "spawn a character in front of Harry, or list them",
        {arg("character", ArgType::String, false, "name (silenthill:Name, <mod>:Name) or 'list'", {"list"},
             complete_characters),
         arg("state", ArgType::String, true, "initial behaviour state number, or auto", {"auto"}),
         // Words, not numbers: ai=... may stand in any place after the name.
         arg("distance", ArgType::String, true, "metres in front of Harry, 1..50 (default 4)"),
         arg("count", ArgType::String, true, "how many (default 1, fanned over an arc)"),
         arg("ai", ArgType::String, true, "ai=auto|full|fallback|none (default auto)",
             {"ai=auto", "ai=full", "ai=fallback", "ai=none"})},
        run_spawn, {}, "Names are case-insensitive. A character the map did not load is loaded on demand.");

    add(registry, "spawnpreview", "enemies", "show a character without AI to look at it, then keep or remove it",
        {arg("character", ArgType::String, false, "name, 'keep' (spawn it where it stands) or 'off'", {"keep", "off"},
             complete_characters),
         arg("distance", ArgType::Int, true, "metres in front of Harry (default 4)"),
         arg("state", ArgType::String, true, "behaviour state number, or auto", {"auto"})},
        run_spawnpreview);

    add(registry, "turntable", "enemies",
        "show a character on the spawn menu's turntable (open with: debugmenu debug.spawn)",
        {arg("character", ArgType::String, false, "name or 'off'", {"off"}, complete_characters)},
        [](Context& ctx, const Args& args) {
            const std::string name = args.str(0);
            if (lower(name) == "off") {
                Pc_SpawnTurntable_Hide();
                ctx.out.info("turntable off");
            } else {
                Pc_SpawnTurntable_Show(name.c_str());
                ctx.out.info("turntable: " + name + (Pc_DebugMenu_IsOpen() ? "" : " (shown while a debug menu is open)"));
            }
            return done();
        });
}

}  // namespace sh::debug::commands
