// i18n-keys: sh.text(key), sh.msg_index(key) and message keys for sh.msg_draw / say.

#include "framework/sh_test.h"

#include "engine/script/script_host.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;
using sh::script::Host;
using sh::script::HostConfig;

int g_drawn = -1;  // the slot msg_draw was last called with

struct TextScratch {
    fs::path root;
    std::vector<std::string> lines;
    TextScratch() : root(fs::path(SH_PROJECT_ROOT) / ".tmp" / "script" / "text_keys") {
        fs::remove_all(root);
        fs::create_directories(root / "scripts");
    }
    ~TextScratch() {
        std::error_code ec;
        fs::remove_all(root, ec);
    }
    HostConfig config(const char* lua) {
        std::ofstream(root / "scripts" / "game.lua", std::ios::binary) << lua;
        HostConfig c;
        c.gamedata_dir = root;
        c.log = [this](int, const std::string& m) { lines.push_back(m); };
        c.game.text_get = [](const char* key) -> const char* {
            return std::strcmp(key, "map1_s02.door_locked") == 0 ? "It's locked." : nullptr;
        };
        c.game.msg_index = [](const char* key) { return std::strcmp(key, "map1_s02.door_locked") == 0 ? 20 : -1; };
        c.game.msg_draw = [](int idx) {
            g_drawn = idx;
            return -1;
        };
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

SH_TEST(script_text_keys) {
    TextScratch t;
    Host host(t.config(R"lua(
        function on_boot()
            local s, found = sh.text("map1_s02.door_locked")
            assert(s == "It's locked." and found)
            local m, missing = sh.text("nope.key")
            assert(m == "nope.key" and not missing)
            assert(sh.msg_index("map1_s02.door_locked") == 20)
            assert(sh.msg_index("nope.key") == -1)
            assert(sh.msg_draw("map1_s02.door_locked") == -1)
            sh.log("keys ok")
        end
        function on_map_load()
            sh.msg_draw("nope.key")
        end
    )lua"));
    SH_CHECK(host.load().value());
    host.on_boot();
    SH_CHECK(t.has("keys ok"));
    SH_CHECK(g_drawn == 20);
    host.on_map_load("map1_s02");
    SH_CHECK(t.has("no message 'nope.key'"));
}

SH_TEST(script_say_accepts_a_key) {
    TextScratch t;
    g_drawn = -1;
    Host host(t.config(R"lua(
        sh.spawn(function()
            sh.log("say=" .. tostring(say("map1_s02.door_locked")))
        end)
    )lua"));
    SH_CHECK(host.load().value());
    host.on_update();
    host.on_update();
    SH_CHECK(t.has("say=-1"));
    SH_CHECK(g_drawn == 20);
}
