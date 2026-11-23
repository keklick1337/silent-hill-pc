// assets/config.cfg is the reference of every setting: each key the game accepts is explained there, and each
// key it sets is one the game accepts. The accepted keys are the game's key tables (config_keys.h) and the keys of
// the other readers of config.cfg, so a key added to either fails here until the file explains it.

#include "engine/config/config_keys.h"
#include "engine/config/retired_keys.h"
#include "engine/input/input_actions.h"
#include "framework/sh_test.h"

#include <cstdio>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::string read(const char* relative) {
    std::ifstream f(std::string(SH_PROJECT_ROOT) + "/" + relative, std::ios::binary);
    std::ostringstream text;
    text << f.rdbuf();
    return text.str();
}

// Every first capture of `pattern` in a source file.
std::set<std::string> scan(const char* relative, const char* pattern) {
    const std::string text = read(relative);
    const std::regex re(pattern);
    std::set<std::string> found;
    for (std::sregex_iterator it(text.begin(), text.end(), re), end; it != end; ++it) found.insert((*it)[1].str());
    return found;
}

bool ends_with(const std::string& s, const char* suffix) {
    const std::string tail(suffix);
    return s.size() > tail.size() && s.compare(s.size() - tail.size(), tail.size(), tail) == 0;
}

std::string without(std::string s, const char* suffix) {
    if (ends_with(s, suffix)) s.resize(s.size() - std::string(suffix).size());
    return s;
}

struct Accepted {
    std::set<std::string> keys;      // taken as they are spelled
    std::set<std::string> binds;     // also with "_altcam"
    std::set<std::string> ignored;   // prefixes read and dropped (keys of features the game does not have)
    std::set<std::string> prefixes;  // prefixes handed to another reader, which knows the keys

    bool has(const std::string& key) const {
        if (keys.count(key) != 0 || binds.count(key) != 0 || binds.count(without(key, "_altcam")) != 0) return true;
        return sh::input::parse_config_key(key).has_value();
    }
};

Accepted accepted_keys() {
    Accepted a;
    // The game's own key tables.
    for (const sh::config::Setting& s : sh::config::kSettings) a.keys.emplace(s.key);
    for (const sh::config::SchemeBind& b : sh::config::kSchemeBinds) a.binds.emplace(b.key);
    for (const sh::config::GlobalBind& b : sh::config::kGlobalBinds) a.binds.emplace(b.key);
    for (const sh::config::KeyPrefix& p : sh::config::kKeyPrefixes) {
        (p.delegated ? a.prefixes : a.ignored).emplace(p.prefix);
    }
    // The readers the parser hands a key to, and the ones that read the file themselves.
    for (const auto& [file, pattern] : std::vector<std::pair<const char*, const char*>>{
             {"src/engine/log/log.cpp", R"re(key == "(log_[a-z_]+)")re"},
             {"src/engine/audio/output/output_settings.cpp", R"re(key == "(audio_[a-z_]+)")re"},
             {"src/engine/vfs/vfs_c.cpp", R"re(config_value\(config_path, "([a-z_]+)"\))re"},
             {"tools/sh-import/options.cpp", R"re(key == "([a-z_]+)")re"},
             {"tools/sh-import/options.cpp", R"re(\{"(import_[a-z]+)", Category::)re"},
             {"src/features/tools/tools_engine.cpp", R"re("(editor_[a-z_]+)")re"},
             {"src/features/tools/tools_host.cpp", R"re("(editor_[a-z_]+)")re"},
         }) {
        const std::set<std::string> found = scan(file, pattern);
        SH_CHECK(!found.empty());
        a.keys.insert(found.begin(), found.end());
    }
    for (int i = 0; i < sh::input::kActionCount; ++i) {
        for (int scheme = 0; scheme < sh::input::kSchemes; ++scheme) {
            a.keys.insert(sh::input::config_key(static_cast<sh::input::Action>(i), scheme));
        }
    }
    return a;
}

// The words of the file that stand before "=", ":", "," or "/" or end their line: an assignment, an entry of a
// list of keys or the head of an explanation.
std::set<std::string> documented_words(const std::string& config) {
    const std::regex word(R"re(([A-Za-z0-9_]+)[ \t]*([=:,/]|$))re");
    std::set<std::string> words;
    std::istringstream lines(config);
    for (std::string line; std::getline(lines, line);) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        for (std::sregex_iterator it(line.begin(), line.end(), word), end; it != end; ++it) {
            words.insert((*it)[1].str());
        }
    }
    return words;
}

}  // namespace

SH_TEST(config_reference_explains_every_accepted_key) {
    const Accepted a = accepted_keys();
    // The scan found the parser: fewer keys than this means its text changed shape and the patterns above with it.
    SH_CHECK(a.keys.size() > 200);
    SH_CHECK(a.binds.size() > 60);
    SH_CHECK(a.prefixes.size() == 2);

    const std::string config = read("assets/config.cfg");
    SH_CHECK(config.size() > 10000);
    const std::set<std::string> words = documented_words(config);
    const auto documented = [&words](const std::string&, const std::string& key) { return words.count(key) != 0; };
    int missing = 0;
    for (const std::string& key : a.keys) {
        if (sh::config::is_retired_key(key)) continue;
        // An alternate-camera action is explained by its classic twin.
        const std::string name = key.rfind("action_", 0) == 0 ? without(key, "_altcam") : key;
        if (!documented(config, name)) {
            std::fprintf(stderr, "assets/config.cfg does not explain %s\n", key.c_str());
            ++missing;
        }
    }
    for (const std::string& key : a.binds) {
        // A second button and an alternate-camera button are explained by the first: pad_cross_2_altcam by pad_cross.
        const std::string classic = without(key, "_altcam");
        if (!documented(config, key) && !documented(config, classic) && !documented(config, without(classic, "_2"))) {
            std::fprintf(stderr, "assets/config.cfg does not explain %s\n", key.c_str());
            ++missing;
        }
    }
    SH_CHECK(missing == 0);
}

SH_TEST(config_reference_sets_only_accepted_keys) {
    const Accepted a = accepted_keys();
    std::istringstream config(read("assets/config.cfg"));
    const std::regex assignment(R"re(^\s*([A-Za-z0-9_]+)\s*=)re");
    int set = 0, unknown = 0;
    for (std::string line; std::getline(config, line);) {
        std::smatch m;
        if (!std::regex_search(line, m, assignment)) continue;
        ++set;
        const std::string key = m[1].str();
        if (!a.has(key) || sh::config::is_retired_key(key)) {
            std::fprintf(stderr, "assets/config.cfg sets %s, which the game does not read\n", key.c_str());
            ++unknown;
        }
    }
    SH_CHECK(set > 100);
    SH_CHECK(unknown == 0);
}
