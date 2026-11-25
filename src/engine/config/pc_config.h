/* SPDX-License-Identifier: GPL-3.0-or-later */
// The settings of config.cfg as the running game reads them (g_PcConfig), and the functions that load the file and
// write single lines back into it. The keys, their defaults and ranges are the table in engine/config/config_keys.h.
#pragma once

#include "render/gpu/render_state.h"

#ifdef __cplusplus
extern "C" {
#endif

// The version stamped into config.cfg as config_version. A file below it gets the migration steps of
// PcConfig_Load: a default that changed is moved to its new value only where the file still holds the old one.
//   1: world_vscale 1.0 -> 1.08          2: crt_aspect_trim 0.98 -> 1.06    3: menu_fps_unlock 1 -> 0
//   4, 5: no step                        6: pad_reload / pad_quick_turn filled where unbound
//   7: Space / Escape / Tab / M binds    8: keyboard binds become action_* lines
//   9: fps_cap 30 -> 60                  10: texture_upscale 8 and 16 -> 4
#define PC_CONFIG_VERSION 10

// One control scheme. A keyboard slot holds an SDL scancode name or "MouseN", a controller slot an SDL gamepad
// button name; "NONE" or "" is unbound. The *2 slots are the second binding of the same button.
typedef struct {
    char keyUp[24], keyDown[24], keyLeft[24], keyRight[24];
    char keyCross[24], keyCircle[24], keyTriangle[24], keySquare[24];
    char keyL1[24], keyR1[24], keyL2[24], keyR2[24], keyL3[24], keyR3[24];
    char keyStart[24], keySelect[24];
    char keyUp2[24], keyDown2[24], keyLeft2[24], keyRight2[24];
    char keyCross2[24], keyCircle2[24], keyTriangle2[24], keySquare2[24];
    char keyL12[24], keyR12[24], keyL22[24], keyR22[24], keyL32[24], keyR32[24];
    char keyStart2[24], keySelect2[24];
    char padCross[24], padCircle[24], padTriangle[24], padSquare[24];
    char padL1[24], padR1[24], padL2[24], padR2[24], padL3[24], padR3[24];
    char padStart[24], padSelect[24];
    char padCross2[24], padCircle2[24], padTriangle2[24], padSquare2[24];
    char padL12[24], padR12[24], padL22[24], padR22[24], padL32[24], padR32[24];
    char padStart2[24], padSelect2[24];
    char keyChangeCam[24], padChangeCam[24];
    char keyReload[24], padReload[24];
    char keyCycleWeapons[24], padCycleWeapons[24];
    char keyQuickHeal[24], padQuickHeal[24];
    char keyReload2[24];
    char keyQuickTurn[24], padQuickTurn[24];
    char keyRearLook[24], padRearLook[24];
    char keyFlashlight[24], padFlashlight[24];
} ControlScheme;

// Each field is described by its key in config_keys.h. The order of the fields is the layout other code and the
// render presets (render_preset.h) rely on.
typedef struct {
    int windowWidth;
    int windowHeight;
    int fullscreen;
    int confineCursor;
    int culling;
    int drawDistancePct;
    int preloadChunks;
    int vsync;
    int refreshRate;
    int fpsCap;
    int weatherSimHz;
    int cutsceneLineGapMs;
    int msgVoiceTailMs;
    int skipIntros;  // 2 also starts a new game; the command line sets it too
    int showConsole;
    int legacyDitherFilter;  // dithering and texture_filter as one 8-way value: the options row cycles it
    int dithering;
    int textureFilter;
    int textureUpscale;  // 0 off, 2, 4, 8, 16: AI upscaled imported textures (tools/sh-upscale)
    int scaling;
    int nativeResolution;
    int widescreenMode;
    int menuPillarbox;
    int residentTextures;
    int attractDemos;
    int menuFpsUnlock;
    int lowHealthGlow;
    int bulletDecals;
    int gamepadIcons;
    int wholeMapExteriors;
    int usePgxp;
    int polySizeCull;
    int msaaSamples;
    char renderer[16];  // checked at startup: an unknown name becomes "gl"
    int vulkanValidation;  // renderer = vulkan: VK_LAYER_KHRONOS_validation when the system has it
    int shaderPrewarm;     // renderer = vulkan: the manifest's pipelines are built at start (RENDER.md 2.2d)
    int softwareGl;        // sh::gpu::SoftwareGl: 0 auto, 1 force, 2 off
    int rayTracing;        // renderer = vulkan: the scene's acceleration structures (RENDER.md section 11)
    int rtShadows;         // ray_tracing: the flashlight's shadows by rays, denoised (RENDER.md section 11)
    int rtReflections;     // ray_tracing: reflections of reflective materials, fog per ray (RENDER.md section 11)
    int rtGi;              // ray_tracing: one bounce of the flashlight, darkness kept (RENDER.md section 11)
    int rtLights;          // ray_tracing: the map's lights and sun, GGX specular (RENDER.md section 10)
    int extraLights;       // lights.json lamps marked "extra", every renderer (RENDER.md 7.16)
    int postProcess;
    int fxaa;
    int wideEdgeFix;
    int tonemap;
    int graphicsPreset;
    float renderScale;
    int deband;
    int softHighlights;
    int shadows;
    int flashlightMode;
    int perPixelFlashlight;  // follows flashlightMode (Pc_FlashlightModeApply)
    int flashlightShadows;   // follows flashlightMode (Pc_FlashlightModeApply)
    float fogStrength;
    int softFog;
    int ambientOcclusion;
    float aoStrength;
    int volumetricFog;
    float fogScatter;
    float fogAnisotropy;
    int mapLights;        // renderer = vulkan: the map's lights and sun without ray tracing (RENDER.md 7.14)
    int sunShadows;       // with mapLights: the sun's shadow map
    int godRays;          // with volumetricFog: the sun scatters in the fog through its shadow map
    int particleShadows;  // falling snow and ash shade the sun and the flashlight
    int upscaler;  // renderer = vulkan: TAA, TAAU, spatial upscale (render/post/temporal.h)
    int upscaleQuality;
    float upscaleSharpness;
    int temporalDebug;
    int presentMode;  // renderer = vulkan: frame pacing
    int framesInFlight;
    int presentWait;
    int loadScreenTrail;
    int foliageEdge;
    int smoothModels;
    float flashlightIntensity;
    float flashlightSize;
    float flashlightIntensityFps;
    float flashlightSizeFps;
    float postProcessIntensity;
    float tonemapIntensity;
    int hdrFrame;  // renderer = vulkan: RGBA16F scene target (render/post/hdr.h)
    int hdrTonemap;
    float bloom;
    float bloomThreshold;
    int hdrOutput;  // HDR10 swapchain where the display offers it
    float hdrPaperWhite;
    float hdrPeak;
    float brightness;
    float contrast;
    int imageAdjustScope;
    float saturation;
    float xaVolume;
    float fmvVolume;
    int fmvOriginalVolume;
    int enableDebugLog;
    int cdFastLoad;
    int cdConsolePace;
    int allowDebugControls;
    int consoleEnabled;
    int debugOptions;
    char consoleFont[260];
    int consoleScale;
    int controllerMovement;
    int movementOriginal;
    int controlStyle;
    int invertMouseY;
    int invertControllerY;
    int tpsCameraCollision;
    int moddingTools;
    int tpsOtsAim;
    int tpsCameraSmoothing;
    int tpsLookSmoothing;
    int tpsCameraRecenter;
    float tpsRecenterDelay;
    int crosshair;
    int crosshairStyle;
    float crosshairSize;
    float textSize;
    int aimAssist;
    int mouseCursor;
    int altButtonSprint;
    int immersiveFpsHeadTracking;
    int control2d;
    int control2dSnap;
    int disableDpadMovement;
    int menuFilter;
    int shadowMapSize;
    int waterQuality;
    int modelUpscaling;  // 0 off, 1 tessellation, 2 mesh shaders (renderer = vulkan only)
    int shadowFilter;
    int fpsDisplay;
    int fpsProfiler;
    int perfRecord;     // perf_record: a performance recording from the start
    int configVersion;  // 0 = a file from before the versions
    int audioOutput;
    float fpsFov;
    float tpsFov;
    float tpsAimZoom;
    float otsFov;
    float otsAimZoom;
    float tpsRestZoom;
    float otsRestZoom;
    // Camera offsets in Q12 view units; Y is stored with the console's downward axis.
    int tpsRestX;
    int tpsRestY;
    int tpsAimX;
    int tpsAimY;
    int otsRestX;
    int otsRestY;
    int otsAimX;
    int otsAimY;
    // The first-person eye in Harry's body frame, Q12: right, up (negative), forward.
    int fpsHeadX;
    int fpsHeadY;
    int fpsHeadZ;
    float fpsMeleeSwing;
    int aspectRaw;
    float crtAspectTrim;
    float worldHScale;
    float worldVScale;
    float pixelAspect;
    float worldVShift;
    float cutsceneVShift;
    float mouseSensitivity;
    float controllerSensitivity;
    // classic is the original camera; altcam is every alternate camera, its keys end in "_altcam".
    ControlScheme classic;
    ControlScheme altcam;
    char keyQuickSave[24], keyQuickLoad[24];
    char padQuickSave[24], padQuickLoad[24];
    char keyDebugMenu[24];
    char padDebugMenu[24];
    char keySwapShoulder[24];
    char padSwapShoulder[24];
    char keyConsole[24];
    char keyExitGame[24];
    char languageCode[16];
    int menuStyle;
    char savegameDir[512];
    char mapName[64];
    int dreamBlur;
    int heroLight;
    int brightnessLevelBias;
    int heroLightWithFlashlight;
    float dreamBlurStrength;
    float loadScreenMin;
    int crosshairFpsAimOnly;
    int crosshairHideMelee;
    int autoReload;
    int pauseMenu;
    int pauseMenuOnBack;
    int saveAnywhere;
    int quickSaveInstant;
    int persistentRemains;
    int forceHoldAim;
    int bulletFatHitbox;
    int forceEnemySpawn;
    int fixNurseGrabDrain;
    int meleeOncePerSwing;
    int aimResumeAfterInventory;
    int slopeFix;
    int stickRadius;
    int nearPickupDraw;
    int scriptRawApi;
    int worldObjectsShadow;
    int quickSetup;  // 1: the quick setup is shown at the next start
    // The touch screen's controls (input/touch/touch_controls.h).
    int touchControls;      // 0 auto (on a touch device), 1 on (also on a desktop, the mouse as a finger), 2 off
    int touchActionButton;  // 0 only when there is something to act on, 1 always
    int touchAimToggle;     // 1: a tap raises the weapon and the next lowers it
    int touchStickRun;      // 1: the stick pushed to its rim runs
    int touchMovement;      // 0 the floating stick, 1 the d-pad with its run toggle
    float touchOpacity;
    float touchScale;
    float phoneVibration;  // the pad rumble on a phone's own vibrator (input/phone_rumble.h): 0 off .. 1 full
    char touchLayout[240];  // input/touch/touch_layout.h; empty = the default places; a 256-byte config line
} s_PcConfig;

extern s_PcConfig g_PcConfig;

// Bumped on every change of a bind at runtime, so a cached resolution of a bind knows to resolve again.
extern int g_PcBindsGen;

// new_game_difficulty: -1 easy, 0 normal, 1 hard; 99 leaves it to the menu.
extern int g_PcNewGameDifficulty;

// Non-zero when -renderer overrode the renderer key for this run: Options then shows the row locked.
extern int g_PcRendererFromCommandLine;

// Defined by the cell stream (engine/runtime/cell_stream_runtime.cpp).
int Pc_WholeMapDrawActive(void);
int Pc_LongViewActive(void);
int Pc_WorldPointInView(int x, int z);

// Sets flashlight_mode 0..3 and the renderer's flashlight. An intensity or size still at the other per-pixel
// style's default moves to this style's; persist writes the keys to config.cfg.
void Pc_FlashlightModeApply(int mode, int persist);
const char* Pc_FlashlightModeLabel(int mode);

// Reads config.cfg; a missing file leaves the defaults. Migrates and stamps an older config_version.
void PcConfig_Load(const char* path);
// Writes the setting lines of the file into the log, so a posted log says what the run used.
void PcConfig_LogEffective(const char* path);
// The built-in defaults with the graphics preset of the file applied.
const s_PcConfig* PcConfig_Defaults(void);
/* 1 when config.cfg names graphics_preset or a render-quality key differs from the default preset. */
int PcConfig_GraphicsChosen(void);

// The writers change the file PcConfig_Load read and keep every other line as it is. A key with no line gets one
// at the end; every copy of a key given twice is rewritten, since the last one is the one read.
void PcConfig_SaveKeyValue(const char* key, const char* value);
void PcConfig_SaveKeyValues(const char* const* keys, const char* const* values, int count);
void PcConfig_SaveMapName(const char* mapName);
// Turns the lines of the keys into comments, so the keys fall back to their defaults on the next start.
void PcConfig_RemoveKeys(const char* const* keys, int count);
// Replaces the custom bind section (`bind ...` lines) with these lines, written as typed.
void PcConfig_SaveBindLines(const char* const* lines, int count);
// Clamps to 0..1, sets the voice volume and writes xa_volume.
void PcConfig_ApplyXaVolume(float norm);

// The live 24-byte slot behind a bind key (key_cross, pad_reload, key_quicksave, action_*) for scheme 0 classic or
// 1 altcam, or NULL. *outPerScheme is 1 when the altcam copy is saved under key + "_altcam".
char* PcConfig_BindField(const char* key, int scheme, int* outPerScheme);
// The built-in value of the same slot, "" when there is none.
const char* PcConfig_BindDefault(const char* key, int scheme);

// The size the frame is rendered at; the configured window size until the window exists. Widescreen extents are
// taken from these, since the window may have been resized away from the configured size.
static inline int PcConfig_RenderWidth(void) {
    return sh::render::published().scene_width > 0 ? sh::render::published().scene_width : g_PcConfig.windowWidth;
}
static inline int PcConfig_RenderHeight(void) {
    return sh::render::published().scene_height > 0 ? sh::render::published().scene_height : g_PcConfig.windowHeight;
}

#ifdef __cplusplus
}
#endif
