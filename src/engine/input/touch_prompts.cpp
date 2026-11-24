// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/input/touch_prompts.h"

#include <chrono>
#include <mutex>
#include <vector>

namespace sh::input {
namespace {

struct Prompt {
    std::string control;
    std::string icon;
    long long at_ms = 0;
};

std::mutex g_mutex;
std::vector<Prompt> g_prompts;

long long now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

}  // namespace

void note_touch_prompt(std::string_view control, std::string_view icon) {
    if (control.empty()) return;
    const std::lock_guard<std::mutex> lock(g_mutex);
    for (Prompt& p : g_prompts) {
        if (p.control == control) {
            p.icon = icon;
            p.at_ms = now_ms();
            return;
        }
    }
    g_prompts.push_back({std::string(control), std::string(icon), now_ms()});
}

std::optional<std::string> touch_prompt(std::string_view control, int ms) {
    const std::lock_guard<std::mutex> lock(g_mutex);
    for (const Prompt& p : g_prompts) {
        if (p.control == control) {
            if (now_ms() - p.at_ms <= ms) return p.icon;
            return std::nullopt;
        }
    }
    return std::nullopt;
}

}  // namespace sh::input
