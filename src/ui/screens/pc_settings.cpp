// SPDX-License-Identifier: GPL-3.0-or-later
// The PC settings by config.cfg key (pc_settings.h).

#include "ui/screens/pc_settings.h"

#include <cstdio>
#include <cstring>
#include <string>

#include "engine/config/pc_config.h"
#include "engine/game_state/game_state.h"
#include "engine/game_state/session.h"
#include "engine/log/sh_log.h"
#include "engine/platform/host_api.h"
#include "engine/platform/window.h"
#include "engine/screens/screens_view.h"
#include "media/fmv/movie_player.h"
#include "render/geometry/hero_fill_light.h"
#include "render/gpu/atmosphere_pass.h"
#include "render/gpu/render_state.h"


namespace sh::ui::pc_settings {
namespace {

// How a setting applies besides its stored value and live copy.
enum class Apply { None, Window, Vsync, Filter, Flashlight, Lighting, FpsHead };

struct IntSetting {
    const char* key;
    int* field;
    int* live;
    bool prefer_live;  // the live copy is the truth (hotkeys and the console change it)
    Apply apply;
    int axis = 0;  // FpsHead: x, y, z
};

struct FloatSetting {
    const char* key;
    float* field;
    float* live;
};

const IntSetting kInts[] = {
    {"fullscreen", &g_PcConfig.fullscreen, nullptr, false, Apply::Window},
    {"vsync", &g_PcConfig.vsync, nullptr, false, Apply::Vsync},
    {"texture_filter", &g_PcConfig.legacyDitherFilter, nullptr, false, Apply::Filter},
    {"use_pgxp", &g_PcConfig.usePgxp, &sh::render::precise().enabled, true, Apply::None},
    // The scene target is rebuilt with the new sample count on the next frame.
    {"msaa", &g_PcConfig.msaaSamples, &sh::render::graphics().msaa_samples, false, Apply::None},
    {"post_process", &g_PcConfig.postProcess, &sh::render::graphics().post_process, true, Apply::None},
    {"tonemap", &g_PcConfig.tonemap, &sh::render::graphics().tonemap, true, Apply::None},
    {"flashlight_mode", &g_PcConfig.flashlightMode, nullptr, false, Apply::Flashlight},
    {"culling", &g_PcConfig.culling, nullptr, false, Apply::None},
    {"fps_cap", &g_PcConfig.fpsCap, nullptr, false, Apply::None},
    {"weather_sim_hz", &g_PcConfig.weatherSimHz, nullptr, false, Apply::None},
    {"control_2d", &g_PcConfig.control2d, nullptr, false, Apply::None},
    {"invert_mouse_y", &g_PcConfig.invertMouseY, nullptr, false, Apply::None},
    {"invert_controller_y", &g_PcConfig.invertControllerY, nullptr, false, Apply::None},
    {"bullet_decals", &g_PcConfig.bulletDecals, nullptr, false, Apply::None},
    {"extra_lights", &g_PcConfig.extraLights, &sh::render::graphics().extra_lights, false, Apply::None},
    {"ambient_occlusion", &g_PcConfig.ambientOcclusion, &sh::render::atmosphere().ambient_occlusion, false,
     Apply::None},
    {"volumetric_fog", &g_PcConfig.volumetricFog, &sh::render::atmosphere().volumetric_fog, false, Apply::None},
    {"map_lights", &g_PcConfig.mapLights, &sh::render::atmosphere().map_lights, false, Apply::None},
    {"sun_shadows", &g_PcConfig.sunShadows, &sh::render::atmosphere().sun_shadows, false, Apply::None},
    {"god_rays", &g_PcConfig.godRays, &sh::render::atmosphere().god_rays, false, Apply::None},
    {"particle_shadows", &g_PcConfig.particleShadows, &sh::render::atmosphere().particle_shadows, false, Apply::None},
    {"aim_assist", &g_PcConfig.aimAssist, nullptr, false, Apply::None},
    {"tps_ots_aim", &g_PcConfig.tpsOtsAim, nullptr, false, Apply::None},
    {"modding_tools", &g_PcConfig.moddingTools, nullptr, false, Apply::None},
    {"tps_camera_collision", &g_PcConfig.tpsCameraCollision, nullptr, false, Apply::None},
    {"low_health_glow", &g_PcConfig.lowHealthGlow, nullptr, false, Apply::None},
    {"crosshair", &g_PcConfig.crosshair, nullptr, false, Apply::None},
    {"shadow_resolution", &g_PcConfig.shadowMapSize, &sh::render::flashlight().shadow_map_size, false, Apply::None},
    {"shadow_filter", &g_PcConfig.shadowFilter, &sh::render::flashlight().shadow_filter, false, Apply::None},
    {"tps_rest_x", &g_PcConfig.tpsRestX, nullptr, false, Apply::None},
    {"tps_rest_y", &g_PcConfig.tpsRestY, nullptr, false, Apply::None},
    {"tps_aim_x", &g_PcConfig.tpsAimX, nullptr, false, Apply::None},
    {"tps_aim_y", &g_PcConfig.tpsAimY, nullptr, false, Apply::None},
    {"ots_rest_x", &g_PcConfig.otsRestX, nullptr, false, Apply::None},
    {"ots_rest_y", &g_PcConfig.otsRestY, nullptr, false, Apply::None},
    {"ots_aim_x", &g_PcConfig.otsAimX, nullptr, false, Apply::None},
    {"ots_aim_y", &g_PcConfig.otsAimY, nullptr, false, Apply::None},
    // The camera's live eye offset follows the stored one.
    {"fps_head_x", &g_PcConfig.fpsHeadX, nullptr, false, Apply::FpsHead, 0},
    {"fps_head_y", &g_PcConfig.fpsHeadY, nullptr, false, Apply::FpsHead, 1},
    {"fps_head_z", &g_PcConfig.fpsHeadZ, nullptr, false, Apply::FpsHead, 2},
    {"dream_blur", &g_PcConfig.dreamBlur, &sh::render::graphics().dream_feedback, false, Apply::None},
    {"disable_dpad_movement", &g_PcConfig.disableDpadMovement, nullptr, false, Apply::None},
    {"crosshair_style", &g_PcConfig.crosshairStyle, nullptr, false, Apply::None},
    {"cutscene_line_gap_ms", &g_PcConfig.cutsceneLineGapMs, nullptr, false, Apply::None},
    {"hero_light", &g_PcConfig.heroLight, nullptr, false, Apply::Lighting},
    {"hero_light_flashlight", &g_PcConfig.heroLightWithFlashlight, nullptr, false, Apply::Lighting},
    {"crosshair_hide_melee", &g_PcConfig.crosshairHideMelee, nullptr, false, Apply::None},
    {"auto_reload", &g_PcConfig.autoReload, nullptr, false, Apply::None},
    {"aim_resume_after_inventory", &g_PcConfig.aimResumeAfterInventory, nullptr, false, Apply::None},
    {"audio_output", &g_PcConfig.audioOutput, nullptr, false, Apply::None},
    {"display_aspect", &g_PcConfig.aspectRaw, &sh::render::picture().aspect_raw, false, Apply::None},
    // The present pass reads the mode every frame, so it applies live.
    {"widescreen_mode", &g_PcConfig.widescreenMode, &sh::render::picture().widescreen_mode, false, Apply::None},
};

const FloatSetting kFloats[] = {
    {"flashlight_intensity", &g_PcConfig.flashlightIntensity, &sh::render::flashlight().intensity},
    {"flashlight_size", &g_PcConfig.flashlightSize, &sh::render::flashlight().size},
    {"flashlight_intensity_fps", &g_PcConfig.flashlightIntensityFps, &sh::render::flashlight().intensity_fps},
    {"flashlight_size_fps", &g_PcConfig.flashlightSizeFps, &sh::render::flashlight().size_fps},
    {"fog_strength", &g_PcConfig.fogStrength, &sh::render::fog().strength},
    {"ao_strength", &g_PcConfig.aoStrength, &sh::render::atmosphere().ao_strength},
    {"fog_scatter", &g_PcConfig.fogScatter, &sh::render::atmosphere().fog_scatter},
    {"fog_anisotropy", &g_PcConfig.fogAnisotropy, &sh::render::atmosphere().fog_anisotropy},
    // The renderer resizes the scene target when the live copy changes.
    {"render_scale", &g_PcConfig.renderScale, &sh::render::picture().render_scale},
    {"fmv_volume", &g_PcConfig.fmvVolume, &sh::fmv::movie_sound().volume},
    {"mouse_sensitivity", &g_PcConfig.mouseSensitivity, nullptr},
    {"controller_sensitivity", &g_PcConfig.controllerSensitivity, nullptr},
    {"fps_fov", &g_PcConfig.fpsFov, nullptr},
    {"tps_fov", &g_PcConfig.tpsFov, nullptr},
    {"ots_fov", &g_PcConfig.otsFov, nullptr},
    {"tps_aim_zoom_amount", &g_PcConfig.tpsAimZoom, nullptr},
    {"ots_aim_zoom_amount", &g_PcConfig.otsAimZoom, nullptr},
    {"tps_rest_zoom_amount", &g_PcConfig.tpsRestZoom, nullptr},
    {"ots_rest_zoom_amount", &g_PcConfig.otsRestZoom, nullptr},
    {"crosshair_size", &g_PcConfig.crosshairSize, nullptr},
    {"text_size", &g_PcConfig.textSize, nullptr},
    {"crt_aspect_trim", &g_PcConfig.crtAspectTrim, &sh::render::picture().crt_aspect_trim},
    {"world_hscale", &g_PcConfig.worldHScale, &sh::render::picture().world_h_scale},
    {"world_vscale", &g_PcConfig.worldVScale, &sh::render::picture().world_v_scale},
    {"pixel_aspect", &g_PcConfig.pixelAspect, &sh::render::picture().pixel_aspect},
    {"world_vshift", &g_PcConfig.worldVShift, &sh::render::picture().world_v_shift},
    {"cutscene_vshift", &g_PcConfig.cutsceneVShift, &sh::render::picture().cutscene_v_shift},
    {"fps_melee_swing", &g_PcConfig.fpsMeleeSwing, nullptr},
    {"dream_blur_strength", &g_PcConfig.dreamBlurStrength, &sh::render::feedback_control().damp_blend},
};

// The speaker layouts as config.cfg spells them.
const char* const kSpeakerIds[] = {"auto", "stereo", "quad", "51", "71", "hrtf"};

// The game's stereo output mode (its sound command).
constexpr int kSoundStereo = 2;
constexpr int kVolumeMax = 128;

// display_aspect = crt shows the picture at 4:3 times the trim, so one framebuffer pixel is this much wider than
// tall at trim 1.0: the bridge between the two control types.
constexpr float kCrtShape = (4.0f / 3.0f) / (320.0f / 224.0f);

const IntSetting* int_setting(const std::string& key)
{
    for (const IntSetting& s : kInts) {
        if (key == s.key) return &s;
    }
    return nullptr;
}

const FloatSetting* float_setting(const std::string& key)
{
    for (const FloatSetting& s : kFloats) {
        if (key == s.key) return &s;
    }
    return nullptr;
}

void warn(const std::string& key)
{
    static bool warned = false;
    if (!warned) {
        warned = true;
        SH_WARN("[MENU] settings.json: unknown setting \"%s\"", key.c_str());
    }
}

void save_int(const char* key, int value)
{
    char buf[24];
    std::snprintf(buf, sizeof(buf), "%d", value);
    PcConfig_SaveKeyValue(key, buf);
}

// Texture Filtering cycles one combined value over the two keys config.cfg stores (dithering and texture_filter);
// the renderer takes the filter mode and the tap count apart.
void apply_filter(int combined)
{
    const int dither = combined == 1 ? 1 : 0;
    const int filter = combined >= 2 && combined <= 7 ? combined - 1 : 0;
    g_PcConfig.dithering = dither;
    g_PcConfig.textureFilter = filter;
    sh::render::graphics().dither = dither;
    sh::render::graphics().texture_filter = filter >= 3 ? 3 : filter;
    if (filter >= 3) sh::render::graphics().aniso_level = 2 << (filter - 3);
    sh::render::graphics().bilinear_filtering = sh::render::graphics().texture_filter > 0;
    save_int("dithering", dither);
    save_int("texture_filter", filter);
}

// Switching the control type must not move the picture: the current shape carries over into the knob the new
// type owns (simple: crt shape x trim; advanced: hfov x vfov / par).
void switch_control_type(int raw)
{
    const float vf = g_PcConfig.worldVScale > 0.0f ? g_PcConfig.worldVScale : 1.0f;
    const float par = g_PcConfig.pixelAspect > 0.0f ? g_PcConfig.pixelAspect : 1.0f;
    char buf[24];
    if (!g_PcConfig.aspectRaw) {
        float hf = (kCrtShape * g_PcConfig.crtAspectTrim) * par / vf;
        if (hf < 0.25f) hf = 0.25f;
        if (hf > 2.00f) hf = 2.00f;
        g_PcConfig.worldHScale = hf;
        sh::render::picture().world_h_scale = hf;
        std::snprintf(buf, sizeof(buf), "%.3f", hf);
        PcConfig_SaveKeyValue("world_hscale", buf);
    } else {
        float tr = (g_PcConfig.worldHScale * vf / par) / kCrtShape;
        if (tr < 0.50f) tr = 0.50f;
        if (tr > 1.50f) tr = 1.50f;
        g_PcConfig.crtAspectTrim = tr;
        sh::render::picture().crt_aspect_trim = tr;
        std::snprintf(buf, sizeof(buf), "%.3f", tr);
        PcConfig_SaveKeyValue("crt_aspect_trim", buf);
    }
    g_PcConfig.aspectRaw = raw;
    sh::render::picture().aspect_raw = g_PcConfig.aspectRaw;
    PcConfig_SaveKeyValue("display_aspect", g_PcConfig.aspectRaw ? "raw" : "crt");
    SH_LOG("View: control type = %s  (trim %.3f, hfov %.3f, vfov %.2f, par %.3f)",
           g_PcConfig.aspectRaw ? "advanced" : "simple", g_PcConfig.crtAspectTrim, g_PcConfig.worldHScale,
           g_PcConfig.worldVScale, g_PcConfig.pixelAspect);
}

sh::game::Settings& game_settings() { return sh::game::session().game.settings; }

}  // namespace

std::int32_t get_int(const std::string& key)
{
    if (key == "music_volume") return game_settings().music_volume;
    if (key == "effects_volume") return game_settings().sfx_volume;
    if (key == "aspect_control_type") return g_PcConfig.aspectRaw;
    const IntSetting* s = int_setting(key);
    if (s == nullptr) {
        warn(key);
        return 0;
    }
    return (s->prefer_live && s->live != nullptr) ? *s->live : *s->field;
}

void set_int(const std::string& key, std::int32_t value, bool save)
{
    sh::game::Settings& g = game_settings();
    if (key == "music_volume" || key == "effects_volume") {
        (key == "music_volume" ? g.music_volume : g.sfx_volume) = static_cast<std::uint8_t>(value);
        sh::screens::set_sound_volumes(kVolumeMax, g.music_volume, g.sfx_volume);
        return;
    }
    if (key == "aspect_control_type") {
        switch_control_type(value);
        return;
    }
    const IntSetting* s = int_setting(key);
    if (s == nullptr) {
        warn(key);
        return;
    }
    *s->field = value;
    if (key == "audio_output") {
        // The layout is the PC's output; the game's own mix stays stereo so surround layouts keep their image.
        PcConfig_SaveKeyValue("audio_output", kSpeakerIds[value]);
        g.mono = 0;
        sh::game_state::sound_command(kSoundStereo);
        return;
    }
    if (key == "display_aspect") {
        *s->live = value;
        PcConfig_SaveKeyValue("display_aspect", value ? "raw" : "crt");
        return;
    }
    if (s->apply != Apply::Filter && save) save_int(s->key, value);
    if (s->live != nullptr) *s->live = value;
    switch (s->apply) {
        case Apply::Window:
            sh::platform::apply_window_mode(g_PcConfig.windowWidth, g_PcConfig.windowHeight,
                                            sh::platform::window_mode_from_config(g_PcConfig.fullscreen));
            break;
        case Apply::Vsync: sh::platform::set_vsync(g_PcConfig.vsync != 0); break;
        case Apply::Filter: apply_filter(value); break;
        case Apply::Flashlight: Pc_FlashlightModeApply(value, 1); break;
        case Apply::Lighting: sh::render::apply_hero_fill_light(); break;
        case Apply::FpsHead: sh::screens::set_fps_head_axis(s->axis, value); break;
        case Apply::None: break;
    }
}

float get_float(const std::string& key)
{
    const FloatSetting* s = float_setting(key);
    if (s == nullptr) {
        warn(key);
        return 0.0f;
    }
    return *s->field;
}

void set_float(const std::string& key, float value, const std::string* text)
{
    const FloatSetting* s = float_setting(key);
    if (s == nullptr) {
        warn(key);
        return;
    }
    *s->field = value;
    if (s->live != nullptr) *s->live = value;
    if (text != nullptr) PcConfig_SaveKeyValue(s->key, text->c_str());
}

// Defaults sit at the same place in the defaults block as the setting in the config.
std::int32_t default_int(const std::string& key)
{
    const IntSetting* s = int_setting(key);
    if (s == nullptr) {
        warn(key);
        return 0;
    }
    const s_PcConfig* d = PcConfig_Defaults();
    return *reinterpret_cast<const int*>(reinterpret_cast<const char*>(d) +
                                         (reinterpret_cast<const char*>(s->field) -
                                          reinterpret_cast<const char*>(&g_PcConfig)));
}

float default_float(const std::string& key)
{
    const FloatSetting* s = float_setting(key);
    if (s == nullptr) {
        warn(key);
        return 0.0f;
    }
    const s_PcConfig* d = PcConfig_Defaults();
    return *reinterpret_cast<const float*>(reinterpret_cast<const char*>(d) +
                                           (reinterpret_cast<const char*>(s->field) -
                                            reinterpret_cast<const char*>(&g_PcConfig)));
}

bool known(const std::string& key, double& value)
{
    if (key == "music_volume" || key == "effects_volume" || key == "aspect_control_type" ||
        int_setting(key) != nullptr) {
        value = get_int(key);
        return true;
    }
    if (float_setting(key) != nullptr) {
        value = get_float(key);
        return true;
    }
    return false;
}

void window_size(std::int32_t& width, std::int32_t& height)
{
    width = g_PcConfig.windowWidth;
    height = g_PcConfig.windowHeight;
}

void set_window_size(std::int32_t width, std::int32_t height)
{
    g_PcConfig.windowWidth = width;
    g_PcConfig.windowHeight = height;
    save_int("width", width);
    save_int("height", height);
    sh::platform::apply_window_mode(g_PcConfig.windowWidth, g_PcConfig.windowHeight,
                                    sh::platform::window_mode_from_config(g_PcConfig.fullscreen));
}

std::string start_map() { return g_PcConfig.mapName; }

void set_start_map(const std::string& name)
{
    std::strncpy(g_PcConfig.mapName, name.c_str(), sizeof(g_PcConfig.mapName) - 1);
    g_PcConfig.mapName[sizeof(g_PcConfig.mapName) - 1] = '\0';
    PcConfig_SaveMapName(name.c_str());
}

// A screenshot is only useful when the view settings behind it are known.
void log_view_change(const std::string& key, const std::string& text)
{
    SH_LOG("View: %s = %s  (aspect %s, trim %.2f, hfov %.2f, vfov %.2f, vshift %+d, par %.3f)", key.c_str(),
           text.c_str(), g_PcConfig.aspectRaw ? "raw" : "crt", g_PcConfig.crtAspectTrim, g_PcConfig.worldHScale,
           g_PcConfig.worldVScale, (int)g_PcConfig.worldVShift, g_PcConfig.pixelAspect);
}

}  // namespace sh::ui::pc_settings
