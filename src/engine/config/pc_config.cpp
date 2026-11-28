// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/config/pc_config.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "audio/voice/voice_line_player.h"
#include "engine/config/config_keys.h"
#include "engine/config/retired_keys.h"
#include "engine/log/sh_log.h"
#include "engine/mods/extra_config.h"
#include "input/bind/console_binds.h"
#include "input/pc_actions.h"
#include "input/pc_quick_actions.h"
#include "render/gpu/render_state.h"

extern "C" void Pc_LoadScreenMinVblanksSet(int v);

constinit s_PcConfig g_PcConfig = sh::config::built_in_defaults();
int g_PcNewGameDifficulty = 99;
int g_PcRendererFromCommandLine = 0;
int g_PcBindsGen = 0;
// The blood colour the player chose. Its writers (options, save load, settings reset) mirror it here and every map
// load applies it again, so a stray write into the game's own record cannot change the colour.
unsigned char g_PcTrustedBloodColor = 0;

namespace {

using sh::config::Hook;
using sh::config::Kind;
using sh::config::Setting;

std::string s_path = "config.cfg";
s_PcConfig s_defaults;
bool s_defaults_taken = false;

// What the file said so far, for the keys whose meaning depends on other keys.
struct Seen {
    bool culling = false;
    bool flashlight_mode = false;
    bool dither_keys = false;
};
Seen s_seen;
// The file sets graphics_preset: the player chose the graphics, a CPU renderer leaves them as they are.
bool s_preset_named = false;

void take_defaults() {
    if (s_defaults_taken) return;
    s_defaults = g_PcConfig;
    s_defaults_taken = true;
}

// The trim of the file's reader: leading blanks and tabs, trailing blanks, tabs and line ends.
std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n')) {
        s.remove_suffix(1);
    }
    return s;
}

std::string_view skip_blanks(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    return s;
}

// The key of a `key = value` line, cut to 63 characters before the trim as the reader always did; empty for a
// comment or a line without '='.
std::string line_key(std::string_view line, std::size_t* eq_out = nullptr) {
    line = skip_blanks(line);
    if (line.empty() || line.front() == '#' || line.front() == ';') return {};
    const std::size_t eq = line.find('=');
    if (eq == std::string_view::npos) return {};
    if (eq_out) *eq_out = eq;
    return std::string(trim(line.substr(0, eq < 63 ? eq : 63)));
}

bool is_line_key(std::string_view line) {
    line = skip_blanks(line);
    return !line.empty() && line.front() != '#' && line.front() != ';' && line.find('=') != std::string_view::npos;
}

void copy_text(std::span<char> field, std::string_view text, bool clear_rest) {
    const std::size_t n = text.size() < field.size() - 1 ? text.size() : field.size() - 1;
    std::memcpy(field.data(), text.data(), n);
    if (clear_rest) {
        std::memset(field.data() + n, 0, field.size() - n);
    } else {
        field[n] = '\0';
    }
}

int clamp_whole(int v, const Setting& s) {
    const int lo = static_cast<int>(s.lo), hi = static_cast<int>(s.hi);
    return v < lo ? lo : v > hi ? hi : v;
}

float clamp_real(float v, const Setting& s) {
    const float lo = static_cast<float>(s.lo), hi = static_cast<float>(s.hi);
    return v < lo ? lo : v > hi ? hi : v;
}

