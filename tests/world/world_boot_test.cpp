// The world module's start and diagnostics (engine/world/world.h): the boot log, the stats line steps and the
// collision wireframe of a cell.

#include "engine/world/world.h"
#include "framework/sh_test.h"

#include <string>
#include <vector>

namespace {

namespace w = sh::world;

}  // namespace

SH_TEST(world_boot_logs_once) {
    std::vector<std::string> lines;
    w::World world;
    SH_CHECK(!world.booted());
    world.boot(nullptr, [&lines](int level, const std::string& m) { lines.push_back(std::to_string(level) + m); });
    SH_CHECK(world.booted());
    SH_CHECK(lines.size() == 1 && lines[0].rfind("0[WORLD] ", 0) == 0);
    world.boot(nullptr, [&lines](int, const std::string& m) { lines.push_back(m); });
    SH_CHECK(lines.size() == 1);
}

SH_TEST(world_stats_steps) {
    int logged = 0;
    w::World world;
    world.boot(nullptr, [&logged](int, const std::string&) { ++logged; });
    logged = 0;
    world.stats().ground_queries = 1023;
    world.log_stats_on_growth();
    SH_CHECK(logged == 0);
    world.stats().ray_queries = 1;
    world.log_stats_on_growth();
    SH_CHECK(logged == 1);
    world.stats().move_queries = 4096 - 1024 - 1;
    world.log_stats_on_growth();
    SH_CHECK(logged == 1);
    world.stats().move_queries += 1;
    world.log_stats_on_growth();
    SH_CHECK(logged == 2);
}
