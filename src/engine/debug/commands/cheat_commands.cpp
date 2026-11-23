// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Console commands of the cheats and developer views: god, noclip, notarget, infammo, freecam, fastforward,
// keyframe, collvis, collinfo, collscope.

#include "engine/debug/collision_view.h"
#include "engine/debug/commands/command_groups.h"
#include "engine/debug/commands/command_kit.h"
#include "engine/debug/commands/game_switches.h"

#include <string>

namespace sh::debug::commands {

void register_cheat_commands(Registry& registry) {
    const std::string on_off_help = "on/off; omitted toggles";

    add(registry, "god", "player", "damage immunity for Harry", {arg("state", ArgType::Bool, true, on_off_help)},
        switch_setting([]() -> int& { return g_PcGodMode; },
                       "god mode %s (no damage + health held full; same as debug key 7)"),
        {}, "Also keeps his health full.");

    add(registry, "noclip", "player", "walk through walls (the floor stays)",
        {arg("state", ArgType::Bool, true, on_off_help)}, [](Context& ctx, const Args& args) {
            const bool on = g_DebugNoWallCollision != 0;
            if (args.has(0) && args.boolean(0) == on) {
                ctx.out.info(std::string("noclip already ") + (on ? "ON" : "OFF"));
                return done();
            }
            g_DebugNoWallCollision = on ? 0 : 1;
            say(ctx, "noclip %s", g_DebugNoWallCollision ? "ON" : "OFF");
            return done();
        });

    add(registry, "notarget", "player", "enemies ignore Harry", {arg("state", ArgType::Bool, true, on_off_help)},
        switch_setting([]() -> int& { return g_DebugNoTarget; }, "enemies ignore Harry %s"));

    add(registry, "infammo", "player", "fire without spending ammo", {arg("state", ArgType::Bool, true, on_off_help)},
        [](Context& ctx, const Args& args) {
            g_PcInfiniteAmmo = switched(args, 0, g_PcInfiniteAmmo != 0) ? 1 : 0;
            say(ctx, "infinite ammo %s%s", g_PcInfiniteAmmo ? "ON" : "OFF",
                g_PcInfiniteAmmo ? " (guns you own fire without spending rounds)" : "");
            return done();
        },
        {"infiniteammo"});

    add(registry, "freecam", "camera", "Free camera (mouse look, WASD, Space/C, Shift fast)",
        {opt_switch("on|off; no argument toggles")}, [](Context& ctx, const Args& args) {
            Pc_FreeCam_Set(switched(args, 0, g_DebugCamEnabled != 0) ? 1 : 0);
            say(ctx, "free camera %s%s", g_DebugCamEnabled ? "ON" : "OFF",
                g_DebugCamEnabled ? " - mouse look, WASD, Space/C, Shift fast" : "");
            return done();
        });

    // The same sticky switch as the quick options' row.
    add(registry, "fastforward", "debug", "Fast forward (sticky)", {opt_switch("on|off; no argument toggles")},
        switch_setting([]() -> int& { return g_PcFastForward; }, "fast forward %s"), {"ff"});

    add(registry, "keyframe", "debug", "Animation keyframe viewer",
        {opt_integer("frame", "show this keyframe; no argument toggles the viewer")},
        [](Context& ctx, const Args& args) {
            const int last = g_DebugAnimKfMax > 0 ? g_DebugAnimKfMax - 1 : 0;
            if (args.has(0)) {
                const int frame = whole(args.str(0));
                g_DebugAnimKf = frame < 0 ? 0 : frame;
                g_DebugAnimKfView = 1;  // naming a frame means looking at it
                say(ctx, "keyframe %d (max %d) - kf toggles, , . step", g_DebugAnimKf, last);
            } else {
                g_DebugAnimKfView = !g_DebugAnimKfView;
                say(ctx, "keyframe viewer %s (keyframe %d / %d)", g_DebugAnimKfView ? "ON" : "OFF", g_DebugAnimKf,
                    last);
            }
            return done();
        },
        {"kf"}, "Use , and . to step.");

    add(registry, "collvis", "debug", "Collision view in the 3D world", {opt_switch("on|off; no argument toggles")},
        [](Context& ctx, const Args& args) {
            CollisionView& view = collision_view();
            view.set_enabled(switched(args, 0, view.enabled()));
            say(ctx, "collision view %s", view.enabled() ? "ON" : "OFF");
            return done();
        },
        {"collision_view"},
        "Walls, floors, pillars and characters as the movement pass walks them, and what it touched.");
    add(registry, "collinfo", "debug", "Print the player's last movement pass (needs collvis)", {},
        [](Context& ctx, const Args&) {
            for (const std::string& line : collision_view().describe()) say(ctx, "%s", line.c_str());
            return done();
        });

    add(registry, "collscope", "debug", "Preload collision scope", {opt_switch("on|off; no argument toggles")},
        [](Context& ctx, const Args& args) {
            // Without a word the command only reports.
            if (args.has(0)) g_PcChunkCollisionLocalScope = args.boolean(0) ? 1 : 0;
            say(ctx, "preload collision local-cell scope: %s",
                g_PcChunkCollisionLocalScope ? "ON (vanilla window)" : "OFF (all chunks)");
            return done();
        },
        {}, "on = local-cell (vanilla window), off = all chunks.");
}

}  // namespace sh::debug::commands
