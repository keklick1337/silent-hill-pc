// SPDX-License-Identifier: GPL-3.0-or-later
// Registers the player-facing options as engine-menu natives. Plain settings go through the
// settings model (src/engine/screens/settings_model.h, hosted by options_screen.cpp): apply,
// clamping, live-or-restart, config.cfg persistence.
// The grouped rows (Quality Preset, Shadows, Flashlight, Lighting) stand for several keys at
// once; their tables live in src/engine/screens/settings_presets.cpp.

#include "ui/menu/options_binds.h"
#include "ui/menu/options_atmosphere.h"
#include "ui/menu/options_temporal.h"
#include "ui/menu/options_hdr.h"
#include "ui/menu/options_ray_tracing.h"

#include <algorithm>
#include <array>
#include <functional>
#include <cmath>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "engine/app/control_scheme.h"
#include "engine/app/texture_upscale_run.h"
#include "engine/config/pc_config.h"
#include "engine/log/sh_log.h"
#include "engine/menu/menu_model.h"
#include "engine/mods/mods_admin.h"
#include "engine/screens/settings_presets.h"
#include "i18n/localization.h"
#include "input/camera_style.h"
#include "ui/screens/bindings_screen.h"
#include "render/settings/render_preset.h"
#include "ui/screens/pc_settings.h"
#include "render/gpu/backend_name.h"
#include "render/gpu/device_host.h"
#include "render/gpu/render_state.h"
#include "render/gpu/renderer.h"
#include "render/post/hdr.h"
#include "render/post/temporal.h"

extern "C" {
const char* PcOpt_SettingText(const char* key);
int PcOpt_SettingStep(const char* key, int dir);
double PcOpt_SettingFraction(const char* key);
void Pc_OptionsGame_Register(void);
void Pc_Options_ResetToDefaults(void);
// Provided by the controls quick-reference screen (src/ui/screens/pc_controls_ref.cpp).
void Pc_ControlsRef_Open(void) __attribute__((weak));
extern int g_PcQuickWeaponsEnabled;
}

std::vector<std::string> PcOpt_SettingTexts(const char* key);

