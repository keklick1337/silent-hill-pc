#include "engine/mods/mods_admin.h"

#include "engine/mods/data_reload.h"

#include <utility>

namespace sh::mods {

namespace {

Registry* g_target = nullptr;

std::vector<std::pair<std::string, Reloader>>& reloaders() {
    static std::vector<std::pair<std::string, Reloader>> list;
    return list;
}

std::function<bool()>& edit_gate() {
    static std::function<bool()> gate;
    return gate;
}

std::function<Result<void>()>& restart_handler() {
    static std::function<Result<void>()> handler;
    return handler;
}

std::size_t indexed_files(const Registry& registry) {
    std::size_t n = 0;
    for (const ResolvedMod* m : registry.active()) {
        n += registry.files(m->candidate.manifest.id).size();
    }
    return n;
}

}  // namespace

Registry& admin_registry() { return g_target != nullptr ? *g_target : registry(); }

void set_admin_registry(Registry* registry) { g_target = registry; }

void add_reloader(std::string name, Reloader fn) {
    for (auto& [n, f] : reloaders()) {
        if (n == name) {
            f = std::move(fn);
            return;
        }
    }
    reloaders().emplace_back(std::move(name), std::move(fn));
}

void clear_reloaders() { reloaders().clear(); }

std::vector<std::string> hot_reload(Registry& registry) {
    std::vector<std::string> lines;
    const std::size_t before = indexed_files(registry);
    const Registry::FileStamps stamps_before = registry.file_stamps();
    registry.reindex();
    const std::size_t after = indexed_files(registry);
    ReloadChanges changes = classify_changes(stamps_before, registry.file_stamps());
    lines.push_back("files of the active mods indexed again: " + std::to_string(before) + " -> " +
                    std::to_string(after) + "; changed: " + std::to_string(changes.live.size()) + " in place, " +
                    std::to_string(changes.data.size()) + " data, " + std::to_string(changes.restart.size()) +
                    " after a restart");
    for (const std::string& f : changes.restart) {
        lines.push_back("changed " + f + ": read when the game starts, applies after a restart");
    }
    set_last_reload_changes(std::move(changes));
    for (const auto& [name, fn] : reloaders()) {
        if (fn) {
            lines.push_back(name + ": " + fn());
        }
    }
    lines.push_back(reloaders().empty() ? "text, menus, scripts, switching mods and the load order: after a restart"
                                        : "switching mods and the load order: after a restart");
    return lines;
}

Result<std::string> edit_and_save(Registry& registry, std::string_view id, std::string_view op) {
    const std::string name(id);
    if (registry.pending().find(id) == nullptr) {
        return Result<std::string>::fail("no mod '" + name + "' (see 'mods')");
    }
    bool changed = false;
    std::string what;
    if (op == "enable" || op == "disable") {
        changed = registry.set_enabled(id, op == "enable");
        what = op == "enable" ? "enabled" : "disabled";
    } else if (op == "up" || op == "down") {
        if (!registry.move(id, op == "up" ? -1 : 1)) {
            return Result<std::string>::ok(name + " is already " + (op == "up" ? "first" : "last"));
        }
        changed = true;
        what = op == "up" ? "moved up" : "moved down";
    } else {
        return Result<std::string>::fail("unknown edit '" + std::string(op) + "'");
    }
    if (!changed) {
        return Result<std::string>::fail("cannot change '" + name + "'");
    }
    if (Result<void> saved = registry.save(); !saved) {
        return Result<std::string>::fail(name + " " + what + " but not saved: " + saved.error());
    }
    if (registry.apply_live()) {
        return Result<std::string>::ok(name + " " + what + "; applied now");
    }
    if (!registry.restart_pending()) {
        return Result<std::string>::ok(name + " " + what);
    }
    return Result<std::string>::ok(name + " " + what + "; applies after restart");
}

void set_edit_gate(std::function<bool()> gate) { edit_gate() = std::move(gate); }

bool editable() { return !edit_gate() || edit_gate()(); }

void set_restart_handler(std::function<Result<void>()> handler) { restart_handler() = std::move(handler); }

Result<void> request_restart() {
    if (!restart_handler()) {
        return Result<void>::fail("this program cannot restart itself");
    }
    return restart_handler()();
}

}  // namespace sh::mods
