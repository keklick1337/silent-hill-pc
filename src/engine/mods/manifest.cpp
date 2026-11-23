#include "engine/mods/manifest.h"

#include "formats/common/json.h"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <system_error>
#include <utility>

namespace sh::mods {

namespace {

constexpr std::string_view kUtf8Bom = "\xEF\xBB\xBF";
constexpr std::string_view kBlanks = " \t\r\n\f\v";

std::string_view trim(std::string_view text) {
    const std::size_t first = text.find_first_not_of(kBlanks);
    if (first == std::string_view::npos) {
        return {};
    }
    return text.substr(first, text.find_last_not_of(kBlanks) - first + 1);
}

std::string at(const json::Value& v) {
    return " (line " + std::to_string(v.line()) + ")";
}

// A relative path inside the mod folder: no root, no drive, no "..".
bool inside_path(std::string_view path) {
    if (path.empty() || path.front() == '/' || path.front() == '\\' || path.find(':') != std::string_view::npos) {
        return false;
    }
    std::size_t start = 0;
    while (start <= path.size()) {
        const std::size_t end = path.find_first_of("/\\", start);
        const std::string_view part = path.substr(start, end == std::string_view::npos ? path.npos : end - start);
        if (part == "..") {
            return false;
        }
        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1;
    }
    return true;
}

class JsonReader {
public:
    JsonReader(std::string_view folder, ManifestResult& out) : file_(std::string(folder) + "/mod.json"), out_(out) {}

    void error(const std::string& message) { out_.errors.push_back(file_ + ": " + message); }
    void warn(const std::string& message) { out_.warnings.push_back(file_ + ": " + message); }

    bool string(const json::Value& v, std::string_view key, std::string& dst) {
        if (!v.is_string()) {
            error(std::string(key) + " must be a string" + at(v));
            return false;
        }
        dst = v.string();
        return true;
    }

    std::optional<VersionRange> range(const json::Value& v, const std::string& what) {
        if (!v.is_string()) {
            error(what + " must be a version range string such as \">=1.0\"" + at(v));
            return std::nullopt;
        }
        Result<VersionRange> r = VersionRange::parse(v.string());
        if (!r) {
            error(what + ": " + r.error() + at(v));
            return std::nullopt;
        }
        return r.value();
    }

    // An engine version is written in full, so "1.2" is not read as a range by one mod and as 1.2.0 by another.
    std::optional<Version> engine_version(const json::Value& v, std::string_view key) {
        const std::string expects = std::string(key) + " must be an engine version MAJOR.MINOR.PATCH such as \"1.2.0\"";
        if (!v.is_string()) {
            error(expects + at(v));
            return std::nullopt;
        }
        const std::string_view text = trim(v.string());
        Result<Version> parsed = Version::parse(text);
        if (!parsed || parsed.value().to_string() != text) {
            error(expects + ", found \"" + v.string() + "\"" + at(v));
            return std::nullopt;
        }
        return parsed.value();
    }

    void string_list(const json::Value& v, std::string_view key, std::vector<std::string>& dst) {
        if (v.is_string()) {
            dst.push_back(v.string());
            return;
        }
        if (!v.is_array()) {
            error(std::string(key) + " must be a list of strings" + at(v));
            return;
        }
        for (const json::Value& item : v.array()) {
            if (item.is_string()) {
                dst.push_back(item.string());
            } else {
                error(std::string(key) + " must hold strings only" + at(item));
            }
        }
    }

    // {"id": "range", ...} or ["id", ...] (any version).
    void refs(const json::Value& v, std::string_view key, std::vector<ModRef>& dst) {
        const auto add = [&](const std::string& id, const json::Value* range_value, const json::Value& where) {
            if (!valid_mod_id(id)) {
                error(std::string(key) + ": '" + id + "' is not a mod id" + at(where));
                return;
            }
            ModRef ref{id, VersionRange::any()};
            if (range_value != nullptr) {
                std::optional<VersionRange> r = range(*range_value, std::string(key) + "." + id);
                if (!r) {
                    return;
                }
                ref.versions = std::move(*r);
            }
            dst.push_back(std::move(ref));
        };
        if (v.is_object()) {
            for (const json::Value::Member& m : v.members()) {
                add(m.first, &m.second, m.second);
            }
        } else if (v.is_array()) {
            for (const json::Value& item : v.array()) {
                if (item.is_string()) {
                    add(item.string(), nullptr, item);
                } else {
                    error(std::string(key) + " must hold mod ids" + at(item));
                }
            }
        } else {
            error(std::string(key) + " must be an object {\"mod_id\": \"version range\"}" + at(v));
        }
    }

    void boolean(const json::Value& v, std::string_view key, bool& dst) {
        if (v.is_bool()) {
            dst = v.boolean();
        } else {
            error(std::string(key) + " must be true or false" + at(v));
        }
    }

