// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/screens/settings_presets.h"

#include <array>
#include <cmath>

namespace sh::screens::presets {

namespace {

// flashlight_mode 1 is the per-pixel cone calibrated to the original beam (2 and 3 are the
// stylised hard spotlight). Its shadows are the separate Shadows row (key "shadows"), so the
// style leaves them alone. The beam's size and brightness go back to the calibrated defaults.
constexpr std::array<KeyValue, 1> kFlashlightClassic = {{{"flashlight_mode", 0.0f}}};
constexpr std::array<KeyValue, 5> kFlashlightModern = {{
    {"flashlight_mode", 1.0f},
    {"flashlight_intensity", 1.20f},
    {"flashlight_size", 3.00f},
    {"flashlight_intensity_fps", 2.10f},
    {"flashlight_size_fps", 1.30f},
}};

// tonemap stays 0 in both: on the 8-bit frame every operator washes out the darkness.
constexpr std::array<KeyValue, 4> kLightingClassic = {{
    {"soft_highlights", 0.0f},
    {"deband", 0.0f},
    {"tonemap", 0.0f},
    {"hero_light", 0.0f},
}};
constexpr std::array<KeyValue, 4> kLightingModern = {{
    {"soft_highlights", 1.0f},
    {"deband", 1.0f},
    {"tonemap", 0.0f},
    {"hero_light", 2.0f},
}};

// Off keeps the map size: it only matters again when the shadows come back.
constexpr std::array<KeyValue, 1> kShadowOffTable = {{{"shadows", 0.0f}}};
constexpr std::array<KeyValue, 2> kShadowLowTable = {{{"shadows", 1.0f}, {"shadow_resolution", 512.0f}}};
constexpr std::array<KeyValue, 2> kShadowMediumTable = {{{"shadows", 1.0f}, {"shadow_resolution", 1024.0f}}};
constexpr std::array<KeyValue, 2> kShadowHighTable = {{{"shadows", 1.0f}, {"shadow_resolution", 2048.0f}}};
constexpr std::array<KeyValue, 2> kShadowUltraTable = {{{"shadows", 1.0f}, {"shadow_resolution", 4096.0f}}};

constexpr std::array<std::string_view, 40> kPresetKeys = {
    "native_resolution", "scaling", "dithering", "texture_filter", "msaa", "flashlight_mode",
    "shadows", "deband", "soft_highlights", "post_process", "post_process_intensity", "tonemap",
    "use_pgxp", "soft_fog", "smooth_models", "foliage_edge", "load_screen_trail",
    "render_scale", "shadow_resolution", "shadow_filter", "water_quality",
    "hdr_frame", "hdr_tonemap", "bloom", "upscaler", "ambient_occlusion", "map_lights", "sun_shadows",
    "volumetric_fog", "god_rays", "particle_shadows", "model_upscaling", "texture_upscale",
    "ray_tracing", "rt_shadows", "rt_reflections", "rt_gi", "rt_lights", "extra_lights",
    "bullet_decals",
};

// water_quality is spelled by name too.
constexpr std::array<std::string_view, 3> kWaterQualityNames = {"low", "medium", "high"};

// load_screen_trail is spelled by name in config.cfg; the index is the value pc_config.cpp parses it to.
constexpr std::array<std::string_view, 3> kLoadScreenTrailNames = {"off", "original", "smooth"};

constexpr std::array<std::string_view, 3> kModelUpscalingNames = {"off", "tessellation", "mesh_shaders"};

bool same(float a, float b)
{
    return std::fabs(a - b) < 0.005f;
}

int detect(std::span<const KeyValue> (*table)(int), int count, const Getter& current)
{
    for (int i = 0; i < count; i++) {
        if (matches(table(i), current)) {
            return i;
        }
    }
    return kCustom;
}

}  // namespace

std::span<const KeyValue> flashlight_style(int style)
{
    switch (style) {
        case kClassic: return kFlashlightClassic;
        case kModern: return kFlashlightModern;
        default: return {};
    }
}

std::span<const KeyValue> lighting_style(int style)
{
    switch (style) {
        case kClassic: return kLightingClassic;
        case kModern: return kLightingModern;
        default: return {};
    }
}

std::span<const KeyValue> shadow_quality(int level)
{
    switch (level) {
        case kShadowOff: return kShadowOffTable;
        case kShadowLow: return kShadowLowTable;
        case kShadowMedium: return kShadowMediumTable;
        case kShadowHigh: return kShadowHighTable;
        case kShadowUltra: return kShadowUltraTable;
        default: return {};
    }
}

std::span<const std::string_view> preset_keys()
{
    return kPresetKeys;
}

std::string_view value_name(std::string_view key, int value)
{
    if (key == "load_screen_trail" && value >= 0 && value < static_cast<int>(kLoadScreenTrailNames.size())) {
        return kLoadScreenTrailNames[static_cast<std::size_t>(value)];
    }
    if (key == "water_quality" && value >= 0 && value < static_cast<int>(kWaterQualityNames.size())) {
        return kWaterQualityNames[static_cast<std::size_t>(value)];
    }
    if (key == "model_upscaling" && value >= 0 && value < static_cast<int>(kModelUpscalingNames.size())) {
        return kModelUpscalingNames[static_cast<std::size_t>(value)];
    }
    return {};
}

int style_for_preset(int preset)
{
    return preset == kOriginal ? kClassic : kModern;
}

bool preset_traces_rays(int preset)
{
    return preset == kUltraRtx || preset == kUltraRtxUpscaled;
}

bool preset_upscales(int preset)
{
    return preset == kUltraUpscaled || preset == kUltraRtxUpscaled;
}

bool matches(std::span<const KeyValue> table, const Getter& current)
{
    if (table.empty()) {
        return false;
    }
    for (const KeyValue& kv : table) {
        if (!same(current(kv.key), kv.value)) {
            return false;
        }
    }
    return true;
}

int detect_flashlight(const Getter& current)
{
    return detect(&flashlight_style, kStyleCount, current);
}

int detect_lighting(const Getter& current)
{
    return detect(&lighting_style, kStyleCount, current);
}

int detect_shadow_quality(const Getter& current)
{
    return detect(&shadow_quality, kShadowCount, current);
}

int detect_preset(const std::function<float(int preset, std::string_view key)>& preset_value,
                  const Getter& current)
{
    for (int p = 0; p < kPresetCount; p++) {
        bool all = true;
        for (std::string_view key : kPresetKeys) {
            if (!same(preset_value(p, key), current(key))) {
                all = false;
                break;
            }
        }
        if (all) {
            return p;
        }
    }
    return kCustom;
}

int step_choice(int current, int count, int dir)
{
    if (count <= 0) {
        return kCustom;
    }
    if (current < 0 || current >= count) {
        return dir < 0 ? count - 1 : 0;
    }
    if (dir == 0) {
        return current;
    }
    return ((current + (dir > 0 ? 1 : -1)) % count + count) % count;
}

}  // namespace sh::screens::presets
