// SPDX-License-Identifier: GPL-3.0-or-later
// The pure parts of the DEBUG OPTIONS natives (pc_debug_binds.cpp): the lists the dropdowns show.
// Kept free of game state so a unit test can check them (tests/menu/debug_binds_test.cpp).
#pragma once

#include "engine/items/item_library.h"
#include "engine/menu/menu_model.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace sh::debug_binds {

// "silenthill:HealthDrink" -> "HealthDrink"; other namespaces keep their prefix.
inline std::string short_name(std::string_view id) {
    constexpr std::string_view kBase = "silenthill:";
    if (id.substr(0, kBase.size()) == kBase) id.remove_prefix(kBase.size());
    return std::string(id);
}

// Dropdown order of the item groups: what a developer reaches for first.
inline int group_rank(std::string_view group) {
    static constexpr std::string_view kOrder[] = {"health", "guns", "ammo", "melee", "portable", "keys", "puzzle"};
    for (int i = 0; i < static_cast<int>(std::size(kOrder)); ++i)
        if (kOrder[i] == group) return i;
    return static_cast<int>(std::size(kOrder));
}

// Every item the game can hand out (an item without a run-time number cannot be added), ordered by
// group, then name. The label is the item's text key (item.<Name>.name), localised by the view.
inline std::vector<menu::Choice> give_item_choices(const std::vector<items::ItemDef>& items) {
    std::vector<const items::ItemDef*> given;
    for (const items::ItemDef& item : items)
        if (item.number != 0) given.push_back(&item);
    std::stable_sort(given.begin(), given.end(), [](const items::ItemDef* a, const items::ItemDef* b) {
        const int ra = group_rank(a->group), rb = group_rank(b->group);
        return ra != rb ? ra < rb : a->id < b->id;
    });
    std::vector<menu::Choice> out;
    out.reserve(given.size());
    for (const items::ItemDef* item : given) out.push_back({item->id, "item." + short_name(item->id) + ".name"});
    return out;
}

// The label of a spawn category: a base one ("CommonEnemies") is the text key
// ui.debug.spawn_cat.CommonEnemies, a mod's own ("mymod:Boats") has no key and shows as it is.
inline std::string spawn_category_label(std::string_view category) {
    if (category.find(':') != std::string_view::npos) return std::string(category);
    return "ui.debug.spawn_cat." + std::string(category);
}

// The row of a character in the browsing list: its name and the AI a spawn of it gets on the current
// map with the chosen AI mode ("map", "own", "fallback"; "none" and "" leave the name bare).
inline std::string spawn_character_label(std::string_view name, std::string_view ai_run) {
    if (ai_run.empty() || ai_run == "none") return std::string(name);
    return std::string(name) + " (" + std::string(ai_run) + " AI)";
}

// The AI modes of a spawn (PcSpawnAi order) as dropdown choices; labels are text keys.
inline std::vector<menu::Choice> spawn_ai_choices() {
    std::vector<menu::Choice> out;
    for (const char* mode : {"auto", "full", "fallback", "none"})
        out.push_back({mode, std::string("ui.debug.spawn_ai.") + mode});
    return out;
}

// Flag names in the order given (bit order), empty names and repeats dropped. The label is the
// short name as it is (a literal, never a text key).
inline std::vector<menu::Choice> flag_choices(const std::vector<std::string>& names) {
    std::vector<menu::Choice> out;
    for (const std::string& name : names) {
        if (name.empty()) continue;
        if (std::any_of(out.begin(), out.end(), [&](const menu::Choice& c) { return c.value == name; })) continue;
        out.push_back({name, short_name(name)});
    }
    return out;
}

// Largest bit the flag table is searched up to (52 words of 32 flags, as the save keeps).
inline constexpr int kFlagBitCount = 52 * 32;

}  // namespace sh::debug_binds
