// The grouped options rows (src/engine/screens/settings_presets.h): the Classic / Modern tables,
// detection with Custom, the Shadows levels, the preset match and stepping out of Custom.

#include "engine/screens/settings_presets.h"

#include "framework/sh_test.h"

#include <map>
#include <string>

namespace {

namespace pr = sh::screens::presets;

// The values a default config (graphics_preset = enhanced) runs with.
std::map<std::string, float> defaults()
{
    return {
        {"flashlight_mode", 1.0f},       {"flashlight_intensity", 1.20f}, {"flashlight_size", 3.00f},
        {"flashlight_intensity_fps", 2.10f}, {"flashlight_size_fps", 1.30f}, {"soft_highlights", 1.0f},
        {"deband", 1.0f},                {"tonemap", 0.0f},               {"hero_light", 2.0f},
        {"shadows", 0.0f},
        {"shadow_resolution", 1024.0f},
    };
}

pr::Getter getter(const std::map<std::string, float>& values)
{
    return [&values](std::string_view key) {
        const auto it = values.find(std::string(key));
        return it != values.end() ? it->second : -999.0f;
    };
}

void apply(std::map<std::string, float>& values, std::span<const pr::KeyValue> table)
{
    for (const pr::KeyValue& kv : table) {
        values[std::string(kv.key)] = kv.value;
    }
}

}  // namespace

SH_TEST(settings_presets_default_config_is_modern)
{
    const auto v = defaults();
    SH_CHECK(pr::detect_flashlight(getter(v)) == pr::kModern);
    SH_CHECK(pr::detect_lighting(getter(v)) == pr::kModern);
    SH_CHECK(pr::detect_shadow_quality(getter(v)) == pr::kShadowOff);
}

SH_TEST(settings_presets_classic_round_trip)
{
    auto v = defaults();
    apply(v, pr::flashlight_style(pr::kClassic));
    apply(v, pr::lighting_style(pr::kClassic));
    SH_CHECK(v["flashlight_mode"] == 0.0f);
    SH_CHECK(v["hero_light"] == 0.0f);
    SH_CHECK(v["soft_highlights"] == 0.0f);
    SH_CHECK(pr::detect_flashlight(getter(v)) == pr::kClassic);
    SH_CHECK(pr::detect_lighting(getter(v)) == pr::kClassic);
    apply(v, pr::flashlight_style(pr::kModern));
    apply(v, pr::lighting_style(pr::kModern));
    SH_CHECK(v == defaults());
}

SH_TEST(settings_presets_hand_edits_show_custom)
{
    auto v = defaults();
    v["flashlight_intensity"] = 1.5f;
    SH_CHECK(pr::detect_flashlight(getter(v)) == pr::kCustom);
    v = defaults();
    v["flashlight_mode"] = 2.0f;  // the stylised spotlight is neither style
    SH_CHECK(pr::detect_flashlight(getter(v)) == pr::kCustom);
    v = defaults();
    v["hero_light"] = 3.0f;
    SH_CHECK(pr::detect_lighting(getter(v)) == pr::kCustom);
    v["tonemap"] = 2.0f;
    v["hero_light"] = 2.0f;
    SH_CHECK(pr::detect_lighting(getter(v)) == pr::kCustom);
    // Values a config file writes with two decimals still match.
    v = defaults();
    v["flashlight_intensity"] = 1.2001f;
    SH_CHECK(pr::detect_flashlight(getter(v)) == pr::kModern);
}

SH_TEST(settings_presets_shadow_levels)
{
    auto v = defaults();
    v["shadows"] = 1.0f;
    SH_CHECK(pr::detect_shadow_quality(getter(v)) == pr::kShadowMedium);
    v["shadow_resolution"] = 8192.0f;
    SH_CHECK(pr::detect_shadow_quality(getter(v)) == pr::kCustom);
    apply(v, pr::shadow_quality(pr::kShadowHigh));
    SH_CHECK(v["shadow_resolution"] == 2048.0f);
    SH_CHECK(pr::detect_shadow_quality(getter(v)) == pr::kShadowHigh);
    apply(v, pr::shadow_quality(pr::kShadowOff));
    SH_CHECK(v["shadows"] == 0.0f);
    SH_CHECK(v["shadow_resolution"] == 2048.0f);  // Off keeps the size for later
    SH_CHECK(pr::detect_shadow_quality(getter(v)) == pr::kShadowOff);
    apply(v, pr::shadow_quality(pr::kShadowLow));
    SH_CHECK(pr::detect_shadow_quality(getter(v)) == pr::kShadowLow);
    // Ultra is the 4096 map the ultra preset sets, so that preset shows Shadows Ultra, not Custom.
    v["shadow_resolution"] = 4096.0f;
    SH_CHECK(pr::detect_shadow_quality(getter(v)) == pr::kShadowUltra);
    apply(v, pr::shadow_quality(pr::kShadowUltra));
    SH_CHECK(v["shadows"] == 1.0f && v["shadow_resolution"] == 4096.0f);
    SH_CHECK(pr::step_choice(pr::kShadowHigh, pr::kShadowCount, 1) == pr::kShadowUltra);
    SH_CHECK(pr::step_choice(pr::kShadowUltra, pr::kShadowCount, 1) == pr::kShadowOff);
}

