// SPDX-License-Identifier: GPL-3.0-or-later
// The keys of config.cfg: what each one sets, its built-in default and the values it takes. PcConfig_Load
// (pc_config.cpp) reads the file with these tables, g_PcConfig starts as built_in_defaults(), and the reference test
// (tests/gconfig/config_reference_test.cpp) checks assets/config.cfg against them.
#pragma once

#include <climits>
#include <cstdint>
#include <span>
#include <string_view>

#include "engine/config/pc_config.h"
#include "input/glyphs/glyph_table.h"
#include "render/settings/render_preset.h"
#include "ui/screens/title_style.h"

namespace sh::config {

// How the text after "=" becomes the value.
enum class Kind : std::uint8_t {
    Flag,          // a number, nonzero = 1
    Whole,         // a number clamped to [lo, hi]
    WholeAtLeast,  // a number taken only when it is at least lo
    WholeOr,       // a number in [lo, hi], anything else is `otherwise`
    Real,          // a decimal clamped to [lo, hi]
    RealPositive,  // a decimal taken only when above zero, then clamped to [lo, hi]
    Word,          // one of `words`; anything else is `otherwise`, or keeps the value with keep_unknown
    Text,          // copied, cut to the size of the field
    TextFit,       // copied only when not empty and it fits the field whole
    Custom,        // read by the hook
    Elsewhere,     // read by another part of the game or only written by it; accepted and left alone
};

// What a key does besides storing its value (pc_config.cpp carries them out). A Custom key is read by its hook;
// any other kind runs the hook after the value is stored.
enum class Hook : std::uint8_t {
    None,
    Culling,            // modern|original or 1|0; marks culling as given, so disable_culling yields to it
    ObsoleteCulling,    // disable_culling: 1 = modern, 0 = original, unless culling was given
    WeatherRate,        // 30 or 60
    Msaa,               // snapped down to 8, 4, 2 or 0
    GraphicsPreset,     // a preset name; already applied before the file was read
    GlyphSet,           // the icon set of a number
    AimZoomSwitch,      // tps_aim_zoom: the on/off form of tps_aim_zoom_amount, 100 or 0
    RecenterDelay,      // 0..30 seconds, a value that is not a number is 0
    NewGameDifficulty,  // easy|normal|hard into g_PcNewGameDifficulty
    QuickWeapons,       // quick_weapon_switch into the quick actions
    DitherKey,          // dithering or texture_filter given: psx_dither does not override them
    FlashlightMode,     // flashlight_mode given: the legacy pair does not derive it
    DreamBlur,          // also switches the renderer's feedback
    DreamBlurStrength,  // also sets the renderer's feedback gain
    LoadScreenMin,      // also sets the load screen's minimum in vblanks
    LogKey,             // a log_* key, handed to the log
};

struct Word {
    std::string_view text;
    int value;
};

struct Setting {
    std::string_view key;
    Kind kind;
    int s_PcConfig::* whole = nullptr;
    float s_PcConfig::* real = nullptr;
    std::span<char> (*text)(s_PcConfig&) = nullptr;
    double initial = 0;
    std::string_view initial_text = {};
    double lo = 0;
    double hi = 0;
    std::span<const Word> words = {};
    int otherwise = 0;
    bool keep_unknown = false;
    Hook hook = Hook::None;
    std::string_view about = {};

