// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// sh.menu.*: the Lua face of the engine menu model (src/engine/menu/menu_model.h). Menus are
// defined by gamedata/menus/*.lua and by each mod's menus/*.lua.
#pragma once

#include <filesystem>
#include <functional>
#include <string>

namespace sh::menu {
class Registry;
}

namespace sh::script {

using MenuLogFn = std::function<void(int level, const std::string& message)>;  // level 0 info, 1 warning

// Runs every <gamedata_dir>/menus/*.lua, then every mod's menus/*.lua (mods in priority order, so a
// later define() of the same id overrides the base) in one sandboxed Lua state (base, table, string,
// math + sh.menu + sh.mods) and fills `registry`; the sh.menu.after_load functions run after the last
// file. A missing folder is normal; a script that errors is logged
// and skipped. The state lives as long as the process: the registered callbacks keep it alive, and
// `registry` must outlive them. `log` defaults to stderr.
void load_menu_scripts(const std::filesystem::path& gamedata_dir, const std::filesystem::path& mods_dir,
                       sh::menu::Registry& registry, MenuLogFn log = {});

// `mods reload`: clears the menus of `registry` (the natives stay) and runs the menu scripts again in a new state.
// The earlier states stay alive: an open menu may still call into them until it copies the new rows. Not from inside
// a menu callback (the script layer defers it to the next frame).
void reload_menu_scripts(const std::filesystem::path& gamedata_dir, const std::filesystem::path& mods_dir,
                         sh::menu::Registry& registry, MenuLogFn log = {});

}  // namespace sh::script