void run_custom(const Setting& s, const char* value) {
    switch (s.hook) {
        case Hook::Culling:
            s_seen.culling = true;
            if (std::strcmp(value, "modern") == 0 || std::strcmp(value, "1") == 0) {
                g_PcConfig.culling = 1;
            } else if (std::strcmp(value, "original") == 0 || std::strcmp(value, "0") == 0) {
                g_PcConfig.culling = 0;
            } else {
                SH_LOG_WARN(CONFIG, "culling = %s is not valid (modern or original); ignored", value);
            }
            break;
        case Hook::ObsoleteCulling:
            SH_LOG_DEBUG(CONFIG, "disable_culling is obsolete; use culling = modern|original");
            if (!s_seen.culling) g_PcConfig.culling = std::atoi(value) != 0 ? 1 : 0;
            break;
        case Hook::WeatherRate:
            g_PcConfig.weatherSimHz = std::atoi(value) == 30 ? 30 : 60;
            break;
        case Hook::Msaa: {
            // Snapped to a count every driver takes, so a typo cannot fail the window.
            const int v = std::atoi(value);
            g_PcConfig.msaaSamples = v >= 8 ? 8 : v >= 4 ? 4 : v >= 2 ? 2 : 0;
            break;
        }
        case Hook::GraphicsPreset: {
            const int preset = RenderPreset_FromName(value);
            if (preset >= 0) g_PcConfig.graphicsPreset = preset;
            break;
        }
        case Hook::GlyphSet:
            g_PcConfig.gamepadIcons = static_cast<int>(sh::input::glyph_set_from(std::atoi(value)));
            break;
        case Hook::AimZoomSwitch:
            g_PcConfig.tpsAimZoom = std::atoi(value) != 0 ? 100.0f : 0.0f;
            break;
        case Hook::RecenterDelay: {
            float v = static_cast<float>(std::atof(value));
            if (!(v >= 0.0f)) v = 0.0f;
            g_PcConfig.tpsRecenterDelay = v > 30.0f ? 30.0f : v;
            break;
        }
        case Hook::NewGameDifficulty:
            g_PcNewGameDifficulty = std::strcmp(value, "easy") == 0     ? -1
                                    : std::strcmp(value, "normal") == 0 ? 0
                                    : std::strcmp(value, "hard") == 0   ? 1
                                                                        : 99;
            break;
        case Hook::QuickWeapons:
            g_PcQuickWeaponsEnabled = std::atoi(value) != 0;
            break;
        default:
            break;
    }
}

void run_after(Hook hook) {
    switch (hook) {
        case Hook::DitherKey:
            s_seen.dither_keys = true;
            break;
        case Hook::FlashlightMode:
            s_seen.flashlight_mode = true;
            break;
        case Hook::DreamBlur:
            sh::render::graphics().dream_feedback = g_PcConfig.dreamBlur;
            break;
        case Hook::DreamBlurStrength:
            sh::render::feedback_control().damp_blend = g_PcConfig.dreamBlurStrength;
            break;
        case Hook::LoadScreenMin:
            Pc_LoadScreenMinVblanksSet(static_cast<int>(g_PcConfig.loadScreenMin * 60.0f + 0.5f));
            break;
        default:
            break;
    }
}

void apply(const Setting& s, const char* value) {
    s_PcConfig& c = g_PcConfig;
    switch (s.kind) {
        case Kind::Flag:
            c.*s.whole = std::atoi(value) != 0;
            break;
        case Kind::Whole:
            c.*s.whole = clamp_whole(std::atoi(value), s);
            break;
        case Kind::WholeAtLeast: {
            const int v = std::atoi(value);
            if (v >= static_cast<int>(s.lo)) c.*s.whole = v;
            break;
        }
        case Kind::WholeOr: {
            const int v = std::atoi(value);
            c.*s.whole = v < static_cast<int>(s.lo) || v > static_cast<int>(s.hi) ? s.otherwise : v;
            break;
        }
        case Kind::Real:
            c.*s.real = clamp_real(static_cast<float>(std::atof(value)), s);
            break;
        case Kind::RealPositive: {
            const float v = static_cast<float>(std::atof(value));
            if (v > 0.0f) c.*s.real = clamp_real(v, s);
            break;
        }
        case Kind::Word: {
            const sh::config::Word* hit = nullptr;
            for (const sh::config::Word& w : s.words) {
                if (w.text == value) {
                    hit = &w;
                    break;
                }
            }
            if (hit) {
                c.*s.whole = hit->value;
            } else if (!s.keep_unknown) {
                c.*s.whole = s.otherwise;
            }
            break;
        }
        case Kind::Text:
            // The renderer was copied with its tail cleared, the other texts with only a terminator.
            copy_text(s.text(c), value, s.text == &sh::config::text_field<&s_PcConfig::renderer>);
            break;
        case Kind::TextFit: {
            const std::span<char> field = s.text(c);
            const std::size_t n = std::strlen(value);
            if (n > 0 && n < field.size()) copy_text(field, value, true);
            break;
        }
        case Kind::Custom:
            run_custom(s, value);
            return;
        case Kind::Elsewhere:
            return;
    }
    run_after(s.hook);
}

const Setting* find_setting(std::string_view key) {
    for (const Setting& s : sh::config::kSettings) {
        if (s.key == key) return &s;
    }
    return nullptr;
}

