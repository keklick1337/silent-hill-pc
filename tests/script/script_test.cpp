#include "framework/sh_test.h"

#include "engine/script/script_host.h"
#include "engine/script/sh_script.h"
#include "engine/version/engine_version.h"

#include <filesystem>
#include <fstream>
#include <cstring>
#include <iterator>
#include <string>
#include <utility>
#include <vector>
#include "framework/mod_fixture.h"

namespace fs = std::filesystem;
using sh::script::Host;
using sh::script::HostConfig;

namespace {

struct Scratch {
    fs::path root;
    explicit Scratch(const char* name) : root(fs::path(SH_PROJECT_ROOT) / ".tmp" / "script" / name) {
        fs::remove_all(root);
        fs::create_directories(root);
    }
    ~Scratch() { std::error_code ec; fs::remove_all(root, ec); }
    void write(const fs::path& relative, std::string_view text) const {
        fs::create_directories(root / relative.parent_path());
        std::ofstream out(root / relative, std::ios::binary);
        out << text;
        out.close();
        sh::test::mod_file_written(root / relative);
    }
};

struct Logged {
    std::vector<std::string> lines;
    HostConfig config(const fs::path& gamedata, const fs::path& mods = {}) {
        HostConfig c;
        c.gamedata_dir = gamedata;
        c.mods_dir = mods;
        c.log = [this](int level, const std::string& message) {
            lines.push_back(std::to_string(level) + "|" + message);
        };
        return c;
    }
    bool has(std::string_view part) const {
        for (const std::string& line : lines) {
            if (line.find(part) != std::string::npos) {
                return true;
            }
        }
        return false;
    }
};

}  // namespace

SH_TEST(no_script_is_the_normal_case) {
    Scratch dir("none");
    Logged log;
    Host host(log.config(dir.root));
    const auto loaded = host.load();
    SH_CHECK(loaded);
    SH_CHECK(!loaded.value());  // no file anywhere: fine
    SH_CHECK(!host.loaded());
    // Every hook is a no-op.
    host.on_boot();
    host.on_new_game();
    host.on_map_load("map1_s02");
    host.on_update();
    SH_CHECK(log.lines.empty());
}

SH_TEST(engine_version_in_lua) {
    Scratch dir("engine_version");
    dir.write("scripts/game.lua", R"lua(
        function on_boot()
            local e = sh.engine
            sh.log("engine " .. e.version .. " = " .. e.major .. "." .. e.minor .. "." .. e.patch)
            sh.log("build " .. e.build)
            sh.log("same " .. tostring(e.at_least(e.major, e.minor, e.patch)))
            sh.log("older " .. tostring(e.at_least(e.major) and e.at_least(0, 99, 99)))
            sh.log("newer " .. tostring(e.at_least(e.major + 1) or e.at_least(e.major, e.minor + 1)
                                        or e.at_least(e.major, e.minor, e.patch + 1)))
        end
    )lua");
    Logged log;
    Host host(log.config(dir.root));
    SH_CHECK(host.load().value());
    host.on_boot();
    const std::string version(sh::version::kText);
    SH_CHECK(log.has("engine " + version + " = " + version));
    SH_CHECK(log.has(std::string("build ") + sh::version::full()));
    SH_CHECK(log.has("same true"));
    SH_CHECK(log.has("older true"));
    SH_CHECK(log.has("newer false"));
}

SH_TEST(hooks_and_api) {
    Scratch dir("hooks");
    dir.write("scripts/game.lua", R"lua(
        local entered = {}
        function on_boot()
            sh.log("boot api=" .. tostring(sh.api_version()))
        end
        function on_new_game()
            sh.log("new game")
        end
        function on_map_load(map)
            entered[map] = true
            sh.log("map " .. map)
        end
        function on_update()
            sh.log("update")
        end
    )lua");
    Logged log;
    Host host(log.config(dir.root));
    SH_CHECK(host.load().value());
    host.on_boot();
    host.on_new_game();
    host.on_map_load("map1_s02");
    host.on_update();
    SH_CHECK(log.has("boot api=3"));
    SH_CHECK(log.has("new game"));
    SH_CHECK(log.has("map map1_s02"));
    SH_CHECK(log.has("update"));
    // A second load() is a no-op; hooks keep working.
    SH_CHECK(host.load().value());
    host.on_map_load("map2_s00");
    SH_CHECK(log.has("map map2_s00"));
}

SH_TEST(sandbox) {
    Scratch dir("sandbox");
    // The dangerous globals are gone; using one is a script error, not an escape.
    dir.write("scripts/game.lua", R"lua(
        function check()
            return io, os, load, dofile, require
        end
    )lua");
    {
        Logged log;
        Host host(log.config(dir.root));
        SH_CHECK(host.load().value());
        // The globals index to nil inside the sandbox; the hook itself runs fine.
        host.on_boot();
        SH_CHECK(log.lines.empty());
    }
    // A script that touches a removed global errors; the host survives and drops hooks.
    dir.write("scripts/game.lua", R"lua(
        function on_boot()
            io.open("x")
        end
    )lua");
    {
        Logged log;
        Host host(log.config(dir.root));
        SH_CHECK(host.load().value());
        host.on_boot();
        SH_CHECK(log.has("on_boot error"));
        // on_update is dropped after its first error, not called into a broken state.
        dir.write("scripts/game.lua", "function on_update() error('boom') end");
    }
    {
        Logged log;
        Host host(log.config(dir.root));
        SH_CHECK(host.load().value());
        host.on_update();
        host.on_update();  // second call skipped: the hook is already dropped
        SH_CHECK(log.lines.size() == 1);
    }
}

