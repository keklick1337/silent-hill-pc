// api-named: the sh.<group>.<name> tables come from the game's list, every call reaches
// named_call with its full name and arguments, and the raw bridge (sh.call, sh.ovl_*, ...) is
// only there with script_raw_api.

#include "framework/sh_test.h"

#include "engine/script/script_host.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;
using sh::script::CallArg;
using sh::script::Host;
using sh::script::HostConfig;

std::string g_calls;  // "name(arg,arg);" per named call
int g_raw = 0;

int named_call(const char* name, int argc, const CallArg* argv, long long* out, int, char* err, int errCap) {
    if (std::strcmp(name, "map.broken") == 0) {
        std::snprintf(err, static_cast<size_t>(errCap), "%s", "unknown map variable 'x'");
        return -1;
    }
    g_calls += name;
    g_calls += "(";
    for (int i = 0; i < argc; ++i) {
        if (i > 0) {
            g_calls += ",";
        }
        switch (argv[i].kind) {
            case 0: g_calls += "nil"; break;
            case 1: g_calls += std::to_string(argv[i].i); break;
            case 2: g_calls += argv[i].s; break;
            case 3: g_calls += argv[i].i != 0 ? "true" : "false"; break;
        }
    }
    g_calls += ");";
    out[0] = 42;
    return std::strcmp(name, "map.get") == 0 ? 1 : 0;
}

const char* const* named_list() {
    static const char* const kNames[] = {"camera.look_at", "map.get", "map.set", "map.broken", nullptr};
    return kNames;
}

struct NamedScratch {
    fs::path root;
    std::vector<std::string> lines;
    NamedScratch() : root(fs::path(SH_PROJECT_ROOT) / ".tmp" / "script" / "named_api") {
        fs::remove_all(root);
        fs::create_directories(root / "scripts");
    }
    ~NamedScratch() {
        std::error_code ec;
        fs::remove_all(root, ec);
    }
    HostConfig config(const char* lua) {
        std::ofstream(root / "scripts" / "game.lua", std::ios::binary) << lua;
        HostConfig c;
        c.gamedata_dir = root;
        c.log = [this](int, const std::string& m) { lines.push_back(m); };
        c.game.named_call = named_call;
        c.game.named_list = named_list;
        c.game.raw_api_enabled = [] { return g_raw; };
        c.game.evb_call = named_call;
        return c;
    }
    bool has(const char* part) const {
        for (const auto& l : lines) {
            if (l.find(part) != std::string::npos) {
                return true;
            }
        }
        return false;
    }
};

}  // namespace

SH_TEST(script_named_api_groups) {
    NamedScratch t;
    g_calls.clear();
    g_raw = 0;
    Host host(t.config(R"lua(
        function on_boot()
            sh.camera.look_at(nil, -375889, true, "camera_target_pos")
            sh.map.set("cutscene_timer", 7, 2)
            sh.log("get=" .. sh.map.get("ufo_light_pos.x", 3))
            sh.log("raw=" .. tostring(sh.call) .. "," .. tostring(sh.ovl_get) .. "," .. tostring(sh.sys_set))
            sh.log("missing=" .. tostring(sh.camera.nope))
            sh.map.broken()
        end
    )lua"));
    SH_CHECK(host.load().value());
    host.on_boot();
    SH_CHECK(g_calls == "camera.look_at(nil,-375889,true,camera_target_pos);map.set(cutscene_timer,7,2);"
                        "map.get(ufo_light_pos.x,3);");
    SH_CHECK(t.has("get=42"));
    SH_CHECK(t.has("raw=nil,nil,nil"));
    SH_CHECK(t.has("missing=nil"));
    SH_CHECK(t.has("sh.map.broken: unknown map variable 'x'"));
}

SH_TEST(script_raw_api_only_with_the_key) {
    NamedScratch t;
    g_calls.clear();
    g_raw = 1;
    Host host(t.config(R"lua(
        function on_boot()
            sh.call("Event_CameraLookAtSet", nil, 1)
            sh.log("ovl=" .. type(sh.ovl_get))
        end
    )lua"));
    SH_CHECK(host.load().value());
    host.on_boot();
    SH_CHECK(g_calls == "Event_CameraLookAtSet(nil,1);");
    SH_CHECK(t.has("ovl=function"));
    g_raw = 0;
}
