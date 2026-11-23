// SPDX-License-Identifier: GPL-3.0-or-later

#include "engine/mods/data_reload.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <string_view>
#include <utility>

namespace sh::mods {

namespace {

std::atomic<std::uint32_t> g_epoch{0};
ReloadChanges g_last;

std::string lower(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

// Files the game reads once when it starts, before or outside anything a reload can redo: the renderer's shaders,
// the font sheets, and the config of the boot, the frame loop and the memory layout.
constexpr std::string_view kRestartPrefixes[] = {"shaders/", "fonts/", "convert/fonts/"};
constexpr std::string_view kRestartFiles[] = {
    "config/boot.json",        "config/boot_audio.json", "config/main_frame.json", "config/packet_arena.json",
    "config/sound_driver.json", "config/chara_vram.json", "config/frame_gate.json", "config/scene_owner.json",
    "config/mesh_draw.json",
    // The sound driver's tables are read by the audio side while it plays; the run-long map words hold their values.
    "config/sound_tables.json", "config/map_globals.json",
};

}  // namespace

std::uint32_t data_epoch() { return g_epoch.load(std::memory_order_acquire); }

void advance_data_epoch() { g_epoch.fetch_add(1, std::memory_order_acq_rel); }

bool data_stale(std::uint32_t& seen) {
    const std::uint32_t now = data_epoch();
    if (seen == now) {
        return false;
    }
    seen = now;
    return true;
}

int change_kind(std::string_view path) {
    const std::string p = lower(path);
    for (std::string_view prefix : kRestartPrefixes) {
        if (p.starts_with(prefix)) {
            return 2;
        }
    }
    for (std::string_view file : kRestartFiles) {
        if (p == file) {
            return 2;
        }
    }
    if (p.starts_with("text/") || p.starts_with("menus/") || p == "scripts/main.lua") {
        return 0;
    }
    return 1;
}

ReloadChanges classify_changes(const Registry::FileStamps& before, const Registry::FileStamps& after) {
    ReloadChanges out;
    const auto add = [&out](const std::string& key) {
        const std::size_t slash = key.find('/');
        const std::string_view path = slash == std::string::npos ? std::string_view(key)
                                                                 : std::string_view(key).substr(slash + 1);
        switch (change_kind(path)) {
            case 0: out.live.push_back(key); break;
            case 1: out.data.push_back(key); break;
            default: out.restart.push_back(key); break;
        }
    };
    for (const auto& [key, stamp] : after) {
        const auto it = before.find(key);
        if (it == before.end() || it->second != stamp) {
            add(key);
        }
    }
    for (const auto& [key, stamp] : before) {
        if (after.find(key) == after.end()) {
            add(key);
        }
    }
    return out;
}

const ReloadChanges& last_reload_changes() { return g_last; }

void set_last_reload_changes(ReloadChanges changes) { g_last = std::move(changes); }

}  // namespace sh::mods
