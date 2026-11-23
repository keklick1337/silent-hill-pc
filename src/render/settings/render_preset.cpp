// SPDX-License-Identifier: GPL-3.0-or-later
#include "render/settings/render_preset.h"

#include <array>
#include <cctype>
#include <cstdio>
#include <string_view>

#include "render/gpu/render_state.h"

extern "C" {
#include "engine/log/sh_log.h"

}

namespace {

struct PresetValues
{
    int   nativeResolution;
    int   scaling;
    int   dithering;
    int   textureFilter;
    int   msaaSamples;
    int   flashlightMode;
    int   shadows;
    int   deband;
    int   softHighlights;
    int   postProcess;
    float postProcessIntensity;
    int   tonemap;
    int   usePgxp;
    int   softFog;
    int   smoothModels;
    int   foliageEdge;
    int   loadScreenTrail;
    float renderScale;
    int   shadowMapSize;
    int   shadowFilter;
    int   waterQuality;
    // The features of RENDER.md section 14; all but texture_upscale need the native Vulkan renderer, which
    // ignores them elsewhere (the Options rows say why).
    int   hdrFrame;
    int   hdrTonemap;
    float bloom;
    int   upscaler;
    int   ambientOcclusion;
    int   mapLights;
    int   sunShadows;
    int   volumetricFog;
    int   godRays;
    int   particleShadows;
    int   extraLights;  // works on every renderer
    int   modelUpscaling;
    int   textureUpscale;
    int   rayTracing;  // the ultra_rtx presets alone: the master switch and every effect under it
};

constexpr std::array<std::string_view, RENDER_PRESET_COUNT> kNames = {
    "original", "enhanced", "high", "ultra", "ultra_upscaled", "ultra_rtx", "ultra_rtx_upscaled", "custom",
};

// Indexed by preset id; custom has no row. The values are config-key values,
// so the table reads the same as config.cfg (texture_filter 2 = trilinear,
// 5 = anisotropic 8x; flashlight_mode 1 = the PSX-calibrated per-pixel cone).
//
// tonemap stays 0 everywhere: the frame is RGBA8, so the operators in the
// post pass only see values already clipped to 1.0. Reinhard halves white and
// Filmic lifts 0.1 to 0.38, which erases the game's darkness. Highlights are
// rolled off where light actually exceeds white instead (soft_highlights).
//
// use_pgxp and smooth_models are what keep models from snapping
// to the PSX grid at PC resolutions (RENDER.md section 7.5), so every preset above
// original has them.
//
// ultra raises only quality: the flashlight stays the calibrated cone and the
// grain stays the high one, so fog, darkness and what the beam shows match high.
//
// Above enhanced each step adds the next most visible feature: high the HDR frame
// with the soft tonemapper (the shader's highlight roll-off as a frame operator,
// so the beam and the sun are not blown out) and a faint bloom, TAA, ambient
// occlusion and the map's lamps; ultra the sun's shadows, the volumetric fog with
// god rays, flake shadows, the extra lamps; ultra_rtx ray tracing.
// TAA and MSAA never meet: the native Vulkan backend has no multisampled targets,
// so MSAA serves GL and SDL3 GPU, TAA the native backend. Both upscalings are
// experimental and not to everyone's taste, so only the *_upscaled twins of ultra
// and ultra_rtx have them. Texture upscaling is x2: x4 tends to look smeared. Model upscaling
// takes tessellation, not mesh shaders: the same surface (the map is tessellated in
// either mode), and more GPUs have the stage.
constexpr PresetValues kOriginal = {
    .nativeResolution = 1, .scaling = 3, .dithering = 1, .textureFilter = 0,
    .msaaSamples = 0, .flashlightMode = 0, .shadows = 0, .deband = 0,
    .softHighlights = 0, .postProcess = 0, .postProcessIntensity = 1.0f,
    .tonemap = 0, .usePgxp = 0, .softFog = 0,
    .smoothModels = 0, .foliageEdge = 0, .loadScreenTrail = 1,
    .renderScale = 1.0f, .shadowMapSize = 1024, .shadowFilter = 1,
    .waterQuality = 0};

constexpr PresetValues Enhanced()
{
    PresetValues v = kOriginal;
    v.nativeResolution = 0;
    v.scaling = 2;
    v.dithering = 0;
    v.textureFilter = 2;
    v.msaaSamples = 4;
    v.flashlightMode = 1;
    v.deband = 1;
    v.softHighlights = 1;
    v.usePgxp = 1;
    v.softFog = 1;
    v.smoothModels = 1;
    v.foliageEdge = 1;
    v.loadScreenTrail = 2;
    v.waterQuality = 1;
    return v;
}

constexpr PresetValues High()
{
    PresetValues v = Enhanced();
    v.textureFilter = 5;
    v.shadows = 1;
    v.postProcess = 5;
    v.postProcessIntensity = 0.35f;
    v.waterQuality = 2;
    v.hdrFrame = 1;
    v.hdrTonemap = 1;
    v.bloom = 0.3f;
    v.upscaler = 1;
    v.ambientOcclusion = 1;
    v.mapLights = 1;
    return v;
}

// High at twice the window resolution (scaled down, Targets caps the size), 16x
// anisotropic, 8x MSAA, 4096 shadow map with the 5x5 filter.
constexpr PresetValues Ultra()
{
    PresetValues v = High();
    v.textureFilter = 6;
    v.msaaSamples = 8;
    v.foliageEdge = 2;
    v.renderScale = 2.0f;
    v.shadowMapSize = 4096;
    v.shadowFilter = 2;
    v.sunShadows = 1;
    v.volumetricFog = 1;
    v.godRays = 1;
    v.particleShadows = 1;
    v.extraLights = 1;
    return v;
}

constexpr PresetValues Upscaled(PresetValues v)
{
    v.modelUpscaling = 1;
    v.textureUpscale = 2;
    return v;
}

constexpr PresetValues UltraRtx()
{
    PresetValues v = Ultra();
    v.rayTracing = 1;
    return v;
}

constexpr std::array<PresetValues, RENDER_PRESET_CUSTOM> kPresets = {
    kOriginal, Enhanced(), High(), Ultra(), Upscaled(Ultra()), UltraRtx(), Upscaled(UltraRtx()),
};

std::string_view Trim(std::string_view s)
{
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())))
        s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
        s.remove_suffix(1);
    return s;
}

