// Game flow data (door sound sets, game-over tips): parsing, the shipped files and the tip choice.

#include "engine/gameflow/flow_data.h"

#include <vector>

#include "framework/sh_test.h"

namespace gf = sh::gameflow;

SH_TEST(door_sounds_parse_and_default) {
    auto r = gf::DoorSounds::parse(R"({"sets":[{"open":null,"close":null},{"open":"a","close":"b","starts_loading":true}]})");
    SH_CHECK(static_cast<bool>(r));
    SH_CHECK(r.value().count() == 2);
    SH_CHECK(r.value().set(1).open == "a" && r.value().set(1).close == "b" && r.value().set(1).starts_loading);
    SH_CHECK(r.value().set(0).open.empty() && !r.value().set(0).starts_loading);
    SH_CHECK(r.value().set(7).open.empty());  // outside the list: no sounds
    SH_CHECK(!gf::DoorSounds::parse(R"({"sets":[{"open":""}]})"));
    SH_CHECK(!gf::DoorSounds::parse(R"({"sets":[]})"));
}

SH_TEST(flow_data_shipped_files_load) {
    std::vector<std::string> warnings;
    auto doors = gf::read_config_file(SH_PROJECT_ROOT "/gamedata", "", "door_sounds.json", nullptr, warnings);
    SH_CHECK(static_cast<bool>(doors));
    auto d = gf::DoorSounds::parse(doors.value());
    SH_CHECK(static_cast<bool>(d) && d.value().count() == 25 && d.value().set(7).starts_loading);
    auto tips = gf::read_config_file(SH_PROJECT_ROOT "/gamedata", "", "game_over_tips.json", nullptr, warnings);
    SH_CHECK(static_cast<bool>(tips));
    auto t = gf::GameOverTips::parse(tips.value());
    SH_CHECK(static_cast<bool>(t) && t.value().count() == 15 && t.value().tip(13).dark && !t.value().tip(12).dark);
}

SH_TEST(game_over_tip_choice) {
    auto r = gf::GameOverTips::parse(R"({"tips":[{"image":"A"},{"image":"B"},{"image":"C","dark":true}]})");
    SH_CHECK(static_cast<bool>(r));
    const gf::GameOverTips& t = r.value();
    // Lit scene, nothing seen: weights 3, 3, 1.
    SH_CHECK(t.total_weight(0, false) == 7);
    SH_CHECK(t.pick(0, false, 0) == 0 && t.pick(0, false, 2) == 0 && t.pick(0, false, 3) == 1);
    SH_CHECK(t.pick(0, false, 6) == 2);
    // Dark scene: weights 1, 1, 3; tip A seen.
    SH_CHECK(t.total_weight(1, true) == 4);
    SH_CHECK(t.pick(1, true, 0) == 1 && t.pick(1, true, 1) == 2 && t.pick(1, true, 3) == 2);
    std::uint16_t seen = 7;
    t.restart_if_all_seen(seen);
    SH_CHECK(seen == 0);
    seen = 3;
    t.restart_if_all_seen(seen);
    SH_CHECK(seen == 3);
    gf::GameOverTips::mark_seen(seen, 2);
    SH_CHECK(seen == 7);
}
