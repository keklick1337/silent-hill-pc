#include "engine/mods/registry.h"

#include "engine/mods/mods.h"

#include "formats/common/json.h"
#include "formats/convert/gamefiles/game_files.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <mutex>
#include <set>
#include <sstream>
#include <system_error>
#include <utility>

namespace sh::mods {

namespace fs = std::filesystem;

namespace {

std::string utf8(const fs::path& path) {
    const std::u8string text = path.generic_u8string();
    return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

std::string lower(std::string_view text) {
    std::string out(text);
    for (char& c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

// "./a\\b//c" -> "a/b/c": the form index keys and lookups share.
std::string normal(std::string_view relative) {
    std::string out;
    out.reserve(relative.size());
    for (const char c : relative) {
        const char ch = c == '\\' ? '/' : c;
        if (ch == '/' && (out.empty() || out.back() == '/')) {
            continue;
        }
        out += ch;
    }
    while (out.starts_with("./")) {
        out.erase(0, 2);
    }
    if (!out.empty() && out.back() == '/') {
        out.pop_back();
    }
    return out;
}

std::optional<std::string> read_text(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

// Files at the top of a mod folder that describe the mod rather than replace game data.
bool is_metadata(const Manifest& m, std::string_view relative) {
    if (relative.find('/') != std::string_view::npos) {
        return false;
    }
    if (relative == kManifestFile || relative == kLegacyManifestFile || relative == m.icon || relative == m.preview) {
        return true;
    }
    const std::string l = lower(relative);
    return l.starts_with("readme") || l.starts_with("license") || l.starts_with("licence") ||
           l.starts_with("changelog") || l.starts_with("credits") || l == "icon.png" || l == "preview.png";
}

std::string version_and_id(const ResolvedMod& m) {
    return m.candidate.manifest.id + " " + m.candidate.manifest.version.to_string();
}

// "x.wav" -> "x.opus": the other name a mod may give a sound file.
std::string alternative_sound(std::string_view wav) {
    return std::string(wav.substr(0, wav.size() - 4)) + ".opus";
}

// A file of the disc the import keeps: in raw/ of an older import, else in the game files store.
bool is_game_file(const fs::path& base_dir, const std::string& path) {
    std::error_code ec;
    if (fs::exists(base_dir / "raw" / fs::path(path), ec)) {
        return true;
    }
    const std::shared_ptr<const sh::convert::gamefiles::Store> store = sh::convert::gamefiles::store_for(base_dir);
    return store && store->find(path) != nullptr;
}

}  // namespace

Result<UserState> parse_user_state(std::string_view text) {
    Result<json::Value> doc = json::parse(text);
    if (!doc) {
        return Result<UserState>::fail(doc.error());
    }
    const json::Value& root = doc.value();
    if (!root.is_object()) {
        return Result<UserState>::fail("expected a JSON object");
    }
    UserState state;
    if (const json::Value* list = root.find("mods"); list != nullptr) {
        if (!list->is_array()) {
            return Result<UserState>::fail("\"mods\" must be a list");
        }
        for (const json::Value& item : list->array()) {
            const json::Value* id = item.find("id");
            if (id == nullptr || !id->is_string()) {
                return Result<UserState>::fail("line " + std::to_string(item.line()) + ": an entry without \"id\"");
            }
            UserEntry e{id->string(), true};
            if (const json::Value* on = item.find("enabled"); on != nullptr && on->is_bool()) {
                e.enabled = on->boolean();
            }
            if (state.find(e.id) == nullptr) {
                state.mods.push_back(std::move(e));
            }
        }
    }
    if (const json::Value* list = root.find("notices"); list != nullptr && list->is_array()) {
        for (const json::Value& item : list->array()) {
            if (item.is_string()) {
                state.notices.push_back(item.string());
            }
        }
    }
    return Result<UserState>::ok(std::move(state));
}

std::string write_user_state(const UserState& state) {
    json::Writer w(true);
    w.begin_object();
    w.member("format", 1);
    w.key("mods").begin_array();
    for (const UserEntry& e : state.mods) {
        w.inline_next().begin_object();
        w.member("id", e.id);
        w.member("enabled", e.enabled);
        w.end_object();
    }
    w.end_array();
    if (!state.notices.empty()) {
        w.key("notices").begin_array();
        for (const std::string& n : state.notices) {
            w.value(n);
        }
        w.end_array();
    }
    w.end_object();
    return w.str() + "\n";
}

std::vector<Candidate> scan_mod_dirs(const std::vector<fs::path>& dirs, std::vector<std::string>& warnings) {
    std::vector<Candidate> out;
    std::set<fs::path> seen_dirs;
    for (const fs::path& dir : dirs) {
        std::error_code ec;
        if (dir.empty() || !fs::is_directory(dir, ec)) {
            continue;
        }
        const fs::path real = fs::weakly_canonical(dir, ec);
        if (!seen_dirs.insert(ec ? dir : real).second) {
            continue;
        }
        std::vector<fs::path> folders;
        for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
            std::error_code entry_ec;
            if (it->is_directory(entry_ec) && !entry_ec) {
                folders.push_back(it->path());
            }
        }
        if (ec) {
            warnings.push_back("cannot read mods folder '" + utf8(dir) + "': " + ec.message());
        }
        // Directory order is unspecified; by name, "the first folder keeps a duplicate id" is predictable.
        std::sort(folders.begin(), folders.end(),
                  [](const fs::path& a, const fs::path& b) { return a.filename() < b.filename(); });
        for (const fs::path& folder : folders) {
            const std::string name = utf8(folder.filename());
            if (name.starts_with('.')) {
                continue;
            }
            if (name.ends_with(".disabled")) {
                // A way to switch a mod off kept for older installs; the Mods screen writes mods/load_order.json.
                warnings.push_back(name + ": folders ending in .disabled are skipped; rename it back and switch the "
                                          "mod off in Options > Mods instead");
                continue;
            }
            ManifestResult parsed;
            if (const std::optional<std::string> text = read_text(folder / kManifestFile)) {
                parsed = parse_manifest_json(*text, name);
            } else if (const std::optional<std::string> cfg = read_text(folder / kLegacyManifestFile)) {
                parsed = parse_legacy_cfg(*cfg, name);
            } else {
                warnings.push_back(name + ": no mod.json, folder skipped (" + utf8(folder) + ")");
                continue;
            }
            if (!parsed.manifest) {
                for (const std::string& w : parsed.warnings) {
                    warnings.push_back(w);
                }
                for (const std::string& e : parsed.errors) {
                    warnings.push_back(e + "; mod skipped");
                }
                continue;
            }
            Candidate c;
            c.manifest = std::move(*parsed.manifest);
            c.root = folder;
            c.folder = name;
            c.warnings = std::move(parsed.warnings);
            out.push_back(std::move(c));
        }
    }
    return out;
}

void Registry::configure(RegistryConfig config) {
    {
        std::unique_lock lock(mutex_);
        config_ = std::move(config);
        configured_ = true;
        overrides_.clear();
    }
    rescan();
}

bool Registry::configured() const {
    std::shared_lock lock(mutex_);
    return configured_;
}

bool Registry::covers(const fs::path& mods_dir) const {
    std::shared_lock lock(mutex_);
    std::error_code ec;
    const fs::path want = fs::weakly_canonical(mods_dir, ec);
    for (const fs::path& dir : config_.dirs) {
        std::error_code ec2;
        if (fs::weakly_canonical(dir, ec2) == want) {
            return true;
        }
    }
    return false;
}

fs::path Registry::state_path() const {
    if (!config_.state_file.empty()) {
        return config_.state_file;
    }
    return config_.dirs.empty() ? fs::path() : config_.dirs.front() / kUserStateFile;
}

void Registry::rescan() {
    std::unique_lock lock(mutex_);
    scan_warnings_.clear();
    candidates_ = scan_mod_dirs(config_.dirs, scan_warnings_);
    user_ = UserState{};
    if (const fs::path file = state_path(); !file.empty()) {
        if (const std::optional<std::string> text = read_text(file)) {
            Result<UserState> state = parse_user_state(*text);
            if (state) {
                user_ = std::move(state.value());
            } else {
                scan_warnings_.push_back(utf8(file) + ": " + state.error() + "; every mod is switched off");
            }
        }
    }
    pending_user_ = user_;

    notices_.clear();
    notice_keys_.clear();
    for (const fs::path& old : config_.legacy_dirs) {
        std::error_code ec;
        if (!fs::is_directory(old, ec) || fs::is_empty(old, ec)) {
            continue;
        }
        const std::string name = utf8(old);
        notice_keys_.push_back("legacy:" + name);
        notices_.push_back("old mod folder " + name +
                           " is no longer read: move its files into mods/<name>/ with a mod.json "
                           "(docs/modding/mod-json.md)");
    }
    for (const Candidate& c : candidates_) {
        if (c.manifest.legacy) {
            notice_keys_.push_back("cfg:" + c.manifest.id);
            notices_.push_back("mod " + c.manifest.id + " uses the old mod.cfg; write a mod.json instead");
        }
    }
    resolve_all();
    build_index();
}

void Registry::rescan_pending() {
    std::unique_lock lock(mutex_);
    std::vector<std::string> warnings;
    candidates_ = scan_mod_dirs(config_.dirs, warnings);
    pending_plan_ = resolve_load_order(candidates_, pending_user_, config_.env);
}

void Registry::reindex() {
    std::unique_lock lock(mutex_);
    build_index();
}

void Registry::resolve_all() {
    plan_ = resolve_load_order(candidates_, user_, config_.env);
    plan_.warnings.insert(plan_.warnings.begin(), scan_warnings_.begin(), scan_warnings_.end());
    pending_plan_ = plan_;
}

void Registry::build_index() {
    index_.clear();
    stamps_.clear();
    for (ResolvedMod& m : plan_.mods) {
        if (m.state != ModState::Active) {
            continue;
        }
        // The Lua of a mod without "scripts": true stays out of the index, so no loader that asks find() runs it.
        // The report carries the warning on every scan; the call only marks it given for the loaders' logs.
        std::vector<std::string> once;
        const bool lua_ok =
            scripts_allowed(m.candidate.manifest.id, m.candidate.root, m.candidate.manifest.scripts, once);
        bool lua_skipped = false;
        std::error_code ec;
        for (fs::recursive_directory_iterator it(m.candidate.root, fs::directory_options::skip_permission_denied, ec),
             end;
             !ec && it != end; it.increment(ec)) {
            std::error_code entry_ec;
            if (!it->is_regular_file(entry_ec) || entry_ec) {
                continue;
            }
            const std::string rel = normal(utf8(fs::relative(it->path(), m.candidate.root, entry_ec)));
            if (entry_ec || rel.empty() || is_metadata(m.candidate.manifest, rel)) {
                continue;
            }
            if (!lua_ok && is_lua_path(rel)) {
                lua_skipped = true;
                continue;
            }
            index_[lower(rel)].push_back(Hit{m.load_index, rel});
            const std::uintmax_t size = it->file_size(entry_ec);
            const auto time = it->last_write_time(entry_ec).time_since_epoch().count();
            stamps_[m.candidate.manifest.id + "/" + rel] =
                static_cast<std::uint64_t>(time) * 1000003u ^ static_cast<std::uint64_t>(size);
        }
        if (lua_skipped) {
            const std::string why = scripts_refused(m.candidate.manifest.id);
            m.warnings.push_back(why);
            for (ResolvedMod& p : pending_plan_.mods) {
                if (p.candidate.root == m.candidate.root &&
                    std::find(p.warnings.begin(), p.warnings.end(), why) == p.warnings.end()) {
                    p.warnings.push_back(why);
                }
            }
        }
    }
    for (auto& [key, hits] : index_) {
        std::sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) { return a.mod < b.mod; });
    }
}

Registry::FileStamps Registry::file_stamps() const {
    std::shared_lock lock(mutex_);
    return stamps_;
}

bool Registry::set_override(std::string_view relative, std::string_view replacement) {
    std::unique_lock lock(mutex_);
    const std::string target = lower(normal(replacement));
    if (index_.find(target) == index_.end()) {
        return false;
    }
    overrides_[lower(normal(relative))] = target;
    ++override_generation_;
    return true;
}

bool Registry::clear_override(std::string_view relative) {
    std::unique_lock lock(mutex_);
    bool cleared = false;
    if (relative.empty()) {
        cleared = !overrides_.empty();
        overrides_.clear();
    } else {
        cleared = overrides_.erase(lower(normal(relative))) > 0;
    }
    if (cleared) {
        ++override_generation_;
    }
    return cleared;
}

std::uint32_t Registry::override_generation() const {
    std::shared_lock lock(mutex_);
    return override_generation_;
}

std::optional<fs::path> Registry::find(std::string_view relative) const {
    std::shared_lock lock(mutex_);
    if (index_.empty()) {
        return std::nullopt;
    }
    std::string key = lower(normal(relative));
    if (!overrides_.empty()) {
        if (const auto it = overrides_.find(key); it != overrides_.end()) {
            key = it->second;
        }
    }
    const Hit* top = nullptr;
    if (const auto it = index_.find(key); it != index_.end() && !it->second.empty()) {
        top = &it->second.back();
    }
    // A sound asked for as WAV may come as Ogg Opus: the readers tell the two apart by their bytes. The later mod
    // wins; within one mod the WAV does.
    if (key.ends_with(".wav")) {
        const auto it = index_.find(alternative_sound(key));
        if (it != index_.end() && !it->second.empty() && (top == nullptr || it->second.back().mod > top->mod)) {
            top = &it->second.back();
        }
    }
    if (top == nullptr) {
        return std::nullopt;
    }
    return plan_.mods[static_cast<std::size_t>(top->mod)].candidate.root / fs::path(top->path);
}

std::vector<fs::path> Registry::find_all(std::string_view relative) const {
    std::shared_lock lock(mutex_);
    std::vector<fs::path> out;
    const auto it = index_.find(lower(normal(relative)));
    if (it == index_.end()) {
        return out;
    }
    for (const Hit& h : it->second) {
        out.push_back(plan_.mods[static_cast<std::size_t>(h.mod)].candidate.root / fs::path(h.path));
    }
    return out;
}

std::string Registry::owner_of(std::string_view relative) const {
    std::shared_lock lock(mutex_);
    const auto it = index_.find(lower(normal(relative)));
    if (it == index_.end() || it->second.empty()) {
        return {};
    }
    return plan_.mods[static_cast<std::size_t>(it->second.back().mod)].candidate.manifest.id;
}

std::vector<ModFile> Registry::files(std::string_view id) const {
    std::shared_lock lock(mutex_);
    std::vector<ModFile> out;
    const ResolvedMod* mod = plan_.find(id);
    if (mod == nullptr || mod->state != ModState::Active) {
        return out;
    }
    for (const auto& [key, hits] : index_) {
        const auto mine = std::find_if(hits.begin(), hits.end(), [&](const Hit& h) { return h.mod == mod->load_index; });
        if (mine == hits.end()) {
            continue;
        }
        ModFile f;
        f.path = mine->path;
        std::error_code ec;
        // A disc file the game reads by sector (BG/*.TIM) is replaced just like a converted file.
        f.replaces = !config_.base_dir.empty() &&
                     (fs::exists(config_.base_dir / fs::path(f.path), ec) || is_game_file(config_.base_dir, f.path));
        // An Ogg Opus sound replaces the WAV of the same name.
        if (!f.replaces && !config_.base_dir.empty() && lower(f.path).ends_with(".opus")) {
            const std::string wav = f.path.substr(0, f.path.size() - 5) + ".wav";
            f.replaces = fs::exists(config_.base_dir / fs::path(wav), ec);
        }
        for (const Hit& h : hits) {
            if (h.mod != mod->load_index) {
                f.also_in.push_back(plan_.mods[static_cast<std::size_t>(h.mod)].candidate.manifest.id);
            }
        }
        f.wins = hits.back().mod == mod->load_index;
        out.push_back(std::move(f));
    }
    return out;
}

std::vector<std::string> Registry::report(bool with_files) const {
    std::vector<std::string> lines;
    std::vector<const ResolvedMod*> active;
    std::vector<const ResolvedMod*> other;
    {
        std::shared_lock lock(mutex_);
        for (const ResolvedMod& m : plan_.mods) {
            (m.state == ModState::Active ? active : other).push_back(&m);
        }
        if (plan_.mods.empty()) {
            lines.push_back("no mods installed");
        } else {
            lines.push_back(std::to_string(active.size()) + " of " + std::to_string(plan_.mods.size()) +
                            " mods active (load order: later wins)");
        }
    }
    for (const ResolvedMod* m : active) {
        const Manifest& man = m->candidate.manifest;
        const std::vector<ModFile> list = files(man.id);
        int replaces = 0;
        int adds = 0;
        int shadowed = 0;
        for (const ModFile& f : list) {
            (f.replaces ? replaces : adds)++;
            shadowed += f.wins ? 0 : 1;
        }
        std::string line = "  [" + std::to_string(m->load_index + 1) + "] " + version_and_id(*m) + " \"" + man.name +
                           "\": " + std::to_string(replaces) + " replaced, " + std::to_string(adds) + " added";
        if (shadowed > 0) {
            line += ", " + std::to_string(shadowed) + " overridden by later mods";
        }
        if (man.gameplay) {
            line += ", GAMEPLAY CHANGE";
        }
        if (man.scripts) {
            line += ", scripts";
        }
        lines.push_back(std::move(line));
        // A whole-file replacement drops the base script (and every other mod's layer stays an extension of it):
        // always worth a line, since it is what usually breaks with a newer base game.
        for (const std::string& path : man.replace) {
            lines.push_back("      replaces whole " + path + " (the base script does not run)");
        }
        for (const std::string& w : m->warnings) {
            lines.push_back("      warning: " + w);
        }
        if (with_files) {
            for (const ModFile& f : list) {
                std::string fl = std::string("      ") + (f.replaces ? "replace " : "add     ") + f.path;
                if (!f.also_in.empty()) {
                    std::string others;
                    for (const std::string& o : f.also_in) {
                        others += (others.empty() ? "" : ", ") + o;
                    }
                    fl += (f.wins ? "  (also in " : "  (overridden; also in ") + others + ")";
                }
                lines.push_back(std::move(fl));
            }
        }
    }
    std::shared_lock lock(mutex_);
    for (const ResolvedMod* m : other) {
        std::string line = "  [-] " + version_and_id(*m) + " \"" + m->candidate.manifest.name + "\": " +
                           state_name(m->state);
        for (const std::string& p : m->problems) {
            line += "; " + p;
        }
        lines.push_back(std::move(line));
    }
    for (const std::string& w : plan_.warnings) {
        lines.push_back("  warning: " + w);
    }
    for (const std::string& n : notices_) {
        lines.push_back("  notice: " + n);
    }
    return lines;
}

std::vector<std::string> Registry::take_new_notices() {
    std::vector<std::string> fresh;
    UserState state;
    {
        std::unique_lock lock(mutex_);
        for (std::size_t i = 0; i < notice_keys_.size(); ++i) {
            const std::string& key = notice_keys_[i];
            if (std::find(user_.notices.begin(), user_.notices.end(), key) == user_.notices.end()) {
                fresh.push_back(notices_[i]);
                user_.notices.push_back(key);
                pending_user_.notices.push_back(key);
            }
        }
        state = user_;
    }
    if (!fresh.empty()) {
        // Only the notices list changes; the player's order and switches are written back as they were read.
        const fs::path file = state_path();
        std::error_code ec;
        if (!file.empty() && fs::is_directory(file.parent_path(), ec)) {
            std::ofstream out(file, std::ios::binary | std::ios::trunc);
            out << write_user_state(state);
        }
    }
    return fresh;
}

bool Registry::set_enabled(std::string_view id, bool enabled) {
    std::unique_lock lock(mutex_);
    const ResolvedMod* mod = pending_plan_.find(id);
    if (mod == nullptr) {
        return false;
    }
    // Installed mods start off: switching a mod on brings its required dependencies along, or it would only
    // turn into an error. Switching one off later leaves the mods needing it in error, as the screen shows.
    std::vector<std::string> also_on;
    if (enabled) {
        std::vector<std::string> todo{std::string(id)};
        while (!todo.empty()) {
            const std::string cur = std::move(todo.back());
            todo.pop_back();
            const ResolvedMod* m = pending_plan_.find(cur);
            if (m == nullptr) {
                continue;
            }
            for (const ModRef& dep : m->candidate.manifest.dependencies) {
                if (dep.id != id && pending_plan_.find(dep.id) != nullptr &&
                    std::find(also_on.begin(), also_on.end(), dep.id) == also_on.end()) {
                    also_on.push_back(dep.id);
                    todo.push_back(dep.id);
                }
            }
        }
    }
    // The player's list becomes the whole pending order, so switching one mod does not reshuffle the others.
    UserState next = pending_user_;
    next.mods.clear();
    for (const ResolvedMod& m : pending_plan_.mods) {
        const UserEntry* old = pending_user_.find(m.candidate.manifest.id);
        next.mods.push_back({m.candidate.manifest.id, old != nullptr && old->enabled});
    }
    for (UserEntry& e : next.mods) {
        if (e.id == id) {
            e.enabled = enabled;
        } else if (std::find(also_on.begin(), also_on.end(), e.id) != also_on.end()) {
            e.enabled = true;
        }
    }
    pending_user_ = std::move(next);
    pending_plan_ = resolve_load_order(candidates_, pending_user_, config_.env);
    return true;
}

bool Registry::move(std::string_view id, int delta) {
    std::unique_lock lock(mutex_);
    std::vector<UserEntry> list;
    for (const ResolvedMod& m : pending_plan_.mods) {
        const UserEntry* old = pending_user_.find(m.candidate.manifest.id);
        list.push_back({m.candidate.manifest.id, old != nullptr && old->enabled});
    }
    const auto it = std::find_if(list.begin(), list.end(), [&](const UserEntry& e) { return e.id == id; });
    if (it == list.end() || delta == 0) {
        return false;
    }
    const long from = std::distance(list.begin(), it);
    const long to = std::clamp(from + delta, 0L, static_cast<long>(list.size()) - 1);
    if (to == from) {
        return false;
    }
    UserEntry e = *it;
    list.erase(it);
    list.insert(list.begin() + to, std::move(e));
    pending_user_.mods = std::move(list);
    pending_plan_ = resolve_load_order(candidates_, pending_user_, config_.env);
    return true;
}

Result<void> Registry::save() {
    std::shared_lock lock(mutex_);
    const fs::path file = state_path();
    if (file.empty()) {
        return Result<void>::fail("no mods folder");
    }
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    const fs::path tmp = fs::path(file).concat(".tmp");
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            return Result<void>::fail("cannot write " + utf8(tmp));
        }
        out << write_user_state(pending_user_);
        if (!out) {
            return Result<void>::fail("cannot write " + utf8(tmp));
        }
    }
    fs::rename(tmp, file, ec);
    if (ec) {
        return Result<void>::fail("cannot replace " + utf8(file) + ": " + ec.message());
    }
    return Result<void>::ok();
}

