// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/menu/options_hdr.h"

#include "engine/config/pc_config.h"
#include "engine/menu/menu_model.h"
#include "render/gpu/device_host.h"
#include "render/gpu/renderer.h"
#include "render/gpu/targets.h"
#include "render/post/hdr.h"
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

std::string on_off(bool on) { return on ? "ui.menu.on" : "ui.menu.off"; }

// The help key of the HDR row when the running renderer cannot do it, or "" when it can.
std::string frame_blocked() {
    sh::gpu::Renderer* r = sh::gpu::renderer();
    if (r == nullptr || r->host().backend() != sh::gpu::BackendKind::Vulkan) return "ui.options.hdr_frame.needs_vulkan";
    if (!r->device().caps().floatTargets) return "ui.options.hdr_frame.no_float";
    return "";
}

// The scene target is RGBA16F now: the rows that act on the HDR frame are shown.
bool frame_active() {
    sh::gpu::Renderer* r = sh::gpu::renderer();
    return r != nullptr && post::hdr().frame && r->targets().scene_format() == sh::rhi::TextureFormat::RGBA16F;
}

bool output_active() {
    sh::gpu::Renderer* r = sh::gpu::renderer();
    return r != nullptr && post::hdr().output && sh::rhi::vulkanHdrStatus(r->device()).active;
}

void save_int(const char* key, int v) { PcConfig_SaveKeyValue(key, std::to_string(v).c_str()); }

void save_float(const char* key, float v, const char* format) {
    char buf[24];
    std::snprintf(buf, sizeof(buf), format, static_cast<double>(v));
    PcConfig_SaveKeyValue(key, buf);
}

// A bar from lo to hi in `step`s, the live value in `live`, the saved one in `field`.
sh::menu::NativeSetting bar(const char* key, float* field, float post::HdrSettings::* live, float lo, float hi,
                            float step, const char* format, const char* unit) {
    sh::menu::NativeSetting s;
    s.step = [=](int dir) {
        const float v = std::clamp(std::round(*field / step) * step + (dir < 0 ? -step : step), lo, hi);
        *field = v;
        post::hdr().*live = v;
        save_float(key, v, format);
    };
    s.text = [=]() {
        char buf[24];
        std::snprintf(buf, sizeof(buf), format, static_cast<double>(*field));
        return std::string(buf) + unit;
    };
    s.fraction = [=]() { return (std::clamp(*field, lo, hi) - lo) / static_cast<double>(hi - lo); };
    s.texts = [=]() {
        char a[24], b[24];
        std::snprintf(a, sizeof(a), format, static_cast<double>(lo));
        std::snprintf(b, sizeof(b), format, static_cast<double>(hi));
        return std::vector<std::string>{std::string(a) + unit, std::string(b) + unit};
    };
    return s;
}

}  // namespace

void register_hdr_options(sh::menu::Registry& reg) {
    sh::menu::NativeSetting frame;
    frame.step = [](int) {
        if (!frame_blocked().empty()) return;
        g_PcConfig.hdrFrame = g_PcConfig.hdrFrame ? 0 : 1;
        post::hdr().frame = g_PcConfig.hdrFrame != 0;
        save_int("hdr_frame", g_PcConfig.hdrFrame);
    };
    frame.text = []() { return on_off(frame_blocked().empty() && g_PcConfig.hdrFrame != 0); };
    frame.texts = []() { return std::vector<std::string>{on_off(true), on_off(false)}; };
    frame.help = []() { return frame_blocked(); };
    reg.native_setting("setting.hdr_frame", std::move(frame));
    reg.native_enabled("setting.hdr_frame", []() { return frame_blocked().empty(); });

    static const char* const kTonemaps[] = {"clamp", "soft", "aces", "reinhard"};
    sh::menu::NativeSetting tonemap;
    tonemap.step = [](int dir) {
        const int n = post::kHdrTonemapCount;
        const int v = (std::clamp(g_PcConfig.hdrTonemap, 0, n - 1) + (dir < 0 ? n - 1 : 1)) % n;
        g_PcConfig.hdrTonemap = v;
        post::hdr().tonemap = v;
        save_int("hdr_tonemap", v);
    };
    tonemap.text = []() { return value(kTonemaps[std::clamp(g_PcConfig.hdrTonemap, 0, post::kHdrTonemapCount - 1)]); };
    tonemap.texts = []() {
        std::vector<std::string> out;
        for (const char* n : kTonemaps) out.push_back(value(n));
        return out;
    };
    reg.native_setting("setting.hdr_tonemap", std::move(tonemap));
    reg.native_visible("setting.hdr_tonemap", frame_active);

    reg.native_setting("setting.bloom",
                       bar("bloom", &g_PcConfig.bloom, &post::HdrSettings::bloom, 0.f, 1.f, 0.05f, "%.2f", ""));
    reg.native_visible("setting.bloom", frame_active);

    sh::menu::NativeSetting output;
    output.step = [](int) {
        g_PcConfig.hdrOutput = g_PcConfig.hdrOutput ? 0 : 1;
        post::hdr().output = g_PcConfig.hdrOutput != 0;
        save_int("hdr_output", g_PcConfig.hdrOutput);
    };
    output.text = []() { return on_off(g_PcConfig.hdrOutput != 0); };
    output.texts = []() { return std::vector<std::string>{on_off(true), on_off(false)}; };
    output.help = []() -> std::string {
        sh::gpu::Renderer* r = sh::gpu::renderer();
        if (r == nullptr || !g_PcConfig.hdrOutput) return "";
        switch (sh::rhi::vulkanHdrStatus(r->device()).block) {
            case sh::rhi::VulkanHdrBlock::NoSurface:
                return "ui.options.hdr_output.offscreen";
            case sh::rhi::VulkanHdrBlock::NoColorspaceExtension:
                return "ui.options.hdr_output.no_extension";
            case sh::rhi::VulkanHdrBlock::NoHdr10Format:
                return "ui.options.hdr_output.no_display";
            default:
                return "";
        }
    };
    reg.native_setting("setting.hdr_output", std::move(output));
    reg.native_visible("setting.hdr_output", frame_active);

    reg.native_setting("setting.hdr_paper_white",
                       bar("hdr_paper_white", &g_PcConfig.hdrPaperWhite, &post::HdrSettings::paper_white, 80.f, 500.f,
                           10.f, "%.0f", " nits"));
    reg.native_visible("setting.hdr_paper_white", output_active);
    reg.native_setting("setting.hdr_peak", bar("hdr_peak", &g_PcConfig.hdrPeak, &post::HdrSettings::peak, 400.f, 4000.f,
                                               100.f, "%.0f", " nits"));
    reg.native_visible("setting.hdr_peak", output_active);
}

}  // namespace sh::ui
