#include "engine/mods/data_merge.h"

#include "engine/mods/registry.h"

#include <cctype>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <mutex>
#include <optional>
#include <system_error>
#include <utility>

namespace sh::mods {

namespace fs = std::filesystem;
using json::Value;

namespace {

std::string lower(std::string_view text) {
    std::string out(text);
    for (char& c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

std::string normal(std::string_view relative) {
    std::string out = lower(relative);
    for (char& c : out) {
        if (c == '\\') {
            c = '/';
        }
    }
    while (out.starts_with("./")) {
        out.erase(0, 2);
    }
    return out;
}

// One path segment against a pattern segment with at most one '*' ("*", "*.json", "map*").
bool segment_matches(std::string_view p, std::string_view q) {
    const std::size_t star = p.find('*');
    if (star == std::string_view::npos) {
        return p == q;
    }
    const std::string_view head = p.substr(0, star);
    const std::string_view tail = p.substr(star + 1);
    return q.size() >= head.size() + tail.size() && q.starts_with(head) && q.ends_with(tail);
}

// '*' never crosses a '/'.
bool matches(std::string_view pattern, std::string_view path) {
    while (!pattern.empty()) {
        const std::size_t ps = pattern.find('/');
        const std::size_t qs = path.find('/');
        const std::string_view p = pattern.substr(0, ps);
        const std::string_view q = path.substr(0, qs);
        if (!segment_matches(p, q)) {
            return false;
        }
        if ((ps == std::string_view::npos) != (qs == std::string_view::npos)) {
            return false;
        }
        if (ps == std::string_view::npos) {
            return true;
        }
        pattern.remove_prefix(ps + 1);
        path.remove_prefix(qs + 1);
    }
    return path.empty();
}

struct FileRules {
    const char* pattern;
    MergeRules rules;
};

MergeRules keyed(std::vector<std::pair<std::string, std::string>> keys) {
    MergeRules r;
    r.array_keys = std::move(keys);
    return r;
}

// Arrays whose entries carry neither "id" nor "name", or whose name is not their identity. Every other file
// merges with the default candidates has the same table for modders.
const std::vector<FileRules>& file_rules() {
    static const std::vector<FileRules> rules = {
        {"maps/*/world_objects.json", keyed({{"objects", "name"}})},
        {"maps/*/handlers.json", keyed({{"handlers", "index"}})},
        {"maps/*/tables.json", keyed({{"symbols", "symbol"}, {"functions", "symbol"}, {"variables", "symbol"}})},
        {"maps/*/footsteps.json", keyed({{"places", "name"}})},
        {"maps/*/lights.json", keyed({{"lights", "name"}})},
        // Entries without "id" are appended: a mod adds its conditional spawns without restating the base ones.
        {"maps/*/spawn_overrides.json", keyed({{"spawn_overrides", "id"}, {"spawn_variants.variants", "name"}})},
        {"maps/*/map.json", keyed({{"points", "index"},
                                   {"cameras", "index"},
                                   {"collision_triggers", "index"},
                                   {"spawns", "group,slot"},
                                   {"entities", "id"},
                                   {"spawn_overrides", "id"},
                                   {"spawn_variants.variants", "name"}})},
        {"maps/*/points.json", keyed({{"points", "index"}})},
        {"maps/*/events.json", keyed({{"events", "index"}})},
        {"maps/*/cameras.json", keyed({{"cameras", "index"}})},
        {"maps/*/collision.json", keyed({{"triggers", "index"}})},
        // Entries are in status order; "index" is their position, so a patch names the entries it changes.
        {"tables/anim/*.json", keyed({{"entries", "index"}})},
        {"config/difficulties.json", keyed({{"difficulties", "label"}})},
        {"config/map_sound_stops.json", keyed({{"stops", "map"}})},
        {"config/main_frame.json", keyed({{"opaque_depth_rooms", "map"}})},
        {"config/pickups.json", keyed({{"pickups", "item"}})},
        // A map's number is its place in the list: a mod's new map goes to the end (or $after / $before).
        {"game.json", keyed({{"maps", "name"}})},
    };
    return rules;
}

std::optional<std::string> read_text(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

std::mutex& trace_mutex() {
    static std::mutex m;
    return m;
}

std::function<void(const std::string&)>& trace_sink() {
    static std::function<void(const std::string&)> sink = [](const std::string& line) {
        constexpr std::string_view kWarning = "warning: ";
        if (std::string_view(line).starts_with(kWarning)) {
            std::fprintf(stderr, "warning: [MODS] %s\n", line.c_str() + kWarning.size());
        } else {
            std::fprintf(stderr, "[MODS] %s\n", line.c_str());
        }
    };
    return sink;
}

}  // namespace

std::string MergedFile::source() const {
    if (!merged()) {
        return path.string();
    }
    std::string out = path.string() + " (merged: " + (base ? "base + " : "") + "mods ";
    for (std::size_t i = 0; i < mods.size(); ++i) {
        out += (i ? ", " : "") + mods[i].mod_id;
    }
    return out + ")";
}

const MergeRules& rules_for(std::string_view relative) {
    static const MergeRules defaults;
    const std::string path = normal(relative);
    for (const FileRules& r : file_rules()) {
        if (matches(r.pattern, path)) {
            return r.rules;
        }
    }
    return defaults;
}

bool replace_listed(const fs::path& root, std::string_view relative) {
    // The manifest's "replace" list (Manifest::replace), read from the file because layers also come from folders
    // outside the registry (tests, tools). load_order.cpp already switched off the later of two mods listing a path.
    const std::optional<std::string> text = read_text(root / "mod.json");
    if (!text) {
        return false;
    }
    const Result<Value> doc = json::parse(*text);
    if (!doc) {
        return false;
    }
    const Value* list = doc.value().find("replace");
    if (list == nullptr || !list->is_array()) {
        return false;
    }
    const std::string wanted = normal(relative);
    for (const Value& v : list->array()) {
        if (v.is_string() && normal(v.string()) == wanted) {
            return true;
        }
    }
    return false;
}

std::vector<DataLayer> layers_of(const std::vector<ModInfo>& mods, std::string_view relative) {
    std::vector<DataLayer> out;
    std::error_code ec;
    for (const ModInfo& m : mods) {
        const fs::path file = m.root / fs::path(std::string(relative));
        if (fs::is_regular_file(file, ec)) {
            out.push_back({m.id, file, m.root, m.gameplay});
        }
    }
    return out;
}

Result<MergedFile> merge_layers(std::string_view relative, const fs::path& base_file,
                                const std::vector<DataLayer>& layers, std::vector<std::string>& warnings) {
    MergedFile out;
    std::error_code ec;
    const bool have_base = !base_file.empty() && fs::is_regular_file(base_file, ec);
    const std::string rel(relative);

    if (!have_base && layers.empty()) {
        return Result<MergedFile>::fail("no file " + (base_file.empty() ? rel : base_file.string()));
    }
    if (layers.empty() || (!have_base && layers.size() == 1)) {
        // One file: its bytes as they are (a run without mods reads what it always read).
        const fs::path file = have_base ? base_file : layers.front().file;
        std::optional<std::string> text = read_text(file);
        if (!text) {
            return Result<MergedFile>::fail("cannot read " + file.string());
        }
        out.text = std::move(*text);
        out.path = file;
        out.base = have_base;
        if (!have_base) {
            out.mods.push_back(layers.front());
            out.trace.push_back("mod " + layers.front().mod_id + ": " + rel + ": added (no base file)");
        }
        return Result<MergedFile>::ok(std::move(out));
    }

    const MergeRules& rules = rules_for(relative);
    std::optional<Value> doc;
    std::string replaced_by;
    if (have_base) {
        std::optional<std::string> text = read_text(base_file);
        if (!text) {
            return Result<MergedFile>::fail("cannot read " + base_file.string());
        }
        Result<Value> parsed = json::parse(*text);
        if (parsed) {
            doc = std::move(parsed.value());
            out.base = true;
            out.path = base_file;
        } else {
            // The mods' copies still apply (a mod may be the fix); the first one stands in for the base.
            warnings.push_back(base_file.string() + ": " + parsed.error() + "; the mods' copies are used without it");
        }
    }

    for (const DataLayer& layer : layers) {
        const std::string who = "mod " + layer.mod_id + ": " + rel;
        std::optional<std::string> text = read_text(layer.file);
        if (!text) {
            warnings.push_back(who + ": cannot read " + layer.file.string() + "; skipped");
            continue;
        }
        Result<Value> parsed = json::parse(*text);
        if (!parsed) {
            warnings.push_back(who + ": " + parsed.error() + "; skipped");
            continue;
        }
        const Value& patch = parsed.value();
        if (!doc) {
            doc = strip_directives(patch);
            out.mods.push_back(layer);
            out.path = layer.file;
            out.trace.push_back(who + ": added (no base file)");
            continue;
        }
        if (wants_replace(patch)) {
            if (replace_listed(layer.root, relative)) {
                if (!replaced_by.empty()) {
                    warnings.push_back("conflict: mods " + replaced_by + " and " + layer.mod_id + " both replace " +
                                       rel + "; " + layer.mod_id + " (later in the load order) wins");
                }
                replaced_by = layer.mod_id;
                doc = strip_directives(patch);
                out.base = false;
                out.mods.clear();
                out.mods.push_back(layer);
                out.path = layer.file;
                out.trace.push_back(who + ": replaced whole (\"$replace\", listed in mod.json \"replace\")");
                continue;
            }
            warnings.push_back(who + ": \"$replace\" ignored: the path is not in mod.json \"replace\"; merged as a "
                                     "patch");
        }
        MergeStats stats;
        std::vector<std::string> merge_warnings;
        merge_patch(*doc, patch, rules, stats, merge_warnings);
        for (const std::string& w : merge_warnings) {
            warnings.push_back(who + ": " + w);
        }
        out.mods.push_back(layer);
        out.path = layer.file;
        out.trace.push_back(who + ": " + stats.describe());
    }

    if (!doc) {
        return Result<MergedFile>::fail("no readable file for " + rel);
    }
    if (!out.merged()) {
        // One copy is left (the rest were skipped or replaced): its own bytes, unless it carries directives.
        if (std::optional<std::string> text = read_text(out.path)) {
            const Result<Value> raw = json::parse(*text);
            if (raw && raw.value() == *doc) {
                out.text = std::move(*text);
                return Result<MergedFile>::ok(std::move(out));
            }
        }
    }
    out.text = json::write(*doc, false);
    out.text += '\n';
    return Result<MergedFile>::ok(std::move(out));
}

Result<MergedFile> read_data_file(const fs::path& gamedata_dir, const fs::path& mods_dir, const fs::path& relative,
                                  std::vector<std::string>& warnings) {
    std::vector<DataLayer> layers;
    std::error_code ec;
    if (!mods_dir.empty() && fs::is_directory(mods_dir, ec)) {
        std::vector<std::string> mod_warnings;
        const std::vector<ModInfo> found = discover(mods_dir, mod_warnings);
        for (const std::string& w : mod_warnings) {
            warnings.push_back("mods: " + w);
        }
        layers = layers_of(found, relative.generic_string());
    }
    Result<MergedFile> merged = merge_layers(relative.generic_string(), gamedata_dir / relative, layers, warnings);
    if (merged) {
        emit_merge_trace(merged.value());
    }
    return merged;
}

Result<std::string> read_data_text(const fs::path& gamedata_dir, const fs::path& mods_dir, const fs::path& relative,
                                   std::string* source) {
    std::vector<std::string> warnings;
    Result<MergedFile> merged = read_data_file(gamedata_dir, mods_dir, relative, warnings);
    emit_merge_warnings(warnings);
    if (!merged) {
        if (source != nullptr) {
            *source = (gamedata_dir / relative).string();
        }
        return Result<std::string>::fail(merged.error().starts_with("no file")
                                             ? "cannot read " + (gamedata_dir / relative).string()
                                             : merged.error());
    }
    if (source != nullptr) {
        *source = merged.value().source();
    }
    return Result<std::string>::ok(std::move(merged.value().text));
}

Result<MergedFile> read_registry_data_file(std::string_view relative, const fs::path& base_dir,
                                           std::vector<std::string>& warnings) {
    Registry& reg = registry();
    std::vector<DataLayer> layers;
    const std::vector<fs::path> files = reg.find_all(relative);
    if (!files.empty()) {
        const std::vector<const ResolvedMod*> active = reg.active();
        for (const fs::path& file : files) {
            const std::string f = file.generic_string();
            for (const ResolvedMod* m : active) {
                const std::string root = m->candidate.root.generic_string();
                if (f.size() > root.size() && f.compare(0, root.size(), root) == 0 &&
                    (f[root.size()] == '/' || root.ends_with('/'))) {
                    layers.push_back({m->candidate.manifest.id, file, m->candidate.root, m->candidate.manifest.gameplay});
                    break;
                }
            }
        }
    }
    const fs::path base = base_dir.empty() ? fs::path() : base_dir / fs::path(std::string(relative));
    Result<MergedFile> merged = merge_layers(relative, base, layers, warnings);
    if (merged) {
        emit_merge_trace(merged.value());
    }
    return merged;
}

void emit_merge_trace(const MergedFile& file) {
    std::lock_guard lock(trace_mutex());
    if (!trace_sink()) {
        return;
    }
    for (const std::string& line : file.trace) {
        trace_sink()(line);
    }
}

void emit_merge_warnings(const std::vector<std::string>& warnings) {
    std::lock_guard lock(trace_mutex());
    if (!trace_sink()) {
        return;
    }
    for (const std::string& line : warnings) {
        trace_sink()("warning: " + line);
    }
}

}  // namespace sh::mods
