// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
#include "engine/items/quick_heal.h"

#include "formats/common/json.h"

namespace sh::items {

namespace {

using sh::json::Value;

std::vector<std::string> names(const Value* list) {
    std::vector<std::string> out;
    if (list != nullptr && list->is_array()) {
        for (const Value& v : list->array()) {
            if (v.is_string()) {
                out.push_back(v.string());
            }
        }
    }
    return out;
}

int integer(const Value& parent, const char* key, int fallback) {
    const Value* v = parent.find(key);
    return v != nullptr && v->is_integer() ? static_cast<int>(v->integer()) : fallback;
}

}  // namespace

Result<QuickActionsData> QuickActionsData::parse(std::string_view json) {
    const Result<Value> doc = sh::json::parse(json);
    if (!doc) {
        return Result<QuickActionsData>::fail("quick_actions.json: " + doc.error());
    }
    if (!doc.value().is_object()) {
        return Result<QuickActionsData>::fail("quick_actions.json: not an object");
    }
    QuickActionsData data;
    data.weapon_keys = names(doc.value().find("weapon_keys"));
    data.weapon_cycle = names(doc.value().find("weapon_cycle"));
    if (const Value* heal = doc.value().find("quick_heal"); heal != nullptr && heal->is_object()) {
        data.full_health = integer(*heal, "full_health", data.full_health);
        if (const Value* tiers = heal->find("tiers"); tiers != nullptr && tiers->is_array()) {
            for (const Value& t : tiers->array()) {
                if (!t.is_object()) {
                    continue;
                }
                QuickHealTier tier;
                tier.below_health = integer(t, "below_health", -1);
                tier.missing_at_most = integer(t, "missing_at_most", -1);
                tier.order = names(t.find("order"));
                data.heal_tiers.push_back(std::move(tier));
            }
        }
        if (const Value* report = heal->find("report"); report != nullptr && report->is_array()) {
            for (const Value& r : report->array()) {
                const Value* item = r.is_object() ? r.find("item") : nullptr;
                const Value* name = r.is_object() ? r.find("name") : nullptr;
                const Value* label = r.is_object() ? r.find("count_label") : nullptr;
                if (item != nullptr && item->is_string()) {
                    data.heal_report.push_back(
                        {item->string(), name != nullptr && name->is_string() ? name->string() : item->string(),
                         label != nullptr && label->is_string() ? label->string() : item->string()});
                }
            }
        }
    }
    return Result<QuickActionsData>::ok(std::move(data));
}

std::string quick_heal_pick(const QuickActionsData& data, int hp, const std::function<bool(std::string_view)>& owned) {
    if (hp <= 0 || hp >= data.full_health) {
        return {};  // dead or already full
    }
    for (const QuickHealTier& tier : data.heal_tiers) {
        if (tier.below_health >= 0 && hp >= tier.below_health) {
            continue;
        }
        if (tier.missing_at_most >= 0 && data.full_health - hp > tier.missing_at_most) {
            continue;
        }
        for (const std::string& item : tier.order) {
            if (owned(item)) {
                return item;
            }
        }
        return {};
    }
    return {};
}

}  // namespace sh::items