namespace {

namespace pr = sh::screens::presets;

static_assert(static_cast<int>(pr::kPresetCount) == static_cast<int>(RENDER_PRESET_CUSTOM),
              "the menu's presets are render_preset.h's, Custom excluded");

// The config keys of the plain settings (gamedata/menus/options.lua lists the rows).
constexpr const char* kSettingKeys[] = {
    // Display
    "resolution_aspect", "resolution", "fullscreen", "vsync", "fps_cap",
    // Graphics
    "texture_filter", "msaa", "use_pgxp", "post_process", "fog_strength", "bullet_decals",
    "extra_lights",
    // Audio
    "music_volume", "effects_volume", "fmv_volume", "audio_output", "cutscene_line_gap_ms",
    // Controls
    "control_2d", "mouse_sensitivity", "controller_sensitivity", "invert_mouse_y", "invert_controller_y",
    "aim_assist", "disable_dpad_movement",
    // Gameplay
    "auto_reload", "aim_resume_after_inventory", "fps_fov", "tps_fov", "ots_fov", "tps_ots_aim",
    // Interface
    "low_health_glow", "crosshair", "crosshair_style", "crosshair_size", "text_size", "modding_tools",
};

// Quick weapon switching is not in the settings model: it is a plain flag the combat code reads.
constexpr const char* kQuickWeaponKey = "quick_weapon_switch";

// The menu's ranges of the bar rows (config.cfg takes render_scale 0.25..2.0 and the beam 0..3).
constexpr float kRenderScaleMin = 0.5f;
constexpr float kRenderScaleMax = 2.0f;
constexpr float kBeamMin = 0.5f;
constexpr float kBeamMax = 3.0f;

// Keys whose old values must not outlive a preset switch: flashlight_mode's legacy pair
// (without flashlight_mode, per_pixel_flashlight = 1 would turn the beam into the stylised one).
constexpr const char* kLegacyFlashlightKeys[] = {"per_pixel_flashlight", "flashlight_shadows"};

// A text key: the options view draws it through the game font's translation.
std::string value_label(const char* name)
{
    return std::string("ui.options.value.") + name;
}

std::string on_off(bool on)
{
    return on ? "ui.menu.on" : "ui.menu.off";
}

// Every value a row can show, for the width of the page's value column.
std::function<std::vector<std::string>()> value_labels(std::vector<std::string> names)
{
    std::vector<std::string> keys;
    for (const std::string& n : names) keys.push_back(value_label(n.c_str()));
    return [keys]() { return keys; };
}

std::function<std::vector<std::string>()> on_off_labels()
{
    return []() { return std::vector<std::string>{on_off(true), on_off(false)}; };
}

std::string style_label(int style)
{
    switch (style) {
        case pr::kClassic: return value_label("classic");
        case pr::kModern: return value_label("modern");
        default: return value_label("custom");
    }
}

// Where a grouped key lives in a config block (ints read as whole floats).
int* int_field(s_PcConfig& c, std::string_view key)
{
    if (key == "native_resolution") return &c.nativeResolution;
    if (key == "scaling") return &c.scaling;
    if (key == "dithering") return &c.dithering;
    if (key == "texture_filter") return &c.textureFilter;
    if (key == "msaa") return &c.msaaSamples;
    if (key == "flashlight_mode") return &c.flashlightMode;
    if (key == "shadows") return &c.shadows;
    if (key == "deband") return &c.deband;
    if (key == "soft_highlights") return &c.softHighlights;
    if (key == "post_process") return &c.postProcess;
    if (key == "tonemap") return &c.tonemap;
    if (key == "use_pgxp") return &c.usePgxp;
    if (key == "soft_fog") return &c.softFog;
    if (key == "smooth_models") return &c.smoothModels;
    if (key == "foliage_edge") return &c.foliageEdge;
    if (key == "hero_light") return &c.heroLight;
    if (key == "shadow_resolution") return &c.shadowMapSize;
    if (key == "shadow_filter") return &c.shadowFilter;
    if (key == "water_quality") return &c.waterQuality;
    if (key == "load_screen_trail") return &c.loadScreenTrail;
    if (key == "hdr_frame") return &c.hdrFrame;
    if (key == "hdr_tonemap") return &c.hdrTonemap;
    if (key == "upscaler") return &c.upscaler;
    if (key == "ambient_occlusion") return &c.ambientOcclusion;
    if (key == "map_lights") return &c.mapLights;
    if (key == "sun_shadows") return &c.sunShadows;
    if (key == "volumetric_fog") return &c.volumetricFog;
    if (key == "god_rays") return &c.godRays;
    if (key == "particle_shadows") return &c.particleShadows;
    if (key == "model_upscaling") return &c.modelUpscaling;
    if (key == "texture_upscale") return &c.textureUpscale;
    if (key == "ray_tracing") return &c.rayTracing;
    if (key == "rt_shadows") return &c.rtShadows;
    if (key == "rt_reflections") return &c.rtReflections;
    if (key == "rt_gi") return &c.rtGi;
    if (key == "rt_lights") return &c.rtLights;
    if (key == "extra_lights") return &c.extraLights;
    return nullptr;
}

float* float_field(s_PcConfig& c, std::string_view key)
{
    if (key == "post_process_intensity") return &c.postProcessIntensity;
    if (key == "flashlight_intensity") return &c.flashlightIntensity;
    if (key == "flashlight_size") return &c.flashlightSize;
    if (key == "flashlight_intensity_fps") return &c.flashlightIntensityFps;
    if (key == "flashlight_size_fps") return &c.flashlightSizeFps;
    if (key == "render_scale") return &c.renderScale;
    if (key == "bloom") return &c.bloom;
    return nullptr;
}

float config_value(s_PcConfig& c, std::string_view key)
{
    if (int* i = int_field(c, key)) return static_cast<float>(*i);
    if (float* f = float_field(c, key)) return *f;
    return 0.0f;
}

// What is in effect: hotkeys and the console change the live copy of these three.
float current_value(std::string_view key)
{
    if (key == "use_pgxp") return sh::render::precise().enabled ? 1.0f : 0.0f;
    if (key == "post_process") return static_cast<float>(sh::render::graphics().post_process);
    if (key == "tonemap") return static_cast<float>(sh::render::graphics().tonemap);
    return config_value(g_PcConfig, key);
}

// Saved as config.cfg spells it: a number, or the name of a value for keys that take names.
void save_int(std::string_view key, int v)
{
    char buf[24];
    const std::string_view name = pr::value_name(key, v);
    if (name.empty()) {
        std::snprintf(buf, sizeof(buf), "%d", v);
    } else {
        std::snprintf(buf, sizeof(buf), "%.*s", static_cast<int>(name.size()), name.data());
    }
    PcConfig_SaveKeyValue(std::string(key).c_str(), buf);
}

// False after a start of the upscaler failed (no tool beside the game): the row says so.
bool g_upscale_available = true;

// Sets one grouped key in g_PcConfig and in the renderer. persist = false leaves config.cfg to
// the caller; the keys that go through pc_settings are saved either way, unless live_only.
void apply_key(std::string_view key, float value, bool persist, bool live_only = false)
{
    const int i = static_cast<int>(std::lround(value));
    const std::string k(key);
    if (key == "flashlight_mode") {
        Pc_FlashlightModeApply(i, persist ? 1 : 0);
        return;
    }
    if (float* f = float_field(g_PcConfig, key)) {
        char buf[24];
        std::snprintf(buf, sizeof(buf), "%.2f", static_cast<double>(value));
        const std::string text(buf);
        if (key == "post_process_intensity" || key == "bloom") {
            *f = value;
            if (key == "bloom") {
                sh::post::hdr().bloom = value;
            } else {
                sh::render::graphics().post_process_intensity = value;
            }
            if (persist) PcConfig_SaveKeyValue(k.c_str(), buf);
        } else {
            sh::ui::pc_settings::set_float(k, value, persist ? &text : nullptr);
        }
        return;
    }
    if (key == "tonemap" || key == "hero_light" || key == "shadow_resolution" || key == "shadow_filter" ||
        key == "use_pgxp" || key == "post_process" || key == "msaa" || key == "ambient_occlusion" ||
        key == "map_lights" || key == "sun_shadows" || key == "volumetric_fog" || key == "god_rays" ||
        key == "particle_shadows") {
        sh::ui::pc_settings::set_int(k, i, !live_only);
        return;
    }
    int* field = int_field(g_PcConfig, key);
    if (field == nullptr) {
        return;
    }
    *field = i;
    if (key == "deband") sh::render::graphics().deband = i ? 1 : 0;
    else if (key == "soft_highlights") sh::render::graphics().soft_highlights = i ? 1 : 0;
    else if (key == "shadows") sh::render::graphics().flashlight_shadows = i ? 1 : 0;
    else if (key == "smooth_models") sh::render::precise().smooth_models = i ? 1 : 0;
    else if (key == "foliage_edge") sh::render::graphics().foliage_aa = i;
    else if (key == "scaling") sh::render::picture().present_scale = i;
    else if (key == "soft_fog") sh::render::fog().soft = i ? 1 : 0;
    else if (key == "load_screen_trail") sh::render::graphics().load_screen_trail = i;
    else if (key == "water_quality") sh::render::graphics().water_quality = i;
    else if (key == "hdr_frame") sh::post::hdr().frame = i != 0;
    else if (key == "hdr_tonemap") sh::post::hdr().tonemap = i;
    else if (key == "upscaler") sh::post::temporal().upscaler = i;
    else if (key == "model_upscaling") sh::render::graphics().model_upscaling = i;
    else if (key == "texture_upscale") g_upscale_available = sh::app::apply_texture_upscale(i);
    else if (key == "ray_tracing") sh::render::graphics().ray_tracing = i;
    else if (key == "rt_shadows") sh::render::graphics().rt_shadows = i;
    else if (key == "rt_reflections") sh::render::graphics().rt_reflections = i;
    else if (key == "rt_gi") sh::render::graphics().rt_gi = i;
    else if (key == "rt_lights") sh::render::graphics().rt_lights = i;
    else if (key == "extra_lights") sh::render::graphics().extra_lights = i;
    // native_resolution sizes the render target when the window opens: next start.
    if (persist) save_int(key, i);
}

void apply_table(std::span<const pr::KeyValue> table, bool persist)
{
    for (const pr::KeyValue& kv : table) {
        apply_key(kv.key, kv.value, persist);
    }
}

const pr::Getter& current_getter()
{
    static const pr::Getter g = [](std::string_view key) { return current_value(key); };
    return g;
}

int detect_preset()
{
    s_PcConfig copies[pr::kPresetCount];
    for (int p = 0; p < pr::kPresetCount; p++) {
        copies[p] = g_PcConfig;
        RenderPreset_Apply(p, &copies[p]);
    }
    return pr::detect_preset([&](int p, std::string_view key) { return config_value(copies[p], key); },
                             current_getter());
}

// Makes the preset's keys follow graphics_preset again: live values from the preset's table,
// explicit lines in config.cfg commented out. Flashlight and Lighting take the preset's look.
// keep_resolution leaves native_resolution as it is (Reset to Defaults keeps the resolution).
void apply_preset(int preset, bool keep_resolution)
{
    s_PcConfig target = g_PcConfig;
    RenderPreset_Apply(preset, &target);
    for (std::string_view key : pr::preset_keys()) {
        if (key == "dithering" || key == "texture_filter" || (keep_resolution && key == "native_resolution")) {
            continue;
        }
        const float v = config_value(target, key);
        if (std::fabs(current_value(key) - v) > 0.0005f) {
            apply_key(key, v, false);
        }
    }
    if (target.dithering != g_PcConfig.dithering || target.textureFilter != g_PcConfig.textureFilter) {
        // The Texture Filtering row's combined value: 1 = dither, n + 1 = filter n.
        sh::ui::pc_settings::set_int("texture_filter", target.dithering ? 1 : (target.textureFilter == 0 ? 0 : target.textureFilter + 1));
    }
    const int style = pr::style_for_preset(preset);
    apply_table(pr::flashlight_style(style), true);
    apply_table(pr::lighting_style(style), true);
    g_PcConfig.graphicsPreset = preset;
    PcConfig_SaveKeyValue("graphics_preset", RenderPreset_Name(preset));

    std::vector<const char*> keys;
    std::vector<std::string> names;
    for (std::string_view key : pr::preset_keys()) {
        if (!(keep_resolution && key == "native_resolution")) {
            names.emplace_back(key);
        }
    }
    for (const std::string& n : names) {
        keys.push_back(n.c_str());
    }
    for (const char* k : kLegacyFlashlightKeys) {
        keys.push_back(k);
    }
    PcConfig_RemoveKeys(keys.data(), static_cast<int>(keys.size()));
}

std::string preset_label(int preset)
{
    switch (preset) {
        case pr::kOriginal: return value_label("original");
        case pr::kEnhanced: return value_label("enhanced");
        case pr::kHigh: return value_label("high");
        case pr::kUltra: return value_label("ultra");
        case pr::kUltraUpscaled: return value_label("ultra_upscaled");
        case pr::kUltraRtx: return value_label("ultra_rtx");
        case pr::kUltraRtxUpscaled: return value_label("ultra_rtx_upscaled");
        default: return value_label("custom");
    }
}

std::string shadow_label(int level)
{
    switch (level) {
        case pr::kShadowOff: return value_label("off");
        case pr::kShadowLow: return value_label("low");
        case pr::kShadowMedium: return value_label("medium");
        case pr::kShadowHigh: return value_label("high");
        case pr::kShadowUltra: return value_label("ultra");
        default: return value_label("custom");
    }
}

void register_setting(sh::menu::Registry& reg, const std::string& key)
{
    sh::menu::NativeSetting s;
    s.step = [key](int dir) { PcOpt_SettingStep(key.c_str(), dir); };
    s.text = [key]() { return std::string(PcOpt_SettingText(key.c_str())); };
    s.fraction = [key]() { return PcOpt_SettingFraction(key.c_str()); };
    s.texts = [key]() { return PcOpt_SettingTexts(key.c_str()); };
    reg.native_setting("setting." + key, std::move(s));
}

void register_grouped(sh::menu::Registry& reg)
{
    sh::menu::NativeSetting preset;
    // The Ultra RTX presets are offered only where rays can be traced; a config that names one elsewhere still
    // shows it.
    preset.step = [](int dir) {
        int next = pr::step_choice(detect_preset(), pr::kPresetCount, dir);
        while (pr::preset_traces_rays(next) && !Pc_OptionsRayTracing_Available())
            next = pr::step_choice(next, pr::kPresetCount, dir);
        apply_preset(next, false);
    };
    preset.text = []() { return preset_label(detect_preset()); };
    preset.texts = value_labels(
        {"original", "enhanced", "high", "ultra", "ultra_upscaled", "ultra_rtx", "ultra_rtx_upscaled", "custom"});
    // What a preset could not switch on here, or that its texture upscaling is still running. The "+AI"
    // captions have no room for an "experimental" tag in the value column, so the help line carries it.
    preset.help = []() -> std::string {
        const int p = detect_preset();
        if (pr::preset_upscales(p))
            return *sh::app::texture_upscale_status() != '\0' ? "ui.options.graphics_preset.upscaling"
                                                              : "ui.options.graphics_preset.experimental";
        if (p < pr::kHigh) return "";
        sh::gpu::Renderer* r = sh::gpu::renderer();
        if (r == nullptr || r->host().backend() != sh::gpu::BackendKind::Vulkan)
            return "ui.options.graphics_preset.vulkan_only";
        if (pr::preset_traces_rays(p) && !Pc_OptionsRayTracing_Available())
            return "ui.options.graphics_preset.no_ray_tracing";
        return "";
    };
    reg.native_setting("setting.graphics_preset", std::move(preset));

    sh::menu::NativeSetting shadows;
    shadows.step = [](int dir) {
        const int level = pr::step_choice(pr::detect_shadow_quality(current_getter()), pr::kShadowCount, dir);
        apply_table(pr::shadow_quality(level), true);
    };
    shadows.text = []() { return shadow_label(pr::detect_shadow_quality(current_getter())); };
    shadows.texts = value_labels({"off", "low", "medium", "high", "ultra", "custom"});
    reg.native_setting("setting.shadow_quality", std::move(shadows));

    // The shadow edge kernel: Low / Medium / High = shadow_filter 0 / 1 / 2 (2x2, 3x3, 5x5 texels).
    sh::menu::NativeSetting filter;
    filter.step = [](int dir) {
        const int level = pr::step_choice(std::clamp(g_PcConfig.shadowFilter, 0, 2), 3, dir);
        apply_key("shadow_filter", static_cast<float>(level), true);
    };
    filter.text = []() {
        static const char* const kNames[] = {"low", "medium", "high"};
        return value_label(kNames[std::clamp(g_PcConfig.shadowFilter, 0, 2)]);
    };
    filter.texts = value_labels({"low", "medium", "high"});
    reg.native_setting("setting.shadow_filter", std::move(filter));

    // Scene resolution against the window, 50..200 % in steps of 25; above 100 % it is supersampling.
    sh::menu::NativeSetting scale;
    scale.step = [](int dir) {
        const float v = std::clamp(std::round(g_PcConfig.renderScale * 4.0f) / 4.0f + (dir < 0 ? -0.25f : 0.25f),
                                   kRenderScaleMin, kRenderScaleMax);
        apply_key("render_scale", v, true);
    };
    scale.text = []() { return std::to_string(std::lround(g_PcConfig.renderScale * 100.0f)) + "%"; };
    scale.fraction = []() {
        return (std::clamp(g_PcConfig.renderScale, kRenderScaleMin, kRenderScaleMax) - kRenderScaleMin) /
               static_cast<double>(kRenderScaleMax - kRenderScaleMin);
    };
    scale.texts = []() { return std::vector<std::string>{"50%", "200%"}; };
    reg.native_setting("setting.render_scale", std::move(scale));

    // The beam's brightness and size, as the Modern style calibrates them (flashlight_intensity, flashlight_size).
    for (const char* key : {"flashlight_intensity", "flashlight_size"}) {
        sh::menu::NativeSetting beam;
        beam.step = [key](int dir) {
            const float now = config_value(g_PcConfig, key);
            const float v = std::clamp(std::round((now + (dir < 0 ? -0.1f : 0.1f)) * 20.0f) / 20.0f, kBeamMin, kBeamMax);
            apply_key(key, v, true);
        };
        beam.text = [key]() {
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%.2f", static_cast<double>(config_value(g_PcConfig, key)));
            return std::string(buf);
        };
        beam.fraction = [key]() {
            return (std::clamp(config_value(g_PcConfig, key), kBeamMin, kBeamMax) - kBeamMin) /
                   static_cast<double>(kBeamMax - kBeamMin);
        };
        beam.texts = []() { return std::vector<std::string>{"0.50", "3.00"}; };
        reg.native_setting(std::string("setting.") + key, std::move(beam));
    }

    // A preset key of its own row: the preset row turns Custom once it differs from the preset's value.
    // Shown in the order Smooth, Original, Off (config values 2, 1, 0).
    sh::menu::NativeSetting trail;
    trail.step = [](int dir) {
        const int shown = 2 - std::clamp(g_PcConfig.loadScreenTrail, 0, 2);
        apply_key("load_screen_trail", static_cast<float>(2 - pr::step_choice(shown, 3, dir)), true);
    };
    trail.text = []() {
        const std::string_view name = pr::value_name("load_screen_trail", std::clamp(g_PcConfig.loadScreenTrail, 0, 2));
        return value_label(std::string(name).c_str());
    };
    trail.texts = value_labels({"smooth", "original", "off"});
    reg.native_setting("setting.load_screen_trail", std::move(trail));

    sh::menu::NativeSetting flashlight;
    flashlight.step = [](int dir) {
        const int style = pr::step_choice(pr::detect_flashlight(current_getter()), pr::kStyleCount, dir);
        apply_table(pr::flashlight_style(style), true);
    };
    flashlight.text = []() { return style_label(pr::detect_flashlight(current_getter())); };
    flashlight.texts = value_labels({"classic", "modern", "custom"});
    reg.native_setting("setting.flashlight_style", std::move(flashlight));

    sh::menu::NativeSetting lighting;
    lighting.step = [](int dir) {
        const int style = pr::step_choice(pr::detect_lighting(current_getter()), pr::kStyleCount, dir);
        apply_table(pr::lighting_style(style), true);
    };
    lighting.text = []() { return style_label(pr::detect_lighting(current_getter())); };
    lighting.texts = value_labels({"classic", "modern", "custom"});
    reg.native_setting("setting.lighting_style", std::move(lighting));
}

// The backend is chosen before the window opens, so a change is only saved for the next start. Until the player
// picks one the row shows the backend that is running, which is gl after a failed vulkan start.
std::optional<sh::gpu::BackendKind> g_renderer_choice;

sh::gpu::BackendKind running_backend() {
    if (sh::gpu::Renderer* r = sh::gpu::renderer(); r != nullptr) return r->host().backend();
    return sh::gpu::backend_from_name(g_PcConfig.renderer).value_or(sh::gpu::BackendKind::Gl);
}

sh::gpu::BackendKind chosen_backend() { return g_renderer_choice.value_or(running_backend()); }

bool renderer_restart_pending() { return chosen_backend() != running_backend(); }

std::string backend_label(sh::gpu::BackendKind b) {
    switch (b) {
        case sh::gpu::BackendKind::Vulkan: return value_label("vulkan");
        case sh::gpu::BackendKind::SdlGpu: return value_label("sdlgpu");
        case sh::gpu::BackendKind::Gl: break;
    }
    return value_label("opengl");
}

// The order the row steps through: OpenGL, the native Vulkan backend, Vulkan through SDL3 GPU.
constexpr std::array<sh::gpu::BackendKind, 3> kBackendOrder = {
    sh::gpu::BackendKind::Gl, sh::gpu::BackendKind::Vulkan, sh::gpu::BackendKind::SdlGpu};

// Model upscaling (RENDER.md section 7.10): Off / Tessellation / Mesh shaders on the native Vulkan renderer. A mode
// the device cannot draw is skipped by the row and named in its help; under another renderer the row is locked.
int upscale_available() {
    sh::gpu::Renderer* r = sh::gpu::renderer();
    if (r == nullptr || r->host().backend() != sh::gpu::BackendKind::Vulkan) return 0;
    const sh::rhi::Caps& caps = r->device().caps();
    if (!caps.tessellation || !caps.storageBuffers) return 0;
    return caps.meshShaders ? 2 : 1;
}

void register_model_upscaling(sh::menu::Registry& reg) {
    static constexpr const char* kNames[] = {"off", "tessellation", "mesh_shaders"};
    sh::menu::NativeSetting up;
    up.step = [](int dir) {
        const int top = upscale_available();
        if (top == 0) return;
        const int v = pr::step_choice(std::clamp(g_PcConfig.modelUpscaling, 0, top), top + 1, dir);
        g_PcConfig.modelUpscaling = v;
        sh::render::graphics().model_upscaling = v;
        PcConfig_SaveKeyValue("model_upscaling", kNames[v]);
    };
    up.text = []() { return value_label(kNames[std::clamp(g_PcConfig.modelUpscaling, 0, 2)]); };
    up.texts = value_labels({"off", "tessellation", "mesh_shaders"});
    up.help = []() -> std::string {
        sh::gpu::Renderer* r = sh::gpu::renderer();
        if (r == nullptr || r->host().backend() != sh::gpu::BackendKind::Vulkan)
            return "ui.options.model_upscaling.needs_vulkan";
        const int top = upscale_available();
        if (top == 0) return "ui.options.model_upscaling.no_tessellation";
        if (top == 1) return "ui.options.model_upscaling.no_mesh_shaders";
        return "";
    };
    reg.native_setting("setting.model_upscaling", std::move(up));
    reg.native_enabled("setting.model_upscaling", []() { return upscale_available() > 0; });
}


void register_texture_upscale(sh::menu::Registry& reg) {
    sh::menu::NativeSetting upscale;
    upscale.step = [](int dir) {
        static constexpr int kValues[] = {0, 2, 4};
        constexpr int kCount = 3;
        int at = 0;
        for (int i = 0; i < kCount; ++i)
            if (kValues[i] == g_PcConfig.textureUpscale) at = i;
        g_PcConfig.textureUpscale = kValues[(at + (dir < 0 ? kCount - 1 : 1)) % kCount];
        save_int("texture_upscale", g_PcConfig.textureUpscale);
        g_upscale_available = sh::app::apply_texture_upscale(g_PcConfig.textureUpscale);
    };
    // The scale, and while its pass runs how far it is: the language-neutral text is shown as it is.
    upscale.text = []() -> std::string {
        if (g_PcConfig.textureUpscale == 0) return on_off(false);
        std::string text = "x" + std::to_string(g_PcConfig.textureUpscale);
        if (const char* status = sh::app::texture_upscale_status(); *status != '\0') text += std::string(" ") + status;
        return text;
    };
    upscale.texts = []() { return std::vector<std::string>{on_off(false), "x4 0000/0000"}; };
    upscale.help = []() -> std::string {
        if (!g_upscale_available || !sh::app::texture_upscale_available())
            return "ui.options.texture_upscale.unavailable";
        return *sh::app::texture_upscale_status() != '\0' ? "ui.options.texture_upscale.running" : "";
    };
    reg.native_setting("setting.texture_upscale", std::move(upscale));
}

void register_renderer(sh::menu::Registry& reg) {
    using sh::gpu::BackendKind;
    sh::menu::NativeSetting renderer;
    renderer.step = [](int direction) {
        if (g_PcRendererFromCommandLine != 0) return;
        std::size_t at = 0;
        while (at + 1 < kBackendOrder.size() && kBackendOrder[at] != chosen_backend()) ++at;
        const std::size_t n = kBackendOrder.size();
        const BackendKind next = kBackendOrder[(at + (direction < 0 ? n - 1 : 1)) % n];
        g_renderer_choice = next;
        PcConfig_SaveKeyValue("renderer", std::string(sh::gpu::backend_name(next)).c_str());
    };
    renderer.text = []() { return backend_label(chosen_backend()); };
    renderer.texts = []() {
        std::vector<std::string> out;
        for (BackendKind b : kBackendOrder) out.push_back(backend_label(b));
        return out;
    };
    renderer.help = []() -> std::string {
        if (g_PcRendererFromCommandLine != 0) return "ui.options.renderer.locked";
        if (renderer_restart_pending()) return "ui.options.renderer.restart";
        const auto asked = sh::gpu::backend_from_name(g_PcConfig.renderer);
        if (!g_renderer_choice && asked && *asked != BackendKind::Gl && running_backend() == BackendKind::Gl) {
            return "ui.options.renderer.fallback";
        }
        return "";
    };
    reg.native_setting("setting.renderer", std::move(renderer));
    reg.native_enabled("setting.renderer", []() { return g_PcRendererFromCommandLine == 0; });

    reg.native_action("options.restart", []() {
        if (const sh::Result<void> done = sh::mods::request_restart(); !done) {
            SH_LOG_WARN(RENDER, "options: cannot restart: %.*s", static_cast<int>(done.error().size()),
                        done.error().data());
        }
    });
    reg.native_visible("options.restart", renderer_restart_pending);
    // Run Quick Setup: the next start shows it, and the game restarts now.
    reg.native_action("options.quick_setup", []() {
        g_PcConfig.quickSetup = 1;
        PcConfig_SaveKeyValue("quick_setup", "1");
        if (const sh::Result<void> done = sh::mods::request_restart(); !done) {
            SH_LOG_WARN(GAME, "options: cannot restart into the quick setup: %.*s",
                        static_cast<int>(done.error().size()), done.error().data());
        }
    });
    register_model_upscaling(reg);
}

void register_interface(sh::menu::Registry& reg)
{
    sh::menu::NativeSetting widescreen;
    widescreen.step = [](int dir) {
        sh::ui::pc_settings::set_int("widescreen_mode", pr::step_choice(g_PcConfig.widescreenMode, 3, dir));
    };
    widescreen.text = []() {
        static const char* const kNames[] = {"pillarbox", "hor_plus", "stretch"};
        const int m = g_PcConfig.widescreenMode;
        return value_label(kNames[(m >= 0 && m <= 2) ? m : 1]);
    };
    widescreen.texts = value_labels({"pillarbox", "hor_plus", "stretch"});
    reg.native_setting("setting.widescreen_mode", std::move(widescreen));

    // The languages are the ones found in the text directories (gamedata/text and the mods' text/), each under
    // its own name. They switch only where the original options allow it: the title screen.
    sh::menu::NativeSetting language;
    language.step = [](int dir) {
        const int n = static_cast<int>(sh::i18n::languages().size());
        if (!sh::i18n::language_switch_allowed() || n <= 1) {
            return;
        }
        sh::i18n::set_language(pr::step_choice(sh::i18n::language_index(), n, dir));
    };
    const auto name_of = [](int index) { return sh::i18n::languages()[static_cast<std::size_t>(index)].name; };
    language.text = [name_of]() { return name_of(sh::i18n::language_index()); };
    language.texts = [name_of]() {
        std::vector<std::string> names;
        for (int i = 0; i < static_cast<int>(sh::i18n::languages().size()); i++) names.push_back(name_of(i));
        return names;
    };
    // In play the row stays in sight, dimmed, and its help line says where it can be changed.
    language.help = []() {
        return std::string(sh::i18n::language_switch_allowed() ? "" : "ui.options.language.locked");
    };
    reg.native_setting("setting.language", std::move(language));
    reg.native_visible("setting.language", []() { return sh::i18n::languages().size() > 1; });
    reg.native_enabled("setting.language", []() { return sh::i18n::language_switch_allowed(); });

    sh::menu::NativeSetting debug;
    debug.step = [](int) {
        g_PcConfig.debugOptions = g_PcConfig.debugOptions != 0 ? 0 : 1;
        save_int("debug_options", g_PcConfig.debugOptions);
    };
    debug.text = []() { return on_off(g_PcConfig.debugOptions != 0); };
    debug.texts = on_off_labels();
    reg.native_setting("setting.debug_options", std::move(debug));

    // GAMEPLAY CHANGE, off by default: the quick keys save and load at once.
    sh::menu::NativeSetting instant;
    instant.step = [](int) {
        g_PcConfig.quickSaveInstant = g_PcConfig.quickSaveInstant != 0 ? 0 : 1;
        save_int("quick_save_instant", g_PcConfig.quickSaveInstant);
    };
    instant.text = []() { return on_off(g_PcConfig.quickSaveInstant != 0); };
    instant.texts = on_off_labels();
    instant.help = []() {
        return std::string(g_PcConfig.quickSaveInstant != 0 ? "" : "ui.options.quick_save_instant.help_off");
    };
    reg.native_setting("setting.quick_save_instant", std::move(instant));

    // The quick keys beside their switch: the row shows the keyboard binds and opens the Controls screen on them.
    sh::menu::NativeSetting quick_keys;
    quick_keys.step = [](int) { sh::ui::bindings_screen().open_at_bind("action_quick_save"); };
    quick_keys.text = []() { return std::string(g_PcConfig.keyQuickSave) + " / " + g_PcConfig.keyQuickLoad; };
    reg.native_setting("setting.quick_save_keys", std::move(quick_keys));
    // GAMEPLAY CHANGE, off by default: dead enemies and their blood stay for good.
    sh::menu::NativeSetting remains;
    remains.step = [](int) {
        g_PcConfig.persistentRemains = g_PcConfig.persistentRemains != 0 ? 0 : 1;
        save_int("persistent_remains", g_PcConfig.persistentRemains);
    };
    remains.text = []() { return on_off(g_PcConfig.persistentRemains != 0); };
    remains.texts = on_off_labels();
    reg.native_setting("setting.persistent_remains", std::move(remains));

    sh::menu::NativeSetting weapons;
    weapons.step = [](int) {
        g_PcQuickWeaponsEnabled = g_PcQuickWeaponsEnabled != 0 ? 0 : 1;
        PcConfig_SaveKeyValue(kQuickWeaponKey, g_PcQuickWeaponsEnabled != 0 ? "1" : "0");
    };
    weapons.text = []() { return on_off(g_PcQuickWeaponsEnabled != 0); };
    weapons.texts = on_off_labels();
    reg.native_setting(std::string("setting.") + kQuickWeaponKey, std::move(weapons));
}

// The Camera page: the camera kind (F9 cycles it in game) and the crosshair flags without a PC
// options row.
void register_camera(sh::menu::Registry& reg)
{
    sh::menu::NativeSetting style;
    style.step = [](int dir) {
        sh::input::camera_styles().set(sh::input::next_camera_style(sh::input::camera_styles().style(), dir));
        // Menus navigate with the classic binds whatever the camera; the style switch just put the
        // camera's own binds in place (camera_style_host.cpp restores them on the way back to play).
        sh::app::apply_classic_control_scheme();
    };
    style.text = []() {
        static const char* const kNames[] = {"classic", "third_person", "over_shoulder", "first_person"};
        const int s = static_cast<int>(sh::input::camera_styles().style());
        return value_label(kNames[(s >= 0 && s <= 3) ? s : 0]);
    };
    style.texts = value_labels({"classic", "third_person", "over_shoulder", "first_person"});
    reg.native_setting("setting.control_style", std::move(style));
    // The assist is part of the third person free aim: the classic camera aims with the original's own
    // auto-aim and first person straight down the view, so the row is dimmed there.
    reg.native_enabled("setting.aim_assist",
                       []() { return sh::input::camera_styles().style() == sh::input::CameraStyle::ThirdPerson || sh::input::camera_styles().style() == sh::input::CameraStyle::OverShoulder; });

    struct Flag {
        const char* key;
        int* field;
    };
    for (const Flag f : {Flag{"crosshair_fps_aim_only", &g_PcConfig.crosshairFpsAimOnly},
                         Flag{"crosshair_hide_melee", &g_PcConfig.crosshairHideMelee},
                         Flag{"tps_camera_collision", &g_PcConfig.tpsCameraCollision},
                         Flag{"tps_camera_recenter", &g_PcConfig.tpsCameraRecenter}}) {
        sh::menu::NativeSetting n;
        n.step = [f](int) {
            *f.field = *f.field != 0 ? 0 : 1;
            save_int(f.key, *f.field);
        };
        n.text = [f]() { return on_off(*f.field != 0); };
        n.texts = on_off_labels();
        reg.native_setting(std::string("setting.") + f.key, std::move(n));
    }

    // The spring arm's smoothing, 0..100 in steps of 10.
    for (const Flag f : {Flag{"tps_camera_smoothing", &g_PcConfig.tpsCameraSmoothing},
                         Flag{"tps_look_smoothing", &g_PcConfig.tpsLookSmoothing}}) {
        sh::menu::NativeSetting n;
        n.step = [f](int dir) {
            *f.field = std::clamp(*f.field + (dir < 0 ? -10 : 10), 0, 100);
            save_int(f.key, *f.field);
        };
        n.text = [f]() { return std::to_string(*f.field) + "%"; };
        n.fraction = [f]() { return *f.field / 100.0; };
        n.texts = []() { return std::vector<std::string>{"0%", "100%"}; };
        reg.native_setting(std::string("setting.") + f.key, std::move(n));
    }

    // visible_if over config.cfg keys: the camera kind and the renderer by their config names, the rest as numbers.
    reg.config_reader("config.cfg", [](std::string_view key) -> std::optional<sh::menu::Value> {
        if (key == "control_style") return sh::menu::Value{std::string(sh::input::camera_style_id(sh::input::camera_styles().style()))};
        if (key == "crosshair_fps_aim_only") return sh::menu::Value{static_cast<double>(g_PcConfig.crosshairFpsAimOnly)};
        if (key == kQuickWeaponKey) return sh::menu::Value{static_cast<double>(g_PcQuickWeaponsEnabled)};
        if (key == "debug_options") return sh::menu::Value{static_cast<double>(g_PcConfig.debugOptions)};
        // The renderer the row has chosen, so its rows come and go before the restart that brings it.
        if (key == "renderer") return sh::menu::Value{std::string(sh::gpu::backend_name(chosen_backend()))};
        if (key == "ray_tracing") return sh::menu::Value{static_cast<double>(g_PcConfig.rayTracing)};
        double v = 0.0;
        if (sh::ui::pc_settings::known(std::string(key), v)) return sh::menu::Value{v};
        return std::nullopt;
    });
}

}  // namespace

