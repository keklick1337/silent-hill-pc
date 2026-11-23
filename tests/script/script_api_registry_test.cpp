// The canonical sh.<group>.<name> names (api_registry.h): every alias is the flat function,
// and every sh.* name the shipped gamedata Lua uses resolves to a binding somewhere.

#include "framework/sh_test.h"

#include "engine/script/api_registry.h"
#include "engine/script/script_host.h"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;
using sh::script::Host;
using sh::script::HostConfig;

std::string slurp(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

std::string strip_comments(const std::string& t) {
    std::string out;
    size_t i = 0;
    while (i < t.size()) {
        if (t.compare(i, 2, "--") == 0) {
            while (i < t.size() && t[i] != '\n') {
                ++i;
            }
        } else {
            out += t[i++];
        }
    }
    return out;
}

}  // namespace

SH_TEST(script_api_aliases_are_the_flat_functions) {
    const fs::path root = fs::path(SH_PROJECT_ROOT) / ".tmp" / "script" / "api_registry";
    fs::remove_all(root);
    fs::create_directories(root / "scripts");
    std::string lua = "function on_boot()\n";
    for (const auto& a : sh::script::api_aliases()) {
        lua += std::string("  if sh.") + a.group + "." + a.name + " ~= sh." + a.legacy + " then sh.log('BAD " + a.group + "." +
               a.name + "') end\n";
    }
    lua += "  sh.log('checked')\nend\n";
    std::ofstream(root / "scripts" / "game.lua", std::ios::binary) << lua;
    std::vector<std::string> lines;
    HostConfig c;
    c.gamedata_dir = root;
    c.log = [&lines](int, const std::string& m) { lines.push_back(m); };
    Host host(c);
    SH_CHECK(host.load().value());
    host.on_boot();
    bool checked = false, bad = false;
    for (const auto& l : lines) {
        checked |= l.find("checked") != std::string::npos;
        bad |= l.find("BAD ") != std::string::npos;
    }
    SH_CHECK(checked);
    SH_CHECK(!bad);
    std::error_code ec;
    fs::remove_all(root, ec);
}

SH_TEST(script_api_bind_names_in_gamedata_resolve) {
    const fs::path src = fs::path(SH_PROJECT_ROOT) / "src";
    std::string all;
    for (const auto& e : fs::recursive_directory_iterator(src)) {
        const auto ext = e.path().extension();
        if (e.is_regular_file() && (ext == ".cpp" || ext == ".h" || ext == ".inc")) {
            all += slurp(e.path());
        }
    }
    auto known = [&all](const std::string& s) { return all.find("\"" + s + "\"") != std::string::npos; };
    std::set<std::string> unresolved;
    size_t used = 0;
    for (const auto& e : fs::recursive_directory_iterator(fs::path(SH_PROJECT_ROOT) / "gamedata")) {
        if (!e.is_regular_file() || e.path().extension() != ".lua") {
            continue;
        }
        const std::string t = strip_comments(slurp(e.path()));
        for (size_t p = t.find("sh."); p != std::string::npos; p = t.find("sh.", p + 3)) {
            if (p > 0 && (std::isalnum(static_cast<unsigned char>(t[p - 1])) || t[p - 1] == '_' || t[p - 1] == '.')) {
                continue;
            }
            size_t q = p + 3;
            std::string g, n;
            while (q < t.size() && (std::isalnum(static_cast<unsigned char>(t[q])) || t[q] == '_')) {
                g += t[q++];
            }
            if (g.empty()) {
                continue;
            }
            if (q + 1 < t.size() && t[q] == '.' && (std::isalpha(static_cast<unsigned char>(t[q + 1])) || t[q + 1] == '_')) {
                ++q;
                while (q < t.size() && (std::isalnum(static_cast<unsigned char>(t[q])) || t[q] == '_')) {
                    n += t[q++];
                }
            }
            ++used;
            if (n.empty()) {
                if (!known(g) && all.find("\"" + g + ".") == std::string::npos) {
                    unresolved.insert(g);
                }
            } else if (!known(g + "." + n) && !known(n) && !known("set_" + n) && !known(g + "." + "set_" + n)) {
                // generated set_<field> binds: "set_" is prepended to a "<group>.<field>" entry
                if (!(n.rfind("set_", 0) == 0 && known(g + "." + n.substr(4)))) {
                    unresolved.insert(g + "." + n);
                }
            }
        }
    }
    SH_CHECK(used > 1000);
    std::string joined;
    for (const auto& u : unresolved) {
        joined += u + " ";
    }
    if (!joined.empty()) {
        ::sh::test::record_failure(__FILE__, __LINE__, joined.c_str());
    }
}
