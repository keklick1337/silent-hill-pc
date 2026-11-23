#include "framework/sh_test.h"
#include "engine/gameflow/warp_story.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

// The story state of a map warp (src/engine/gameflow/warp_story.*): chapters before the map's own give their
// flags, a map in no chapter gets all of them, and the shipped data keeps the cafe exit out of map2_s00's own warp.

namespace {

namespace gf = sh::gameflow;

std::string read_file(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

}  // namespace

SH_TEST(warp_story_chapters_give_earlier_flags) {
    const auto r = gf::WarpStory::parse(R"({"chapters": [
        {"name": "A", "maps": ["m0"], "done_flags": ["F1", "F2"]},
        {"name": "B", "maps": ["m1", "m2"], "done_flags": ["F3", "F1"]},
        {"name": "C", "maps": ["m3"]}]})");
    SH_CHECK(static_cast<bool>(r));
    const gf::WarpStory& s = r.value();
    SH_CHECK(s.flags_for("m0").empty());
    SH_CHECK((s.flags_for("m2") == std::vector<std::string>{"F1", "F2"}));
    SH_CHECK((s.flags_for("m3") == std::vector<std::string>{"F1", "F2", "F3"}));
    SH_CHECK((s.flags_for("unlisted") == std::vector<std::string>{"F1", "F2", "F3"}));
    SH_CHECK(gf::WarpStory::builtin().flags_for("m3").empty());
}

SH_TEST(warp_story_rejects_bad_data) {
    SH_CHECK(!gf::WarpStory::parse("{}"));
    SH_CHECK(!gf::WarpStory::parse(R"({"chapters": [{"maps": ["m0"]}]})"));
    SH_CHECK(!gf::WarpStory::parse(R"({"chapters": [{"name": "A"}]})"));
    SH_CHECK(!gf::WarpStory::parse(R"({"chapters": [{"name": "A", "maps": ["m0"], "done_flags": [1]}]})"));
    SH_CHECK(!gf::WarpStory::parse(R"({"chapters": [{"name": "A", "maps": ["m0"]}, {"name": "B", "maps": ["m0"]}]})"));
}

SH_TEST(warp_story_shipped_cafe_exit) {
    const auto r =
        gf::WarpStory::parse(read_file(std::filesystem::path(SH_PROJECT_ROOT) / "gamedata/config/warp_story.json"));
    SH_CHECK(static_cast<bool>(r));
    const gf::WarpStory& s = r.value();
    // The town's own warp still plays the scene at the cafe door; the school after the boss starts past it.
    SH_CHECK(s.flags_for("map2_s00").empty());
    SH_CHECK(s.flags_for("map0_s00").empty());
    const std::vector<std::string> school = s.flags_for("map1_s06");
    SH_CHECK(!school.empty());
    const auto has = [](const std::vector<std::string>& v, const char* f) {
        return std::find(v.begin(), v.end(), f) != v.end();
    };
    SH_CHECK(has(school, "M2S00_CafeExitCutsceneSeen"));
    SH_CHECK(!has(school, "M1S06_WokeUpAfterBoss"));
    // Later chapters carry the earlier ones: the amusement park starts after the school, the hospital and the resort.
    const std::vector<std::string> park = s.flags_for("map6_s04");
    SH_CHECK(has(park, "M2S00_CafeExitCutsceneSeen"));
    SH_CHECK(has(park, "M1S06_WokeUpAfterBoss"));
    SH_CHECK(!has(park, "M5S03_KaufmannHasMotorcycleVial"));  // a side story choice, not story progress
    SH_CHECK(s.flags_for("map7_s03").size() > park.size());
}
