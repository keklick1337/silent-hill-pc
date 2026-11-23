// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Console commands that tune how the picture and the interface are shown: pixel and CRT aspect, brightness,
// text and crosshair size, the dream blur, and how the inventory lays out its items.

#include "engine/config/pc_config.h"
#include "engine/debug/commands/command_groups.h"
#include "engine/debug/commands/command_kit.h"
#include "engine/debug/commands/game_switches.h"
#include "render/gpu/render_state.h"

#include <cstdio>
#include <string>

namespace sh::debug::commands {

namespace {

// A size in percent kept in the config as a whole number: "<command> [percent]".
RunFn percent_command(float s_PcConfig::* field, float lowest, float highest, const char* key, const char* line) {
    return [=](Context& ctx, const Args& args) {
        if (args.has(0)) {
            float percent = static_cast<float>(real(args.str(0)));
            if (percent < lowest) percent = lowest;
            if (percent > highest) percent = highest;
            g_PcConfig.*field = percent;
            PcConfig_SaveKeyValue(key, std::to_string(static_cast<int>(percent + 0.5f)).c_str());
        }
        say(ctx, line, static_cast<int>(g_PcConfig.*field + 0.5f));
        return done();
    };
}

// An inventory offset or share that is taken only inside its range: "<command> [value]".
RunFn ranged_command(Setting<int> value, int lowest, int highest, const char* line) {
    return [=](Context& ctx, const Args& args) {
        const int typed = whole(args.str(0));
        if (typed >= lowest && typed <= highest) value() = typed;
        say(ctx, line, value());
        return done();
    };
}

}  // namespace

void register_display_commands(Registry& registry) {
    // The correct value is set at boot (320x224 shown as 4:3 is 15/14); this is for comparing live.
    add(registry, "par", "display", "Pixel aspect compensation",
        {opt_number("ratio", "15/14 = 1.0714 gives the 4:3 picture; 1.0 = square pixels")},
        number_setting([]() -> float& { return sh::render::picture().pixel_aspect; },
                       "pixel aspect compensation: %.4f (15/14 = 1.0714 is the 4:3 picture; 1.0 = square pixels)"),
        {}, "Live A/B only; the correct value is baked at boot.");
    // A console shows 224 lines inside a taller window and sets overscan its own way, so true 4:3 is a few
    // percent either side of the textbook value. Judge it on Harry, not on a corridor.
    add(registry, "crtaspect", "display", "Trim on the 4:3 CRT picture",
        {opt_number("trim", "0.5..1.5; 1.0 = 4:3, lower = taller/thinner figures")},
        [](Context& ctx, const Args& args) {
            float& trim = sh::render::picture().crt_aspect_trim;
            if (args.has(0)) trim = static_cast<float>(real(args.str(0)));
            if (trim < 0.5f) trim = 0.5f;
            if (trim > 1.5f) trim = 1.5f;
            say(ctx, "crt aspect trim: %.4f (1.0 = 4:3; lower = taller/thinner figures)", static_cast<double>(trim));
            return done();
        });
    // The value of the Options brightness setting; the post shader applies it, menus included.
    add(registry, "bright", "display", "Brightness", {opt_number("value", "0.2..4.0; 1.0 = stock")},
        [](Context& ctx, const Args& args) {
            if (args.has(0)) {
                float value = static_cast<float>(real(args.str(0)));
                if (value < 0.2f) value = 0.2f;
                if (value > 4.0f) value = 4.0f;
                sh::render::graphics().brightness = value;
            }
            say(ctx, "brightness: %.2f (0.2..4.0; 1.0=stock; config key brightness)",
                static_cast<double>(sh::render::graphics().brightness));
            return done();
        });

    add(registry, "textsize", "display", "Subtitle and message text size", {opt_number("percent", "100..150")},
        percent_command(&s_PcConfig::textSize, 100.0f, 150.0f, "text_size",
                        "text size: %d%% (100..150, subtitles and messages)"));
    add(registry, "crosshairsize", "display", "Crosshair size", {opt_number("percent", "25..125")},
        percent_command(&s_PcConfig::crosshairSize, 25.0f, 125.0f, "crosshair_size",
                        "crosshair size: %d%% (25..125)"));

    // The full-screen ghosting of the dream scenes; the loading screen's trail uses the same store and stays.
    add(registry, "dreamblur", "graphics", "Dream screen blur", {opt_switch("on|off; no argument toggles")},
        [](Context& ctx, const Args& args) {
            g_PcConfig.dreamBlur = switched(args, 0, g_PcConfig.dreamBlur != 0) ? 1 : 0;
            sh::render::graphics().dream_feedback = g_PcConfig.dreamBlur;
            PcConfig_SaveKeyValue("dream_blur", g_PcConfig.dreamBlur ? "1" : "0");
            say(ctx, "dream screen blur: %s", g_PcConfig.dreamBlur ? "on" : "off");
            return done();
        });

    // ---- the inventory's item view ----
    add(registry, "invaspect", "ui", "Inventory item aspect", {opt_switch("on|off; no argument toggles")},
        switch_setting([]() -> int& { return g_PcInvAspectSquare; }, "inventory item aspect: %s",
                       "SQUARE (true proportions)", "PSX-faithful"),
        {}, "on = square (true proportions), off = PS1-faithful.");
    add(registry, "invscale", "ui", "Inventory item vertical scale", {opt_integer("percent", "50..200 of square")},
        ranged_command([]() -> int& { return g_PcInvAspectPct; }, 50, 200,
                       "inventory item vertical scale: %d%% of square"));
    add(registry, "invcary", "ui", "Carousel item Y offset", {opt_integer("pixels", "+ = down")},
        whole_setting([]() -> int& { return g_PcInvCarouselYOff; }, "carousel item Y offset: %d (+ down)"));
    add(registry, "inveqy", "ui", "Equipped item Y offset", {opt_integer("pixels", "+ = down")},
        whole_setting([]() -> int& { return g_PcInvEquipYOff; }, "equipped item Y offset: %d (+ down)"));
    add(registry, "invdim", "ui", "Off-centre carousel dimming", {opt_integer("percent", "0..100")},
        ranged_command([]() -> int& { return g_PcInvDimStrength; }, 0, 100, "off-center carousel dim: %d%%"));
}

}  // namespace sh::debug::commands
