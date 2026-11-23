// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "framework/sh_test.h"

#include "engine/items/quick_heal.h"

#include <fstream>
#include <iterator>
#include <string>

// The shipped gamedata/config/quick_actions.json "quick_heal" against the choices of the built-in policy
// (fixed tiers, the values below).
namespace {

const sh::items::QuickActionsData& shipped() {
    static const sh::items::QuickActionsData s_data = [] {
        std::ifstream in(std::string(SH_PROJECT_ROOT) + "/gamedata/config/quick_actions.json");
        const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        auto parsed = sh::items::QuickActionsData::parse(text);
        return parsed ? parsed.value() : sh::items::QuickActionsData{};
    }();
    return s_data;
}

std::string pick(int hp, bool drink, bool kit, bool ampoule) {
    return sh::items::quick_heal_pick(shipped(), hp, [&](std::string_view item) {
        return (item == "HealthDrink" && drink) || (item == "FirstAidKit" && kit) || (item == "Ampoule" && ampoule);
    });
}

}  // namespace

SH_TEST(quick_heal_dead_or_full_heals_nothing) {
    SH_CHECK(pick(0, true, true, true) == "");
    SH_CHECK(pick(-3, true, true, true) == "");
    SH_CHECK(pick(100, true, true, true) == "");
    SH_CHECK(pick(150, true, true, true) == "");
}

SH_TEST(quick_heal_critical_prefers_strongest) {
    // hp < 10: ampoule, then kit, then drink.
    SH_CHECK(pick(9, true, true, true) == "Ampoule");
    SH_CHECK(pick(1, true, true, false) == "FirstAidKit");
    SH_CHECK(pick(5, true, false, false) == "HealthDrink");
    SH_CHECK(pick(9, false, false, false) == "");
}

SH_TEST(quick_heal_light_prefers_drink) {
    // 10 <= hp, deficit <= 40: drink first, then kit, then ampoule.
    SH_CHECK(pick(60, true, true, true) == "HealthDrink");
    SH_CHECK(pick(99, true, true, true) == "HealthDrink");
    SH_CHECK(pick(60, false, true, true) == "FirstAidKit");
    SH_CHECK(pick(80, false, false, true) == "Ampoule");
    SH_CHECK(pick(70, false, false, false) == "");
}

SH_TEST(quick_heal_moderate_prefers_kit) {
    // deficit > 40: kit first, then drink, then ampoule.
    SH_CHECK(pick(50, true, true, true) == "FirstAidKit");
    SH_CHECK(pick(10, true, true, true) == "FirstAidKit");
    SH_CHECK(pick(50, true, false, true) == "HealthDrink");
    SH_CHECK(pick(30, false, false, true) == "Ampoule");
    SH_CHECK(pick(59, false, true, false) == "FirstAidKit");
}