bool EqualsNoCase(std::string_view a, std::string_view b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); i++)
    {
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    }
    return true;
}

int FromName(std::string_view name)
{
    for (size_t i = 0; i < kNames.size(); i++)
    {
        if (EqualsNoCase(name, kNames[i]))
            return static_cast<int>(i);
    }
    return -1;
}

} // namespace

extern "C" int RenderPreset_FromName(const char* name)
{
    return name ? FromName(Trim(name)) : -1;
}

extern "C" const char* RenderPreset_Name(int preset)
{
    if (preset < 0 || preset >= RENDER_PRESET_COUNT)
        return "?";
    return kNames[static_cast<size_t>(preset)].data();
}

extern "C" int RenderPreset_Scan(FILE* f, int* named)
{
    int  preset = RENDER_PRESET_DEFAULT;
    char line[256];

    if (named)
        *named = 0;
    if (!f)
        return preset;

    while (std::fgets(line, sizeof(line), f))
    {
        std::string_view s = Trim(line);
        if (s.empty() || s.front() == '#' || s.front() == ';')
            continue;

        const size_t eq = s.find('=');
        if (eq == std::string_view::npos || Trim(s.substr(0, eq)) != "graphics_preset")
            continue;

        if (named)
            *named = 1;
        const std::string_view value = Trim(s.substr(eq + 1));
        const int              id    = FromName(value);
        if (id < 0)
        {
            std::fprintf(stderr, "[CONFIG] graphics_preset '%.*s' is unknown; using %s\n",
                         static_cast<int>(value.size()), value.data(), RenderPreset_Name(RENDER_PRESET_DEFAULT));
            preset = RENDER_PRESET_DEFAULT;
        }
        else
        {
            preset = id;
        }
    }

    std::rewind(f);
    return preset;
}

extern "C" void RenderPreset_Apply(int preset, s_PcConfig* cfg)
{
    if (!cfg)
        return;

    cfg->graphicsPreset = (preset >= 0 && preset < RENDER_PRESET_COUNT) ? preset : RENDER_PRESET_DEFAULT;
    if (preset < 0 || preset >= RENDER_PRESET_CUSTOM)
        return;

    const PresetValues& v = kPresets[static_cast<size_t>(preset)];

    cfg->nativeResolution     = v.nativeResolution;
    cfg->scaling              = v.scaling;
    cfg->dithering            = v.dithering;
    cfg->textureFilter        = v.textureFilter;
    cfg->msaaSamples          = v.msaaSamples;
    cfg->flashlightMode       = v.flashlightMode;
    cfg->shadows              = v.shadows;
    cfg->deband               = v.deband;
    cfg->softHighlights       = v.softHighlights;
    cfg->postProcess          = v.postProcess;
    cfg->postProcessIntensity = v.postProcessIntensity;
    cfg->tonemap              = v.tonemap;
    cfg->usePgxp              = v.usePgxp;
    cfg->softFog              = v.softFog;
    cfg->smoothModels         = v.smoothModels;
    cfg->foliageEdge          = v.foliageEdge;
    cfg->loadScreenTrail      = v.loadScreenTrail;
    cfg->renderScale          = v.renderScale;
    cfg->shadowMapSize        = v.shadowMapSize;
    cfg->shadowFilter         = v.shadowFilter;
    cfg->waterQuality         = v.waterQuality;
    cfg->hdrFrame             = v.hdrFrame;
    cfg->hdrTonemap           = v.hdrTonemap;
    cfg->bloom                = v.bloom;
    cfg->upscaler             = v.upscaler;
    cfg->ambientOcclusion     = v.ambientOcclusion;
    cfg->mapLights            = v.mapLights;
    cfg->sunShadows           = v.sunShadows;
    cfg->volumetricFog        = v.volumetricFog;
    cfg->godRays              = v.godRays;
    cfg->particleShadows      = v.particleShadows;
    cfg->extraLights          = v.extraLights;
    cfg->modelUpscaling       = v.modelUpscaling;
    cfg->textureUpscale       = v.textureUpscale;
    cfg->rayTracing           = v.rayTracing;
    cfg->rtShadows            = v.rayTracing;
    cfg->rtReflections        = v.rayTracing;
    cfg->rtGi                 = v.rayTracing;
    cfg->rtLights             = v.rayTracing;
}

