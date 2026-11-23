// The editor's position snippets (engine/devtools/position_snippets.h): metres that
// read back as the exact Q12 point under the float truncation of Q12(), and the Lua and JSON forms.

#include "engine/devtools/position_snippets.h"

#include "framework/sh_test.h"

#include <cstdlib>
#include <string>

namespace {

namespace dt = sh::devtools;

bool contains(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }

}  // namespace

SH_TEST(devtools_snippets_q12_truncates_in_float) {
    // 134.6f reads 551321 in float; negatives truncate toward zero.
    SH_CHECK(dt::q12_of_metres(134.6f) == 551321);
    SH_CHECK(dt::q12_of_metres(-1.5f) == -6144);
    SH_CHECK(dt::q12_of_metres(-0.0001f) == 0);
}

SH_TEST(devtools_snippets_metres_round_trip) {
    for (std::int32_t q = -300000; q <= 300000; q += 37) {
        const std::string text = dt::metres_text(q);
        SH_CHECK(dt::q12_of_metres(std::strtof(text.c_str(), nullptr)) == q);
    }
    SH_CHECK(dt::metres_text(4096) == "1.0");
    SH_CHECK(dt::metres_text(-245760) == "-60.0");
}

SH_TEST(devtools_snippets_forms) {
    const dt::Q12Point p = dt::q12_from_geometry(-15360.0, 0.0, -16307.2);
    SH_CHECK(p.x == -245760 && p.y == 0 && p.z == -260915);
    const auto snippets = dt::position_snippets(p, 1024);
    SH_CHECK(snippets.size() == 6);
    SH_CHECK(contains(snippets[0].text, "sh.player_set_pos(-245760, 0, -260915)"));
    SH_CHECK(contains(snippets[1].text, "sh.vec.set(0, -245760, 0, -260915)"));
    SH_CHECK(contains(snippets[2].text, "sh.chara_spawn(\"GreyChild\", 0, -245760, -260915, 1024, 3)"));
    SH_CHECK(contains(snippets[4].text, "\"position\": [-60.0, 0.0, "));
    SH_CHECK(contains(snippets[5].text, "sh.camera_pos(-60.0, 0.0, "));
}
