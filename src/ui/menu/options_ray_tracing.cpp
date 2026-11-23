// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The ray tracing rows of Options > Graphics (RENDER.md section 11): on/off settings that only the native
// Vulkan backend on a GPU with the KHR ray tracing extensions can run. Elsewhere a row is locked and its help line
// says why; the effect rows also wait for the ray_tracing row.

#include "ui/menu/options_ray_tracing.h"

#include "engine/config/pc_config.h"
#include "engine/menu/menu_model.h"
#include "render/gpu/render_state.h"
#include "render/gpu/renderer.h"
#include "render/rhi/rhi_vulkan_rt.h"

#include <cstdio>
#include <string>

namespace {

struct RayRow {
    const char* key;
    int s_PcConfig::* config;
    int sh::render::GraphicsSettings::* live;
    bool needs_master;  // only with ray_tracing on
};

constexpr RayRow kRows[] = {
    {"ray_tracing", &s_PcConfig::rayTracing, &sh::render::GraphicsSettings::ray_tracing, false},
    {"rt_shadows", &s_PcConfig::rtShadows, &sh::render::GraphicsSettings::rt_shadows, true},
    {"rt_reflections", &s_PcConfig::rtReflections, &sh::render::GraphicsSettings::rt_reflections, true},
    {"rt_gi", &s_PcConfig::rtGi, &sh::render::GraphicsSettings::rt_gi, true},
    {"rt_lights", &s_PcConfig::rtLights, &sh::render::GraphicsSettings::rt_lights, true},
};

sh::rhi::RayTracingSupport support() {
    sh::gpu::Renderer* r = sh::gpu::renderer();
    if (!r) return sh::rhi::RayTracingSupport::NotNativeVulkan;
    return sh::rhi::rayTracingSupport(r->device());
}

bool row_enabled(const RayRow& row) {
    if (support() != sh::rhi::RayTracingSupport::Available) return false;
    return !row.needs_master || g_PcConfig.rayTracing != 0;
}

}  // namespace

bool Pc_OptionsRayTracing_Available() { return support() == sh::rhi::RayTracingSupport::Available; }

void Pc_OptionsRayTracing_Register(sh::menu::Registry& reg) {
    for (const RayRow& row : kRows) {
        sh::menu::NativeSetting s;
        s.step = [row](int) {
            if (!row_enabled(row)) return;
            const int v = (g_PcConfig.*row.config) ? 0 : 1;
            g_PcConfig.*row.config = v;
            sh::render::graphics().*row.live = v;
            char text[4];
            std::snprintf(text, sizeof text, "%d", v);
            PcConfig_SaveKeyValue(row.key, text);
        };
        s.text = [row]() -> std::string {
            return (g_PcConfig.*row.config) && row_enabled(row) ? "ui.menu.on" : "ui.menu.off";
        };
        s.texts = []() { return std::vector<std::string>{"ui.menu.on", "ui.menu.off"}; };
        s.help = [row]() -> std::string {
            switch (support()) {
                case sh::rhi::RayTracingSupport::NotNativeVulkan:
                    return "ui.options.ray_tracing.no_vulkan";
                case sh::rhi::RayTracingSupport::NoHardware:
                    return "ui.options.ray_tracing.no_hardware";
                case sh::rhi::RayTracingSupport::Available:
                    break;
            }
            if (row.needs_master && !g_PcConfig.rayTracing) return "ui.options.ray_tracing.needs_master";
            return "";
        };
        const std::string native = std::string("setting.") + row.key;
        reg.native_setting(native, std::move(s));
        reg.native_enabled(native, [row]() { return row_enabled(row); });
    }
}
