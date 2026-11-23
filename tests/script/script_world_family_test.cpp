// The mods' world queries (bind_world.cpp): sh.game.difficulty, sh.player.facing, sh.world.walkable /
// nearest_walkable and sh.entity.spawn{...} against a fake WorldQueryHost; the flow reads of sh.game against the
// session.

#include "framework/sh_test.h"

#include "engine/game_state/session.h"
#include "engine/script/bind_world.h"
#include "engine/script/host_interfaces.h"
#include "engine/script/script_host.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;

struct FakeWorld final : sh::script::WorldQueryHost {
    bool game = true;
    int diff = 1;
    std::vector<std::string> spawns;
    bool in_game() override { return game; }
    int difficulty() override { return diff; }
    int player_facing() override { return 4096 + 1024; }
    // Ground everywhere with x >= 0; a wall at z == 8192 cuts walks across it.
    bool walkable(int x, int z, const int* from, int* y) override {
        if (x < 0) return false;
        *y = -x / 16;
        if (from != nullptr && ((from[1] < 8192) != (z < 8192))) return false;
        return true;
    }
    int spawn(const char* chara, int x, int z, int rot, const char* ai, int state, char* err, int cap) override {
        if (std::strcmp(chara, "Groaner") != 0) {
            std::snprintf(err, static_cast<std::size_t>(cap), "unknown character '%s'", chara);
            return -1;
        }
        spawns.push_back(std::string(chara) + " " + std::to_string(x) + " " + std::to_string(z) + " " +
                         std::to_string(rot) + " " + ai + " " + std::to_string(state));
        return static_cast<int>(spawns.size());
    }
};

}  // namespace

SH_TEST(script_world_nearest_walkable_search) {
    // Only x >= 4096 is walkable: from (0, 0) the nearest point is one metre to +x.
    const auto found = sh::script::nearest_walkable(0, 0, 8192, 1024, [](int x, int) { return x >= 4096; });
    SH_CHECK(found.has_value());
    if (found) {
        // The first ring that reaches x >= 4096 (rings are 1024 apart).
        SH_CHECK(found->x >= 4096);
        const double r = std::hypot(static_cast<double>(found->x), static_cast<double>(found->z));
        SH_CHECK(r <= 5120 + 1);
    }
    SH_CHECK(!sh::script::nearest_walkable(0, 0, 2048, 1024, [](int x, int) { return x >= 4096; }).has_value());
    const auto here = sh::script::nearest_walkable(5, 6, 0, 1024, [](int, int) { return true; });
    SH_CHECK(here && here->x == 5 && here->z == 6);
}