char* scheme_slot(s_PcConfig& c, std::string_view key, int scheme) {
    for (const sh::config::SchemeBind& b : sh::config::kSchemeBinds) {
        if (b.key == key) {
            ControlScheme& s = scheme != 0 ? c.altcam : c.classic;
            return s.*b.slot;
        }
    }
    return nullptr;
}

char* global_slot(s_PcConfig& c, std::string_view key) {
    for (const sh::config::GlobalBind& b : sh::config::kGlobalBinds) {
        if (b.key == key) return c.*b.slot;
    }
    return nullptr;
}

void store_bind(char* slot, const char* value) {
    std::strncpy(slot, value, sizeof(sh::config::BindSlot) - 1);
    slot[sizeof(sh::config::BindSlot) - 1] = '\0';
}

// A key the tables do not name: an action bind, a scheme bind (with "_altcam" for the alternate scheme), a global
// bind, or a setting of the modding tools, kept by name.
void apply_unnamed(const std::string& key, const char* value) {
    if (PcActions_ParseConfigLine(key.c_str(), value)) return;
    constexpr std::string_view kAltcam = "_altcam";
    std::string_view base = key;
    int scheme = 0;
    if (base.size() > kAltcam.size() && base.ends_with(kAltcam)) {
        base.remove_suffix(kAltcam.size());
        scheme = 1;
    }
    char* slot = scheme_slot(g_PcConfig, base, scheme);
    if (!slot) slot = global_slot(g_PcConfig, key);
    if (slot) {
        store_bind(slot, value);
        return;
    }
    sh::mods::extra_config().store(key, value);
    SH_LOG_DEBUG(CONFIG, "key '%s' kept for the tools", key.c_str());
}

void read_line(const char* line) {
    std::string_view p = skip_blanks(line);
    if (p.empty() || p.front() == '#' || p.front() == ';' || p.front() == '\n' || p.front() == '\r') return;
    // Custom binds are kept as the console command that made them, not as key = value.
    if (p.starts_with("bind ") || p.starts_with("BIND ")) {
        sh::input::console_binds().load_config_line(p.substr(5));
        return;
    }
    std::size_t eq = 0;
    const std::string key = line_key(p, &eq);
    if (!is_line_key(p)) return;
    std::string_view raw = p.substr(eq + 1);
    // The reader took at most 127 characters of a value.
    const std::string value(trim(raw.substr(0, raw.size() < 127 ? raw.size() : 127)));

    if (sh::config::is_retired_key(key)) {
        SH_LOG_DEBUG(CONFIG, "%s is retired and ignored", key.c_str());
        return;
    }
    if (const Setting* s = find_setting(key)) {
        apply(*s, value.c_str());
        return;
    }
    for (const sh::config::KeyPrefix& prefix : sh::config::kKeyPrefixes) {
        if (!std::string_view(key).starts_with(prefix.prefix)) continue;
        if (prefix.hook == Hook::LogKey && !ShLog_ConfigKey(key.c_str(), value.c_str())) {
            SH_LOG_WARN(CONFIG, "unknown key '%s'", key.c_str());
        }
        return;
    }
    apply_unnamed(key, value.c_str());
}

// The settings the file implies together, once every line is read.
void settle_combined_keys() {
    s_PcConfig& c = g_PcConfig;
    // Without flashlight_mode, the legacy per-pixel pair says it: with shadows Modern + Shadows, else Modern.
    if (!s_seen.flashlight_mode && c.perPixelFlashlight) c.flashlightMode = c.flashlightShadows ? 3 : 2;

    if (c.legacyDitherFilter >= 0 && !s_seen.dither_keys) {
        // psx_dither 0..7: off, dither, bilinear, trilinear, anisotropic 2x..16x.
        c.dithering = c.legacyDitherFilter == 1;
        c.textureFilter = c.legacyDitherFilter >= 2 ? c.legacyDitherFilter - 1 : 0;
        SH_LOG("[CFG] psx_dither = %d migrated to dithering = %d, texture_filter = %d", c.legacyDitherFilter,
               c.dithering, c.textureFilter);
    } else {
        c.legacyDitherFilter = c.dithering ? 1 : c.textureFilter == 0 ? 0 : c.textureFilter + 1;
    }
}

void save_real(const char* key, float value) {
    char text[24];
    std::snprintf(text, sizeof text, "%.2f", value);
    PcConfig_SaveKeyValue(key, text);
}

