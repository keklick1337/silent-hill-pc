// SPDX-License-Identifier: GPL-3.0-or-later
// The engine's settings by config.cfg key, for the settings model (src/engine/screens/settings_model.h) and the
// engine menu's setting natives: where each key lives (the PC config, the renderer's
// live copy, the game's settings block), how it applies and how it is saved.
#pragma once

#include <cstdint>
#include <string>

namespace sh::ui::pc_settings {

std::int32_t get_int(const std::string& key);
// save = false changes only the running game (benchmarks); config.cfg keeps its value.
void set_int(const std::string& key, std::int32_t value, bool save = true);
float get_float(const std::string& key);
// `text` (optional) is the value as config.cfg stores it; null leaves config.cfg alone.
void set_float(const std::string& key, float value, const std::string* text);
// True and the current value for a key this file knows.
bool known(const std::string& key, double& value);
std::int32_t default_int(const std::string& key);
float default_float(const std::string& key);

void window_size(std::int32_t& width, std::int32_t& height);
void set_window_size(std::int32_t width, std::int32_t height);

std::string start_map();
void set_start_map(const std::string& name);

// Logs a view setting change with the view settings behind it.
void log_view_change(const std::string& key, const std::string& text);

}  // namespace sh::ui::pc_settings
