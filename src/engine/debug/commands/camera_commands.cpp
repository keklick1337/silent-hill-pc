// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Console commands that tune the view live: the first- and third-person fields of view, the third-person
// camera, and the scale, shift and crop of the 3D picture.

#include "engine/config/pc_config.h"
#include "engine/debug/commands/command_groups.h"
#include "engine/debug/commands/command_kit.h"
#include "engine/debug/commands/game_switches.h"
#include "render/gpu/render_state.h"

#include <algorithm>
#include <cstdio>
#include <string>

namespace sh::debug::commands {

namespace {

constexpr float kOwnFov = 71.1f;  // the game's own projection, in degrees across the 4:3 frame

sh::render::PictureShape& picture() { return sh::render::picture(); }

void save_setting(const char* key, const char* format, double value) {
    char text[16];
    std::snprintf(text, sizeof(text), format, value);
    PcConfig_SaveKeyValue(key, text);
}

// "fov [degrees|default]": the same value as the Options slider, kept in the config.
RunFn fov_command(float s_PcConfig::* field, const char* key, const char* line) {
    return [field, key, line](Context& ctx, const Args& args) {
        const std::string typed = upper(args.joined(0));
        if (!typed.empty()) {
            float degrees = typed == "DEFAULT" ? kOwnFov : static_cast<float>(real(typed));
            if (degrees < 55.0f) degrees = 55.0f;
            if (degrees > 110.0f) degrees = 110.0f;
            g_PcConfig.*field = degrees;
            save_setting(key, "%.1f", degrees);
        }
        say(ctx, line, static_cast<double>(g_PcConfig.*field));
        return done();
    };
}

// An on/off setting of the config that is saved when it changes hands.
RunFn saved_switch(int s_PcConfig::* field, const char* key, const char* line) {
    return [field, key, line](Context& ctx, const Args& args) {
        const bool on = switched(args, 0, g_PcConfig.*field != 0);
        g_PcConfig.*field = on ? 1 : 0;
        PcConfig_SaveKeyValue(key, on ? "1" : "0");
        say(ctx, line, on ? "ON" : "OFF");
        return done();
    };
}

}  // namespace

void register_camera_commands(Registry& registry) {
    add(registry, "fov", "camera", "First-person field of view",
        {opt_word("degrees", "55..110; 'default' = 71.1 (original)", {"default"})},
        fov_command(&s_PcConfig::fpsFov, "fps_fov",
                    "first-person FOV %.1f deg (71.1 = original; applies in FPS gameplay only)"),
        {}, "Applies in first-person gameplay only. Saved to the config (fps_fov).");
    add(registry, "tpsfov", "camera", "Third-person field of view",
        {opt_word("degrees", "55..110; 'default' = 71.1", {"default"})},
        fov_command(&s_PcConfig::tpsFov, "tps_fov",
                    "thirdperson FOV %.1f deg (71.1 = the game's own FOV; applies in TPS gameplay only)"),
        {}, "Applies in third-person gameplay only. Saved to the config (tps_fov).");

    // How far the third-person camera moves in while aiming, as a share of its zoom range.
    add(registry, "tpsaimzoom", "camera", "Third-person aim zoom amount",
        {opt_number("percent", "0..200; 100 = original full zoom, 0 = none")}, [](Context& ctx, const Args& args) {
            if (args.has(0)) {
                float percent = static_cast<float>(real(args.str(0)));
                if (percent < 0.0f) percent = 0.0f;
                if (percent > 200.0f) percent = 200.0f;
                g_PcConfig.tpsAimZoom = percent;
                save_setting("tps_aim_zoom_amount", "%.0f", percent);
            }
            say(ctx, "TPS aim zoom %.0f%% (100 = original full zoom, 200 = 2x zoom, 0 = none)",
                static_cast<double>(g_PcConfig.tpsAimZoom));
            return done();
        });
    add(registry, "tpsotsaim", "camera", "Shoulder aiming in third person",
        {opt_switch("on|off; no argument toggles")},
        saved_switch(&s_PcConfig::tpsOtsAim, "tps_ots_aim",
                     "Shoulder aiming in thirdperson %s (default on; camera eases over the shoulder while aiming)"),
        {}, "On: the camera eases over the shoulder while aiming.");
    // Scripted runs have no mouse: the orbit, and with it Harry's steering, turns by degrees.
    add(registry, "tpsturn", "camera", "Turn the third-person orbit",
        {opt_number("degrees", "+ = right; for scripted runs without a mouse")}, [](Context& ctx, const Args& args) {
            const int turn = static_cast<int>(real(args.str(0)) * 4096.0 / 360.0);
            g_TpsCamYaw = (g_TpsCamYaw + turn + 4096 * 4) & 4095;
            say(ctx, "thirdperson orbit yaw %d", g_TpsCamYaw);
            return done();
        });
    add(registry, "camcollide", "camera", "Third-person camera collision", {opt_switch("on|off; no argument toggles")},
        saved_switch(&s_PcConfig::tpsCameraCollision, "tps_camera_collision",
                     "thirdperson camera collision %s (off = the camera may pass through walls)"),
        {}, "Off lets the camera pass through walls.");
    add(registry, "camsnap", "camera", "Dump the current camera state to the log", {}, [](Context&, const Args&) {
        Pc_CamSnapDump();
        return done();
    });

    add(registry, "vfov", "camera", "World vertical FOV scale",
        {opt_number("scale", "1.0 = off; vertical crop/zoom of the world")},
        number_setting([]() -> float& { return picture().world_v_scale; },
                       "world vertical FOV scale: %.3f (1.0=off; vertical crop/zoom of the world)"));
    add(registry, "hfov", "camera", "World horizontal scale",
        {opt_number("scale", "1.0 = off; >1 wider models, <1 narrower")},
        number_setting([]() -> float& { return picture().world_h_scale; },
                       "world horizontal scale: %.3f (1.0=off; >1 wider models, <1 narrower; all 3D modes)"));
    // 0 lets cutscenes follow the gameplay crop; any other value is a tuning override only.
    add(registry, "cutfov", "camera", "Cutscene vertical scale override",
        {opt_number("scale", "0 (default) = follow the gameplay vfov")},
        number_setting([]() -> float& { return picture().cutscene_v_scale; },
                       "cutscene vscale override: %.3f (0=follow vfov, the default)"));
    add(registry, "vcropanchor", "camera", "Where the vfov crop sits",
        {opt_number("anchor", "0 = keep top (default), 0.5 = centred, 1 = keep bottom")},
        [](Context& ctx, const Args& args) {
            float& anchor = picture().world_v_crop_anchor;
            if (args.has(0)) anchor = static_cast<float>(real(args.str(0)));
            if (anchor < 0.0f) anchor = 0.0f;
            if (anchor > 1.0f) anchor = 1.0f;
            say(ctx, "vfov crop anchor: %.2f (0=top/default, 0.5=centred, 1=bottom)", static_cast<double>(anchor));
            return done();
        });
    add(registry, "vshift", "camera", "World vertical view shift", {opt_number("units", "PS1 units, + = view up; 0 = off")},
        number_setting([]() -> float& { return picture().world_v_shift; },
                       "world vertical view shift: %.1f psx-units (+ = view up; 0=off)"));
    add(registry, "cutshift", "camera", "Cutscene vertical view shift",
        {opt_number("units", "PS1 units, + = view up; 0 = off")},
        number_setting([]() -> float& { return picture().cutscene_v_shift; },
                       "cutscene vertical view shift: %.1f psx-units (+ = view up; 0=off)"));

    add(registry, "msgshift", "ui", "Message box up-shift (compensates the vfov bottom crop)",
        {opt_integer("units", "PS1 units")},
        whole_setting([]() -> int& { return picture().message_v_shift; },
                      "message box up-shift: %d psx-units (compensates the VFOV bottom crop)"));
    add(registry, "bary", "ui", "Letterbox bar Y position",
        {opt_integer("outer", "outer edge; inner = outer - 16. Raise until the bars hit the screen edges")},
        [](Context& ctx, const Args& args) {
            if (args.has(0)) {
                picture().letterbox_outer = whole(args.str(0));
                picture().letterbox_inner = picture().letterbox_outer - 16;
            }
            say(ctx, "letterbox bar Y: outer=%d inner=%d (raise until bars hit the screen edges)",
                picture().letterbox_outer, picture().letterbox_inner);
            return done();
        });
}

}  // namespace sh::debug::commands
