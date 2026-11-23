// SPDX-License-Identifier: GPL-3.0-or-later
// The Graphics rows of TAA, TAAU, the spatial upscale and frame pacing (RENDER.md section 7.13): setting.upscaler,
// setting.upscale_quality, setting.upscale_sharpness, setting.present_mode, setting.frames_in_flight and
// setting.present_wait. They run on the native Vulkan renderer only; elsewhere the rows are locked and their help
// line says why.

#include "ui/menu/options_temporal.h"

#include "engine/config/pc_config.h"
#include "engine/menu/menu_model.h"
#include "render/gpu/device_host.h"
#include "render/gpu/renderer.h"
#include "render/gpu/targets.h"
#include "render/gpu/temporal_pass.h"
#include "render/post/temporal.h"
#include "render/rhi/rhi_vulkan.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace sh::ui {

namespace {

namespace post = sh::post;

std::string value(const char* name) { return std::string("ui.options.value.") + name; }

void save_int(const char* key, int v) { PcConfig_SaveKeyValue(key, std::to_string(v).c_str()); }

bool native_vulkan() {
    sh::gpu::Renderer* r = sh::gpu::renderer();
    return r != nullptr && sh::rhi::vulkanCaps(r->device()) != nullptr;
}

// The help key of the upscaler rows when they cannot run, or "" when they can.
std::string upscaler_blocked() {
    sh::gpu::Renderer* r = sh::gpu::renderer();
    if (r == nullptr || !native_vulkan()) return "ui.options.atmosphere.vulkan_only";
    if (!r->temporal().available()) return "ui.options.atmosphere.unsupported";
    if (r->targets().samples() > 1) return "ui.options.temporal.msaa";
    return "";
}

bool upscaler_ok() { return upscaler_blocked().empty(); }
bool upscaler_on() { return upscaler_ok() && g_PcConfig.upscaler != 0; }

bool wait_ok() {
    sh::gpu::Renderer* r = sh::gpu::renderer();
    return r != nullptr && sh::rhi::vulkanPresentWaitSupported(r->device());
}

std::string vulkan_help() { return native_vulkan() ? "" : "ui.options.atmosphere.vulkan_only"; }

// A row of named values over an int setting from `lo`, saved under `key` and copied into the live settings.
sh::menu::NativeSetting choice(const char* key, int* field, int post::TemporalSettings::* live,
                               std::vector<std::string> names, int lo, bool (*enabled)()) {
    sh::menu::NativeSetting s;
    const int n = static_cast<int>(names.size());
    s.step = [=](int dir) {
        if (!enabled()) return;
        const int v = (std::clamp(*field - lo, 0, n - 1) + (dir < 0 ? n - 1 : 1)) % n + lo;
        *field = v;
        post::temporal().*live = v;
        save_int(key, v);
    };
    s.text = [=]() { return names[static_cast<std::size_t>(std::clamp(*field - lo, 0, n - 1))]; };
    s.texts = [=]() { return names; };
    return s;
}

}  // namespace

void register_temporal_rows(sh::menu::Registry& reg) {
    sh::menu::NativeSetting up = choice("upscaler", &g_PcConfig.upscaler, &post::TemporalSettings::upscaler,
                                        {"ui.menu.off", value("taa"), value("taau"), value("spatial")}, 0, upscaler_ok);
    up.help = upscaler_blocked;
    reg.native_setting("setting.upscaler", std::move(up));
    reg.native_enabled("setting.upscaler", upscaler_ok);

    reg.native_setting("setting.upscale_quality",
                       choice("upscale_quality", &g_PcConfig.upscaleQuality, &post::TemporalSettings::quality,
                              {value("quality"), value("balanced"), value("performance"), value("ultra_performance")},
                              0, upscaler_ok));
    reg.native_visible("setting.upscale_quality", []() {
        return upscaler_ok() && (g_PcConfig.upscaler == static_cast<int>(post::Upscaler::Taau) ||
                                 g_PcConfig.upscaler == static_cast<int>(post::Upscaler::Spatial));
    });

    // 0.00 .. 1.00 in steps of 0.05.
    sh::menu::NativeSetting sharp;
    sharp.step = [](int dir) {
        const float v = std::clamp(
            std::round((g_PcConfig.upscaleSharpness + (dir < 0 ? -0.05f : 0.05f)) * 20.0f) / 20.0f, 0.0f, 1.0f);
        g_PcConfig.upscaleSharpness = v;
        post::temporal().sharpness = v;
        char text[16];
        std::snprintf(text, sizeof text, "%.2f", static_cast<double>(v));
        PcConfig_SaveKeyValue("upscale_sharpness", text);
    };
    sharp.text = []() {
        char text[16];
        std::snprintf(text, sizeof text, "%.2f", static_cast<double>(g_PcConfig.upscaleSharpness));
        return std::string(text);
    };
    sharp.fraction = []() { return std::clamp(static_cast<double>(g_PcConfig.upscaleSharpness), 0.0, 1.0); };
    sharp.texts = []() { return std::vector<std::string>{"0.00", "1.00"}; };
    reg.native_setting("setting.upscale_sharpness", std::move(sharp));
    reg.native_visible("setting.upscale_sharpness", upscaler_on);

    sh::menu::NativeSetting mode = choice(
        "present_mode", &g_PcConfig.presentMode, &post::TemporalSettings::present_mode,
        {value("auto"), value("fifo"), value("mailbox"), value("immediate"), value("fifo_relaxed")}, 0, native_vulkan);
    mode.help = vulkan_help;
    reg.native_setting("setting.present_mode", std::move(mode));
    reg.native_enabled("setting.present_mode", native_vulkan);

    sh::menu::NativeSetting flight = choice("frames_in_flight", &g_PcConfig.framesInFlight,
                                            &post::TemporalSettings::frames_in_flight, {"1", "2"}, 1, native_vulkan);
    flight.help = vulkan_help;
    reg.native_setting("setting.frames_in_flight", std::move(flight));
    reg.native_enabled("setting.frames_in_flight", native_vulkan);

    sh::menu::NativeSetting wait =
        choice("present_wait", &g_PcConfig.presentWait, &post::TemporalSettings::present_wait,
               {"ui.menu.off", "ui.menu.on"}, 0, wait_ok);
    wait.help = []() -> std::string {
        if (!native_vulkan()) return "ui.options.atmosphere.vulkan_only";
        return wait_ok() ? "" : "ui.options.present_wait.unsupported";
    };
    reg.native_setting("setting.present_wait", std::move(wait));
    reg.native_enabled("setting.present_wait", wait_ok);
}

}  // namespace sh::ui