    void image(const json::Value& v, std::string_view key, std::string& dst) {
        if (string(v, key, dst) && !inside_path(dst)) {
            warn(std::string(key) + " '" + dst + "' must be a path inside the mod folder, ignored");
            dst.clear();
        }
    }

private:
    std::string file_;
    ManifestResult& out_;
};

}  // namespace

bool valid_mod_id(std::string_view id) {
    if (id.empty() || id.size() > 64 || id.front() == '.' || id.front() == '-' || id == "silenthill") {
        return false;
    }
    for (const char c : id) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
                        c == '-' || c == '.';
        if (!ok) {
            return false;
        }
    }
    return true;
}

ManifestResult parse_manifest_json(std::string_view text, std::string_view folder) {
    ManifestResult out;
    JsonReader r(folder, out);
    Result<json::Value> doc = json::parse(text);
    if (!doc) {
        r.error(doc.error());
        return out;
    }
    const json::Value& root = doc.value();
    if (!root.is_object()) {
        r.error("the manifest must be a JSON object");
        return out;
    }

    Manifest m;
    bool have_id = false;
    bool have_version = false;
    for (const json::Value::Member& member : root.members()) {
        const std::string& key = member.first;
        const json::Value& v = member.second;
        if (key == "format") {
            if (!v.is_integer() || v.integer() < 1) {
                r.error("format must be a positive integer" + at(v));
            } else if (v.integer() > kManifestFormat) {
                r.warn("format " + std::to_string(v.integer()) + " is newer than this engine reads (" +
                       std::to_string(kManifestFormat) + "); unknown keys are ignored");
            }
        } else if (key == "id") {
            if (r.string(v, key, m.id)) {
                if (!valid_mod_id(m.id)) {
                    r.error("id '" + m.id + "' is not valid (letters, digits, '_', '-', '.'; not 'silenthill')" +
                            at(v));
                } else {
                    have_id = true;
                }
            }
        } else if (key == "name") {
            r.string(v, key, m.name);
        } else if (key == "version") {
            if (r.string(v, key, m.version_text)) {
                Result<Version> ver = Version::parse(m.version_text);
                if (!ver) {
                    r.error("version: " + ver.error() + at(v));
                } else {
                    m.version = ver.value();
                    have_version = true;
                    // "1" and "1.2" are read as 1.0.0 and 1.2.0; dependents compare the full form.
                }
            }
        } else if (key == "description") {
            r.string(v, key, m.description);
        } else if (key == "authors" || key == "author") {
            r.string_list(v, key, m.authors);
        } else if (key == "homepage") {
            r.string(v, key, m.homepage);
        } else if (key == "min_engine_version") {
            m.min_engine_version = r.engine_version(v, key);
        } else if (key == "max_engine_version") {
            m.max_engine_version = r.engine_version(v, key);
        } else if (key == "engine") {
            m.engine = r.range(v, "engine");
        } else if (key == "api") {
            m.api = r.range(v, "api");
        } else if (key == "game") {
            r.string_list(v, key, m.games);
        } else if (key == "dependencies") {
            r.refs(v, key, m.dependencies);
        } else if (key == "optional_dependencies") {
            r.refs(v, key, m.optional_dependencies);
        } else if (key == "conflicts") {
            r.refs(v, key, m.conflicts);
        } else if (key == "load_after") {
            r.string_list(v, key, m.load_after);
        } else if (key == "load_before") {
            r.string_list(v, key, m.load_before);
        } else if (key == "priority") {
            if (!v.is_integer() || v.integer() < -1000000 || v.integer() > 1000000) {
                r.error("priority must be an integer between -1000000 and 1000000" + at(v));
            } else {
                m.priority = static_cast<int>(v.integer());
            }
        } else if (key == "gameplay") {
            r.boolean(v, key, m.gameplay);
        } else if (key == "scripts") {
            r.boolean(v, key, m.scripts);
        } else if (key == "requires_restart") {
            r.boolean(v, key, m.requires_restart);
        } else if (key == "replace") {
            r.string_list(v, key, m.replace);
            for (std::string& path : m.replace) {
                std::replace(path.begin(), path.end(), '\\', '/');
                while (path.starts_with("./") || path.starts_with('/')) {
                    path.erase(0, path.starts_with('/') ? 1 : 2);
                }
            }
            std::erase_if(m.replace, [](const std::string& p) { return p.empty(); });
        } else if (key == "icon") {
            r.image(v, key, m.icon);
        } else if (key == "preview") {
            r.image(v, key, m.preview);
        } else if (key.starts_with('_') || key == "$schema") {
            // Comments and editor hints.
        } else {
            r.warn("unknown key '" + key + "' ignored" + at(v));
        }
    }
    if (!m.requires_restart && (m.scripts || m.gameplay || !m.replace.empty())) {
        r.warn("\"requires_restart\": false ignored: scripts and gameplay rules load only at start");
        m.requires_restart = true;
    }
    if (m.min_engine_version && m.max_engine_version && *m.max_engine_version < *m.min_engine_version) {
        r.error("max_engine_version " + m.max_engine_version->to_string() + " is older than min_engine_version " +
                m.min_engine_version->to_string() + ": no engine can run the mod");
    }
    if (!have_id && out.errors.empty()) {
        r.error("\"id\" is missing");
    }
    if (!have_version && out.errors.empty()) {
        r.error("\"version\" is missing (for example \"1.0.0\")");
    }
    if (have_id && m.id != folder) {
        r.warn("the folder is named '" + std::string(folder) + "' but the mod id is '" + m.id +
               "'; ids of this mod are " + m.id + ":Name");
    }
    for (const ModRef& dep : m.dependencies) {
        if (dep.id == m.id) {
            r.error("the mod depends on itself");
        }
    }
    if (m.name.empty()) {
        m.name = m.id;
    }
    if (out.errors.empty()) {
        out.manifest = std::move(m);
    }
    return out;
}

