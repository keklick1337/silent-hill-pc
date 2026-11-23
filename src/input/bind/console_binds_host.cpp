// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The game's console key binds (console_binds.h): keys by the cap the active layout prints, output to the
// console, lines kept in config.cfg.

#include "input/bind/console_binds.h"

#include "engine/config/pc_config.h"
#include "engine/debug/overlay/debug_overlay.h"
#include "engine/log/sh_log.h"
#include "input/bind/bind_input.h"

#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_scancode.h>

extern "C" void Pc_ConsoleExec(const char* line);
extern int g_PcAllowDebugControls;

namespace sh::input {
namespace {

static_assert(SDL_SCANCODE_RETURN == 40 && SDL_SCANCODE_BACKSPACE == 42 && SDL_SCANCODE_GRAVE == 53 &&
              SDL_SCANCODE_KP_ENTER == 88 && SDL_SCANCODE_LCTRL == 224 && SDL_SCANCODE_RGUI == 231,
              "console_binds.cpp numbers keyboard positions as SDL does");

class GameConsoleBindHost final : public ConsoleBindHost {
public:
    int scancode_of(std::string_view key_cap) const override { return position_of_key_cap(key_cap); }

    int console_scancode() const override {
        return static_cast<int>(SDL_GetScancodeFromName(g_PcConfig.keyConsole[0] != '\0' ? g_PcConfig.keyConsole : "`"));
    }

    bool commands_allowed() const override { return g_PcAllowDebugControls != 0; }

    void print(std::string_view line) override { sh::debug::console_print(std::string(line).c_str()); }

    void run(std::string_view command) override { Pc_ConsoleExec(std::string(command).c_str()); }

    void save(std::span<const std::string> lines) override {
        std::vector<const char*> raw;
        raw.reserve(lines.size());
        for (const std::string& l : lines) {
            raw.push_back(l.c_str());
        }
        PcConfig_SaveBindLines(raw.data(), static_cast<int>(raw.size()));
    }

    void rejected_config_line(std::string_view key, std::string_view commands) override {
        SH_DBG("[BIND] config line rejected: bind %.*s %.*s", static_cast<int>(key.size()), key.data(),
               static_cast<int>(commands.size()), commands.data());
    }
};

}  // namespace

ConsoleBinds& console_binds() {
    static GameConsoleBindHost host;
    static ConsoleBinds binds(host);
    return binds;
}

}  // namespace sh::input