bool unbound(const char* slot) { return slot[0] == '\0' || std::strcmp(slot, "NONE") == 0; }

// Moves a changed default to its new value where the file still holds the old one, then stamps the version. Each
// step writes its line at once: the action migration in between reads and writes the same file.
void migrate(int from) {
    s_PcConfig& c = g_PcConfig;
    if (from < 1 && c.worldVScale > 0.999f && c.worldVScale < 1.001f) {
        c.worldVScale = 1.08f;
        sh::render::picture().world_v_scale = 1.08f;
        save_real("world_vscale", c.worldVScale);
    }
    if (from < 2 && c.crtAspectTrim > 0.979f && c.crtAspectTrim < 0.981f) {
        c.crtAspectTrim = 1.06f;
        sh::render::picture().crt_aspect_trim = 1.06f;
        save_real("crt_aspect_trim", c.crtAspectTrim);
    }
    if (from < 3 && c.menuFpsUnlock == 1) {
        c.menuFpsUnlock = 0;
        PcConfig_SaveKeyValue("menu_fps_unlock", "0");
    }
    if (from < 9 && c.fpsCap == 30) {
        c.fpsCap = 60;
        PcConfig_SaveKeyValue("fps_cap", "60");
    }
    // The key's range already clamped 8 and 16 to 4 in memory; the file still holds them.
    if (from < 10 && c.textureUpscale == 4) PcConfig_SaveKeyValue("texture_upscale", "4");
    if (from < 11 && c.msgVoiceTailMs == 1000) {
        c.msgVoiceTailMs = 500;
        PcConfig_SaveKeyValue("msg_voice_tail_ms", "500");
    }
    // The over-the-shoulder camera is gone: its name reads as third person, and the file says so.
    if (from < 13 && c.controlStyle == 1) PcConfig_SaveKeyValue("control_style", "tps");
    // The crosshair of the free cameras is on by default, shown while a gun is aimed.
    if (from < 13) {
        for (const auto& [key, field] : {std::pair{"crosshair", &c.crosshair},
                                         std::pair{"crosshair_fps_aim_only", &c.crosshairFpsAimOnly},
                                         std::pair{"crosshair_hide_melee", &c.crosshairHideMelee}}) {
            if (*field != 0) continue;
            *field = 1;
            PcConfig_SaveKeyValue(key, "1");
        }
    }
    ControlScheme* const schemes[2] = {&c.classic, &c.altcam};
    const auto save_slot = [](const char* key, int scheme, const char* value) {
        const std::string full = std::string(key) + (scheme ? "_altcam" : "");
        PcConfig_SaveKeyValue(full.c_str(), value);
    };
    if (from < 6) {
        struct Fill {
            const char* key;
            sh::config::BindSlot ControlScheme::* slot;
            const char* value;
        };
        static constexpr Fill kFills[] = {{"pad_reload", &ControlScheme::padReload, "rightshoulder"},
                                          {"pad_quick_turn", &ControlScheme::padQuickTurn, "leftshoulder"}};
        for (int s = 0; s < 2; ++s) {
            for (const Fill& f : kFills) {
                char* slot = schemes[s]->*f.slot;
                if (!unbound(slot)) continue;
                std::snprintf(slot, sizeof(sh::config::BindSlot), "%s", f.value);
                save_slot(f.key, s, f.value);
            }
        }
    }
    if (from < 7) {
        // Both schemes go through the whole list, the "_altcam" rows included: that is how version 7 shipped,
        // and a file migrated by it is what later versions read.
        struct Move {
            const char* key;
            sh::config::BindSlot ControlScheme::* slot;
            const char* was;  // "" = only an unbound slot moves
            const char* now;
        };
        static constexpr Move kMoves[] = {
            {"key_cross", &ControlScheme::keyCross, "C", "Space"},
            {"key_circle", &ControlScheme::keyCircle, "V", "Escape"},
            {"key_cross_2", &ControlScheme::keyCross2, "", "C"},
            {"key_circle_2", &ControlScheme::keyCircle2, "", "V"},
            {"key_start", &ControlScheme::keyStart, "Return", "Tab"},
            {"key_select", &ControlScheme::keySelect, "Space", "M"},
            {"key_start_2", &ControlScheme::keyStart2, "", "Return"},
            {"key_select_2", &ControlScheme::keySelect2, "", "Space"},
            {"key_circle_altcam", &ControlScheme::keyCircle, "F", "Escape"},
            {"key_circle_2_altcam", &ControlScheme::keyCircle2, "", "F"},
            {"key_start_altcam", &ControlScheme::keyStart, "Return", "Tab"},
            {"key_select_altcam", &ControlScheme::keySelect, "Space", "M"},
            {"key_start_2_altcam", &ControlScheme::keyStart2, "", "Return"},
            {"key_select_2_altcam", &ControlScheme::keySelect2, "", "Space"},
        };
        for (int s = 0; s < 2; ++s) {
            for (const Move& m : kMoves) {
                char* slot = schemes[s]->*m.slot;
                const bool old = m.was[0] != '\0' ? std::strcmp(slot, m.was) == 0 : unbound(slot);
                if (!old) continue;
                std::snprintf(slot, sizeof(sh::config::BindSlot), "%s", m.now);
                save_slot(m.key, s, m.now);
            }
        }
        // Escape and then P were earlier defaults of the exit key.
        if (std::strcmp(c.keyExitGame, "Escape") == 0 || std::strcmp(c.keyExitGame, "P") == 0) {
            std::snprintf(c.keyExitGame, sizeof c.keyExitGame, "NONE");
            PcConfig_SaveKeyValue("key_exit_game", "NONE");
        }
    }
    for (const sh::config::BindMigration& m : sh::config::kBindMigrations) {
        char* slot = schemes[m.altcam ? 1 : 0]->*m.slot;
        if (!sh::config::bind_migrates(m, from, slot)) continue;
        std::snprintf(slot, sizeof(sh::config::BindSlot), "%.*s", static_cast<int>(m.now.size()), m.now.data());
        save_slot(m.key.data(), m.altcam ? 1 : 0, slot);
    }
    PcActions_FinishLoad(from < 8);
    c.configVersion = PC_CONFIG_VERSION;
    PcConfig_SaveKeyValue("config_version", std::to_string(PC_CONFIG_VERSION).c_str());
}