ManifestResult parse_legacy_cfg(std::string_view text, std::string_view folder) {
    ManifestResult out;
    const std::string file = std::string(folder) + "/mod.cfg";
    out.warnings.push_back(file + ": old manifest format; write mod.json instead");

    Manifest m;
    m.legacy = true;
    int number = 0;
    std::string_view rest = text;
    if (rest.starts_with(kUtf8Bom)) {
        rest.remove_prefix(kUtf8Bom.size());
    }
    while (!rest.empty()) {
        ++number;
        const std::size_t nl = rest.find('\n');
        std::string_view line = rest.substr(0, nl);
        rest = nl == std::string_view::npos ? std::string_view() : rest.substr(nl + 1);
        const auto warn = [&](const std::string& message) {
            out.warnings.push_back(file + ":" + std::to_string(number) + ": " + message);
        };
        if (const std::size_t hash = line.find('#'); hash != std::string_view::npos) {
            line = line.substr(0, hash);
        }
        line = trim(line);
        if (line.empty()) {
            continue;
        }
        const std::size_t eq = line.find('=');
        if (eq == std::string_view::npos || trim(line.substr(0, eq)).empty()) {
            warn("expected 'key = value', line ignored");
            continue;
        }
        const std::string_view key = trim(line.substr(0, eq));
        const std::string value(trim(line.substr(eq + 1)));
        if (key == "id") {
            m.id = value;
        } else if (key == "name") {
            m.name = value;
        } else if (key == "version") {
            m.version_text = value;
        } else if (key == "priority") {
            int p = 0;
            const auto [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), p);
            if (ec != std::errc() || ptr != value.data() + value.size()) {
                warn("invalid priority '" + value + "', using 0");
                p = 0;
            }
            m.priority = p;
        } else if (key == "gameplay") {
            if (value != "0" && value != "1") {
                warn("invalid gameplay '" + value + "', using 0");
            }
            m.gameplay = value == "1";
        } else {
            warn("unknown key '" + std::string(key) + "', ignored");
        }
    }
    if (m.id.empty()) {
        m.id = std::string(folder);
    }
    if (!valid_mod_id(m.id)) {
        out.errors.push_back(file + ": id '" + m.id + "' is not valid (letters, digits, '_', '-', '.')");
        return out;
    }
    if (!m.version_text.empty()) {
        Result<Version> v = Version::parse(m.version_text);
        if (v) {
            m.version = v.value();
        } else {
            out.warnings.push_back(file + ": version '" + m.version_text + "' is not MAJOR.MINOR.PATCH, using 0.0.0");
        }
    }
    if (m.name.empty()) {
        m.name = m.id;
    }
    out.manifest = std::move(m);
    return out;
}

std::string write_manifest_json(const Manifest& m) {
    json::Writer w(true);
    w.begin_object();
    w.member("format", kManifestFormat);
    w.member("id", m.id);
    w.member("name", m.name);
    w.member("version", m.version.to_string());
    if (!m.description.empty()) {
        w.member("description", m.description);
    }
    w.key("authors").inline_next().begin_array();
    for (const std::string& a : m.authors) {
        w.value(a);
    }
    w.end_array();
    if (m.min_engine_version) {
        w.member("min_engine_version", m.min_engine_version->to_string());
    }
    if (m.max_engine_version) {
        w.member("max_engine_version", m.max_engine_version->to_string());
    }
    if (m.engine) {
        w.member("engine", m.engine->text());
    }
    if (m.api) {
        w.member("api", m.api->text());
    }
    const auto refs = [&](std::string_view key, const std::vector<ModRef>& list) {
        if (list.empty()) {
            return;
        }
        w.key(key).begin_object();
        for (const ModRef& r : list) {
            w.member(r.id, r.versions.text());
        }
        w.end_object();
    };
    refs("dependencies", m.dependencies);
    refs("optional_dependencies", m.optional_dependencies);
    refs("conflicts", m.conflicts);
    w.member("priority", m.priority);
    w.member("gameplay", m.gameplay);
    w.member("scripts", m.scripts);
    if (!m.requires_restart) {
        w.member("requires_restart", false);
    }
    if (!m.replace.empty()) {
        w.key("replace").begin_array();
        for (const std::string& p : m.replace) {
            w.value(p);
        }
        w.end_array();
    }
    w.end_object();
    return w.str() + "\n";
}

}  // namespace sh::mods
