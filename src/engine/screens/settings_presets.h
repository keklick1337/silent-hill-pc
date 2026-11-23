// SPDX-License-Identifier: GPL-3.0-or-later
// The options menu's grouped settings: one row that stands for
// several config.cfg keys. Flashlight and Lighting are Classic / Modern, Shadows is Off / Low /
// Medium / High / Ultra, and the Quality Preset row follows graphics_preset. Each choice is a table of
// config key -> value; the row shows the choice whose table the current values match, or Custom
// when the keys were set by hand. Pure: the caller reads and writes the keys.

#pragma once

#include <functional>
#include <span>
#include <string_view>

namespace sh::screens::presets {

// One config.cfg key and the value a choice gives it (ints are stored as whole floats).
struct KeyValue {
    std::string_view key;
    float value;
};

// The current value of a config key.
using Getter = std::function<float(std::string_view key)>;

// Shown when the values match no choice of a row.
constexpr int kCustom = -1;

enum Style : int { kClassic = 0, kModern = 1, kStyleCount = 2 };
enum Shadow : int { kShadowOff = 0, kShadowLow, kShadowMedium, kShadowHigh, kShadowUltra, kShadowCount };
// The ids of render/settings/render_preset.h (custom is not a menu choice).
enum Preset : int {
    kOriginal = 0,
    kEnhanced = 1,
    kHigh = 2,
    kUltra = 3,
    kUltraUpscaled = 4,
    kUltraRtx = 5,
    kUltraRtxUpscaled = 6,
    kPresetCount = 7
};

std::span<const KeyValue> flashlight_style(int style);
std::span<const KeyValue> lighting_style(int style);
std::span<const KeyValue> shadow_quality(int level);

// The config keys a graphics preset sets (render_preset.cpp), in config.cfg spelling.
std::span<const std::string_view> preset_keys();

// How config.cfg spells `value` of a key that takes a name instead of a number
// (load_screen_trail: 0 off, 1 original, 2 smooth; model_upscaling: off, tessellation, mesh_shaders);
// "" for a numeric key or a value out of range.
std::string_view value_name(std::string_view key, int value);

// The Flashlight and Lighting look that goes with a graphics preset.
int style_for_preset(int preset);

// The presets that switch on ray tracing, and those with the experimental model and texture upscaling.
bool preset_traces_rays(int preset);
bool preset_upscales(int preset);

// True when every key of the table has its value (floats within 0.005).
bool matches(std::span<const KeyValue> table, const Getter& current);

int detect_flashlight(const Getter& current);
int detect_lighting(const Getter& current);
int detect_shadow_quality(const Getter& current);
// The preset whose values (preset_value(preset, key)) the current ones match on every preset
// key, or kCustom.
int detect_preset(const std::function<float(int preset, std::string_view key)>& preset_value,
                  const Getter& current);

// The next choice of a cycling row; Custom sits before the first choice.
int step_choice(int current, int count, int dir);

}  // namespace sh::screens::presets