SH_TEST(instruction_budget) {
    Scratch dir("budget");
    dir.write("scripts/game.lua", "function on_update() while true do end end");
    Logged log;
    Host host(log.config(dir.root));
    SH_CHECK(host.load().value());
    host.on_update();  // must return, not hang
    SH_CHECK(log.has("instruction budget"));
    host.on_update();  // dropped
    SH_CHECK(log.lines.size() == 1);
}

SH_TEST(script_error_at_load) {
    Scratch dir("bad");
    dir.write("scripts/game.lua", "this is not lua");
    Logged log;
    Host host(log.config(dir.root));
    const auto loaded = host.load();
    SH_CHECK(!loaded);
    SH_CHECK(!host.loaded());
    SH_CHECK(loaded.error().find("scripts/game.lua") != std::string::npos);
    host.on_boot();  // no-op
    SH_CHECK(log.lines.empty());
}

namespace {

// The place of the first line containing `part`, or -1.
int line_of(const Logged& log, std::string_view part) {
    for (std::size_t i = 0; i < log.lines.size(); ++i) {
        if (log.lines[i].find(part) != std::string::npos) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

}  // namespace

// A mod's scripts/game.lua does not replace the base script: mods add scripts/main.lua.
SH_TEST(mod_game_lua_does_not_replace_base) {
    Scratch dir("mod");
    dir.write("base/scripts/game.lua", "function on_boot() sh.log('base') end");
    dir.write("mods/mine/mod.json", R"({"id": "mine", "version": "1.0.0", "scripts": true})");
    dir.write("mods/mine/scripts/game.lua", "function on_boot() sh.log('mine') end");
    Logged log;
    Host host(log.config(dir.root / "base", dir.root / "mods"));
    SH_CHECK(host.load().value());
    host.on_boot();
    SH_CHECK(log.has("[SCRIPT] base"));
    SH_CHECK(!log.has("[SCRIPT] mine"));
    SH_CHECK(log.has("mod mine: scripts/game.lua no longer replaces the base game script"));
}

// Two mods: own globals, `mod` with their identity and storage, hooks after the base in load order.
SH_TEST(mod_main_lua_environments_and_hook_order) {
    Scratch dir("mod_main");
    dir.write("base/scripts/game.lua", R"lua(
        function on_boot() sh.log("base boot") end
        function on_new_game() sh.log("base sees x=" .. tostring(x) .. " y=" .. tostring(y)) end
        function on_map_load(map) sh.log("base map " .. map) end
        function check_upper() sh.log("base upper " .. string.upper("q")) end
    )lua");
    dir.write("mods/alpha/mod.json", R"({"id": "alpha", "name": "Alpha", "version": "1.2.3", "scripts": true})");
    dir.write("mods/alpha/scripts/main.lua", R"lua(
        x = "alpha"
        _G.y = 1
        string.upper = function() return "broken" end
        function on_boot()
            sh.log("alpha boot x=" .. x .. " id=" .. mod.id .. " name=" .. mod.name .. " v=" .. mod.version)
            sh.log("alpha dir ok=" .. tostring(mod.dir:sub(-6) == "/alpha"))
        end
        function on_map_load(map)
            mod.storage.maps = (mod.storage.maps or 0) + 1
            sh.log("alpha map " .. map .. " n=" .. mod.storage.maps)
        end
    )lua");
    dir.write("mods/beta/mod.json",
              R"({"id": "beta", "version": "0.1.0", "scripts": true, "load_after": ["alpha"]})");
    dir.write("mods/beta/scripts/main.lua", R"lua(
        x = "beta"
        function on_boot()
            sh.log("beta boot x=" .. x .. " id=" .. mod.id .. " upper=" .. string.upper("q"))
        end
        function on_map_load(map) sh.log("beta map " .. map) end
    )lua");
    Logged log;
    Host host(log.config(dir.root / "base", dir.root / "mods"));
    SH_CHECK(host.load().value());
    SH_CHECK((host.loaded_mods() == std::vector<std::string>{"alpha", "beta"}));
    host.on_boot();
    host.on_new_game();
    host.on_map_load("map1_s02");
    host.on_map_load("map1_s03");
    SH_CHECK(host.call_global("check_upper"));

    SH_CHECK(log.has("alpha boot x=alpha id=alpha name=Alpha v=1.2.3"));
    SH_CHECK(log.has("alpha dir ok=true"));
    SH_CHECK(log.has("beta boot x=beta id=beta upper=Q"));   // alpha's string.upper stayed alpha's
    SH_CHECK(log.has("base sees x=nil y=nil"));              // no mod global reached the base
    SH_CHECK(log.has("base upper Q"));
    SH_CHECK(log.has("alpha map map1_s03 n=2"));             // storage lives with the host
    const int base = line_of(log, "base boot");
    const int alpha = line_of(log, "alpha boot");
    const int beta = line_of(log, "beta boot");
    SH_CHECK(base >= 0 && alpha > base && beta > alpha);
    SH_CHECK(line_of(log, "base map map1_s02") < line_of(log, "alpha map map1_s02"));
    SH_CHECK(line_of(log, "alpha map map1_s02") < line_of(log, "beta map map1_s02"));
}

// A mod hook that errors is logged once and switched off for that mod; the base and other mods go on.
SH_TEST(mod_hook_error_is_contained) {
    Scratch dir("mod_error");
    dir.write("base/scripts/game.lua", "function on_update() sh.log('base tick') end");
    dir.write("mods/bad/mod.json", R"({"id": "bad", "version": "1.0.0", "scripts": true})");
    dir.write("mods/bad/scripts/main.lua", "function on_update() error('boom') end");
    dir.write("mods/good/mod.json", R"({"id": "good", "version": "1.0.0", "scripts": true, "load_after": ["bad"]})");
    dir.write("mods/good/scripts/main.lua", "function on_update() sh.log('good tick') end");
    dir.write("mods/broken/mod.json", R"({"id": "broken", "version": "1.0.0", "scripts": true})");
    dir.write("mods/broken/scripts/main.lua", "function on_update() sh.log('never') end error('at load')");
    dir.write("mods/spin/mod.json", R"({"id": "spin", "version": "1.0.0", "scripts": true})");
    dir.write("mods/spin/scripts/main.lua", "while true do end");
    Logged log;
    Host host(log.config(dir.root / "base", dir.root / "mods"));
    SH_CHECK(host.load().value());
    host.on_update();
    host.on_update();
    int errors = 0;
    int good = 0;
    int base = 0;
    for (const std::string& line : log.lines) {
        errors += line.find("mod bad: on_update error") != std::string::npos ? 1 : 0;
        good += line.find("good tick") != std::string::npos ? 1 : 0;
        base += line.find("base tick") != std::string::npos ? 1 : 0;
    }
    SH_CHECK(errors == 1);
    SH_CHECK(good == 2 && base == 2);
    SH_CHECK(log.has("mod broken: ") && log.has("at load"));  // a main.lua error leaves that mod out
    SH_CHECK(!log.has("never"));
    SH_CHECK(log.has("mod spin: ") && log.has("instruction budget"));  // a loop at load is cut, not a hang
    SH_CHECK((host.loaded_mods() == std::vector<std::string>{"bad", "good"}));
}

// Without "scripts": true a mod's Lua does not run, with one warning; mods run with no base script too.
SH_TEST(mod_scripts_need_the_flag) {
    Scratch dir("mod_flag");
    dir.write("mods/quiet/mod.json", R"({"id": "quiet", "version": "1.0.0"})");
    dir.write("mods/quiet/scripts/main.lua", "function on_boot() sh.log('quiet ran') end");
    dir.write("mods/loud/mod.json", R"({"id": "loud", "version": "1.0.0", "scripts": true})");
    dir.write("mods/loud/scripts/main.lua", "function on_boot() sh.log('loud ran') end");
    Logged log;
    Host host(log.config(dir.root / "base", dir.root / "mods"));
    SH_CHECK(host.load().value());  // no base game.lua: the mods still get a state
    host.on_boot();
    SH_CHECK(log.has("loud ran"));
    SH_CHECK(!log.has("quiet ran"));
    SH_CHECK(log.has("mod quiet has Lua files but mod.json does not say \"scripts\": true; its scripts are not run"));

    // Once per mod: a second host of the same folders stays quiet about it.
    Logged again;
    Host second(again.config(dir.root / "base", dir.root / "mods"));
    SH_CHECK(second.load().value());
    SH_CHECK(!again.has("mod quiet has Lua files"));
}

// What mod code can reach: the safe libraries and sh, nothing that touches the machine or loads code.
SH_TEST(mod_sandbox) {
    Scratch dir("mod_sandbox");
    dir.write("mods/probe/mod.json", R"({"id": "probe", "version": "1.0.0", "scripts": true})");
    dir.write("mods/probe/scripts/main.lua", R"lua(
        local gone = {"io", "os", "require", "package", "debug", "load", "loadfile", "dofile", "loadstring",
                      "collectgarbage", "utf8"}
        for _, name in ipairs(gone) do
            if _G[name] ~= nil or _ENV[name] ~= nil then sh.log("leak " .. name) end
        end
        if string.dump ~= nil then sh.log("leak string.dump") end
        if getmetatable("") ~= false then sh.log("leak string metatable") end
        if getmetatable(_ENV) ~= false then sh.log("leak environment metatable") end
        local have = {"string", "table", "math", "coroutine", "sh", "pairs", "ipairs", "pcall", "setmetatable",
                      "tostring", "type", "print"}
        for _, name in ipairs(have) do
            if _ENV[name] == nil then sh.log("missing " .. name) end
        end
        if type(sh.log) ~= "function" or type(sh.spawn) ~= "function" then sh.log("missing sh api") end
        sh.log("probe done")
    )lua");
    Logged log;
    Host host(log.config(dir.root / "base", dir.root / "mods"));
    SH_CHECK(host.load().value());
    SH_CHECK(log.has("probe done"));
    SH_CHECK(!log.has("leak"));
    SH_CHECK(!log.has("missing"));
}

// The shipped base script must load and define no hooks.
SH_TEST(shipped_game_lua_is_a_noop) {
    Logged log;
    Host host(log.config(fs::path(SH_PROJECT_ROOT) / "gamedata"));
    const auto loaded = host.load();
    SH_CHECK(loaded);
    if (loaded.value()) {
        host.on_boot();
        host.on_new_game();
        host.on_map_load("map0_s00");
        host.on_update();
        SH_CHECK(log.lines.empty());
    }
}

// ---- API v3: the game binds behind sh.* (mocked here; the real ones are pc_script_api) ----

struct MockGame {
    int flags[64] = {};
    int items[8] = {};          // itemId -> count
    int pos[3] = {1, 2, 3};
    int frozen = 0;
    int msg_state_calls = 0;
    int msg_result = 0;         // what msg_draw returns after a few ticks
    int sfx = -1;
    int bgm = -1;
    int fades = 0;              // fade_start calls
    int fade_done_at = 3;       // fade_done turns true after this many calls
    int fade_calls = 0;
    struct MapEvent { int sysState, param, mapIdx, completeFlag; } last_event{-1, -1, -1, -1};

    sh::script::GameApi api() {
        sh::script::GameApi a;
        a.flag_get = [](int id) { return mock()->flags[id & 63]; };
        a.flag_set = [](int id, int v) { mock()->flags[id & 63] = v != 0; };
        a.flag_set_alt = [](int id) { mock()->flags[id & 63] = 1; };
        a.give_item = [](int id, int) { mock()->items[id & 7]++; };
        a.item_count = [](int id) { return mock()->items[id & 7]; };
        a.take_item = [](int id, int c) { auto* m = mock(); m->items[id & 7] -= c; if (m->items[id & 7] < 0) m->items[id & 7] = 0; };
        a.player_pos = [](int out[3]) { auto* m = mock(); out[0] = m->pos[0]; out[1] = m->pos[1]; out[2] = m->pos[2]; };
        a.player_set_pos = [](const int xyz[3]) { auto* m = mock(); m->pos[0] = xyz[0]; m->pos[1] = xyz[1]; m->pos[2] = xyz[2]; };
        a.player_freeze = [](int on) { mock()->frozen = on; };
        a.map_name = [] { return "map1_s02"; };
        a.msg_draw = [](int) { auto* m = mock(); return ++m->msg_state_calls >= 3 ? m->msg_result : 0; };
        a.sfx_play = [](int id) { mock()->sfx = id; };
        a.bgm_change = [](int t) { mock()->bgm = t; };
        a.fade_start = [](int, int) { mock()->fades++; };
        a.fade_done = []() -> int { return ++mock()->fade_calls >= mock()->fade_done_at ? 1 : 0; };
        a.map_event_set = [](int s, int p, int m, int c) {
            mock()->last_event = {s, p, m, c};
        };
        return a;
    }

    static MockGame* mock() {
        static MockGame instance;
        return &instance;
    }

    void reset() { *this = MockGame{}; }
};

SH_TEST(api_v3_game_functions) {
    Scratch dir("api3");    dir.write("scripts/game.lua", R"lua(
        function on_boot()
            sh.flag_set(10, true)
            assert(sh.flag_get(10))
            sh.flag_set_alt(11)
            assert(sh.flag_get(11))
            sh.give_item(3)
            sh.give_item(3)
            assert(sh.item_count(3) == 2)
            assert(sh.has_item(3))
            sh.take_item(3, 1)
            assert(sh.item_count(3) == 1)
            local x, y, z = sh.player_pos()
            assert(x == 1 and y == 2 and z == 3)
            sh.player_set_pos(10, 20, 30)
            sh.player_freeze(true)
            sh.sfx(1287)
            sh.bgm(6)
            sh.fade(true, false)
            sh.map_event_set(5, 7, 2, 40)
        end
    )lua");
    Logged log;
    HostConfig config = log.config(dir.root);
    MockGame* game = MockGame::mock();
    game->reset();
    config.game = game->api();
    Host host(config);
    SH_CHECK(host.load().value());
    host.on_boot();
    SH_CHECK(log.lines.empty());  // every assert held
    SH_CHECK(game->flags[10] && game->flags[11]);
    SH_CHECK(game->items[3] == 1);
    SH_CHECK(game->pos[0] == 10 && game->pos[1] == 20 && game->pos[2] == 30);
    SH_CHECK(game->frozen == 1);
    SH_CHECK(game->sfx == 1287 && game->bgm == 6 && game->fades == 1);
    SH_CHECK(game->last_event.sysState == 5 && game->last_event.param == 7);
    SH_CHECK(game->last_event.mapIdx == 2 && game->last_event.completeFlag == 40);
}

// A missing bind is a script error naming the function, not a crash.
SH_TEST(api_v3_missing_bind_errors) {
    Scratch dir("missing");
    dir.write("scripts/game.lua", "function on_boot() sh.bgm(1) end");
    Logged log;
    Host host(log.config(dir.root));  // no game api at all
    SH_CHECK(host.load().value());
    host.on_boot();
    SH_CHECK(log.has("on_boot error") && log.has("sh.bgm"));
}

// Cutscene states by name: the argument resolves against the character of the slot
// (slot 0 = Harry's "states"); the number still works, an unknown name is a script error.
SH_TEST(api_state_names) {
    Scratch dir("states");
    dir.write("scripts/game.lua", R"lua(
        function on_boot()
            assert(sh.chara_state("Harry", "pickup_crouch") == 59)
            assert(sh.chara_state("Nobody", "pickup_crouch") == -1)
            assert(sh.chara_state("Harry", "no_such_state", 7) == 7)
            sh.event_chara_anim(0, 0, 88, false)
            sh.event_chara_anim(0, 0, "pickup_crouch", false)
            sh.chara_anim_play_to_end(0, "walk_forward")
            sh.event_waypoint_player("run_forward", 1, false)
            sh.event_chara_anim(0, 0, "no_such_state", false)
        end
    )lua");
    struct States {
        int anim[4]{};
        int play[2]{};
        int way[2]{};
        int calls = 0;
        static States* self() {
            static States s;
            return &s;
        }
        sh::script::GameApi api() {
            sh::script::GameApi a;  // only the binds under test
            a.chara_id_find = [](const char* name) { return std::strcmp(name, "Harry") == 0 ? 1 : -1; };
            a.chara_slot_chara_id_get = [](int slot) { return slot == 0 ? 1 : -1; };
            a.chara_state_number = [](int id, const char* name) {
                if (id != 1) {
                    return -1;
                }
                return std::strcmp(name, "pickup_crouch") == 0   ? 59
                     : std::strcmp(name, "walk_forward") == 0   ? 53
                     : std::strcmp(name, "run_forward") == 0    ? 54
                                                                : -1;
            };
            a.event_chara_anim = [](int cmd, int slot, int state, int inc) {
                States* g = self();
                g->anim[0] = cmd; g->anim[1] = slot; g->anim[2] = state; g->anim[3] = inc;
                g->calls++;
            };
            a.chara_anim_play_to_end = [](int slot, int state) {
                States* g = self();
                g->play[0] = slot; g->play[1] = state;
                g->calls++;
            };
            a.event_waypoint_player = [](int state, int count, int /*inc*/) {
                States* g = self();
                g->way[0] = state; g->way[1] = count;
                g->calls++;
            };
            return a;
        }
    };
    States* got = States::self();
    *got = States{};
    Logged log;
    HostConfig config = log.config(dir.root);
    config.game = got->api();
    Host host(config);
    SH_CHECK(host.load().value());
    host.on_boot();
    SH_CHECK(got->calls == 4);
    SH_CHECK(got->anim[0] == 0 && got->anim[1] == 0 && got->anim[2] == 59 && got->anim[3] == 0);
    SH_CHECK(got->play[0] == 0 && got->play[1] == 53);
    SH_CHECK(got->way[0] == 54 && got->way[1] == 1);
    // The last call named an unknown state: a script error naming it and the slot.
    SH_CHECK(log.has("on_boot error") && log.has("no_such_state") && log.has("slot 0"));
}

// v3.2: the message steppers and the paper-map pickup.
SH_TEST(api_v3_2_event_steppers) {
    Scratch dir("steppers");
    dir.write("scripts/game.lua", R"lua(
        function on_boot()
            sh.event_msg(false, 15, 0, 0, 0, false)
            sh.event_msg_with_sfx(11, 1334)
            sh.event_msg_with_bg(1931, 0, 0, 19)
            sh.event_paper_map_take(2, 171, 23)
            sh.paper_map_cmd(0, "OldTown")
            sh.paper_map_state(2, "FogCentralTown")
        end
    )lua");
    struct Steppers {
        int calls = 0;
        int msg[6]{};
        int sfx[2]{};
        int bg[4]{};
        int paper[3]{};
        int cmd[2]{};
        int state[2]{};
        static Steppers* self() {
            static Steppers s;
            return &s;
        }
        sh::script::GameApi api() {
            sh::script::GameApi a;  // only the binds under test
            a.event_msg = [](int has, int idx, int s0, int s1, int s2, int inc) {
                Steppers* g = self();
                g->msg[0] = has; g->msg[1] = idx; g->msg[2] = s0; g->msg[3] = s1;
                g->msg[4] = s2; g->msg[5] = inc;
                g->calls++;
            };
            a.event_msg_with_sfx = [](int idx, int sfx) {
                Steppers* g = self();
                g->sfx[0] = idx; g->sfx[1] = sfx;
                g->calls++;
            };
            a.event_msg_with_bg = [](int tex, int f0, int f1, int idx) {
                Steppers* g = self();
                g->bg[0] = tex; g->bg[1] = f0; g->bg[2] = f1; g->bg[3] = idx;
                g->calls++;
            };
            a.event_paper_map_take = [](int paper, int flag, int idx) {
                Steppers* g = self();
                g->paper[0] = paper; g->paper[1] = flag; g->paper[2] = idx;
                g->calls++;
            };
            a.paper_map_number = [](const char* name) {
                return std::strcmp(name, "OldTown") == 0 ? 1 : (std::strcmp(name, "FogCentralTown") == 0 ? 2 : -1);
            };
            a.paper_map_cmd = [](int cmdIdx, int paper) {
                Steppers* g = self();
                g->cmd[0] = cmdIdx; g->cmd[1] = paper;
                g->calls++;
            };
            a.paper_map_state = [](int stateIdx, int paper) {
                Steppers* g = self();
                g->state[0] = stateIdx; g->state[1] = paper;
                g->calls++;
            };
            return a;
        }
    };
    Steppers* got = Steppers::self();
    *got = Steppers{};
    Logged log;
    HostConfig config = log.config(dir.root);
    config.game = got->api();
    Host host(config);
    SH_CHECK(host.load().value());
    host.on_boot();
    SH_CHECK(got->calls == 6);
    SH_CHECK(got->msg[0] == 0 && got->msg[1] == 15 && got->msg[5] == 0);
    SH_CHECK(got->sfx[0] == 11 && got->sfx[1] == 1334);
    SH_CHECK(got->bg[0] == 1931 && got->bg[3] == 19);
    SH_CHECK(got->paper[0] == 2 && got->paper[1] == 171 && got->paper[2] == 23);
    SH_CHECK(got->cmd[0] == 0 && got->cmd[1] == 1);      // "OldTown"
    SH_CHECK(got->state[0] == 2 && got->state[1] == 2);  // "FogCentralTown"
}

// v3.3: input, camera, flashlight, gamma, wait timer, two-index texture messages.
SH_TEST(api_v3_3_world_binds) {
    Scratch dir("v33");
    dir.write("scripts/game.lua", R"lua(
        function on_boot()
            assert(sh.input_action("skip") == false)
            assert(sh.input_axis(0, 0) == -12)
            sh.camera_pos(19.72, -5.2, -27.6, 0, 4096, 0, 0, 0, 0, 0, true)
            sh.camera_look(19.72, -3.3, -24.0)
            sh.event_msg_with_texture(1923, 10240, 6144, 25, 22)
            sh.flashlight(false)
            sh.background_gamma(104)
            assert(sh.event_wait_timer(4915, 0) == true)
        end
    )lua");
    struct World {
        int calls = 0;
        bool camera_pos_ok = false;
        bool camera_look_ok = false;
        int texture[5]{};
        int flashlight = -1;
        int gamma = -1;
        int wait[2]{};
        static World* self() {
            static World w;
            return &w;
        }
        sh::script::GameApi api() {
            sh::script::GameApi a;
            a.input_action = [](const char*, int) { World* g = self(); g->calls++; return 0; };
            a.input_axis = [](int, int) { World* g = self(); g->calls++; return -12; };
            a.camera_pos = [](int warp, int pos[3], int args[7]) {
                World* g = self();
                g->camera_pos_ok = warp == 1 && pos[0] == 80773 && pos[1] == -21299 && pos[2] == -113049
                                   && args[1] == 4096;
                g->calls++;
            };
            a.camera_look = [](int warp, int look[3], int args[7]) {
                World* g = self();
                g->camera_look_ok = warp == 0 && look[0] == 80773 && args[0] == 0;
                g->calls++;
            };
            a.event_msg_with_texture = [](int tex, int f0, int f1, int m0, int m1) {
                World* g = self();
                g->texture[0] = tex; g->texture[1] = f0; g->texture[2] = f1;
                g->texture[3] = m0; g->texture[4] = m1;
                g->calls++;
            };
            a.flashlight = [](int on) { World* g = self(); g->flashlight = on; g->calls++; };
            a.background_gamma = [](int v) { World* g = self(); g->gamma = v; g->calls++; };
            a.event_wait_timer = [](int d, int u) {
                World* g = self();
                g->wait[0] = d; g->wait[1] = u;
                g->calls++;
                return 1;
            };
            return a;
        }
    };
    World* got = World::self();
    *got = World{};
    Logged log;
    HostConfig config = log.config(dir.root);
    config.game = got->api();
    Host host(config);
    SH_CHECK(host.load().value());
    host.on_boot();
    SH_CHECK(log.lines.empty());  // the asserts held
    SH_CHECK(got->calls == 8);
    SH_CHECK(got->camera_pos_ok && got->camera_look_ok);
    SH_CHECK(got->texture[0] == 1923 && got->texture[3] == 25 && got->texture[4] == 22);
    SH_CHECK(got->flashlight == 0 && got->gamma == 104);
    SH_CHECK(got->wait[0] == 4915 && got->wait[1] == 0);
}

// ---- coroutines ----

SH_TEST(coroutines_wait_and_order) {
    Scratch dir("coro");
    dir.write("scripts/game.lua", R"lua(
        local trace = {}
        function on_update()
            if #trace == 3 then
                sh.log("trace=" .. table.concat(trace, ","))
            end
        end
        sh.spawn(function()
            table.insert(trace, "a0")
            wait(0.1)            -- 3 ticks at 30 Hz
            table.insert(trace, "a1")
        end)
        sh.spawn(function()
            wait_frames(2)
            table.insert(trace, "b")
        end)
    )lua");
    Logged log;
    Host host(log.config(dir.root));
    SH_CHECK(host.load().value());
    // load() runs the chunk: both coroutines are scheduled, none started yet.
    SH_CHECK(host.coroutine_count() == 2);
    host.on_update();  // tick 1: a0, both count down
    host.on_update();  // tick 2
    host.on_update();  // tick 3: b resumes -> "b"
    host.on_update();  // tick 4: a resumes -> "a1"
    host.on_update();  // tick 5: the hook sees the full trace
    SH_CHECK(log.has("trace=a0,b,a1"));
    SH_CHECK(host.coroutine_count() == 0);
}

SH_TEST(coroutine_wait_until_and_say) {
    Scratch dir("until");
    dir.write("scripts/game.lua", R"lua(
        sh.spawn(function()
            local ready = false
            sh.spawn(function()
                wait_until(function() return sh.flag_get(5) end)
                sh.log("flag seen")
            end)
            wait_frames(1)
            local pick = say(20)          -- msg_draw answers 0,0 then -1
            sh.log("say=" .. tostring(pick))
            sh.fade(false, false)
            wait_fade()
            sh.log("fade done")
            load_map(3, 8, 42)            -- prelude over map_event_set
            fmv(77)                       -- overwritten by the line below? no: order
        end)
    )lua");
    Logged log;
    MockGame* game = MockGame::mock();
    game->reset();
    HostConfig config = log.config(dir.root);
    config.game = game->api();
    game->msg_result = -1;
    Host host(config);
    SH_CHECK(host.load().value());
    for (int i = 0; i < 12; ++i) {
        if (i == 4) {
            game->flags[5] = 1;  // the predicate turns true mid-run
        }
        host.on_update();
    }
    SH_CHECK(log.has("flag seen"));
    SH_CHECK(log.has("say=-1"));
    SH_CHECK(log.has("fade done"));
    SH_CHECK(game->last_event.sysState == 4 && game->last_event.param == 77);  // fmv won
    SH_CHECK(host.coroutine_count() == 0);
}

// A coroutine that errors is logged once and dropped; the rest keep running.
SH_TEST(coroutine_error_is_contained) {
    Scratch dir("coroerr");
    dir.write("scripts/game.lua", R"lua(
        sh.spawn(function() error("coroutine boom") end)
        sh.spawn(function()
            wait_frames(1)
            sh.log("survivor")
        end)
    )lua");
    Logged log;
    Host host(log.config(dir.root));
    SH_CHECK(host.load().value());
    host.on_update();
    host.on_update();
    SH_CHECK(log.has("coroutine error") && log.has("coroutine boom"));
    SH_CHECK(log.has("survivor"));
    SH_CHECK(host.coroutine_count() == 0);
}

// The instruction budget applies inside coroutines too.
SH_TEST(coroutine_budget) {
    Scratch dir("corobudget");
    dir.write("scripts/game.lua", "sh.spawn(function() while true do end end)");
    Logged log;
    Host host(log.config(dir.root));
    SH_CHECK(host.load().value());
    host.on_update();  // must return
    SH_CHECK(log.has("instruction budget"));
    SH_CHECK(host.coroutine_count() == 0);
}

// ---- Lua map handlers: has_global / call_global (the dispatcher's primitives) ----

SH_TEST(handler_globals) {
    Scratch dir("globals");
    dir.write("scripts/game.lua", R"lua(
        calls = 0
        function MapEvent_Test() calls = calls + 1 end
        function MapEvent_Report() return calls end
    )lua");
    Logged log;
    Host host(log.config(dir.root));
    SH_CHECK(host.load().value());
    SH_CHECK(host.has_global("MapEvent_Test"));
    SH_CHECK(!host.has_global("MapEvent_Missing"));
    SH_CHECK(!host.has_global("calls"));  // a number, not a function
    SH_CHECK(host.call_global("MapEvent_Test"));
    SH_CHECK(host.call_global("MapEvent_Test"));
    SH_CHECK(host.call_global("MapEvent_Report"));  // returns 2 now; no error
    SH_CHECK(!host.call_global("NoSuchHandler"));  // missing: false, no crash
}

// The end-to-end dispatcher: handlers.json names the slot, events.lua defines the
// function, ShScript_CallMapHandler drives it instead of the C++ handler.
SH_TEST(map_handler_dispatch) {
    Scratch dir("dispatch");
    // A fake map with two handlers: one in Lua, one only named (no Lua body).
    dir.write("maps/testmap/handlers.json",
              R"({"map": "testmap", "handlers": [)"
              R"({"index": 2, "symbol": "MapEvent_Ported", "shape": "item"},)"
              R"({"index": 3, "symbol": "MapEvent_StillCpp", "shape": "script"},)"
              R"({"index": 4, "symbol": null, "shape": null}]})"
              "\n");
    dir.write("maps/testmap/events.lua", R"lua(
        ported_calls = 0
        function MapEvent_Ported()
            ported_calls = ported_calls + 1
            sh.event_common_item_take(1, 599)
        end
    )lua");

    static int common_take_calls = 0;
    ShScriptGameApi api{};
    api.event_common_item_take = [](int pickup, int flag) {
        common_take_calls++;
        SH_CHECK(pickup == 1 && flag == 599);
    };
    ShScript_SetGameApi(&api);
    ShScript_Boot(dir.root.string().c_str(), nullptr);
    ShScript_OnMapLoad("testmap");
    SH_CHECK(ShScript_CallMapHandler(2) == 1);  // Lua ran
    SH_CHECK(ShScript_CallMapHandler(2) == 1);  // per tick, like the C++ handler
    SH_CHECK(common_take_calls == 2);
    SH_CHECK(ShScript_CallMapHandler(3) == 0);  // named but no Lua body: C++ keeps it
    SH_CHECK(ShScript_CallMapHandler(4) == 0);  // NULL slot
    SH_CHECK(ShScript_CallMapHandler(99) == 0); // out of the table
    ShScript_OnMapLoad("");                      // unloads the map script
    SH_CHECK(ShScript_CallMapHandler(2) == 0);
}

SH_TEST(game_api_functions) {
    Scratch dir("api");
    // A flag store the test drives through the binding: the values are visible both ways.
    // GameApi holds plain function pointers, so the state lives in statics.
    static std::vector<int> flags(600, 0);
    static std::vector<std::pair<int, int>> given;
    flags.assign(flags.size(), 0);
    given.clear();

    dir.write("scripts/game.lua", R"lua(
        function on_boot()
            sh.flag_set(64, true)
            if not sh.flag_get(64) then error("flag_set did not stick") end
            sh.flag_set(64, false)
            if sh.flag_get(64) then error("flag_get did not see the clear") end
            sh.give_item(160, 15)
            local x, y, z = sh.player_pos()
            if x ~= 11 or y ~= 22 or z ~= 33 then error("player_pos wrong") end
            sh.log("api ok")
        end
    )lua");
    Logged log;
    HostConfig config = log.config(dir.root);
    config.game.flag_get = [](int id) { return flags.at(id); };
    config.game.flag_set = [](int id, int v) { flags.at(id) = v; };
    config.game.give_item = [](int id, int c) { given.emplace_back(id, c); };
    config.game.player_pos = [](int out[3]) { out[0] = 11; out[1] = 22; out[2] = 33; };
    Host host(std::move(config));
    SH_CHECK(host.load().value());
    host.on_boot();
    SH_CHECK(log.has("api ok"));
    SH_CHECK((given == std::vector<std::pair<int, int>>{{160, 15}}));

    // Without a binding the function errors in the script; the host survives.
    dir.write("scripts/game.lua", "function on_boot() sh.flag_get(1) end");
    Logged log2;
    Host bare(log2.config(dir.root));
    SH_CHECK(bare.load().value());
    bare.on_boot();
    SH_CHECK(log2.has("not available"));
}

SH_TEST(map_script_via_load_file) {
    Scratch dir("mapscript");
    // load_file on a missing file: false, no error; on an existing one: runs and hooks work.
    Logged log;
    Host host(log.config(dir.root));
    SH_CHECK(host.load_file(dir.root / "maps" / "map1_s02" / "events.lua"));
    SH_CHECK(!host.load_file(dir.root / "maps" / "map1_s02" / "events.lua").value());

    dir.write("maps/map1_s02/events.lua", R"lua(
        function on_boot() sh.not_a_function() end
    )lua");
    Logged log2;
    Host host2(log2.config(dir.root));
    SH_CHECK(host2.load_file(dir.root / "maps" / "map1_s02" / "events.lua").value());
    host2.on_boot();
    SH_CHECK(log2.has("on_boot error"));  // a nil API entry errors in the script, logged

    dir.write("maps/map1_s02/events.lua", "function on_update() sh.log('tick') end");
    Logged log3;
    Host host3(log3.config(dir.root));
    SH_CHECK(host3.load_file(dir.root / "maps" / "map1_s02" / "events.lua").value());
    host3.on_update();
    SH_CHECK(log3.has("tick"));
}