// The file as lines, each with its line end; nothing when it cannot be opened.
bool read_lines(std::vector<std::string>& lines) {
    FILE* f = std::fopen(s_path.c_str(), "r");
    if (!f) return false;
    std::string text;
    char chunk[4096];
    for (std::size_t n; (n = std::fread(chunk, 1, sizeof chunk, f)) > 0;) text.append(chunk, n);
    std::fclose(f);
    for (std::size_t start = 0; start < text.size();) {
        const std::size_t end = text.find('\n', start);
        const std::size_t stop = end == std::string::npos ? text.size() : end + 1;
        lines.emplace_back(text, start, stop - start);
        start = stop;
    }
    return true;
}

void write_lines(const std::vector<std::string>& lines, const std::string& tail = {}) {
    FILE* f = std::fopen(s_path.c_str(), "w");
    if (!f) return;
    for (const std::string& line : lines) std::fputs(line.c_str(), f);
    std::fputs(tail.c_str(), f);
    std::fclose(f);
}

}  // namespace

void Pc_FlashlightModeApply(int mode, int persist) {
    if (mode < 0 || mode > 3) mode = 0;
    const int per_pixel = mode != 0;
    const int classic_style = mode == 1;  // per pixel, calibrated to the console's look
    const int shadows = mode == 1 || mode == 3;
    s_PcConfig& c = g_PcConfig;

    bool swapped = false;
    if (per_pixel) {
        // Each per-pixel style has its own defaults; only a value still at the other style's default follows.
        const float intensity = classic_style ? 1.20f : 2.10f, other_intensity = classic_style ? 2.10f : 1.20f;
        const float size = classic_style ? 3.00f : 2.40f, other_size = classic_style ? 2.40f : 3.00f;
        const auto near = [](float v, float to) { return v > to - 0.005f && v < to + 0.005f; };
        if (near(c.flashlightIntensity, other_intensity)) {
            c.flashlightIntensity = intensity;
            swapped = true;
        }
        if (near(c.flashlightSize, other_size)) {
            c.flashlightSize = size;
            swapped = true;
        }
        sh::render::flashlight().intensity = c.flashlightIntensity;
        sh::render::flashlight().size = c.flashlightSize;
    }
    c.flashlightMode = mode;
    c.perPixelFlashlight = per_pixel;
    c.flashlightShadows = shadows;
    sh::render::flashlight().per_pixel = per_pixel;
    sh::render::flashlight().shadows = shadows;
    sh::render::flashlight().style = classic_style;

    if (!persist) return;
    char mode_text[16], intensity_text[16], size_text[16];
    std::snprintf(mode_text, sizeof mode_text, "%d", mode);
    std::snprintf(intensity_text, sizeof intensity_text, "%.2f", c.flashlightIntensity);
    std::snprintf(size_text, sizeof size_text, "%.2f", c.flashlightSize);
    const char* keys[] = {"flashlight_mode", "per_pixel_flashlight", "flashlight_shadows", "flashlight_intensity",
                          "flashlight_size"};
    const char* values[] = {mode_text, per_pixel ? "1" : "0", shadows ? "1" : "0", intensity_text, size_text};
    PcConfig_SaveKeyValues(keys, values, swapped ? 5 : 3);
}