SH_TEST(settings_presets_preset_match)
{
    SH_CHECK(pr::style_for_preset(pr::kOriginal) == pr::kClassic);
    SH_CHECK(pr::style_for_preset(pr::kEnhanced) == pr::kModern);
    SH_CHECK(pr::style_for_preset(pr::kHigh) == pr::kModern);
    SH_CHECK(pr::style_for_preset(pr::kUltra) == pr::kModern);
    SH_CHECK(pr::style_for_preset(pr::kUltraRtx) == pr::kModern);
    SH_CHECK(pr::style_for_preset(pr::kUltraRtxUpscaled) == pr::kModern);
    SH_CHECK(!pr::preset_traces_rays(pr::kUltra) && !pr::preset_traces_rays(pr::kUltraUpscaled));
    SH_CHECK(pr::preset_traces_rays(pr::kUltraRtx) && pr::preset_traces_rays(pr::kUltraRtxUpscaled));
    SH_CHECK(!pr::preset_upscales(pr::kUltra) && !pr::preset_upscales(pr::kUltraRtx));
    SH_CHECK(pr::preset_upscales(pr::kUltraUpscaled) && pr::preset_upscales(pr::kUltraRtxUpscaled));
    SH_CHECK(!pr::preset_traces_rays(pr::kCustom) && !pr::preset_upscales(pr::kCustom));
    SH_CHECK(pr::preset_keys().size() == 40);
    // Every feature of RENDER.md section 14 is a preset key, so its own row turns the preset row Custom.
    for (std::string_view key : {"render_scale", "shadow_resolution", "shadow_filter", "msaa", "texture_filter", "hdr_frame",
                                 "bloom", "upscaler", "map_lights", "volumetric_fog", "model_upscaling",
                                 "texture_upscale", "ray_tracing", "rt_lights", "bullet_decals"}) {
        bool found = false;
        for (std::string_view k : pr::preset_keys()) found = found || k == key;
        SH_CHECK(found);
    }
    SH_CHECK(pr::value_name("load_screen_trail", 0) == "off");
    SH_CHECK(pr::value_name("load_screen_trail", 1) == "original");
    SH_CHECK(pr::value_name("load_screen_trail", 2) == "smooth");
    SH_CHECK(pr::value_name("load_screen_trail", 3).empty());
    SH_CHECK(pr::value_name("msaa", 4).empty());
    SH_CHECK(pr::value_name("model_upscaling", 1) == "tessellation");

    // Preset p gives every key the value p; the current values say 1 except one key.
    const auto preset_value = [](int p, std::string_view) { return static_cast<float>(p); };
    std::string odd;
    const pr::Getter current = [&odd](std::string_view key) { return key == odd ? 5.0f : 1.0f; };
    SH_CHECK(pr::detect_preset(preset_value, current) == pr::kEnhanced);
    odd = "msaa";
    SH_CHECK(pr::detect_preset(preset_value, current) == pr::kCustom);
    // Every key at the value 6 is the ultra_rtx_upscaled preset, the last choice: stepping on wraps to original.
    const pr::Getter all6 = [](std::string_view) { return 6.0f; };
    SH_CHECK(pr::detect_preset(preset_value, all6) == pr::kUltraRtxUpscaled);
    SH_CHECK(pr::step_choice(pr::kHigh, pr::kPresetCount, 1) == pr::kUltra);
    SH_CHECK(pr::step_choice(pr::kUltra, pr::kPresetCount, 1) == pr::kUltraUpscaled);
    SH_CHECK(pr::step_choice(pr::kUltraUpscaled, pr::kPresetCount, 1) == pr::kUltraRtx);
    SH_CHECK(pr::step_choice(pr::kUltraRtx, pr::kPresetCount, 1) == pr::kUltraRtxUpscaled);
    SH_CHECK(pr::step_choice(pr::kUltraRtxUpscaled, pr::kPresetCount, 1) == pr::kOriginal);
    SH_CHECK(pr::step_choice(pr::kCustom, pr::kPresetCount, -1) == pr::kUltraRtxUpscaled);
}

SH_TEST(settings_presets_step_choice)
{
    SH_CHECK(pr::step_choice(0, 2, 1) == 1);
    SH_CHECK(pr::step_choice(1, 2, 1) == 0);
    SH_CHECK(pr::step_choice(0, 2, -1) == 1);
    SH_CHECK(pr::step_choice(pr::kCustom, 2, 1) == 0);
    SH_CHECK(pr::step_choice(pr::kCustom, 2, -1) == 1);
    SH_CHECK(pr::step_choice(2, 4, -1) == 1);
    SH_CHECK(pr::step_choice(3, 4, 1) == 0);
    SH_CHECK(pr::step_choice(7, 3, 1) == 0);  // out of range counts as Custom
}

SH_TEST(settings_presets_unknown_choice_is_empty)
{
    SH_CHECK(pr::flashlight_style(pr::kCustom).empty());
    SH_CHECK(pr::lighting_style(5).empty());
    SH_CHECK(pr::shadow_quality(pr::kShadowCount).empty());
    SH_CHECK(!pr::matches({}, getter(defaults())));
}
