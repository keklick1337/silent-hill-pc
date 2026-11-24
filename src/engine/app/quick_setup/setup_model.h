// SPDX-License-Identifier: GPL-3.0-or-later
// The quick setup's decisions without a window: when it is shown,
// what the detected hardware suggests (renderer, graphics preset, display), which choices the hardware allows, and
// the config.cfg lines the choices become.
#pragma once

#include "engine/app/screen_kit/script.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sh::quick_setup {

enum class GpuKind { None, Cpu, Integrated, Discrete, Other };

// What the probe found (hardware_probe.h). Every field may stay at its default: nothing was found.
struct Hardware {
    bool gl = false;           // an OpenGL 3.3 context could be made
    bool gl_software = false;  // ...and it draws on the CPU (llvmpipe and the like)
    bool gl_legacy = false;    // ...through the system's frozen OpenGL layer (macOS: 4.1 over Metal), Vulkan the better
                               // path where there is one
    std::string gl_renderer;   // GL_RENDERER
    bool vulkan = false;       // a Vulkan device the game's Vulkan renderer accepts
    std::string vulkan_error;  // why not, when it is not
    std::string gpu_name;      // the Vulkan device's name
    GpuKind kind = GpuKind::None;
    std::uint64_t vram_bytes = 0;  // device-local memory; 0 = unknown
    bool ray_tracing = false;      // the device runs what the ray-traced effects need
};

GpuKind gpu_kind_from_vulkan_type(std::string_view device_type);

enum class Renderer { Gl, Vulkan };

// The config.cfg spelling of a renderer: "gl", "vulkan".
std::string_view renderer_name(Renderer renderer);

// A graphics preset of the preset table (render/settings/render_preset.h), in table order.
struct Preset {
    std::string name;          // config.cfg spelling: "original", "ultra_rtx", ...
    bool ray_tracing = false;  // the preset turns ray tracing on
};

enum class Availability {
    Available,
    NoVulkan,      // Vulkan: no usable device
    NoRayTracing,  // a ray-traced preset: the renderer or the GPU traces no rays
};

Availability renderer_availability(const Hardware& hw, Renderer renderer);
Availability preset_availability(const Hardware& hw, Renderer renderer, const Preset& preset);

// The renderer that fits the hardware: Vulkan on a discrete GPU that has it and wherever the system's OpenGL is a
// legacy layer (macOS), OpenGL (close to the original and the lightest) everywhere else.
Renderer recommend_renderer(const Hardware& hw);

// The preset that fits the hardware on `renderer`: a CPU renderer gets original, an integrated GPU enhanced, a
// discrete one high, ultra from 6 GiB of video memory, ultra_rtx from 8 GiB when it traces rays. A preset the table
// lacks or the hardware cannot run gives the nearest lighter one. Empty only for an empty table.
std::string recommend_preset(const Hardware& hw, Renderer renderer, std::span<const Preset> presets);

// config.cfg's fullscreen values.
enum class WindowMode : int { Windowed = 0, Fullscreen = 1, Borderless = 2 };

struct Resolution {
    int width = 0;
    int height = 0;
    bool operator==(const Resolution&) const = default;
};

// The display's modes as the resolution choices: each size once, largest first, nothing below 640x480, the desktop
// size always among them. Falls back to 1024x768 when nothing is known.
std::vector<Resolution> resolution_choices(std::span<const Resolution> modes, Resolution desktop);

// The index of `wanted` in `choices`, else of the largest choice that fits in it, else 0.
std::size_t nearest_resolution(std::span<const Resolution> choices, Resolution wanted);

// What the player chose.
struct Choices {
    std::string language;  // a text directory's code ("en", "ru", ...)
    Renderer renderer = Renderer::Gl;
    std::string preset;
    Resolution resolution{1024, 768};
    WindowMode window_mode = WindowMode::Windowed;
};

using KeyValues = std::vector<std::pair<std::string, std::string>>;

// The config.cfg keys the choices set, quick_setup = 0 among them so the setup is not shown again.
KeyValues config_values(const Choices& choices);

// A new config.cfg holding only `values` (when there was none): a header, config_version first.
std::string new_config_text(const KeyValues& values, int config_version);

// When the setup is shown: --setup always; otherwise on the first start (no config.cfg yet, or the file still says
// quick_setup = 1, as the shipped one does), never in a run a command script drives. A display someone can answer
// is checked separately.
struct StartState {
    bool asked = false;  // --setup
    bool config_exists = false;
    int quick_setup_key = 0;  // config.cfg's quick_setup
    bool scripted = false;    // -consolecmd or --quit-after-import
};
bool should_show(const StartState& state);

// The language to start the screen in: the config's (unless "auto" or unknown), else the environment locale's
// language ("pt" of "pt_BR.UTF-8"), else English.
std::string initial_language(std::string_view config_language, std::string_view locale,
                             std::span<const std::string> known_codes);

// A test script (--setup-script <file>); the verbs are the screen's (setup_screen.cpp).
using sh::screen_kit::parse_script;
using sh::screen_kit::ScriptCommand;

}  // namespace sh::quick_setup