extern "C" int Pc_Options_GraphicsKeyValue(const char* key, float* value)
{
    const std::string_view k(key);
    if (int_field(g_PcConfig, k) == nullptr && float_field(g_PcConfig, k) == nullptr) return 0;
    *value = current_value(k);
    return 1;
}

extern "C" int Pc_Options_ApplyGraphicsKeyLive(const char* key, float value)
{
    const std::string_view k(key);
    if (int_field(g_PcConfig, k) == nullptr && float_field(g_PcConfig, k) == nullptr) return 0;
    apply_key(k, value, false, true);
    return 1;
}

extern "C" void Pc_OptionsBinds_Register(void)
{
    sh::menu::Registry& reg = sh::menu::registry();
    for (const char* key : kSettingKeys) {
        register_setting(reg, key);
    }
    register_grouped(reg);
    register_interface(reg);
    register_camera(reg);
    register_renderer(reg);
    Pc_OptionsRayTracing_Register(reg);
    sh::ui::register_hdr_options(reg);
    sh::ui::register_atmosphere_rows(reg);
    sh::ui::register_temporal_rows(reg);
    register_texture_upscale(reg);

    reg.native_action("options.reset_defaults", []() {
        Pc_Options_ResetToDefaults();
        // Last, so the grouped keys end on the shipped look whatever the pages reset above.
        apply_preset(RENDER_PRESET_DEFAULT, true);
        g_PcConfig.debugOptions = 0;
        PcConfig_SaveKeyValue("debug_options", "0");
        g_PcQuickWeaponsEnabled = 0;
        PcConfig_SaveKeyValue(kQuickWeaponKey, "0");
        g_PcConfig.quickSaveInstant = PcConfig_Defaults()->quickSaveInstant;
        save_int("quick_save_instant", g_PcConfig.quickSaveInstant);
        g_PcConfig.persistentRemains = PcConfig_Defaults()->persistentRemains;
        save_int("persistent_remains", g_PcConfig.persistentRemains);
        g_PcConfig.textureUpscale = PcConfig_Defaults()->textureUpscale;
        save_int("texture_upscale", g_PcConfig.textureUpscale);
        sh::app::apply_texture_upscale(g_PcConfig.textureUpscale);
        g_PcConfig.crosshairFpsAimOnly = PcConfig_Defaults()->crosshairFpsAimOnly;
        save_int("crosshair_fps_aim_only", g_PcConfig.crosshairFpsAimOnly);
        g_PcConfig.crosshairHideMelee = PcConfig_Defaults()->crosshairHideMelee;
        save_int("crosshair_hide_melee", g_PcConfig.crosshairHideMelee);
        g_PcConfig.tpsCameraCollision = PcConfig_Defaults()->tpsCameraCollision;
        save_int("tps_camera_collision", g_PcConfig.tpsCameraCollision);
        g_PcConfig.tpsCameraRecenter = PcConfig_Defaults()->tpsCameraRecenter;
        save_int("tps_camera_recenter", g_PcConfig.tpsCameraRecenter);
        g_PcConfig.tpsCameraSmoothing = PcConfig_Defaults()->tpsCameraSmoothing;
        save_int("tps_camera_smoothing", g_PcConfig.tpsCameraSmoothing);
        g_PcConfig.tpsLookSmoothing = PcConfig_Defaults()->tpsLookSmoothing;
        save_int("tps_look_smoothing", g_PcConfig.tpsLookSmoothing);
    });
    reg.native_action("options.edit_keybinds", []() { sh::ui::bindings_screen().open_from_menu(); });
    reg.native_action("options.controls_reference", []() {
        if (Pc_ControlsRef_Open != nullptr) {
            Pc_ControlsRef_Open();
        }
    });
    // The original options' rows (vibration, volumes, the extra options) and the screen's own actions.
    Pc_OptionsGame_Register();
}
