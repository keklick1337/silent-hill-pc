// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Console commands of the flashlight, its shadows and the post-processing: flmode, shadows and the shadow
// tuning values, the intensities, postfx and tonemap.

#include "engine/config/pc_config.h"
#include "engine/debug/commands/command_groups.h"
#include "engine/debug/commands/command_kit.h"
#include "engine/debug/commands/game_switches.h"
#include "input/camera_style.h"
#include "render/gpu/render_state.h"

#include <algorithm>
#include <cstdio>
#include <span>
#include <string>

namespace sh::debug::commands {

namespace {

sh::render::Flashlight& flashlight() { return sh::render::flashlight(); }

// The flashlight modes: classic or modern, each plain (even) or with shadows (the next odd number).
constexpr const char* kFlashlightModes[] = {"classic", "classicshadows", "modern", "modernshadows"};

void apply_flashlight_mode(Context& ctx, int mode) {
    Pc_FlashlightModeApply(mode, 1);
    say(ctx, "Flashlight: %s", Pc_FlashlightModeLabel(g_PcConfig.flashlightMode));
}

// A strength between two limits that is kept in the config with two decimals: "<command> [value]".
RunFn intensity_command(Setting<float> value, float highest, const char* key, const char* line) {
    return [=](Context& ctx, const Args& args) {
        if (args.has(0)) {
            float typed = static_cast<float>(real(args.str(0)));
            if (typed < 0.0f) typed = 0.0f;
            if (typed > highest) typed = highest;
            value() = typed;
            char text[16];
            std::snprintf(text, sizeof(text), "%.2f", static_cast<double>(typed));
            PcConfig_SaveKeyValue(key, text);
        }
        say(ctx, line, static_cast<double>(value()));
        return done();
    };
}

// A look picked by its number from a fixed list, kept in the config: "<command> [number]".
RunFn look_command(const char* command, std::span<const char* const> looks, Setting<int> value, int s_PcConfig::* field,
                   const char* key, const char* label) {
    return [=](Context& ctx, const Args& args) {
        const int count = static_cast<int>(looks.size());
        if (args.has(0)) {
            const std::string& typed = args.str(0);
            const int look = whole(typed);
            if (look < 0 || look >= count || typed.empty() || typed[0] < '0' || typed[0] > '9') {
                say(ctx, "usage: %s <0-%d>", command, count - 1);
                return done();
            }
            value() = look;
            g_PcConfig.*field = look;
            PcConfig_SaveKeyValue(key, std::to_string(look).c_str());
        }
        const int now = value();
        say(ctx, "%s: %d %s", label, now, now >= 0 && now < count ? looks[static_cast<std::size_t>(now)] : "?");
        return done();
    };
}

constexpr const char* kPostLooks[] = {"Off",        "CRT",     "Scanlines", "Vignette", "Color Grade",
                                      "Film Grain", "Sharpen", "PSX Retro", "Cinematic"};
constexpr const char* kTonemaps[] = {"Off", "Reinhard", "ACES", "Filmic"};

}  // namespace

void register_light_commands(Registry& registry) {
    add(registry, "flmode", "graphics", "Flashlight mode",
        {arg("mode", ArgType::Enum, true, "mode name", {"classic", "classicshadows", "modern", "modernshadows"})},
        [](Context& ctx, const Args& args) {
            int mode = g_PcConfig.flashlightMode;
            for (int known = 0; known < 4; ++known) {
                if (args.str(0) == kFlashlightModes[known]) mode = known;
            }
            apply_flashlight_mode(ctx, mode);
            return done();
        });
    add(registry, "shadows", "graphics", "Flashlight shadows", {opt_switch("on|off; no argument toggles")},
        [](Context& ctx, const Args& args) {
            const int mode = g_PcConfig.flashlightMode;
            const bool shadowed = mode == 1 || mode == 3;
            const bool modern = !(mode == 0 || mode == 1);
            apply_flashlight_mode(ctx, (modern ? 2 : 0) + (switched(args, 0, shadowed) ? 1 : 0));
            return done();
        },
        {}, "Switches the flashlight mode between its plain and shadowed variants.");

    // The shadow map is rebuilt on the next frame that needs it; the renderer keeps the size in its limits.
    add(registry, "shadowres", "graphics", "Flashlight shadow map size",
        {opt_integer("pixels", "256..8192; default 1024")}, [](Context& ctx, const Args& args) {
            if (args.has(0)) flashlight().shadow_map_size = whole(args.str(0));
            say(ctx, "flashlight shadow map: %dx%d (256..8192; default 1024)", flashlight().shadow_map_size,
                flashlight().shadow_map_size);
            return done();
        });
    add(registry, "shadowfilter", "graphics", "Flashlight shadow edge filter",
        {opt_integer("filter", "0 = 2x2, 1 = 3x3 (default), 2 = 5x5")}, [](Context& ctx, const Args& args) {
            if (args.has(0)) flashlight().shadow_filter = std::clamp(whole(args.str(0)), 0, 2);
            say(ctx, "flashlight shadow filter: %d (0 = 2x2, 1 = 3x3 default, 2 = 5x5)", flashlight().shadow_filter);
            return done();
        });
    add(registry, "shadowbias", "graphics", "Flashlight shadow bias", {opt_number("value", "")},
        number_setting([]() -> float& { return flashlight().shadow_bias; }, "shadow bias = %.5f"));
    add(registry, "shadowstrength", "graphics", "Flashlight shadow strength",
        {opt_number("value", "1 = full (default), lower = softer")},
        number_setting([]() -> float& { return flashlight().shadow_strength; },
                       "shadow strength = %.3f (1=full/default, lower=softer)"));
    add(registry, "shadowfade", "graphics", "Flashlight shadow contact fade",
        {opt_number("distance", "0 = off; view units behind the occluder")},
        number_setting([]() -> float& { return flashlight().shadow_fade_distance; },
                       "shadow contact-fade = %.1f (0=off; >0 = view units behind occluder)"));
    add(registry, "shadownormal", "graphics", "Flashlight shadow normal offset", {opt_number("value", "0 = off")},
        number_setting([]() -> float& { return flashlight().shadow_normal_offset; },
                       "shadow normal-offset = %.5f (0=off)"));
    add(registry, "shadowfpsdrop", "graphics", "First-person shadow-light drop", {opt_number("value", "")},
        number_setting([]() -> float& { return flashlight().shadow_fps_drop; }, "FPS shadow-light drop = %.1f"));
    add(registry, "shadowplayer", "graphics", "The player casts a flashlight shadow",
        {opt_integer("on", "1 = casts (default), 0 = does not")}, [](Context& ctx, const Args& args) {
            if (args.has(0)) flashlight().shadow_player = whole(args.str(0)) != 0 ? 1 : 0;
            say(ctx, "player casts a flashlight shadow = %d", flashlight().shadow_player);
            return done();
        });

    // First person has a flashlight brightness of its own: the command tunes the one in use.
    add(registry, "flintensity", "graphics", "Flashlight intensity (the first-person value while in first person)",
        {opt_number("value", "0..3")}, [](Context& ctx, const Args& args) {
            const bool first_person = sh::input::camera_styles().first_person();
            const RunFn tune = first_person
                                   ? intensity_command([]() -> float& { return flashlight().intensity_fps; }, 3.0f,
                                                       "flashlight_intensity_fps", "flashlight intensity (fps): %.2f (0..3)")
                                   : intensity_command([]() -> float& { return flashlight().intensity; }, 3.0f,
                                                       "flashlight_intensity", "flashlight intensity: %.2f (0..3)");
            return tune(ctx, args);
        },
        {"flint"});
    add(registry, "postintensity", "graphics", "Post-process intensity", {opt_number("value", "0..1")},
        intensity_command([]() -> float& { return sh::render::graphics().post_process_intensity; }, 1.0f,
                          "post_process_intensity", "post-process intensity: %.2f (0..1)"),
        {"postint"});
    add(registry, "tmintensity", "graphics", "Tonemap intensity", {opt_number("value", "0..1")},
        intensity_command([]() -> float& { return sh::render::graphics().tonemap_intensity; }, 1.0f,
                          "tonemap_intensity", "tonemap intensity: %.2f (0..1)"),
        {"tmint"});

    add(registry, "postfx", "graphics", "Post-process look (saved as post_process)",
        {opt_integer("mode", "0 Off, 1 CRT, 2 Scanlines, 3 Vignette, 4 Color Grade, 5 Film Grain, 6 Sharpen, 7 PSX Retro, "
                             "8 Cinematic")},
        look_command("postfx", kPostLooks, []() -> int& { return sh::render::graphics().post_process; },
                     &s_PcConfig::postProcess, "post_process", "post-process"));
    add(registry, "tonemap", "graphics", "Tone mapping operator (saved as tonemap)",
        {opt_integer("mode", "0 Off, 1 Reinhard, 2 ACES, 3 Filmic")},
        look_command("tonemap", kTonemaps, []() -> int& { return sh::render::graphics().tonemap; },
                     &s_PcConfig::tonemap, "tonemap", "tonemap"));
}

}  // namespace sh::debug::commands
