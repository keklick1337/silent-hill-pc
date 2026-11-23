// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The data of the PC's quick actions (gamedata/config/quick_actions.json): the weapons
// of the number keys and of the cycle bind, and which healing item the quick heal spends for the current health.
// Items by name; the inventory, the restore amounts (gamedata/config/gameplay.json, health_items) and the feedback
// stay with the caller.

#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "formats/common/status.h"

namespace sh::items {

// One band of health: the first band whose condition holds picks the first owned item of its order.
struct QuickHealTier {
    int below_health = -1;     // "below_health": health under this (-1: no such condition)
    int missing_at_most = -1;  // "missing_at_most": at most this much below full health (-1: none)
    std::vector<std::string> order;
};

struct QuickActionsData {
    std::vector<std::string> weapon_keys;   // "weapon_keys": the number keys 1..0 and '-' in order; the wheel walks it
    std::vector<std::string> weapon_cycle;  // "weapon_cycle": the cycle bind's order
    int full_health = 100;                  // "quick_heal.full_health": no heal at or over it
    std::vector<QuickHealTier> heal_tiers;  // "quick_heal.tiers"
    // "quick_heal.report": the healing items the toast after a heal names ("Used <name>") and counts ("<count_label>
    // <n>"), in its order.
    struct Report {
        std::string item, name, count_label;
    };
    std::vector<Report> heal_report;

    static Result<QuickActionsData> parse(std::string_view json);
};

// The healing item to spend at `hp` whole health points (nothing at or below 0 or at full health); `owned` says
// whether the inventory holds a usable one of an item. Empty when none fits.
std::string quick_heal_pick(const QuickActionsData& data, int hp, const std::function<bool(std::string_view)>& owned);

}  // namespace sh::items
