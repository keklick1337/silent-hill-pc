#include "framework/sh_test.h"

#include "engine/chara/chara_library.h"
#include "engine/chara/chara_script.h"
#include "engine/script/script_host.h"

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using sh::chara::CharaLibrary;
using sh::script::Host;
using sh::script::HostConfig;

namespace {

// The sh.chara_* binds are the engine's (chara_script.cpp); this wires them the
// way the game's script host does, over a real library loaded from the repo's data.
HostConfig chara_config(const CharaLibrary& library, std::vector<std::string>& log) {
    HostConfig c;
    c.gamedata_dir = fs::path(SH_PROJECT_ROOT) / "gamedata";
    static std::vector<std::string>* s_log;
    s_log = &log;
    c.log = [](int level, const std::string& message) { s_log->push_back(std::to_string(level) + "|" + message); };
    sh::chara::ScriptCharaDeps deps;
    deps.library = &library;
    deps.actions_provider = nullptr;
    sh::chara::bind_script_api(&c.game, deps);
    return c;
}

}  // namespace

SH_TEST(lua_chara_binds) {
    std::vector<std::string> warnings;
    auto loaded = CharaLibrary::load(fs::path(SH_PROJECT_ROOT) / "gamedata", {}, warnings);
    SH_CHECK(loaded);
    const CharaLibrary library = std::move(loaded.value());

    const fs::path root = fs::path(SH_PROJECT_ROOT) / ".tmp" / "chara" / "lua";
    fs::remove_all(root);
    fs::create_directories(root / "scripts");
    std::ofstream(root / "scripts" / "game.lua") << R"lua(
        function on_boot()
            assert(sh.chara_id("Chara_Cheryl") == 28)
            assert(sh.chara_id("Cheryl") == 28)
            assert(sh.chara_id("Nobody") == nil)
            -- by name and by id; unknown slot/character -> the fallback (default -1)
            assert(sh.chara_sfx("Chara_Cheryl", "footstep", 1) == 1353)
            assert(sh.chara_sfx(28, "footstep") == 1353)
            assert(sh.chara_sfx("Cheryl", "nothing", 1353) == 1353)
            assert(sh.chara_sfx("Nobody", "footstep", 7) == 7)
            assert(sh.chara_sfx(4, "nothing") == -1)
            -- params: plain value, Q12 exactly like the original literal, fallback/nil when unset
            assert(sh.chara_param("Groaner", "health_max") == 280.0)
            assert(sh.chara_param("Groaner", "nothing") == nil)
            assert(sh.chara_param("Groaner", "nothing", 5) == 5)
            assert(sh.chara_param_q12("HangedScratcher", "health_hard_scale", 0) == 5529)
            assert(sh.chara_param_q12("Nobody", "health", 12) == 12)
            -- named animations -> original indices
            assert(sh.chara_anim("Harry", "walk_forward") == 2)
            assert(sh.chara_anim("Groaner", "nothing", 9) == 9)
        end
    )lua";

    std::vector<std::string> log;
    Host host(chara_config(library, log));
    SH_CHECK(host.load().value());
    host.on_boot();
    for (const std::string& line : log) {
        SH_CHECK(line.empty());  // every assert held: no error lines
    }
    SH_CHECK(log.empty());
    fs::remove_all(root);
}
