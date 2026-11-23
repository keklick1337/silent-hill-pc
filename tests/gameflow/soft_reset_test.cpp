#include "framework/golden.h"
#include "framework/sh_test.h"
#include "engine/gameflow/soft_reset.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

// The soft reset rules (src/engine/gameflow/soft_reset.*): the shipped data equals the built-ins, the
// built-ins are the buttons and times of the reset, and the blocked states are the recorded ones.

namespace {

namespace gf = sh::gameflow;

std::string read_file(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

bool same(const gf::SoftResetRules& a, const gf::SoftResetRules& b) {
    if (a.spec.hold_buttons != b.spec.hold_buttons || a.spec.hold_vblanks != b.spec.hold_vblanks ||
        a.spec.hold_name != b.spec.hold_name || a.spec.demo_vblanks != b.spec.demo_vblanks ||
        a.spec.combos.size() != b.spec.combos.size() || a.blocked_below_state != b.blocked_below_state ||
        a.blocked_steps.size() != b.blocked_steps.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.spec.combos.size(); ++i) {
        if (a.spec.combos[i].held != b.spec.combos[i].held || a.spec.combos[i].pressed != b.spec.combos[i].pressed ||
            a.spec.combos[i].name != b.spec.combos[i].name) {
            return false;
        }
    }
    for (std::size_t i = 0; i < a.blocked_steps.size(); ++i) {
        if (a.blocked_steps[i].game_state != b.blocked_steps[i].game_state ||
            a.blocked_steps[i].steps != b.blocked_steps[i].steps) {
            return false;
        }
    }
    return true;
}

}  // namespace

SH_TEST(soft_reset_shipped_equals_builtin) {
    const auto r = gf::SoftResetRules::parse(read_file(std::filesystem::path(SH_PROJECT_ROOT) / "gamedata/config/soft_reset.json"));
    SH_CHECK(static_cast<bool>(r));
    SH_CHECK(same(r.value(), gf::SoftResetRules::builtin()));
    SH_CHECK(!gf::SoftResetRules::parse("{\"hold\": {\"buttons\": [\"Nope\"], \"vblanks\": 1, \"name\": \"x\"}}"));
    SH_CHECK(!gf::SoftResetRules::parse("{\"blocked_below_state\": \"Nope\"}"));
}

// Select and Start held for two seconds, or either button combination; a demo starts after thirty seconds (60
// vertical blanks a second, the pad's button bits).
SH_TEST(soft_reset_builtin_buttons_and_times) {
    constexpr std::uint32_t select = 1u << 0, start = 1u << 3, l2 = 1u << 8, r2 = 1u << 9, l1 = 1u << 10,
                            r1 = 1u << 11, triangle = 1u << 12, square = 1u << 15;
    const gf::SoftResetRules b = gf::SoftResetRules::builtin();
    SH_CHECK(b.spec.hold_buttons == (select | start) && b.spec.hold_vblanks == 60 * 2 &&
             b.spec.hold_name == "select+start-2s" && b.spec.demo_vblanks == 60 * 30);
    SH_CHECK(b.spec.combos.size() == 2);
    SH_CHECK(b.spec.combos[0].held == (select | start | l2 | r2 | l1 | r1) &&
             b.spec.combos[0].pressed == (select | start | l2 | r2 | l1 | r1) && b.spec.combos[0].name == "combo");
    SH_CHECK(b.spec.combos[1].held == (start | triangle | square) && b.spec.combos[1].pressed == start &&
             b.spec.combos[1].name == "combo-alt");
}

SH_TEST(soft_reset_blocked_states_as_recorded) {
    const gf::SoftResetRules b = gf::SoftResetRules::builtin();
    const auto rows = sh::test::golden_rows("gameflow/soft_reset.txt", "blocked");
    SH_CHECK(rows.size() == 34 * 15);
    int differences = 0;
    for (const auto& r : rows) {
        const bool got = b.blocked(static_cast<std::int32_t>(r.num(0)), static_cast<std::int32_t>(r.num(1)));
        if (got != r.flag(2) && ++differences <= 3) {
            sh::test::golden_mismatch("gameflow/soft_reset.txt", r, got ? "1" : "0");
        }
    }
    SH_CHECK(differences == 0);
}

// The reset does nothing while the game boots and plays its logos and intro, and while a memory card screen is in
// the steps that read or write the card; everywhere else it is allowed.
SH_TEST(soft_reset_blocked_while_booting_and_saving) {
    const gf::SoftResetRules b = gf::SoftResetRules::builtin();
    constexpr int kAlternateIntro = 5, kMainMenu = 7, kLoadScreen = 8, kInGame = 11, kSaveScreen = 16;
    for (int state = 0; state < kAlternateIntro; ++state) SH_CHECK(b.blocked(state, 0) && b.blocked(state, 9));
    SH_CHECK(!b.blocked(kAlternateIntro, 0) && !b.blocked(kMainMenu, 0) && !b.blocked(kInGame, 3));
    SH_CHECK(b.blocked(kLoadScreen, 4) && !b.blocked(kLoadScreen, 3) && !b.blocked(kLoadScreen, 5));
    SH_CHECK(b.blocked(kSaveScreen, 2) && b.blocked(kSaveScreen, 3));
    SH_CHECK(!b.blocked(kSaveScreen, 1) && !b.blocked(kSaveScreen, 4));
}
