// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Console commands of loads and screen transitions: the pace of loads and of the loading screen, the door
// fade, the framebuffer feedback behind the trail and the dream overlays, and the weather's rate.

#include "engine/config/pc_config.h"
#include "engine/debug/commands/command_groups.h"
#include "engine/debug/commands/command_kit.h"
#include "engine/debug/commands/game_switches.h"
#include "render/gpu/render_state.h"

#include <cstdio>
#include <string>
#include <vector>

namespace sh::debug::commands {

void register_loading_commands(Registry& registry) {
    // Off paces blocking loads like the console's drive: one small step a vblank, seconds of black at a door.
    add(registry, "fastload", "graphics", "Fast blocking loads", {opt_switch("on|off; no argument toggles")},
        switch_setting([]() -> int& { return g_PcFastBlockingLoads; }, "fast blocking loads: %s", "ON (disk speed)",
                       "OFF (PSX CD pace)"),
        {}, "on = disk speed, off = PS1 CD pace.");

    // The running Harry of the loading screen steps every N vblanks, each step at most 1/30 s: past two
    // vblanks his run slows to the console's jog. The load itself is never slowed.
    add(registry, "loadpace", "graphics", "Loading screen pace",
        {opt_integer("vblanks", "0..12; step every N vblanks, <=1 = full speed")}, [](Context& ctx, const Args& args) {
            if (args.has(0)) Pc_LoadScreenPaceVblanksSet(whole(args.str(0)));
            if (Pc_LoadScreenPaceVblanks() > 12) Pc_LoadScreenPaceVblanksSet(12);
            const int pace = Pc_LoadScreenPaceVblanks();
            if (pace <= 1) {
                say(ctx, "loading screen pace: every frame, full speed");
            } else {
                say(ctx, "loading screen pace: a step every %d vblanks (%.0f fps), Harry at %.0f%% speed", pace,
                    60.0 / pace, 100.0 * (pace > 2 ? 2.0 : static_cast<double>(pace)) / pace);
            }
            return done();
        });
    // How long the loading screen stays up at least; the load finishes underneath. Kept in the config.
    add(registry, "loadmin", "graphics", "Loading screen minimum duration", {opt_number("seconds", "0..10")},
        [](Context& ctx, const Args& args) {
            if (args.has(0)) {
                float seconds = static_cast<float>(real(args.str(0)));
                if (seconds < 0.0f) seconds = 0.0f;
                if (seconds > 10.0f) seconds = 10.0f;
                g_PcConfig.loadScreenMin = seconds;
                Pc_LoadScreenMinVblanksSet(static_cast<int>(seconds * 60.0f + 0.5f));
                char text[16];
                std::snprintf(text, sizeof(text), "%.1f", static_cast<double>(seconds));
                PcConfig_SaveKeyValue("load_screen_min", text);
            }
            say(ctx, "loading screen minimum: %.1f s", Pc_LoadScreenMinVblanks() / 60.0);
            return done();
        });
    add(registry, "txnhold", "graphics", "Load-screen hold", {opt_integer("frames", "retail constant is 60")},
        [](Context& ctx, const Args& args) {
            if (args.has(0)) Pc_LoadScreenHoldFramesSet(whole(args.str(0)));
            const int frames = Pc_LoadScreenHoldFrames();
            say(ctx, "load-screen minimum: %d frames (%.2f s); retail constant is 60", frames,
                static_cast<double>(static_cast<float>(frames) / 60.0f));
            return done();
        });
    // On the console the fade lasted as long as the disc read, so there is no constant to match.
    add(registry, "txnfade", "graphics", "Door out-fade duration", {opt_number("seconds", "minimum 0.05")},
        [](Context& ctx, const Args& args) {
            if (args.has(0)) {
                float seconds = static_cast<float>(real(args.str(0)));
                if (seconds < 0.05f) seconds = 0.05f;
                g_PcTxnFadeTimestep = static_cast<int>(4096.0f / seconds);
            }
            say(ctx, "door out-fade: %.2f s", static_cast<double>(4096.0f / static_cast<float>(g_PcTxnFadeTimestep)));
            return done();
        });

    // ---- framebuffer feedback: the loading trail, the door fade, the dream overlays ----
    add(registry, "fbexact", "graphics", "Loading trail store", {opt_switch("on|off; no argument toggles")},
        switch_setting([]() -> int& { return sh::render::feedback_control().exact; }, "loading trail: %s",
                       "sharp (pixel-exact store)", "soft (old filtered store)"),
        {}, "on = sharp pixel-exact, off = soft filtered.");
    add(registry, "fbscreen", "graphics", "Framebuffer feedback resolution", {opt_switch("on|off; no argument toggles")},
        switch_setting([]() -> int& { return sh::render::feedback_control().screen; }, "framebuffer feedback: %s",
                       "render resolution", "320x224 VRAM store"),
        {}, "on = render resolution, off = 320x224 VRAM store.");
    // The loop's gain: 1 is the hardware's (the trail loses one 5-bit level a frame), lower fades faster. The
    // second number is for readers that blend (the dream overlays), whose own 50/50 mix already decays.
    add(registry, "fbdamp", "graphics", "Framebuffer feedback damping",
        {opt_number("opaque", "loading trail"), opt_number("blended", "dream overlays")},
        [](Context& ctx, const Args& args) {
            const std::vector<std::string> words = spaced_words(args);
            sh::render::FeedbackControl& feedback = sh::render::feedback_control();
            if (words.size() >= 1) feedback.damp = static_cast<float>(real(words[0]));
            if (words.size() >= 2) feedback.damp_blend = static_cast<float>(real(words[1]));
            say(ctx, "framebuffer feedback damp: %.4f opaque (loading trail), %.4f blended (dream overlays)",
                static_cast<double>(feedback.damp), static_cast<double>(feedback.damp_blend));
            return done();
        });

    add(registry, "weatherhz", "graphics", "Weather display rate",
        {arg("hz", ArgType::Enum, true, "30 = only the steps, 60 = smooth between them", {"30", "60"})},
        [](Context& ctx, const Args& args) {
            if (args.has(0)) g_PcConfig.weatherSimHz = args.str(0) == "30" ? 30 : 60;
            const bool original = g_PcConfig.weatherSimHz == 30;
            say(ctx, "Weather (rain/snow) steps 30 times a second; shown %s",
                original ? "at its steps only (weather_sim_hz 30)" : "moving on every frame (weather_sim_hz 60)");
            return done();
        });
}

}  // namespace sh::debug::commands
