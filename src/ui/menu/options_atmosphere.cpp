// SPDX-License-Identifier: GPL-3.0-or-later
// The Graphics rows of ambient occlusion and volumetric fog (RENDER.md section 7.10): setting.ambient_occlusion,
// setting.ao_strength, setting.volumetric_fog and setting.fog_scatter, and of the map's lights without ray tracing
// (section 7.14): setting.map_lights, sun_shadows, god_rays, particle_shadows. They run on the native Vulkan renderer
// only; elsewhere the rows are locked and their help line says why. A strength row shows while its effect is on.

#include "ui/menu/options_atmosphere.h"

#include "engine/config/pc_config.h"
#include "engine/menu/menu_model.h"
#include "render/gpu/atmosphere_pass.h"
#include "render/gpu/device_host.h"
#include "render/gpu/renderer.h"
#include "ui/screens/pc_settings.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace sh::ui {

namespace {

bool available() {
    sh::gpu::Renderer* r = sh::gpu::renderer();
    return r != nullptr && r->atmosphere().available();
}

std::string unavailable_help() {
    if (available()) return "";
    sh::gpu::Renderer* r = sh::gpu::renderer();
    if (r == nullptr || r->host().backend() != sh::gpu::BackendKind::Vulkan) return "ui.options.atmosphere.vulkan_only";
    return "ui.options.atmosphere.unsupported";
}

void flag_row(sh::menu::Registry& reg, const char* key, int* field, const int* shown_by = nullptr) {
    sh::menu::NativeSetting n;
    n.step = [key, field](int) {
        if (!available()) return;
        pc_settings::set_int(key, *field != 0 ? 0 : 1);
    };
    n.text = [field]() { return std::string(*field != 0 ? "ui.menu.on" : "ui.menu.off"); };
    n.texts = []() { return std::vector<std::string>{"ui.menu.on", "ui.menu.off"}; };
    n.help = unavailable_help;
    reg.native_setting(std::string("setting.") + key, std::move(n));
    reg.native_enabled(std::string("setting.") + key, available);
    if (shown_by != nullptr) reg.native_visible(std::string("setting.") + key, [shown_by]() { return *shown_by != 0; });
}

// 0.00 .. 1.00 in steps of 0.05.
void bar_row(sh::menu::Registry& reg, const char* key, float* field, const int* shown_by) {
    sh::menu::NativeSetting n;
    n.step = [key, field](int dir) {
        const float v = std::clamp(std::round((*field + (dir < 0 ? -0.05f : 0.05f)) * 20.0f) / 20.0f, 0.0f, 1.0f);
        char text[16];
        std::snprintf(text, sizeof text, "%.2f", static_cast<double>(v));
        const std::string saved(text);
        pc_settings::set_float(key, v, &saved);
    };
    n.text = [field]() {
        char text[16];
        std::snprintf(text, sizeof text, "%.2f", static_cast<double>(*field));
        return std::string(text);
    };
    n.fraction = [field]() { return std::clamp(static_cast<double>(*field), 0.0, 1.0); };
    n.texts = []() { return std::vector<std::string>{"0.00", "1.00"}; };
    reg.native_setting(std::string("setting.") + key, std::move(n));
    reg.native_visible(std::string("setting.") + key, [shown_by]() { return *shown_by != 0 && available(); });
}

}  // namespace

void register_atmosphere_rows(sh::menu::Registry& reg) {
    flag_row(reg, "ambient_occlusion", &g_PcConfig.ambientOcclusion);
    bar_row(reg, "ao_strength", &g_PcConfig.aoStrength, &g_PcConfig.ambientOcclusion);
    flag_row(reg, "volumetric_fog", &g_PcConfig.volumetricFog);
    bar_row(reg, "fog_scatter", &g_PcConfig.fogScatter, &g_PcConfig.volumetricFog);
    flag_row(reg, "god_rays", &g_PcConfig.godRays, &g_PcConfig.volumetricFog);
    flag_row(reg, "map_lights", &g_PcConfig.mapLights);
    flag_row(reg, "sun_shadows", &g_PcConfig.sunShadows, &g_PcConfig.mapLights);
    flag_row(reg, "particle_shadows", &g_PcConfig.particleShadows);
}

}  // namespace sh::ui