const char* Pc_FlashlightModeLabel(int mode) {
    static const char* const kLabels[] = {"Classic", "Classic + Shadows", "Modern", "Modern + Shadows"};
    return kLabels[mode >= 0 && mode <= 3 ? mode : 0];
}

const s_PcConfig* PcConfig_Defaults(void) {
    take_defaults();
    return &s_defaults;
}

int PcConfig_GraphicsChosen(void) {
    return s_preset_named || !RenderPreset_Matches(RENDER_PRESET_DEFAULT, &g_PcConfig) ? 1 : 0;
}

void PcConfig_Load(const char* path) {
    take_defaults();
    PcActions_ResetDefaults();
    s_seen = {};
    if (path) s_path.assign(path, std::strlen(path) < 511 ? std::strlen(path) : 511);

    FILE* f = std::fopen(path, "r");
    // The preset is the default of the render-quality keys: applied before the lines, which still win, and to the
    // defaults, since a reset to defaults means the preset.
    int named = 0;
    const int preset = RenderPreset_Scan(f, &named);
    s_preset_named = named != 0;
    RenderPreset_Apply(preset, &g_PcConfig);
    RenderPreset_Apply(preset, &s_defaults);
    if (!f) {
        SH_LOG_INFO(CONFIG, "%s not found, using defaults (%dx%d, fullscreen=%d, map=%s)", path, g_PcConfig.windowWidth,
                    g_PcConfig.windowHeight, g_PcConfig.fullscreen, g_PcConfig.mapName);
        PcActions_FinishLoad(0);
        return;
    }
    SH_LOG_INFO(CONFIG, "loading %s", path);
    // 256 bytes at a time, as the file was always read: the rest of a longer line is read as a line of its own.
    char line[256];
    while (std::fgets(line, sizeof line, f)) read_line(line);
    std::fclose(f);

    settle_combined_keys();
    if (g_PcConfig.configVersion < PC_CONFIG_VERSION) {
        migrate(g_PcConfig.configVersion);
    } else {
        PcActions_FinishLoad(0);
    }
    SH_LOG_INFO(CONFIG, "resolution %dx%d, fullscreen %d, culling %s, map %s", g_PcConfig.windowWidth,
                g_PcConfig.windowHeight, g_PcConfig.fullscreen, g_PcConfig.culling ? "modern" : "original",
                g_PcConfig.mapName);
}

// The raw lines rather than the parsed values: a key the game does not read shows up too, which is how a typo
// becomes visible. Retired keys stay out, since an old file may carry an account token and logs are posted.
void PcConfig_LogEffective(const char* path) {
    FILE* f = std::fopen(path, "r");
    if (!f) {
        SH_DBG("[CFG] %s not found -- every setting is at its compiled-in default", path);
        return;
    }
    SH_DBG("[CFG] ---- %s ----", path);
    int count = 0;
    char line[512];
    while (std::fgets(line, sizeof line, f)) {
        const std::string_view text = trim(line);
        if (text.empty() || text.front() == '#' || text.front() == ';' || sh::config::is_retired_line(text)) continue;
        SH_DBG("[CFG] %.*s", static_cast<int>(text.size()), text.data());
        ++count;
    }
    std::fclose(f);
    SH_DBG("[CFG] ---- %d setting(s) ----", count);
}

