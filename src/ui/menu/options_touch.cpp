// SPDX-License-Identifier: GPL-3.0-or-later
// The Touch Controls page (Options > Controls, shown on a touch screen): the layout editor, whether the controls
// show, the stick or the d-pad, the action button, aim, the stick's run, opacity and size (config.cfg touch_*).
// Also the strength of the phone's vibrator (phone_vibration, a Controls row on a phone), the touch look's speed
// (touch_look_sensitivity, the same) and the Interface page's
// performance overlay (fps_display and fps_profiler as one row), which a phone has no key for.

#include "ui/menu/options_touch.h"

#include "engine/config/pc_config.h"
#include "engine/menu/menu_model.h"
#include "input/touch/touch_controls.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace sh::ui {

namespace {

std::string value(const char* name) { return std::string("ui.options.value.") + name; }
std::string on_off(bool on) { return on ? "ui.menu.on" : "ui.menu.off"; }

void save_int(const char* key, int v) { PcConfig_SaveKeyValue(key, std::to_string(v).c_str()); }

void save_float(const char* key, float v) {
    char text[32];
    std::snprintf(text, sizeof text, "%.2f", v);
    PcConfig_SaveKeyValue(key, text);
}

void flag_row(sh::menu::Registry& reg, const char* key, int* field) {
    sh::menu::NativeSetting n;
    n.step = [key, field](int) {
        *field = *field != 0 ? 0 : 1;
        save_int(key, *field);
    };
    n.text = [field]() { return on_off(*field != 0); };
    n.texts = []() { return std::vector<std::string>{on_off(true), on_off(false)}; };
    reg.native_setting(std::string("setting.") + key, std::move(n));
}

// A bar of percentages in steps of 10.
void percent_row(sh::menu::Registry& reg, const char* key, float* field, float lo, float hi) {
    sh::menu::NativeSetting n;
    n.step = [key, field, lo, hi](int dir) {
        const float v = std::round(*field * 10.0f) / 10.0f + (dir < 0 ? -0.1f : 0.1f);
        *field = std::clamp(v, lo, hi);
        save_float(key, *field);
    };
    n.text = [field]() { return std::to_string(static_cast<int>(std::lround(*field * 100.0f))) + "%"; };
    n.fraction = [field, lo, hi]() { return (*field - lo) / (hi - lo); };
    n.texts = [lo, hi]() {
        return std::vector<std::string>{std::to_string(static_cast<int>(lo * 100.0f)) + "%",
                                        std::to_string(static_cast<int>(hi * 100.0f)) + "%"};
    };
    reg.native_setting(std::string("setting.") + key, std::move(n));
}

// A bar of whole numbers in steps of `step`.
void whole_row(sh::menu::Registry& reg, const char* key, float* field, float lo, float hi, float step) {
    sh::menu::NativeSetting n;
    n.step = [key, field, lo, hi, step](int dir) {
        const float v = std::round(*field / step) * step + (dir < 0 ? -step : step);
        *field = std::clamp(v, lo, hi);
        save_int(key, static_cast<int>(std::lround(*field)));
    };
    n.text = [field]() { return std::to_string(static_cast<int>(std::lround(*field))); };
    n.fraction = [field, lo, hi]() { return (*field - lo) / (hi - lo); };
    n.texts = [lo, hi]() {
        return std::vector<std::string>{std::to_string(static_cast<int>(lo)), std::to_string(static_cast<int>(hi))};
    };
    reg.native_setting(std::string("setting.") + key, std::move(n));
}

}  // namespace

void register_touch_rows(sh::menu::Registry& reg) {
    // Auto, On, Off: the config's 0, 1, 2.
    sh::menu::NativeSetting controls;
    controls.step = [](int dir) {
        g_PcConfig.touchControls = (g_PcConfig.touchControls + (dir < 0 ? 2 : 1)) % 3;
        static const char* const kNames[] = {"auto", "on", "off"};
        PcConfig_SaveKeyValue("touch_controls", kNames[g_PcConfig.touchControls]);
    };
    controls.text = []() {
        const int v = g_PcConfig.touchControls;
        return v == 1 ? on_off(true) : v == 2 ? on_off(false) : value("auto");
    };
    controls.texts = []() { return std::vector<std::string>{value("auto"), on_off(true), on_off(false)}; };
    reg.native_setting("setting.touch_controls", std::move(controls));

    sh::menu::NativeSetting action;
    action.step = [](int) {
        g_PcConfig.touchActionButton = g_PcConfig.touchActionButton != 0 ? 0 : 1;
        save_int("touch_action_button", g_PcConfig.touchActionButton);
    };
    action.text = []() { return value(g_PcConfig.touchActionButton != 0 ? "always" : "when_needed"); };
    action.texts = []() { return std::vector<std::string>{value("always"), value("when_needed")}; };
    reg.native_setting("setting.touch_action_button", std::move(action));

    // The floating stick or the d-pad with its run toggle: the config's 0, 1.
    sh::menu::NativeSetting movement;
    movement.step = [](int) {
        g_PcConfig.touchMovement = g_PcConfig.touchMovement != 0 ? 0 : 1;
        PcConfig_SaveKeyValue("touch_movement", g_PcConfig.touchMovement != 0 ? "dpad" : "stick");
    };
    movement.text = []() { return value(g_PcConfig.touchMovement != 0 ? "touch_dpad" : "touch_stick"); };
    movement.texts = []() { return std::vector<std::string>{value("touch_stick"), value("touch_dpad")}; };
    reg.native_setting("setting.touch_movement", std::move(movement));

    flag_row(reg, "touch_aim_toggle", &g_PcConfig.touchAimToggle);
    flag_row(reg, "touch_stick_run", &g_PcConfig.touchStickRun);
    percent_row(reg, "touch_opacity", &g_PcConfig.touchOpacity, 0.2f, 1.0f);
    percent_row(reg, "touch_scale", &g_PcConfig.touchScale, 0.6f, 1.6f);
    percent_row(reg, "phone_vibration", &g_PcConfig.phoneVibration, 0.0f, 1.0f);
    // Degrees for a drag across the screen's width.
    whole_row(reg, "touch_look_sensitivity", &g_PcConfig.touchLookSensitivity, 30.0f, 360.0f, 10.0f);

    reg.native_action("options.touch_edit_layout", []() { sh::input::touch::open_editor(); });
    reg.native_action("options.touch_reset_layout", []() { sh::input::touch::reset_layout(); });

    // Off, the frame rate, the profiler: fps_display 0 / 1 (top left) and fps_profiler.
    sh::menu::NativeSetting fps;
    fps.step = [](int dir) {
        const int now = g_PcConfig.fpsProfiler != 0 ? 2 : g_PcConfig.fpsDisplay != 0 ? 1 : 0;
        const int next = (now + (dir < 0 ? 2 : 1)) % 3;
        g_PcConfig.fpsDisplay = next == 0 ? 0 : (g_PcConfig.fpsDisplay != 0 ? g_PcConfig.fpsDisplay : 1);
        g_PcConfig.fpsProfiler = next == 2 ? 1 : 0;
        save_int("fps_display", g_PcConfig.fpsDisplay);
        save_int("fps_profiler", g_PcConfig.fpsProfiler);
    };
    fps.text = []() {
        return g_PcConfig.fpsProfiler != 0 ? value("profiler") : g_PcConfig.fpsDisplay != 0 ? value("fps")
                                                                                              : on_off(false);
    };
    fps.texts = []() { return std::vector<std::string>{on_off(false), value("fps"), value("profiler")}; };
    reg.native_setting("setting.fps_overlay", std::move(fps));
}

}  // namespace sh::ui
