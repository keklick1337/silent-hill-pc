#include "engine/mods/mods.h"

#include "engine/mods/registry.h"

#include <algorithm>
#include <fstream>
#include <mutex>
#include <set>
#include <sstream>
#include <system_error>
#include <utility>

namespace sh::mods {

namespace fs = std::filesystem;

namespace {

ModInfo info_of(const ResolvedMod& m) {
    const Manifest& man = m.candidate.manifest;
    ModInfo info;
    info.id = man.id;
    info.name = man.name;
    info.version = man.version.to_string();
    info.priority = man.priority;
    info.load_index = m.load_index;
    info.gameplay = man.gameplay;
    info.scripts = man.scripts;
    info.root = m.candidate.root;
    info.replace = man.replace;
    return info;
}

}  // namespace

std::vector<ModInfo> discover(const fs::path& mods_dir, std::vector<std::string>& warnings) {
    std::vector<ModInfo> out;
    Registry& reg = registry();
    if (reg.configured() && reg.covers(mods_dir)) {
        for (const ResolvedMod* m : reg.active()) {
            out.push_back(info_of(*m));
        }
        return out;
    }

    std::error_code ec;
    if (!fs::is_directory(mods_dir, ec)) {
        return out;
    }
    std::vector<Candidate> candidates = scan_mod_dirs({mods_dir}, warnings);
    UserState user;
    if (std::ifstream in(mods_dir / kUserStateFile, std::ios::binary); in) {
        std::ostringstream text;
        text << in.rdbuf();
        Result<UserState> parsed = parse_user_state(text.str());
        if (parsed) {
            user = std::move(parsed.value());
        } else {
            warnings.push_back(std::string(kUserStateFile) + ": " + parsed.error());
        }
    }
    const Plan plan = resolve_load_order(std::move(candidates), user, RegistryConfig{}.env);
    for (const ResolvedMod& m : plan.mods) {
        for (const std::string& w : m.warnings) {
            warnings.push_back(w);
        }
        if (m.state == ModState::Active) {
            out.push_back(info_of(m));
        } else if (m.state == ModState::Error) {
            std::string why;
            for (const std::string& p : m.problems) {
                why += (why.empty() ? "" : "; ") + p;
            }
            warnings.push_back(m.candidate.folder + ": mod disabled: " + why);
        }
    }
    return out;
}

fs::path resolve(std::string_view relative, const fs::path& base) {
    if (std::optional<fs::path> mod = registry().find(relative)) {
        return *mod;
    }
    return base / fs::path(relative);
}

std::optional<fs::path> find_mod_file(std::string_view relative) {
    return registry().find(relative);
}

bool is_lua_path(std::string_view relative) {
    if (relative.size() < 4) {
        return false;
    }
    const std::string_view ext = relative.substr(relative.size() - 4);
    return ext[0] == '.' && (ext[1] | 0x20) == 'l' && (ext[2] | 0x20) == 'u' && (ext[3] | 0x20) == 'a';
}

std::string scripts_refused(std::string_view id) {
    return "mod " + std::string(id) +
           " has Lua files but mod.json does not say \"scripts\": true; its scripts are not run";
}

bool scripts_allowed(std::string_view id, const fs::path& root, bool scripts, std::vector<std::string>& warnings) {
    if (scripts) {
        return true;
    }
    // Keyed by folder, not id: tests reuse ids across scratch folders, and the game has one folder per mod.
    static std::mutex mutex;
    static std::set<std::string> warned;
    std::error_code ec;
    const std::string key = fs::weakly_canonical(root, ec).generic_string();
    {
        std::lock_guard lock(mutex);
        if (warned.contains(key)) {
            return false;
        }
    }
    bool has_lua = false;
    for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
         !ec && it != end && !has_lua; it.increment(ec)) {
        std::error_code entry_ec;
        has_lua = it->is_regular_file(entry_ec) && is_lua_path(it->path().generic_string());
    }
    if (has_lua) {
        std::lock_guard lock(mutex);
        if (warned.insert(key).second) {
            warnings.push_back(scripts_refused(id));
        }
    }
    return false;
}

bool scripts_allowed(const ModInfo& mod, std::vector<std::string>& warnings) {
    return scripts_allowed(mod.id, mod.root, mod.scripts, warnings);
}

std::optional<fs::path> find_in(const fs::path& mods_dir, std::string_view relative,
                                std::vector<std::string>& warnings) {
    std::error_code ec;
    if (mods_dir.empty() || !fs::is_directory(mods_dir, ec)) {
        return std::nullopt;
    }
    Registry& reg = registry();
    if (reg.configured() && reg.covers(mods_dir)) {
        return reg.find(relative);
    }
    const bool lua = is_lua_path(relative);
    const std::vector<ModInfo> found = discover(mods_dir, warnings);
    for (auto it = found.rbegin(); it != found.rend(); ++it) {  // the last mod wins
        const fs::path file = it->root / fs::path(relative);
        if (fs::is_regular_file(file, ec) && (!lua || scripts_allowed(*it, warnings))) {
            return file;
        }
    }
    return std::nullopt;
}

namespace {

bool same_path(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        const auto fold = [](char c) {
            c = c == '\\' ? '/' : c;
            return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
        };
        if (fold(a[i]) != fold(b[i])) {
            return false;
        }
    }
    return true;
}

}  // namespace

LuaLayers lua_layers(const fs::path& mods_dir, const fs::path& base_dir, std::string_view relative,
                     std::vector<std::string>& warnings) {
    LuaLayers out;
    std::error_code ec;
    if (!base_dir.empty() && fs::is_regular_file(base_dir / fs::path(relative), ec)) {
        out.base = base_dir / fs::path(relative);
    }
    if (mods_dir.empty() || !fs::is_directory(mods_dir, ec)) {
        return out;
    }
    const std::vector<ModInfo> found = discover(mods_dir, warnings);
    // The registry's index is case-insensitive; a folder outside it is looked at as written.
    std::vector<fs::path> indexed;
    Registry& reg = registry();
    const bool covered = reg.configured() && reg.covers(mods_dir);
    if (covered) {
        indexed = reg.find_all(relative);
    }
    for (const ModInfo& mod : found) {
        std::optional<fs::path> file;
        if (covered) {
            for (const fs::path& p : indexed) {
                const fs::path rel = p.lexically_relative(mod.root);
                if (!rel.empty() && *rel.begin() != "..") {
                    file = p;
                    break;
                }
            }
        } else if (fs::is_regular_file(mod.root / fs::path(relative), ec) && scripts_allowed(mod, warnings)) {
            file = mod.root / fs::path(relative);
        }
        const bool replaces = std::any_of(mod.replace.begin(), mod.replace.end(),
                                          [&](const std::string& p) { return same_path(p, relative); });
        if (replaces && (file || mod.scripts)) {
            // The load order lets one mod replace a path; the later wins here too should two slip through.
            out.base = file ? *file : fs::path();
            out.base_owner = mod.id;
            continue;
        }
        if (file) {
            out.extensions.push_back(LuaLayer{mod, *file});
        }
    }
    return out;
}

}  // namespace sh::mods
