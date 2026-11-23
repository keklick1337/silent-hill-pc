// The player-facing edits of the mod registry, shared by sh.mods.* (the Mods screen) and the `mods` console
// command: which Registry they act on, and the hot reload of what can change without a restart.
#pragma once

#include "engine/mods/registry.h"

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace sh::mods {

// The registry sh.mods.* and `mods` edit: registry() unless a test pointed them at its own (nullptr = back).
Registry& admin_registry();
void set_admin_registry(Registry* registry);

// A subsystem that can reread mod files in place (text, textures) returns one line saying what it did.
// The game registers them at start; a name registered again replaces the earlier one.
using Reloader = std::function<std::string()>;
void add_reloader(std::string name, Reloader fn);
void clear_reloaders();

// Hot reload: Registry::reindex() (files added to or removed from active mods), then every reloader.
// The set of mods and their order do not change; that needs a restart. Returns the lines to show.
std::vector<std::string> hot_reload(Registry& registry);

// "enable", "disable", "up", "down": changes the pending choice and saves load_order.json; a change of mods
// with "requires_restart": false applies at once (Registry::apply_live). Returns the line to show, or why it failed.
Result<std::string> edit_and_save(Registry& registry, std::string_view id, std::string_view op);

// Whether the Mods screen may change mods now. The game allows it only in Options opened from the title: a
// running session keeps the set it started with. No gate set = allowed (tests, tools).
void set_edit_gate(std::function<bool()> gate);
bool editable();

// Restarts the game so a new set of mods loads (sh.mods.restart()). On success the handler does not return;
// without one (tests, tools) it fails.
void set_restart_handler(std::function<Result<void>()> handler);
Result<void> request_restart();

}  // namespace sh::mods