    constexpr Setting then(Hook h) const {
        Setting s = *this;
        s.hook = h;
        return s;
    }
    constexpr Setting owning(int s_PcConfig::* field, double value) const {
        Setting s = *this;
        s.whole = field;
        s.initial = value;
        return s;
    }
    constexpr Setting owning(float s_PcConfig::* field, double value) const {
        Setting s = *this;
        s.real = field;
        s.initial = value;
        return s;
    }
};

template <auto Field>
constexpr std::span<char> text_field(s_PcConfig& c) {
    return std::span<char>(c.*Field);
}

namespace keys {

constexpr Setting flag(std::string_view key, int s_PcConfig::* field, int initial, std::string_view about) {
    return {.key = key, .kind = Kind::Flag, .whole = field, .initial = double(initial), .about = about};
}
constexpr Setting whole(std::string_view key, int s_PcConfig::* field, int initial, int lo, int hi,
                        std::string_view about) {
    return {.key = key,
            .kind = Kind::Whole,
            .whole = field,
            .initial = double(initial),
            .lo = double(lo),
            .hi = double(hi),
            .about = about};
}
constexpr Setting any_whole(std::string_view key, int s_PcConfig::* field, int initial, std::string_view about) {
    return whole(key, field, initial, INT_MIN, INT_MAX, about);
}
constexpr Setting at_least(std::string_view key, int s_PcConfig::* field, int initial, int lo, std::string_view about) {
    return {.key = key,
            .kind = Kind::WholeAtLeast,
            .whole = field,
            .initial = double(initial),
            .lo = double(lo),
            .about = about};
}
constexpr Setting whole_or(std::string_view key, int s_PcConfig::* field, int initial, int lo, int hi, int otherwise,
                           std::string_view about) {
    return {.key = key,
            .kind = Kind::WholeOr,
            .whole = field,
            .initial = double(initial),
            .lo = double(lo),
            .hi = double(hi),
            .otherwise = otherwise,
            .about = about};
}
constexpr Setting real(std::string_view key, float s_PcConfig::* field, double initial, double lo, double hi,
                       std::string_view about) {
    return {.key = key, .kind = Kind::Real, .real = field, .initial = initial, .lo = lo, .hi = hi, .about = about};
}
constexpr Setting positive(std::string_view key, float s_PcConfig::* field, double initial, double lo, double hi,
                           std::string_view about) {
    return {
        .key = key, .kind = Kind::RealPositive, .real = field, .initial = initial, .lo = lo, .hi = hi, .about = about};
}
constexpr Setting word(std::string_view key, int s_PcConfig::* field, int initial, std::span<const Word> words,
                       int otherwise, std::string_view about) {
    return {.key = key,
            .kind = Kind::Word,
            .whole = field,
            .initial = double(initial),
            .words = words,
            .otherwise = otherwise,
            .about = about};
}
constexpr Setting word_or_keep(std::string_view key, int s_PcConfig::* field, int initial, std::span<const Word> words,
                               std::string_view about) {
    return {.key = key,
            .kind = Kind::Word,
            .whole = field,
            .initial = double(initial),
            .words = words,
            .keep_unknown = true,
            .about = about};
}
constexpr Setting text(std::string_view key, std::span<char> (*field)(s_PcConfig&), std::string_view initial,
                       std::string_view about) {
    return {.key = key, .kind = Kind::Text, .text = field, .initial_text = initial, .about = about};
}
constexpr Setting text_fit(std::string_view key, std::span<char> (*field)(s_PcConfig&), std::string_view initial,
                           std::string_view about) {
    return {.key = key, .kind = Kind::TextFit, .text = field, .initial_text = initial, .about = about};
}
constexpr Setting custom(std::string_view key, Hook hook, std::string_view about) {
    return {.key = key, .kind = Kind::Custom, .hook = hook, .about = about};
}
constexpr Setting elsewhere(std::string_view key, std::string_view about) {
    return {.key = key, .kind = Kind::Elsewhere, .about = about};
}

inline constexpr Word kTrail[] = {{"off", 0}, {"original", 1}, {"smooth", 2}};
inline constexpr Word kStickOrPad[] = {{"analog", 0}, {"dpad", 1}};
inline constexpr Word kTitleStyles[] = {{"us", ui::kTitleStyleUs},
                                        {"western", ui::kTitleStyleUs},
                                        {"japanese", ui::kTitleStyleJapanese},
                                        {"jp", ui::kTitleStyleJapanese}};
// "ots" was a third person held over one shoulder; third person rests there now.
inline constexpr Word kCameraStyles[] = {{"tps", 1}, {"fps", 2}, {"ots", 1}};
inline constexpr Word kSpeakers[] = {{"stereo", 1}, {"quad", 2}, {"51", 3}, {"71", 4}, {"hrtf", 5}};
inline constexpr Word kAspects[] = {{"raw", 1}, {"accurate", 1}};
inline constexpr Word kSoftwareGl[] = {{"auto", 0}, {"force", 1}, {"off", 2}};
inline constexpr Word kTouchControls[] = {{"auto", 0}, {"0", 0}, {"on", 1}, {"1", 1}, {"off", 2}, {"2", 2}};
inline constexpr Word kTouchMovement[] = {{"stick", 0}, {"0", 0}, {"dpad", 1}, {"1", 1}};
inline constexpr Word kWater[] = {{"low", 0}, {"0", 0}, {"medium", 1}, {"1", 1}, {"high", 2}, {"2", 2}};
inline constexpr Word kUpscaling[] = {{"off", 0},          {"0", 0}, {"tessellation", 1}, {"1", 1},
                                      {"mesh_shaders", 2}, {"2", 2}};

using C = s_PcConfig;

}  // namespace keys

// clang-format off
inline constexpr Setting kSettings[] = {
    // Window and frame pacing.
    keys::at_least("width", &keys::C::windowWidth, 1024, 320, "window width in pixels, from 320"),
    keys::at_least("height", &keys::C::windowHeight, 768, 240, "window height in pixels, from 240"),
    keys::whole_or("fullscreen", &keys::C::fullscreen, 0, 0, 2, 0, "0 windowed, 1 exclusive, 2 borderless"),
    keys::flag("confine_cursor", &keys::C::confineCursor, 1, "keep the pointer in a fullscreen window"),
    keys::any_whole("vsync", &keys::C::vsync, 0, "0 off, 1 on, -1 adaptive"),
    keys::at_least("refresh_rate", &keys::C::refreshRate, 0, 0, "fullscreen refresh rate in Hz, 0 = the display's"),
    keys::any_whole("fps_cap", &keys::C::fpsCap, 60, "gameplay frame cap, 0 = none"),
    keys::flag("menu_fps_unlock", &keys::C::menuFpsUnlock, 0, "screens other than gameplay follow fps_cap"),
    keys::custom("weather_sim_hz", Hook::WeatherRate, "rain and snow between their 30 steps a second: 60 moving on every frame, 30 the steps only")
        .owning(&keys::C::weatherSimHz, 60),
    keys::whole("skip_intros", &keys::C::skipIntros, 0, 0, 2, "1 skips the logos and the intro, 2 also starts a game"),
    keys::flag("quick_setup", &keys::C::quickSetup, 0, "1 shows the quick setup at the next start"),
    keys::whole_or("show_console", &keys::C::showConsole, 0, 0, 3, 0, "1 or 3 opens an external console window"),
    keys::any_whole("config_version", &keys::C::configVersion, 0, "the version the file was last migrated to"),

    // Picture.
    keys::custom("culling", Hook::Culling, "world draw culling: modern or original").owning(&keys::C::culling, 1),
    keys::custom("disable_culling", Hook::ObsoleteCulling, "obsolete form of culling: 1 modern, 0 original"),
    keys::whole("draw_distance_pct", &keys::C::drawDistancePct, 100, 100, 200, "far draw cap in percent of the original"),
    keys::flag("preload_chunks", &keys::C::preloadChunks, 1, "load every chunk of a map at once"),
    keys::whole("psx_dither", &keys::C::legacyDitherFilter, -1, 0, 7, "obsolete: dithering and texture_filter in one value"),
    keys::flag("dithering", &keys::C::dithering, 1, "the console's ordered dither").then(Hook::DitherKey),
    keys::whole("texture_filter", &keys::C::textureFilter, 0, 0, 6, "0 point, 1 bilinear, 2 trilinear, 3..6 anisotropic")
        .then(Hook::DitherKey),
    keys::whole("texture_upscale", &keys::C::textureUpscale, 0, 0, 4, "AI upscaled textures: 0 off, 2, 4"),
    keys::flag("image_adjust_scope", &keys::C::imageAdjustScope, 1, "brightness, contrast, saturation: 1 world frames only"),
    keys::flag("native_resolution", &keys::C::nativeResolution, 0, "render at the console's own resolution"),
    keys::whole_or("scaling", &keys::C::scaling, 2, 0, 3, 2, "upscale: 0 integer, 1 nearest, 2 bilinear, 3 sharp"),
    keys::flag("menu_filter", &keys::C::menuFilter, 0, "bilinear filter on 2D screens"),
    keys::whole_or("widescreen_mode", &keys::C::widescreenMode, 1, 0, 2, 0, "0 pillarbox, 1 Hor+, 2 stretch"),
    keys::flag("menu_pillarbox", &keys::C::menuPillarbox, 1, "2D screens keep 4:3 with bars"),
    keys::flag("resident_textures", &keys::C::residentTextures, 1, "every material keeps its own texture slot"),
    keys::whole("whole_map_exteriors", &keys::C::wholeMapExteriors, 2, 0, 2, "draw the whole town: 0 off, 1 on, 2 under thin fog"),
    keys::flag("use_pgxp", &keys::C::usePgxp, 1, "sub-pixel vertices and perspective-correct textures"),
    keys::flag("psx_poly_size_cull", &keys::C::polySizeCull, 1, "drop triangles larger than the console's GPU drew"),
    keys::custom("msaa", Hook::Msaa, "multisample count: 0, 2, 4 or 8").owning(&keys::C::msaaSamples, 0),
    keys::text("renderer", &text_field<&keys::C::renderer>, "gl", "graphics backend: gl, vulkan or sdlgpu"),
    keys::flag("vulkan_validation", &keys::C::vulkanValidation, 0, "Vulkan validation layers when installed"),
    keys::flag("shader_prewarm", &keys::C::shaderPrewarm, 1, "renderer = vulkan: build known pipelines before play"),
    keys::word("software_gl", &keys::C::softwareGl, 0, keys::kSoftwareGl, 0, "rendering on the CPU: auto, force or off"),
    keys::flag("ray_tracing", &keys::C::rayTracing, 0, "renderer = vulkan: KHR ray tracing of the scene"),
    keys::flag("rt_shadows", &keys::C::rtShadows, 0, "ray_tracing: flashlight shadows by rays"),
    keys::flag("rt_reflections", &keys::C::rtReflections, 0, "ray_tracing: reflections on water, wet and polished surfaces"),
    keys::flag("rt_gi", &keys::C::rtGi, 0, "ray_tracing: one bounce of the flashlight"),
    keys::flag("rt_lights", &keys::C::rtLights, 0, "ray_tracing: map lights, sun and specular highlights"),
    keys::flag("extra_lights", &keys::C::extraLights, 0, "lamps placed by hand where the rooms show fixtures"),
    keys::whole("post_process", &keys::C::postProcess, 0, 0, INT_MAX, "post-process filter, 0 = off"),
    keys::flag("fxaa", &keys::C::fxaa, 0, "FXAA edge smoothing"),
    keys::flag("widescreen_edge_fix", &keys::C::wideEdgeFix, 1, "widen the oversize-triangle limit with the aspect"),
    keys::whole("tonemap", &keys::C::tonemap, 0, 0, 3, "0 off, 1 Reinhard, 2 ACES, 3 Filmic"),
    keys::custom("graphics_preset", Hook::GraphicsPreset, "the defaults of the keys marked (preset)")
        .owning(&keys::C::graphicsPreset, RENDER_PRESET_DEFAULT),
    keys::positive("render_scale", &keys::C::renderScale, 1.0, 0.25, 2.0, "scene size relative to the window"),
    keys::flag("deband", &keys::C::deband, 0, "noise against banding when dither is off"),
    keys::flag("soft_highlights", &keys::C::softHighlights, 0, "the per-pixel flashlight rolls off toward white"),
    keys::flag("shadows", &keys::C::shadows, 1, "0 = no flashlight shadows whatever the mode"),
    keys::whole("flashlight_mode", &keys::C::flashlightMode, 0, 0, 3, "0 Classic, 1 Classic + Shadows, 2 Modern, 3 Modern + Shadows")
        .then(Hook::FlashlightMode),
    keys::flag("per_pixel_flashlight", &keys::C::perPixelFlashlight, 0, "obsolete half of flashlight_mode"),
    keys::flag("flashlight_shadows", &keys::C::flashlightShadows, 1, "obsolete half of flashlight_mode"),
    keys::real("flashlight_intensity", &keys::C::flashlightIntensity, 1.20, 0.0, 3.0, "per-pixel flashlight brightness"),
    keys::real("flashlight_size", &keys::C::flashlightSize, 3.00, 0.0, 3.0, "per-pixel flashlight cone size"),
    keys::real("flashlight_intensity_fps", &keys::C::flashlightIntensityFps, 2.10, 0.0, 3.0, "flashlight brightness in first person"),
    keys::real("flashlight_size_fps", &keys::C::flashlightSizeFps, 1.30, 0.0, 3.0, "flashlight cone size in first person"),
    keys::real("fog_strength", &keys::C::fogStrength, 1.00, 0.0, 2.0, "distance fog density"),
    keys::flag("soft_fog", &keys::C::softFog, 0, "smooth fog ramp instead of the stepped one"),
    keys::flag("ambient_occlusion", &keys::C::ambientOcclusion, 0, "ambient occlusion of the map's light (native Vulkan)"),
    keys::real("ao_strength", &keys::C::aoStrength, 0.35, 0.0, 1.0, "how dark ambient occlusion gets"),
    keys::flag("volumetric_fog", &keys::C::volumetricFog, 0, "froxel fog lit by the flashlight (native Vulkan)"),
    keys::real("fog_scatter", &keys::C::fogScatter, 0.25, 0.0, 1.0, "share of the beam the volumetric fog scatters"),
    keys::real("fog_anisotropy", &keys::C::fogAnisotropy, 0.2, -0.9, 0.9, "forward scattering of the volumetric fog"),
    keys::flag("map_lights", &keys::C::mapLights, 0, "the map's lights and sun without ray tracing (native Vulkan)"),
    keys::flag("sun_shadows", &keys::C::sunShadows, 0, "map_lights: the sun's shadow map (native Vulkan)"),
    keys::flag("god_rays", &keys::C::godRays, 0, "volumetric_fog: the sun's shafts through the shadow map"),
    keys::flag("particle_shadows", &keys::C::particleShadows, 0, "falling snow and ash cast small soft shadows"),
    keys::whole("upscaler", &keys::C::upscaler, 0, 0, 3, "native Vulkan: 0 off, 1 TAA, 2 TAAU, 3 spatial upscale"),
    keys::whole("upscale_quality", &keys::C::upscaleQuality, 0, 0, 3,
                "TAAU / spatial render size: 0 quality, 1 balanced, 2 performance, 3 ultra performance"),
    keys::real("upscale_sharpness", &keys::C::upscaleSharpness, 0.25, 0.0, 1.0, "sharpening after TAA or the upscale"),
    keys::flag("temporal_debug", &keys::C::temporalDebug, 0, "show the motion vectors instead of the picture"),
    keys::whole("present_mode", &keys::C::presentMode, 0, 0, 4,
                "native Vulkan: 0 auto, 1 FIFO, 2 mailbox, 3 immediate, 4 FIFO relaxed"),
    keys::whole("frames_in_flight", &keys::C::framesInFlight, 2, 1, 2, "native Vulkan: frames recorded ahead of the GPU"),
    keys::flag("present_wait", &keys::C::presentWait, 0, "native Vulkan: wait for the frame before to be shown"),
    keys::word_or_keep("load_screen_trail", &keys::C::loadScreenTrail, 2, keys::kTrail, "loading screen trail: off, original, smooth"),
    keys::whole("foliage_edge", &keys::C::foliageEdge, 0, 0, 2, "foliage cutout: 0 original, 1 contour, 2 with alpha to coverage"),
    keys::flag("smooth_models", &keys::C::smoothModels, 1, "full-precision bone transforms for drawing"),
    keys::real("post_process_intensity", &keys::C::postProcessIntensity, 1.0, 0.0, 1.0, "post-process mix"),
    keys::real("tonemap_intensity", &keys::C::tonemapIntensity, 1.0, 0.0, 1.0, "tone-map mix"),
    keys::flag("hdr_frame", &keys::C::hdrFrame, 0, "Vulkan: float scene target, light past white kept"),
    keys::whole("hdr_tonemap", &keys::C::hdrTonemap, 0, 0, 3, "HDR frame: 0 clamp, 1 soft, 2 ACES, 3 Reinhard"),
    keys::real("bloom", &keys::C::bloom, 0.0, 0.0, 1.0, "HDR frame: share of the light past the threshold that spreads"),
    keys::real("bloom_threshold", &keys::C::bloomThreshold, 1.0, 0.5, 4.0, "HDR frame: light above this level blooms"),
    keys::flag("hdr_output", &keys::C::hdrOutput, 0, "HDR10 output where the display offers it (needs hdr_frame)"),
    keys::real("hdr_paper_white", &keys::C::hdrPaperWhite, 200.0, 80.0, 500.0, "HDR10: nits of SDR white"),
    keys::real("hdr_peak", &keys::C::hdrPeak, 1000.0, 400.0, 10000.0, "HDR10: brightest nits sent"),
    keys::real("brightness", &keys::C::brightness, 1.0, 0.25, 2.0, "output brightness"),
    keys::real("contrast", &keys::C::contrast, 1.0, 0.5, 2.0, "output contrast"),
    keys::real("saturation", &keys::C::saturation, 1.0, 0.0, 2.0, "output saturation"),
    keys::flag("low_health_glow", &keys::C::lowHealthGlow, 0, "red edge glow at low health"),
    keys::flag("bullet_decals", &keys::C::bulletDecals, 0, "bullet holes on the world"),
    keys::custom("gamepad_icons", Hook::GlyphSet, "button icons: 0 auto, 1..5 a set")
        .owning(&keys::C::gamepadIcons, static_cast<int>(input::GlyphSet::Auto)),
    keys::flag("dream_blur", &keys::C::dreamBlur, 1, "the dream ghosting blur").then(Hook::DreamBlur),
    keys::real("dream_blur_strength", &keys::C::dreamBlurStrength, 1.0, 0.0, 1.0, "feedback gain of that blur")
        .then(Hook::DreamBlurStrength),
    keys::whole("hero_light", &keys::C::heroLight, 2, 0, 3, "fill light on characters: 0 off .. 3 high"),
    keys::flag("hero_light_flashlight", &keys::C::heroLightWithFlashlight, 0, "keep the fill light while the flashlight is on"),
    keys::whole("brightness_level_bias", &keys::C::brightnessLevelBias, 3, 0, 7, "steps the Brightness Level is shifted down"),
    keys::whole("shadow_resolution", &keys::C::shadowMapSize, 1024, 256, 8192, "flashlight shadow map size"),
    keys::word_or_keep("water_quality", &keys::C::waterQuality, 1, keys::kWater, "low, medium or high; anything else keeps the preset's"),
    keys::whole("shadow_filter", &keys::C::shadowFilter, 1, 0, 2, "shadow edge filter: 2x2, 3x3, 5x5"),
    keys::word("model_upscaling", &keys::C::modelUpscaling, 0, keys::kUpscaling, 0, "off, tessellation or mesh_shaders (renderer = vulkan)"),
    keys::whole("fps_display", &keys::C::fpsDisplay, 0, 0, 4, "FPS counter corner, 0 = off"),
    keys::flag("fps_profiler", &keys::C::fpsProfiler, 0, "the counter grows into the profiler"),
    keys::flag("perf_record", &keys::C::perfRecord, 0, "record frame times and GPU passes from the start to perfrec/"),
    keys::word("display_aspect", &keys::C::aspectRaw, 0, keys::kAspects, 0, "crt (4:3) or raw (pixel_aspect)"),
    keys::positive("crt_aspect_trim", &keys::C::crtAspectTrim, 1.06, 0.50, 1.50, "multiplier of the crt 4:3 target"),
    keys::positive("world_hscale", &keys::C::worldHScale, 1.0, 0.25, 2.00, "horizontal field of view scale"),
    keys::positive("world_vscale", &keys::C::worldVScale, 1.08, 0.25, 2.00, "vertical field of view scale"),
    keys::positive("pixel_aspect", &keys::C::pixelAspect, 35.0 / 32.0, 0.50, 2.00, "pixel aspect of display_aspect = raw"),
    keys::real("world_vshift", &keys::C::worldVShift, 0.0, -60.0, 60.0, "projection centre shift in rows, gameplay"),
    keys::real("cutscene_vshift", &keys::C::cutsceneVShift, 0.0, -60.0, 60.0, "projection centre shift in rows, cutscenes"),

    // Sound and movies.
    keys::real("xa_volume", &keys::C::xaVolume, 1.0, 0.0, 1.0, "cutscene voice volume"),
    keys::real("fmv_volume", &keys::C::fmvVolume, 1.0, 0.0, 1.0, "movie volume"),
    keys::flag("fmv_psx_volume", &keys::C::fmvOriginalVolume, 1, "movies at the console's 80/128 level"),
    keys::word("audio_output", &keys::C::audioOutput, 0, keys::kSpeakers, 0, "auto, stereo, quad, 51, 71 or hrtf"),
    keys::any_whole("cutscene_line_gap_ms", &keys::C::cutsceneLineGapMs, 300, "least silence between cutscene lines"),
    keys::whole("msg_voice_tail_ms", &keys::C::msgVoiceTailMs, 500, 0, 10000, "how long a voiced text box stays after the clip"),
    keys::whole("msg_voice_lead_ms", &keys::C::msgVoiceLeadMs, 500, 0, 5000, "how long a voiced text box shows before the clip"),

    // Loading.
    keys::flag("cd_fast_load", &keys::C::cdFastLoad, 1, "blocking loads skip the console's waits"),
    keys::flag("cd_speed_ps1", &keys::C::cdConsolePace, 0, "reads at the console's drive speed"),
    keys::real("load_screen_min", &keys::C::loadScreenMin, 3.0, 0.0, 10.0, "least seconds of the loading screen")
        .then(Hook::LoadScreenMin),
    keys::flag("attract_demos", &keys::C::attractDemos, 1, "demo plays after the title sits idle"),

    // Developer.
    keys::flag("enable_debug_log", &keys::C::enableDebugLog, 0, "obsolete: a log file that takes debug lines"),
    keys::flag("world_objects_shadow", &keys::C::worldObjectsShadow, 0, "log what a map draws of its world objects"),
    keys::flag("allow_debug_controls", &keys::C::allowDebugControls, 0, "developer keys"),
    keys::flag("debug_options", &keys::C::debugOptions, 0, "debug options in the pause menu"),
    keys::flag("console_enabled", &keys::C::consoleEnabled, 1, "key_console opens the console"),
    keys::text("console_font", &text_field<&keys::C::consoleFont>, "", "console font file, empty = system monospace"),
    keys::whole("console_scale", &keys::C::consoleScale, 0, 0, 8, "console glyph scale, 0 = from the window"),
    keys::flag("script_raw_api", &keys::C::scriptRawApi, 0, "scripts also get the raw bridge"),
    keys::flag("modding_tools", &keys::C::moddingTools, 0, "F2 opens the modding tools"),
    keys::custom("new_game_difficulty", Hook::NewGameDifficulty, "easy, normal or hard for a started game"),

    // Controls and cameras.
    keys::word("controller_movement", &keys::C::controllerMovement, 2, keys::kStickOrPad, 2, "analog, dpad or both"),
    keys::flag("movement_original", &keys::C::movementOriginal, 1, "the original movement state machine"),
    keys::word("control_style", &keys::C::controlStyle, 0, keys::kCameraStyles, 0, "classic, tps or fps"),
    keys::elsewhere("control_styles", "the camera styles of this build, written by the game"),
    keys::flag("invert_mouse_y", &keys::C::invertMouseY, 0, "invert mouse look"),
    keys::flag("invert_controller_y", &keys::C::invertControllerY, 0, "invert stick look"),
    keys::real("tps_rest_zoom_amount", &keys::C::tpsRestZoom, 0.0, -200.0, 200.0, "thirdperson resting dolly, percent"),
    keys::real("tps_aim_zoom_amount", &keys::C::tpsAimZoom, 100.0, -200.0, 200.0, "thirdperson aim dolly, percent"),
    keys::custom("tps_aim_zoom", Hook::AimZoomSwitch, "on/off form of tps_aim_zoom_amount"),
    keys::real("fps_fov", &keys::C::fpsFov, 71.1, 40.0, 140.0, "first-person field of view"),
    keys::real("tps_fov", &keys::C::tpsFov, 71.1, 40.0, 140.0, "thirdperson field of view"),
    keys::whole("tps_rest_x", &keys::C::tpsRestX, 1638, -20000, 20000, "thirdperson rest offset, Q12"),
    keys::whole("tps_rest_y", &keys::C::tpsRestY, -410, -20000, 20000, "thirdperson rest offset, Q12"),
    keys::whole("tps_aim_x", &keys::C::tpsAimX, 3686, -20000, 20000, "thirdperson aim offset, Q12"),
    keys::whole("tps_aim_y", &keys::C::tpsAimY, 0, -20000, 20000, "thirdperson aim offset, Q12"),
    keys::whole("fps_head_x", &keys::C::fpsHeadX, -29, -20000, 20000, "first-person eye, Q12"),
    keys::whole("fps_head_y", &keys::C::fpsHeadY, -6836, -20000, 20000, "first-person eye, Q12"),
    keys::whole("fps_head_z", &keys::C::fpsHeadZ, 919, -20000, 20000, "first-person eye, Q12"),
    keys::real("fps_melee_swing", &keys::C::fpsMeleeSwing, 0.5, 0.0, 1.0, "first-person pullback of a melee swing"),
    keys::flag("tps_ots_aim", &keys::C::tpsOtsAim, 1, "aiming in thirdperson moves to the shoulder"),
    keys::flag("tps_camera_collision", &keys::C::tpsCameraCollision, 1, "the camera is pulled in off walls"),
    keys::whole("tps_camera_smoothing", &keys::C::tpsCameraSmoothing, 50, 0, 100, "how softly the camera follows"),
    keys::whole("tps_look_smoothing", &keys::C::tpsLookSmoothing, 0, 0, 100, "look smoothing"),
    keys::flag("tps_camera_recenter", &keys::C::tpsCameraRecenter, 1, "the camera levels itself behind Harry"),
    keys::custom("tps_recenter_delay", Hook::RecenterDelay, "seconds before it does, 0..30")
        .owning(&keys::C::tpsRecenterDelay, 2.0),
    keys::flag("crosshair", &keys::C::crosshair, 1, "crosshair of the free cameras"),
    keys::whole("crosshair_style", &keys::C::crosshairStyle, 0, 0, 5, "cross, dot, circle, dashes, chevron, ring+dot"),
    keys::real("crosshair_size", &keys::C::crosshairSize, 100.0, 25.0, 125.0, "crosshair size, percent"),
    keys::flag("crosshair_fps_aim_only", &keys::C::crosshairFpsAimOnly, 1, "crosshair only while aiming"),
    keys::flag("crosshair_hide_melee", &keys::C::crosshairHideMelee, 1, "no crosshair without a gun"),
    keys::real("text_size", &keys::C::textSize, 100.0, 100.0, 150.0, "subtitle and message size, percent"),
    keys::flag("aim_assist", &keys::C::aimAssist, 1, "aim assist of the free-aim cameras"),
    keys::flag("mouse_cursor", &keys::C::mouseCursor, 1, "the mouse drives puzzles and the main menu"),
    keys::custom("quick_weapon_switch", Hook::QuickWeapons, "the quick weapon actions"),
    keys::flag("altcam_button_sprint", &keys::C::altButtonSprint, 0, "sprint only on the run button"),
    keys::flag("immersive_fps_head_tracking", &keys::C::immersiveFpsHeadTracking, 0, "first-person view follows the head bone"),
    keys::word("touch_controls", &keys::C::touchControls, 0, keys::kTouchControls, 0,
               "on-screen controls: auto (touch screens), on or off"),
    keys::whole("touch_action_button", &keys::C::touchActionButton, 0, 0, 1,
                "the action button: 0 when there is something to act on, 1 always"),
    keys::flag("touch_aim_toggle", &keys::C::touchAimToggle, 1, "a tap of the aim button raises the weapon, the next lowers it"),
    keys::flag("touch_stick_run", &keys::C::touchStickRun, 1, "the stick pushed most of the way forward runs"),
    keys::word("touch_movement", &keys::C::touchMovement, 0, keys::kTouchMovement, 0,
               "movement by touch: stick (the floating stick) or dpad (four arrows and a run toggle)"),
    keys::real("touch_opacity", &keys::C::touchOpacity, 0.6, 0.2, 1.0, "opacity of the on-screen controls"),
    keys::real("touch_scale", &keys::C::touchScale, 1.0, 0.6, 1.6, "size of the on-screen controls"),
    keys::real("phone_vibration", &keys::C::phoneVibration, 1.0, 0.0, 1.0,
               "strength of the rumble on a phone's own vibrator, 0 = off"),
    keys::real("touch_look_sensitivity", &keys::C::touchLookSensitivity, 150.0, 30.0, 360.0,
               "degrees the alternate cameras turn for a drag across the screen's width"),
    keys::text("touch_layout", &text_field<&keys::C::touchLayout>, "", "places of the on-screen controls, empty = default"),
    keys::flag("control_2d", &keys::C::control2d, 0, "screen-relative movement"),
    keys::flag("control_2d_snap", &keys::C::control2dSnap, 0, "screen-relative movement turns at once"),
    keys::flag("disable_dpad_movement", &keys::C::disableDpadMovement, 0, "the d-pad does not move Harry"),
    keys::whole("look_speed", &keys::C::lookSpeed, 100, 25, 300,
                "camera turn speed of the alternate cameras, percent, on top of the mouse, stick and touch factors"),
    keys::whole("pad_deadzone", &keys::C::padDeadzone, 10, 0, 50,
                "move stick: the radius at the centre that reads as still, percent of the travel"),
    keys::whole("pad_look_deadzone", &keys::C::padLookDeadzone, 0, 0, 50,
                "look stick: the radius at the centre that reads as still, percent, before the camera's own zone"),
    keys::whole("pad_outer_deadzone", &keys::C::padOuterDeadzone, 10, 0, 30,
                "both sticks: the band at the rim that reads as a full push, percent of the travel"),
    keys::real("mouse_sensitivity", &keys::C::mouseSensitivity, 1.0, 0.02, 4.0, "mouse look speed"),
    keys::real("controller_sensitivity", &keys::C::controllerSensitivity, 1.0, 0.1, 4.0, "stick look speed"),

    // Game rules; the GAMEPLAY CHANGE ones are off in the original game.
    keys::whole("auto_reload", &keys::C::autoReload, 2, 0, 2, "0 reload button only, 1 after a dry fire, 2 original"),
    keys::flag("aim_resume_after_inventory", &keys::C::aimResumeAfterInventory, 1, "the aim comes back up after the inventory"),
    keys::flag("pause_menu", &keys::C::pauseMenu, 0, "GAMEPLAY CHANGE: the PC pause menu with the clock stopped"),
    keys::flag("pause_menu_on_back", &keys::C::pauseMenuOnBack, 1, "Back opens the PC pause menu"),
    keys::flag("save_anywhere", &keys::C::saveAnywhere, 0, "GAMEPLAY CHANGE: the quick save key opens the save screen"),
    keys::flag("quick_save_instant", &keys::C::quickSaveInstant, 0, "GAMEPLAY CHANGE: quick save and load at once"),
    keys::flag("persistent_remains", &keys::C::persistentRemains, 0, "GAMEPLAY CHANGE: dead enemies stay"),
    keys::flag("force_hold_aim", &keys::C::forceHoldAim, 0, "GAMEPLAY CHANGE: Weapon Control pinned to hold"),
    keys::flag("bullet_fat_hitbox", &keys::C::bulletFatHitbox, 0, "GAMEPLAY CHANGE: bullets hit the whole body"),
    keys::flag("force_enemy_spawn", &keys::C::forceEnemySpawn, 0, "GAMEPLAY CHANGE: enemies regardless of the story"),
    keys::flag("fix_nurse_grab_drain", &keys::C::fixNurseGrabDrain, 0, "GAMEPLAY CHANGE: a nurse's hold drains health"),
    keys::flag("melee_once_per_swing", &keys::C::meleeOncePerSwing, 0, "GAMEPLAY CHANGE: one hit per swing"),
    keys::flag("slope_fix", &keys::C::slopeFix, 0, "GAMEPLAY CHANGE: steep slopes do not slow the move"),
    keys::flag("stick_radius", &keys::C::stickRadius, 0, "GAMEPLAY CHANGE: the stick push is its radius"),
    keys::flag("near_pickup_draw", &keys::C::nearPickupDraw, 0, "pickups drawn from the neighbouring cell"),

    // Text, saves and the start.
    keys::text("language", &text_field<&keys::C::languageCode>, "auto", "a text language directory"),
    keys::word("menu_style", &keys::C::menuStyle, ui::kTitleStyleAuto, keys::kTitleStyles, ui::kTitleStyleAuto,
               "title picture: auto, us or japanese"),
    keys::text("savegame_dir", &text_field<&keys::C::savegameDir>, "", "where saves live, empty = beside the game"),
    keys::text_fit("map", &text_field<&keys::C::mapName>, "", "the map of a new game, empty = the manifest's"),

    // Read by other parts of the game.
    keys::elsewhere("gamedata_dir", "the first run"),
    keys::elsewhere("map_format", "the file system"),
    keys::elsewhere("anim_source", "the file system"),
    keys::elsewhere("keep_raw", "the file system"),
};
// clang-format on

// Key prefixes after the keys above: their keys belong to another reader, or to nothing the game has.
struct KeyPrefix {
    std::string_view prefix;
    bool delegated;  // another reader knows the keys
    Hook hook;
};

inline constexpr KeyPrefix kKeyPrefixes[] = {
    {"log_", true, Hook::LogKey},      // the log; an unknown one is warned about
    {"import_", true, Hook::None},     // the first run, which warns itself
    {"minimap", false, Hook::None},    // there is no minimap
    {"launcher_", false, Hook::None},  // belongs to no part of the game
};

using BindSlot = char[PC_BIND_SLOT_LEN];

// A bind of one control scheme: the key sets the classic scheme, the key + "_altcam" the alternate one.
struct SchemeBind {
    std::string_view key;
    BindSlot ControlScheme::* slot;
    std::string_view classic;
    std::string_view altcam;
};

// clang-format off
inline constexpr SchemeBind kSchemeBinds[] = {
    {"key_up", &ControlScheme::keyUp, "Up", "W"},
    {"key_down", &ControlScheme::keyDown, "Down", "S"},
    {"key_left", &ControlScheme::keyLeft, "Left", "Left"},
    {"key_right", &ControlScheme::keyRight, "Right", "Right"},
    {"key_cross", &ControlScheme::keyCross, "Space", "Mouse1"},
    {"key_circle", &ControlScheme::keyCircle, "Escape", "Escape"},
    {"key_triangle", &ControlScheme::keyTriangle, "Z", "Tab"},
    {"key_square", &ControlScheme::keySquare, "X", "Left Shift"},
    {"key_l1", &ControlScheme::keyL1, "A", "A"},
    {"key_r1", &ControlScheme::keyR1, "D", "D"},
    {"key_l2", &ControlScheme::keyL2, "Right Shift", "NONE"},
    {"key_r2", &ControlScheme::keyR2, "Left Shift", "Mouse2"},
    {"key_l3", &ControlScheme::keyL3, "NONE", "NONE"},
    {"key_r3", &ControlScheme::keyR3, "NONE", "NONE"},
    {"key_start", &ControlScheme::keyStart, "Tab", "Tab"},
    {"key_select", &ControlScheme::keySelect, "M", "M"},
    {"key_up_2", &ControlScheme::keyUp2, "", ""},
    {"key_down_2", &ControlScheme::keyDown2, "", ""},
    {"key_left_2", &ControlScheme::keyLeft2, "", ""},
    {"key_right_2", &ControlScheme::keyRight2, "", ""},
    {"key_cross_2", &ControlScheme::keyCross2, "C", "E"},
    {"key_circle_2", &ControlScheme::keyCircle2, "V", "F"},
    {"key_triangle_2", &ControlScheme::keyTriangle2, "", ""},
    {"key_square_2", &ControlScheme::keySquare2, "", ""},
    {"key_l1_2", &ControlScheme::keyL12, "", ""},
    {"key_r1_2", &ControlScheme::keyR12, "", ""},
    {"key_l2_2", &ControlScheme::keyL22, "", ""},
    {"key_r2_2", &ControlScheme::keyR22, "", ""},
    {"key_l3_2", &ControlScheme::keyL32, "", ""},
    {"key_r3_2", &ControlScheme::keyR32, "", ""},
    {"key_start_2", &ControlScheme::keyStart2, "Return", "Return"},
    {"key_select_2", &ControlScheme::keySelect2, "Space", "Space"},
    {"pad_cross", &ControlScheme::padCross, "a", "righttrigger"},
    {"pad_circle", &ControlScheme::padCircle, "b", "b"},
    {"pad_triangle", &ControlScheme::padTriangle, "y", "y"},
    {"pad_square", &ControlScheme::padSquare, "x", "leftshoulder"},
    {"pad_l1", &ControlScheme::padL1, "leftshoulder", "NONE"},
    {"pad_r1", &ControlScheme::padR1, "rightshoulder", "NONE"},
    {"pad_l2", &ControlScheme::padL2, "lefttrigger", "NONE"},
    {"pad_r2", &ControlScheme::padR2, "righttrigger", "lefttrigger"},
    {"pad_l3", &ControlScheme::padL3, "leftstick", "leftstick"},
    {"pad_r3", &ControlScheme::padR3, "rightstick", "rightstick"},
    {"pad_start", &ControlScheme::padStart, "start", "start"},
    {"pad_select", &ControlScheme::padSelect, "back", "back"},
    {"pad_cross_2", &ControlScheme::padCross2, "", "a"},
    {"pad_circle_2", &ControlScheme::padCircle2, "", ""},
    {"pad_triangle_2", &ControlScheme::padTriangle2, "", ""},
    {"pad_square_2", &ControlScheme::padSquare2, "", ""},
    {"pad_l1_2", &ControlScheme::padL12, "", ""},
    {"pad_r1_2", &ControlScheme::padR12, "", ""},
    {"pad_l2_2", &ControlScheme::padL22, "", ""},
    {"pad_r2_2", &ControlScheme::padR22, "", ""},
    {"pad_l3_2", &ControlScheme::padL32, "", ""},
    {"pad_r3_2", &ControlScheme::padR32, "", ""},
    {"pad_start_2", &ControlScheme::padStart2, "", ""},
    {"pad_select_2", &ControlScheme::padSelect2, "", ""},
    {"key_change_cam", &ControlScheme::keyChangeCam, "F9", "F9"},
    {"pad_change_cam", &ControlScheme::padChangeCam, "rightstick", "rightstick"},
    {"key_reload", &ControlScheme::keyReload, "R", "R"},
    {"pad_reload", &ControlScheme::padReload, "rightshoulder", "rightshoulder"},
    {"key_cycle_weapons", &ControlScheme::keyCycleWeapons, "NONE", "NONE"},
    {"pad_cycle_weapons", &ControlScheme::padCycleWeapons, "NONE", "NONE"},
    {"key_quick_heal", &ControlScheme::keyQuickHeal, "NONE", "NONE"},
    {"pad_quick_heal", &ControlScheme::padQuickHeal, "NONE", "NONE"},
    {"key_reload_2", &ControlScheme::keyReload2, "NONE", "NONE"},
    {"key_quick_turn", &ControlScheme::keyQuickTurn, "NONE", "NONE"},
    {"pad_quick_turn", &ControlScheme::padQuickTurn, "leftshoulder+rightshoulder", "leftshoulder"},
    {"key_rear_look", &ControlScheme::keyRearLook, "NONE", "NONE"},
    {"pad_rear_look", &ControlScheme::padRearLook, "NONE", "NONE"},
    {"key_flashlight", &ControlScheme::keyFlashlight, "F", "F"},
    {"pad_flashlight", &ControlScheme::padFlashlight, "NONE", "NONE"},
};
// clang-format on

// A scheme bind whose built-in value changed: a file written before `version` that still holds `was` moves to `now`
// (pc_config.cpp's migrate).
struct BindMigration {
    int version;
    std::string_view key;
    BindSlot ControlScheme::* slot;
    bool altcam;  // the "_altcam" copy, else the classic one
    std::string_view was;
    std::string_view now;
};

inline constexpr BindMigration kBindMigrations[] = {
    // The original turns Harry round on both side step buttons together, and L1 is its side step left: the quick
    // turn on L1 alone stepped and turned at once. The alternate cameras have no side step on L1 and keep theirs.
    {12, "pad_quick_turn", &ControlScheme::padQuickTurn, false, "leftshoulder", "leftshoulder+rightshoulder"},
};

// Whether a slot holding `value` in a file of version `from` takes the migration's new value.
constexpr bool bind_migrates(const BindMigration& m, int from, std::string_view value) {
    return from < m.version && value == m.was;
}

// A bind outside the schemes.
struct GlobalBind {
    std::string_view key;
    BindSlot s_PcConfig::* slot;
    std::string_view initial;
};

inline constexpr GlobalBind kGlobalBinds[] = {
    {"key_quicksave", &s_PcConfig::keyQuickSave, "F6"},
    {"key_quickload", &s_PcConfig::keyQuickLoad, "F8"},
    {"pad_quicksave", &s_PcConfig::padQuickSave, "NONE"},
    {"pad_quickload", &s_PcConfig::padQuickLoad, "NONE"},
    {"key_debug_menu", &s_PcConfig::keyDebugMenu, "F10"},
    {"pad_debug_menu", &s_PcConfig::padDebugMenu, ""},
    {"key_swap_shoulder", &s_PcConfig::keySwapShoulder, "Mouse3"},
    {"pad_swap_shoulder", &s_PcConfig::padSwapShoulder, ""},
    {"key_console", &s_PcConfig::keyConsole, "`"},
    // Unbound: the pause menu has Exit behind a confirmation, and one key back to the title was too easy to hit.
    {"key_exit_game", &s_PcConfig::keyExitGame, "NONE"},
};

constexpr void put_text(std::span<char> field, std::string_view text) {
    for (std::size_t i = 0; i < field.size(); ++i) field[i] = i < text.size() && i + 1 < field.size() ? text[i] : '\0';
}

constexpr s_PcConfig built_in_defaults() {
    s_PcConfig c{};
    for (const Setting& s : kSettings) {
        if (s.whole) c.*s.whole = static_cast<int>(s.initial);
        if (s.real) c.*s.real = static_cast<float>(s.initial);
        if (s.text) put_text(s.text(c), s.initial_text);
    }
    for (const SchemeBind& b : kSchemeBinds) {
        put_text(c.classic.*b.slot, b.classic);
        put_text(c.altcam.*b.slot, b.altcam);
    }
    for (const GlobalBind& b : kGlobalBinds) put_text(c.*b.slot, b.initial);
    return c;
}

}  // namespace sh::config