namespace {

std::vector<const ResolvedMod*> restart_mods(const Plan& plan) {
    std::vector<const ResolvedMod*> out;
    for (const ResolvedMod* m : plan.active()) {
        if (m->candidate.manifest.requires_restart) {
            out.push_back(m);
        }
    }
    return out;
}

bool same_ids(const std::vector<const ResolvedMod*>& a, const std::vector<const ResolvedMod*>& b) {
    return std::equal(a.begin(), a.end(), b.begin(), b.end(), [](const ResolvedMod* x, const ResolvedMod* y) {
        return x->candidate.manifest.id == y->candidate.manifest.id;
    });
}

}  // namespace

bool Registry::restart_pending() const {
    std::shared_lock lock(mutex_);
    // Mods with "requires_restart": false come and go live (apply_live); only the others need a new process.
    return !same_ids(restart_mods(plan_), restart_mods(pending_plan_));
}

bool Registry::live_pending() const {
    std::shared_lock lock(mutex_);
    return !same_ids(plan_.active(), pending_plan_.active()) &&
           same_ids(restart_mods(plan_), restart_mods(pending_plan_));
}

bool Registry::apply_live() {
    if (!live_pending()) {
        return false;
    }
    // The state file already holds the pending choice (save() runs first), so a rescan makes it the running set.
    rescan();
    return true;
}

Registry& registry() {
    static Registry r;
    return r;
}

}  // namespace sh::mods