SH_TEST(script_world_family_calls) {
    const fs::path root = fs::path(SH_PROJECT_ROOT) / ".tmp" / "script" / "world_family";
    fs::remove_all(root);
    fs::create_directories(root / "scripts");
    std::ofstream(root / "scripts" / "game.lua", std::ios::binary) << R"(
function on_boot()
  sh.log("diff " .. tostring(sh.game.difficulty()))
  sh.log("facing " .. sh.player.facing())
  local ok, y = sh.world.walkable(1600, 0)
  sh.log("walk " .. tostring(ok) .. " " .. y)
  sh.log("wall " .. tostring((sh.world.walkable(100, 9000, 100, 0))))
  sh.log("void " .. tostring((sh.world.walkable(-5, 0))))
  local x, z = sh.world.nearest_walkable(-1024, 0, 4096)
  sh.log("near " .. tostring(x ~= nil and x >= 0))
  sh.log("none " .. tostring(sh.world.nearest_walkable(-100000, 0, 4096)))
  local slot = sh.entity.spawn{chara = "Groaner", x = 4096, z = 8192, rot = 2048, ai = "full"}
  sh.log("slot " .. slot)
  local bad, err = sh.entity.spawn{chara = "Nobody", x = 0, z = 0}
  sh.log("bad " .. tostring(bad) .. " " .. err)
  if pcall(sh.entity.spawn, {chara = "Groaner", x = 1.5, z = 0}) then sh.log("BAD float") end
  if pcall(sh.entity.spawn, {chara = "Groaner", z = 0}) then sh.log("BAD missing") end
  if pcall(sh.world.walkable, 1.5, 0) then sh.log("BAD walk float") end
  sh.log("done")
end
)";
    FakeWorld world;
    sh::script::set_world_query_host(&world);
    std::vector<std::string> lines;
    sh::script::HostConfig c;
    c.gamedata_dir = root;
    c.log = [&lines](int, const std::string& m) { lines.push_back(m); };
    {
        sh::script::Host host(c);
        SH_CHECK(host.load().value());
        host.on_boot();
    }
    world.game = false;
    sh::script::set_world_query_host(nullptr);
    std::string all;
    for (const auto& l : lines) all += l + "\n";
    const auto has = [&](const char* s) { return all.find(s) != std::string::npos; };
    SH_CHECK(has("diff hard"));
    SH_CHECK(has("facing 1024"));
    SH_CHECK(has("walk true -100"));
    SH_CHECK(has("wall false"));
    SH_CHECK(has("void false"));
    SH_CHECK(has("near true"));
    SH_CHECK(has("none nil"));
    SH_CHECK(has("slot 1"));
    SH_CHECK(has("bad nil unknown character 'Nobody'"));
    SH_CHECK(!has("BAD"));
    SH_CHECK(has("done"));
    SH_CHECK((world.spawns == std::vector<std::string>{"Groaner 4096 8192 2048 full -1"}));
    if (!has("done") || has("BAD")) ::sh::test::record_failure(__FILE__, __LINE__, all.c_str());
    std::error_code ec;
    fs::remove_all(root, ec);
}

SH_TEST(script_game_flow_reads) {
    const fs::path root = fs::path(SH_PROJECT_ROOT) / ".tmp" / "script" / "game_flow";
    fs::remove_all(root);
    fs::create_directories(root / "scripts");
    std::ofstream(root / "scripts" / "game.lua", std::ios::binary) << R"(
function on_boot()
  sh.log("screen " .. tostring(sh.game.screen()) .. " state " .. tostring(sh.game.state()))
  sh.log("prev " .. tostring(sh.game.screen_prev()))
  sh.log("steps " .. sh.game.screen_step() .. " " .. sh.game.state_step(2))
  sh.log("cutscene " .. tostring(sh.game.cutscene()))
  if pcall(sh.game.state_step, 3) then sh.log("BAD level") end
  sh.log("done")
end
)";
    sh::game::Session& s = sh::game::session();
    const sh::game::SessionFlow saved_flow = s.system.flow;
    const sh::game::GameBlock saved_game = s.game;
    s.game.game_state = sh::game::game_state::kInGame;
    s.game.game_state_previous = sh::game::game_state::kMainLoadScreen;
    s.system.flow.sys_state = sh::game::sys_state::kPaused;
    s.game.game_steps[0] = 4;
    s.system.flow.sys_steps[2] = 9;
    s.system.flow.cutscene_border = sh::game::kCutsceneBorderNone;
    s.set_sys_flag(sh::game::sys_flag::kCutscene, false);
    std::vector<std::string> lines;
    sh::script::HostConfig c;
    c.gamedata_dir = root;
    c.log = [&lines](int, const std::string& m) { lines.push_back(m); };
    {
        sh::script::Host host(c);
        SH_CHECK(host.load().value());
        host.on_boot();
    }
    s.system.flow = saved_flow;
    s.game = saved_game;
    std::string all;
    for (const auto& l : lines) all += l + "\n";
    const auto has = [&](const char* t) { return all.find(t) != std::string::npos; };
    SH_CHECK(has("screen InGame state GamePaused"));
    SH_CHECK(has("steps 4 9"));
    SH_CHECK(has("prev MainLoadScreen"));
    SH_CHECK(has("cutscene false"));
    SH_CHECK(!has("BAD"));
    if (!has("done") || has("BAD")) ::sh::test::record_failure(__FILE__, __LINE__, all.c_str());
    std::error_code ec;
    fs::remove_all(root, ec);
}