extern "C" int RenderPreset_Matches(int preset, const s_PcConfig* cfg)
{
    if (!cfg || preset < 0 || preset >= RENDER_PRESET_CUSTOM)
        return 0;
    s_PcConfig p = *cfg;
    RenderPreset_Apply(preset, &p);
    const s_PcConfig& c = *cfg;
    return c.nativeResolution == p.nativeResolution && c.scaling == p.scaling && c.dithering == p.dithering &&
           c.textureFilter == p.textureFilter && c.msaaSamples == p.msaaSamples &&
           c.flashlightMode == p.flashlightMode && c.shadows == p.shadows && c.deband == p.deband &&
           c.softHighlights == p.softHighlights && c.postProcess == p.postProcess &&
           c.postProcessIntensity == p.postProcessIntensity && c.tonemap == p.tonemap && c.usePgxp == p.usePgxp &&
           c.softFog == p.softFog && c.smoothModels == p.smoothModels && c.foliageEdge == p.foliageEdge &&
           c.loadScreenTrail == p.loadScreenTrail && c.renderScale == p.renderScale &&
           c.shadowMapSize == p.shadowMapSize && c.shadowFilter == p.shadowFilter && c.waterQuality == p.waterQuality &&
           c.hdrFrame == p.hdrFrame && c.hdrTonemap == p.hdrTonemap && c.bloom == p.bloom && c.upscaler == p.upscaler &&
           c.ambientOcclusion == p.ambientOcclusion && c.mapLights == p.mapLights && c.sunShadows == p.sunShadows &&
           c.volumetricFog == p.volumetricFog && c.godRays == p.godRays && c.particleShadows == p.particleShadows &&
           c.extraLights == p.extraLights && c.modelUpscaling == p.modelUpscaling &&
           c.textureUpscale == p.textureUpscale && c.rayTracing == p.rayTracing && c.rtShadows == p.rtShadows &&
           c.rtReflections == p.rtReflections && c.rtGi == p.rtGi && c.rtLights == p.rtLights;
}

extern "C" void RenderSettings_Push(void)
{
    sh::render::picture().render_scale          = g_PcConfig.renderScale;
    sh::render::graphics().deband             = g_PcConfig.deband ? 1 : 0;
    sh::render::graphics().soft_highlights     = g_PcConfig.softHighlights ? 1 : 0;
    sh::render::graphics().flashlight_shadows  = g_PcConfig.shadows ? 1 : 0;
    sh::render::flashlight().shadow_map_size   = g_PcConfig.shadowMapSize;
    sh::render::flashlight().shadow_filter     = g_PcConfig.shadowFilter;
    sh::render::graphics().water_quality       = g_PcConfig.waterQuality;
    sh::render::graphics().model_upscaling     = g_PcConfig.modelUpscaling;
    sh::render::precise().smooth_models = g_PcConfig.smoothModels ? 1 : 0;
    sh::render::graphics().foliage_aa          = g_PcConfig.foliageEdge;
    sh::render::graphics().load_screen_trail   = g_PcConfig.loadScreenTrail;

    SH_LOG("Graphics preset: %s (render scale %.2f, deband %d, soft highlights %d, shadows %d at %d, "
           "shadow filter %d, pgxp %d, smooth models %d; keys in config.cfg override the preset)",
           RenderPreset_Name(g_PcConfig.graphicsPreset), sh::render::picture().render_scale,
           sh::render::graphics().deband, sh::render::graphics().soft_highlights,
           sh::render::graphics().flashlight_shadows, sh::render::flashlight().shadow_map_size,
           sh::render::flashlight().shadow_filter, g_PcConfig.usePgxp ? 1 : 0, sh::render::precise().smooth_models);
}
