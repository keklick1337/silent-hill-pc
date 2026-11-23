// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The engine settings the editor borrows while it is open (tools_internal.h) and the viewer settings' keys in
// config.cfg. Everything taken here is given back by leave_editor (tools_host.cpp).

#include "features/tools/tools_internal.h"

#include <cstdio>
#include <cstdlib>
#include <string>

#include "engine/app/game_view.h"
#include "engine/config/pc_config.h"
#include "engine/console/instance.h"
#include "engine/console/registry.h"
#include "engine/debug/collision_view.h"
#include "engine/log/sh_log.h"
#include "engine/mods/extra_config.h"

extern int g_DebugFogDisabled;

namespace sh::tools {

namespace {

// Command output to the console when it exists, else to the journal.
class EchoOutput final : public sh::console::Output {
public:
    void print(sh::console::Level, std::string_view line) override {
        SH_DBG_ECHO("%.*s", static_cast<int>(line.size()), line.data());
    }
};

bool flag(const char* key, bool fallback) {
    const char* v = config_value(key);
    return v != nullptr ? std::atoi(v) != 0 : fallback;
}

float number(const char* key, float fallback, float lo, float hi) {
    const char* v = config_value(key);
    if (v == nullptr) return fallback;
    const float f = std::strtof(v, nullptr);
    return f < lo ? lo : (f > hi ? hi : f);
}

}  // namespace

const char* config_value(const char* key) { return sh::mods::extra_config().value(key); }

bool tools_enabled() { return g_PcConfig.moddingTools != 0; }

bool world_fog_enabled() { return sh::app::view::fog_enabled(); }
void set_world_fog_enabled(bool on) { sh::app::view::set_fog_enabled(on); }
int fog_debug_disabled() { return g_DebugFogDisabled; }
void set_fog_debug_disabled(int value) { g_DebugFogDisabled = value; }

void draw_distance(int* pct, int* whole_map) {
    *pct = g_PcConfig.drawDistancePct;
    *whole_map = g_PcConfig.wholeMapExteriors;
}

void set_draw_distance(int pct, int whole_map) {
    g_PcConfig.drawDistancePct = pct;
    g_PcConfig.wholeMapExteriors = whole_map;
}

bool collision_view_enabled() { return sh::debug::collision_view().enabled(); }

void set_collision_view(bool on) {
    if (sh::debug::collision_view().enabled() != on) sh::debug::collision_view().set_enabled(on);
}

void run_command_now(const std::string& line) {
    sh::console::Output* out = sh::console::active_output();
    EchoOutput echo;
    sh::console::global_registry().execute(line, out != nullptr ? *out : echo);
}

void load_viewer_settings() {
    ViewerSettings& v = viewer();
    v.fog_off = flag("editor_fog_off", v.fog_off);
    v.full_bright = flag("editor_full_bright", v.full_bright);
    v.light_scale = number("editor_light_scale", v.light_scale, 0.0f, 4.0f);
    v.flashlight_off = flag("editor_flashlight_off", v.flashlight_off);
    v.brightness = number("editor_brightness", v.brightness, 0.25f, 4.0f);
    v.contrast = number("editor_contrast", v.contrast, 0.25f, 4.0f);
    v.wireframe = flag("editor_wireframe", v.wireframe);
    v.collision_view = flag("editor_collision_view", v.collision_view);
}

void save_viewer_settings() {
    const ViewerSettings& v = viewer();
    char light[32], bright[32], contrast[32];
    std::snprintf(light, sizeof(light), "%.2f", v.light_scale);
    std::snprintf(bright, sizeof(bright), "%.2f", v.brightness);
    std::snprintf(contrast, sizeof(contrast), "%.2f", v.contrast);
    const char* keys[] = {"editor_fog_off",    "editor_full_bright", "editor_light_scale",   "editor_flashlight_off",
                          "editor_brightness", "editor_contrast",    "editor_wireframe",     "editor_collision_view"};
    const char* values[] = {v.fog_off ? "1" : "0",        v.full_bright ? "1" : "0", light,
                            v.flashlight_off ? "1" : "0", bright,                    contrast,
                            v.wireframe ? "1" : "0",      v.collision_view ? "1" : "0"};
    constexpr int kCount = static_cast<int>(sizeof(keys) / sizeof(keys[0]));
    PcConfig_SaveKeyValues(keys, values, kCount);
    // The parser keeps the keys it does not know by name; the saved values are read back from there next time.
    for (int i = 0; i < kCount; ++i) sh::mods::extra_config().store(keys[i], values[i]);
    SH_DBG_ECHO("[EDITOR] viewer settings saved to config.cfg");
}

}  // namespace sh::tools
