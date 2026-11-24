// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/app/quick_setup/setup_model.h"

#include <algorithm>
#include <array>
#include <cctype>

namespace sh::quick_setup {

namespace {

constexpr std::uint64_t kGiB = 1024ull * 1024ull * 1024ull;

// The presets a recommendation may land on, heaviest first; the experimental ones are only ever chosen by hand.
constexpr std::array<std::string_view, 5> kLadder = {"ultra_rtx", "ultra", "high", "enhanced", "original"};

std::string_view wished_preset(const Hardware& hw, Renderer renderer) {
    if (renderer == Renderer::Gl) {
        if (!hw.gl || hw.gl_software) return "original";
        return hw.kind == GpuKind::Discrete ? "high" : "enhanced";
    }
    switch (hw.kind) {
        case GpuKind::Discrete:
            if (hw.ray_tracing && hw.vram_bytes >= 8 * kGiB) return "ultra_rtx";
            return hw.vram_bytes >= 6 * kGiB ? "ultra" : "high";
        case GpuKind::Cpu:
            return "original";
        default:
            return "enhanced";
    }
}

}  // namespace

GpuKind gpu_kind_from_vulkan_type(std::string_view device_type) {
    if (device_type == "discrete") return GpuKind::Discrete;
    if (device_type == "integrated") return GpuKind::Integrated;
    if (device_type == "cpu") return GpuKind::Cpu;
    if (device_type.empty()) return GpuKind::None;
    return GpuKind::Other;
}

std::string_view renderer_name(Renderer renderer) { return renderer == Renderer::Vulkan ? "vulkan" : "gl"; }

Availability renderer_availability(const Hardware& hw, Renderer renderer) {
    if (renderer == Renderer::Vulkan && (!hw.vulkan || hw.kind == GpuKind::Cpu)) return Availability::NoVulkan;
    return Availability::Available;
}

Availability preset_availability(const Hardware& hw, Renderer renderer, const Preset& preset) {
    if (preset.ray_tracing && (renderer != Renderer::Vulkan || !hw.ray_tracing)) return Availability::NoRayTracing;
    return Availability::Available;
}

Renderer recommend_renderer(const Hardware& hw) {
    if (renderer_availability(hw, Renderer::Vulkan) == Availability::Available &&
        (hw.kind == GpuKind::Discrete || hw.gl_legacy))
        return Renderer::Vulkan;
    return Renderer::Gl;
}

std::string recommend_preset(const Hardware& hw, Renderer renderer, std::span<const Preset> presets) {
    const auto usable = [&](std::string_view name) {
        return std::any_of(presets.begin(), presets.end(), [&](const Preset& p) {
            return p.name == name && preset_availability(hw, renderer, p) == Availability::Available;
        });
    };
    const std::string_view wish = wished_preset(hw, renderer);
    auto at = std::find(kLadder.begin(), kLadder.end(), wish);
    for (; at != kLadder.end(); ++at)
        if (usable(*at)) return std::string(*at);
    for (const Preset& p : presets)
        if (preset_availability(hw, renderer, p) == Availability::Available) return p.name;
    return presets.empty() ? std::string() : presets.front().name;
}

std::vector<Resolution> resolution_choices(std::span<const Resolution> modes, Resolution desktop) {
    std::vector<Resolution> out;
    const auto add = [&](Resolution r) {
        if (r.width < 640 || r.height < 480) return;
        if (std::find(out.begin(), out.end(), r) == out.end()) out.push_back(r);
    };
    add(desktop);
    for (const Resolution& r : modes) add(r);
    if (out.empty()) out.push_back({1024, 768});
    std::sort(out.begin(), out.end(), [](const Resolution& a, const Resolution& b) {
        return a.width != b.width ? a.width > b.width : a.height > b.height;
    });
    return out;
}

std::size_t nearest_resolution(std::span<const Resolution> choices, Resolution wanted) {
    for (std::size_t i = 0; i < choices.size(); ++i)
        if (choices[i] == wanted) return i;
    // Largest first: the first that fits is the largest that fits.
    for (std::size_t i = 0; i < choices.size(); ++i)
        if (choices[i].width <= wanted.width && choices[i].height <= wanted.height) return i;
    return 0;
}

KeyValues config_values(const Choices& c) {
    return {
        {"language", c.language.empty() ? std::string("auto") : c.language},
        {"renderer", std::string(renderer_name(c.renderer))},
        {"graphics_preset", c.preset},
        {"width", std::to_string(c.resolution.width)},
        {"height", std::to_string(c.resolution.height)},
        {"fullscreen", std::to_string(static_cast<int>(c.window_mode))},
        {"quick_setup", "0"},
    };
}

std::string new_config_text(const KeyValues& values, int config_version) {
    std::string text =
        "# Silent Hill PC configuration, written by the quick setup. Every other setting keeps its default;\n"
        "# the Options screens write here what they change. The full list of settings with their defaults is\n"
        "# assets/config.cfg in the game's source, or the config.cfg of a release package.\n\n";
    text += "config_version = " + std::to_string(config_version) + "\n";
    for (const auto& [key, value] : values) text += key + " = " + value + "\n";
    return text;
}

bool should_show(const StartState& s) {
    if (s.asked) return true;
    if (s.scripted) return false;
    return !s.config_exists || s.quick_setup_key == 1;
}

std::string initial_language(std::string_view config_language, std::string_view locale,
                             std::span<const std::string> known_codes) {
    const auto known = [&](std::string_view code) {
        return std::find(known_codes.begin(), known_codes.end(), code) != known_codes.end();
    };
    if (!config_language.empty() && config_language != "auto" && known(config_language))
        return std::string(config_language);
    std::string code;
    for (char c : locale) {
        if (!std::isalpha(static_cast<unsigned char>(c))) break;
        code += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    if (!code.empty() && known(code)) return code;
    return "en";
}

}  // namespace sh::quick_setup