void PcConfig_SaveKeyValues(const char* const* keys, const char* const* values, int count) {
    if (!keys || !values || count <= 0) return;
    if (count > 256) count = 256;
    std::vector<std::string> lines;
    if (!read_lines(lines)) return;

    std::vector<bool> written(static_cast<std::size_t>(count), false);
    for (int k = 0; k < count; ++k) {
        if (keys[k] && sh::config::is_retired_key(keys[k])) {
            written[k] = true;
            SH_LOG_DEBUG(CONFIG, "%s is retired; not written", keys[k]);
        }
    }
    const auto assignment = [](const char* key, const char* value) { return std::string(key) + " = " + value + "\n"; };
    for (std::string& line : lines) {
        if (!is_line_key(line)) continue;
        const std::string key = line_key(line);
        if (sh::config::is_retired_key(key)) continue;
        for (int k = 0; k < count; ++k) {
            if (keys[k] && values[k] && key == keys[k]) {
                line = assignment(keys[k], values[k]);
                written[k] = true;
                break;
            }
        }
    }
    std::string tail;
    for (int k = 0; k < count; ++k) {
        if (!written[k] && keys[k] && keys[k][0] != '\0' && values[k]) tail += assignment(keys[k], values[k]);
    }
    write_lines(lines, tail);
}

void PcConfig_SaveKeyValue(const char* key, const char* value) {
    if (!key || key[0] == '\0' || !value) return;
    PcConfig_SaveKeyValues(&key, &value, 1);
}

void PcConfig_SaveMapName(const char* mapName) {
    if (mapName && mapName[0] != '\0') PcConfig_SaveKeyValue("map", mapName);
}

void PcConfig_RemoveKeys(const char* const* keys, int count) {
    if (!keys || count <= 0) return;
    std::vector<std::string> lines;
    if (!read_lines(lines)) return;
    bool changed = false;
    for (std::string& line : lines) {
        if (!is_line_key(line)) continue;
        const std::string key = line_key(line);
        for (int k = 0; k < count; ++k) {
            if (keys[k] && key == keys[k]) {
                line = "# " + std::string(skip_blanks(line));
                changed = true;
                break;
            }
        }
    }
    if (changed) write_lines(lines);
}

void PcConfig_SaveBindLines(const char* const* binds, int count) {
    std::vector<std::string> lines;
    if (!read_lines(lines)) return;
    constexpr std::string_view kHeader = "# --- Custom key binds";
    std::erase_if(lines, [&](const std::string& line) {
        const std::string_view p = skip_blanks(line);
        return p.starts_with("bind ") || p.starts_with("BIND ") || p.starts_with(kHeader);
    });
    // Trailing blank lines go, so the section does not move down the file on every write.
    while (!lines.empty() && lines.back().find_first_not_of(" \t\r\n") == std::string::npos) lines.pop_back();

    std::string tail;
    if (count > 0) {
        tail = "\n# --- Custom key binds (console: bind / unbind / unbindall) ---\n";
        for (int i = 0; i < count; ++i) {
            if (binds[i] && binds[i][0] != '\0') tail += std::string(binds[i]) + "\n";
        }
    }
    write_lines(lines, tail);
}

void PcConfig_ApplyXaVolume(float norm) {
    norm = norm < 0.0f ? 0.0f : norm > 1.0f ? 1.0f : norm;
    g_PcConfig.xaVolume = norm;
    sh::audio::voice_lines().set_volume(norm);
    char text[16];
    std::snprintf(text, sizeof text, "%.3f", norm);
    PcConfig_SaveKeyValue("xa_volume", text);
}

namespace {

char* bind_slot(s_PcConfig& c, const char* key, int scheme, int* out_per_scheme) {
    if (out_per_scheme) *out_per_scheme = 0;
    if (!key) return nullptr;
    if (char* slot = scheme_slot(c, key, scheme)) {
        if (out_per_scheme) *out_per_scheme = 1;
        return slot;
    }
    return global_slot(c, key);
}

}  // namespace

char* PcConfig_BindField(const char* key, int scheme, int* outPerScheme) {
    if (key && std::strncmp(key, "action_", 7) == 0) return PcActions_BindField(key, scheme, outPerScheme);
    return bind_slot(g_PcConfig, key, scheme, outPerScheme);
}

const char* PcConfig_BindDefault(const char* key, int scheme) {
    if (key && std::strncmp(key, "action_", 7) == 0) return PcActions_BindDefault(key, scheme);
    take_defaults();
    const char* slot = bind_slot(s_defaults, key, scheme, nullptr);
    return slot ? slot : "";
}
