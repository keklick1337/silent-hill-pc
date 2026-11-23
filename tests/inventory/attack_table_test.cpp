// SPDX-License-Identifier: GPL-3.0-or-later
// The enemies' rows of gamedata/config/attacks.json carry names; scripts resolve them to the row numbers the
// original packed, so the names must give exactly those rows.

#include "engine/inventory/game/attack_table.h"

#include <filesystem>
#include <utility>

#include "framework/sh_test.h"

namespace {

namespace fs = std::filesystem;
namespace inv = sh::inventory::game;

}  // namespace

SH_TEST(attack_table_names_give_the_original_rows) {
    inv::load_attack_table(fs::path(SH_PROJECT_ROOT) / "gamedata", {});
    const std::pair<const char*, int> rows[] = {
        {"AirScreamerStandBite", 40}, {"AirScreamerHoverBite", 41}, {"AirScreamerScratch", 42},
        {"GroanerJumpBite", 43}, {"HangedScratcherCeilingSlash", 44}, {"HangedScratcherSlash", 45},
        {"UnusedAttack46", 46}, {"SplitHeadSwallow", 47}, {"StalkerLunge", 48}, {"StalkerLegGrab", 49},
        {"StalkerLegGrabDrain", 50}, {"LarvalStalkerGrab", 51}, {"LarvalStalkerDrain", 52}, {"CreeperBite", 53},
        {"RomperPin", 54}, {"RomperPinDrain", 55}, {"PuppetNurseGrab", 56}, {"PuppetNurseSwing", 57},
        {"PuppetDoctorSwing", 58}, {"FloatstingerSting", 59}, {"FloatstingerAcid", 60}, {"TwinfeelerStrike", 61},
        {"TwinfeelerAcid", 62}, {"CybilHandgun", 63}, {"MonsterCybilStrike", 64}, {"MonsterCybilRifleStrike", 65},
        {"MonsterCybilNeckGrab", 66}, {"BossRoomLightning", 67}, {"Unknown23Shock", 68},
        {"BloodsuckerTentacle", 69},
    };
    for (const auto& [name, row] : rows) {
        SH_CHECK(inv::attack_number(name) == row);
    }
    SH_CHECK(inv::attack_number("silenthill:StalkerLunge") == 48);
    SH_CHECK(inv::attack_number("NoSuchAttack") == -1);
    SH_CHECK(inv::attack_number("") == -1);
    // The rows keep their numbers: the lunge's power is the original's.
    SH_CHECK(inv::attack_table()[48].power == 15);
}
