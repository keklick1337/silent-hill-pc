// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/items/quick_actions.h"

#include <string>
#include <vector>
#include <cstdio>

#include "engine/items/weapon_cycle.h"

namespace sh::items {

namespace {

// An order entry whose item the data does not define; never owned, never matches the item in hand.
constexpr int kMissing = -1;

using Order = std::vector<int>;

// An order of gamedata/config/quick_actions.json as item numbers.
Order resolve(const std::vector<std::string>& names, QuickActionsHost& host) {
    Order order;
    for (const std::string& name : names) {
        const int n = host.item_number(name);
        order.push_back(n >= 0 ? n : kMissing);
    }
    return order;
}

int index_of(const Order& order, int item) {
    for (int i = 0; i < static_cast<int>(order.size()); ++i) {
        if (order[i] != kMissing && order[i] == item) {
            return i;
        }
    }
    return -1;
}

int owned(void* ctx, int item) {
    return item != kMissing && find_item_slot(*static_cast<QuickActionsHost*>(ctx), item) >= 0;
}

// Amount in the first live slot holding `item`, 0 when absent. A spent healing slot keeps its item until reused,
// so ownership of a healing item is a positive amount, not a matching slot.
int item_amount(QuickActionsHost& host, int item) {
    const int slot = find_item_slot(host, item);
    return slot >= 0 ? host.slot_amount(slot) : 0;
}

constexpr int kSlotCapacity = 40;

}  // namespace

// Only the live slots count: past the slot count the save keeps whatever a previous game left there.
int find_item_slot(QuickActionsHost& host, int item) {
    if (item < 0) {
        return -1;
    }
    const int count = host.slot_count();
    for (int i = 0; i < count && i < kSlotCapacity; ++i) {
        if (host.slot_item(i) == item) {
            return i;
        }
    }
    return -1;
}

void QuickActions::equip_slot(int item, int slot, QuickActionsHost& host) {
    const int ammo = host.slot_amount(slot);
    int reserve = 0;
    if (host.is_gun(item)) {
        reserve = item_amount(host, host.ammo_item(item));
    }
    host.equip(item, slot, ammo, reserve);
}

bool QuickActions::equip_owned(int item, QuickActionsHost& host) {
    const int slot = find_item_slot(host, item);
    if (slot < 0 || !host.action_safe()) {
        return false;
    }
    equip_slot(item, slot, host);
    return true;
}

void QuickActions::cycle_weapons(QuickActionsHost& host) {
    const Order order = resolve(host.quick_data().weapon_cycle, host);
    const int current = index_of(order, host.equipped_item());
    const int next = weapon_cycle_next(order.data(), order.size(), current, 1, owned, &host);
    if (next >= 0) {
        const int slot = find_item_slot(host, order[next]);
        if (slot >= 0) {
            equip_slot(order[next], slot, host);
        }
    }
}

void QuickActions::quick_weapons(const QuickActionPresses& presses, QuickActionsHost& host) {
    if (!host.quick_weapons_enabled() || !host.action_safe() || host.console_open() || host.debug_menu_open()) {
        return;
    }
    const Order order = resolve(host.quick_data().weapon_keys, host);
    int target = presses.weapon_key;
    if (target == kQuickWeaponCount - 1 && host.keyframe_viewer()) {
        target = -1;
    }
    if (target >= 0 && target < static_cast<int>(order.size())) {
        if (order[target] == kMissing) {
            return;
        }
        const int slot = find_item_slot(host, order[target]);
        if (slot >= 0 && host.equipped_item() != order[target]) {
            equip_slot(order[target], slot, host);
        }
        return;
    }
    if (presses.wheel == 0) {
        return;
    }
    int steps = presses.wheel > 0 ? presses.wheel : -presses.wheel;
    const int direction = presses.wheel > 0 ? -1 : 1;
    if (steps > static_cast<int>(order.size())) {
        steps = static_cast<int>(order.size());
    }
    const int equipped = host.equipped_item();
    int current = equipped;
    for (int j = 0; j < steps; ++j) {
        int index = index_of(order, current);
        // An item outside the list starts the walk just before the front going forward, at the front going back.
        if (index < 0) {
            index = direction > 0 ? -1 : 0;
        }
        index = weapon_cycle_next(order.data(), order.size(), index, direction, owned, &host);
        if (index >= 0) {
            current = order[index];
        }
    }
    if (current != equipped) {
        const int slot = find_item_slot(host, current);
        if (slot >= 0) {
            equip_slot(current, slot, host);
        }
    }
}

void QuickActions::quick_heal(QuickActionsHost& host) {
    std::int32_t health = host.health();
    const int hp = health >> 12;
    const QuickActionsData& data = host.quick_data();
    // gamedata/config/quick_actions.json "quick_heal": only the items its tiers name may ever be spent here.
    const std::string pick = quick_heal_pick(data, hp, [&](std::string_view item) {
        return item_amount(host, host.item_number(item)) > 0;
    });
    const int chosen = pick.empty() ? -1 : host.item_number(pick);
    if (chosen < 0) {
        return;
    }

    // One line per use: which slot paid and the whole live inventory, to trace a heal "with no healing items".
    {
        std::string inv;
        const int count = host.slot_count();
        char piece[32];
        for (int i = 0; i < count && i < kSlotCapacity && inv.size() < 256 - 12; ++i) {
            std::snprintf(piece, sizeof(piece), " %d:%d", host.slot_item(i), host.slot_amount(i));
            inv += piece;
        }
        char line[400];
        std::snprintf(line, sizeof(line), "[QUICKHEAL] hp=%d chose item %d (slot %d) | slots=%d:%s", hp, chosen,
                      find_item_slot(host, chosen), count, inv.c_str());
        host.log(line);
    }

    std::int32_t amount = host.heal_amount(chosen);
    if (!host.quick_heal_allowed(chosen, health, amount)) {
        return;
    }

    // Spend first and heal only when the item really left the inventory.
    if (!host.spend_item(chosen)) {
        char line[128];
        std::snprintf(line, sizeof(line),
                      "[QUICKHEAL] item %d looked owned but Player_ItemRemove found none -- no heal", chosen);
        host.log(line);
        return;
    }

    health += amount;
    if (host.fills_saturation(chosen)) {
        host.set_saturation(host.max_saturation());
    }
    const std::int32_t max = host.max_health();
    host.set_health(health < 0 ? 0 : (health > max ? max : health));
    host.heal_feedback();

    // Quick heal picks the item for the player, with the inventory closed: say what was spent and what is left.
    std::string name = pick;
    std::string counts;
    for (const QuickActionsData::Report& r : data.heal_report) {
        if (r.item == pick) {
            name = r.name;
        }
        counts += (counts.empty() ? "" : ", ") + r.count_label + " " +
                  std::to_string(item_amount(host, host.item_number(r.item)));
    }
    host.toast("Used " + name + "  -  " + counts);
}

void QuickActions::frame(const QuickActionPresses& presses, QuickActionsHost& host) {
    // Sampled every frame so the bind's state stays current; the short latch carries a press over a frame the
    // combat machine cannot act on (recoil, the frame after a reload).
    reload_.sample(presses.reload, host.reload_possible(), host.frame_time(), kReloadWindow);
    if (!host.player_in_control()) {
        reload_.clear();
    }

    quick_weapons(presses, host);
    // Typing in the console presses these keys too.
    if (host.console_open()) {
        host.set_quick_turn_request(false);
        reload_.set_active(false);
        return;
    }
    if (!host.action_safe()) {
        host.set_quick_turn_request(false);
        return;
    }
    if (presses.cycle_weapons) {
        cycle_weapons(host);
    }
    if (presses.quick_heal) {
        quick_heal(host);
    }
    if (presses.flashlight) {
        host.toggle_flashlight();
    }
    // Assigned every safe frame, never latched: a press the current state ignores must not turn later.
    host.set_quick_turn_request(presses.quick_turn);
}

}  // namespace sh::items
