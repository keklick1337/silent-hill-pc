// The active mods as a plain list, for the loaders that patch their data mod by mod (items, characters, flags,
// sounds, menus...). The rules live in registry.h / load_order.h is the modder's guide.
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sh::mods {

struct ModInfo {
    std::string id;       // the mod's namespace (mod.json "id"): its new things are "<id>:Name"
    std::string name;
    std::string version;  // MAJOR.MINOR.PATCH
    int priority = 0;
    int load_index = 0;   // 0 loads first; a later mod wins
    bool gameplay = false;  // GAMEPLAY CHANGE: rules, balance or story, not only looks
    bool scripts = false;
    std::filesystem::path root;  // the folder; it mirrors gamedata/ paths (root/items/HealthDrink.json)
    std::vector<std::string> replace;  // mod.json "replace": layered Lua files replaced whole
};

// A layered Lua file (maps/<map>/events.lua, hooks.lua, world_objects.lua, chara/<Name>/*.lua): the base file
// runs in the script state's own globals, then every other active mod's copy runs as an extension in its own
// environment (script_host.h). A mod that lists the path in mod.json "replace" takes the base's place.
struct LuaLayer {
    ModInfo mod;
    std::filesystem::path file;
};
struct LuaLayers {
    std::filesystem::path base;             // empty: no base file (or a replacing mod without a copy)
    std::string base_owner = "silenthill";  // the replacing mod's id
    std::vector<LuaLayer> extensions;       // load order
    bool empty() const { return base.empty() && extensions.empty(); }
};
// `base_dir` is gamedata/; Lua only from mods that may run scripts (warnings as find_in).
LuaLayers lua_layers(const std::filesystem::path& mods_dir, const std::filesystem::path& base_dir,
                     std::string_view relative, std::vector<std::string>& warnings);

// The active mods of `mods_dir` in load order: the first loads first, the last has the last word.
// For a folder of the game's registry (<exe>/mods, mods_dir) this is the running set, resolved once at start,
// and `warnings` stays empty (the registry logged them). Any other folder is read and resolved on each call on
// its own (tests, tools), with its problems appended to `warnings`.
std::vector<ModInfo> discover(const std::filesystem::path& mods_dir, std::vector<std::string>& warnings);

// The file of the highest active mod that has `relative` (a gamedata path), or `base / relative` when none does.
std::filesystem::path resolve(std::string_view relative, const std::filesystem::path& base);
// Only the mod file; nothing when no active mod has one.
std::optional<std::filesystem::path> find_mod_file(std::string_view relative);

// The one rule for a mod's Lua: it runs only when mod.json says "scripts": true. Every loader of Lua from mods
// asks this (the registry's index already leaves such files out, so find() never answers with one).
bool is_lua_path(std::string_view relative);
// False when `mod` may not run Lua. The first time a mod whose folder holds .lua files is refused in this process,
// the warning "mod X has Lua files but mod.json does not say "scripts": true; its scripts are not run" is
// appended to `warnings`; later calls (from any loader) stay quiet, so the log carries it once per mod.
bool scripts_allowed(const ModInfo& mod, std::vector<std::string>& warnings);
bool scripts_allowed(std::string_view id, const std::filesystem::path& root, bool scripts,
                     std::vector<std::string>& warnings);
std::string scripts_refused(std::string_view id);  // that warning's text
// The highest active mod of `mods_dir` that has `relative` (Lua only from mods that may run scripts): the
// registry's answer for the game's folders, a discover() of `mods_dir` otherwise (tests, tools).
std::optional<std::filesystem::path> find_in(const std::filesystem::path& mods_dir, std::string_view relative,
                                             std::vector<std::string>& warnings);

}  // namespace sh::mods
