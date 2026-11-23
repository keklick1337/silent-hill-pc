// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The attack table (attack_table.h): gamedata/config/attacks.json into the game's row layout.

#include "engine/inventory/game/attack_table.h"

#include <algorithm>
#include <optional>
#include <string>

#include "engine/log/sh_log.h"
#include "engine/mods/data_merge.h"
#include "formats/common/json.h"

namespace sh::inventory::game {

namespace {

static_assert(sizeof(void*) != 4 || sizeof(AttackRow) == 24);

// The word every row links to; nothing reads it.
const std::uint32_t s_unused_word = 0;

AttackRow empty_row()
{
    AttackRow row{};
    row.unused_link = &s_unused_word;
    return row;
}

std::vector<AttackRow> s_table(kInitialAttackRows, empty_row());
std::vector<std::optional<items::WeaponAttack>> s_weapon_rows(kInitialAttackRows);
int s_row_count = kAttackRows;
std::vector<std::string> s_names(kInitialAttackRows);

void grow_to(std::size_t rows)
{
    if (rows > s_table.size())
    {
        s_table.resize(rows, empty_row());
        s_weapon_rows.resize(rows);
        s_names.resize(rows);
    }
}

std::int64_t field(const json::Value& row, std::string_view key)
{
    const json::Value* v = row.find(key);
    return v != nullptr && v->is_integer() ? v->integer() : 0;
}

}  // namespace

std::vector<AttackRow>& attack_table()
{
    return s_table;
}

int attack_row_count()
{
    return s_row_count;
}

void load_attack_table(const std::filesystem::path& gamedata_dir, const std::filesystem::path& mods_dir)
{
    // A second read (`mods reload`) starts from an empty table: a row a mod no longer sets must not keep its values.
    s_table.assign(kInitialAttackRows, empty_row());
    s_weapon_rows.assign(kInitialAttackRows, std::nullopt);
    s_names.assign(kInitialAttackRows, std::string());
    s_row_count = kAttackRows;
    std::string source;
    const Result<std::string> text =
        mods::read_data_text(gamedata_dir, mods_dir, std::filesystem::path("config") / "attacks.json", &source);
    if (!text)
    {
        SH_ERR("[ITEMS] %s - the attack table stays empty", text.error().c_str());
        return;
    }
    const Result<json::Value> doc = json::parse(text.value());
    const json::Value* rows = doc ? doc.value().find("rows") : nullptr;
    if (rows == nullptr || !rows->is_array())
    {
        SH_ERR("[ITEMS] %s: no \"rows\" array - the attack table stays empty", source.c_str());
        return;
    }
    int loaded = 0;
    for (const json::Value& row : rows->array())
    {
        const std::int64_t index = field(row, "index");
        if (index < 0 || index >= kAttackRows)
        {
            SH_WARN("[ITEMS] %s: attack row %lld is past the table", source.c_str(), static_cast<long long>(index));
            continue;
        }
        AttackRow& out = s_table[static_cast<std::size_t>(index)];
        out.reach = static_cast<std::int16_t>(field(row, "reach_q12"));
        out.reach_start = static_cast<std::int16_t>(field(row, "reach_start_q12"));
        out.power = static_cast<std::uint16_t>(field(row, "power"));
        if (const json::Value* shape = row.find("shape"); shape != nullptr && shape->is_array())
        {
            for (std::size_t i = 0; i < 6 && i < shape->array().size(); i++)
                out.shape[i] = static_cast<std::uint8_t>(shape->array()[i].integer());
        }
        out.knockback = static_cast<std::int16_t>(field(row, "knockback_q12"));
        out.keyframe_start = static_cast<std::uint8_t>(field(row, "keyframe_start"));
        out.keyframe_length = static_cast<std::uint8_t>(field(row, "keyframe_length"));
        out.hit_type = static_cast<std::uint8_t>(field(row, "hit_type"));
        out.hurt_sound = static_cast<std::uint8_t>(field(row, "hurt_sound"));
        out.blood_splatter = static_cast<std::uint8_t>(field(row, "blood_splatter"));
        if (const json::Value* name = row.find("name"); name != nullptr && name->is_string())
        {
            // A merged mod row keeps the base row's name unless it gives its own.
            const std::string_view bare = name->string();
            s_names[static_cast<std::size_t>(index)] =
                std::string(bare.starts_with("silenthill:") ? bare.substr(11) : bare);
        }
        loaded++;
    }
    SH_LOG("[ITEMS] %s: %d attack table rows", source.c_str(), loaded);
}

int attack_number(std::string_view name)
{
    if (name.starts_with("silenthill:"))
        name.remove_prefix(11);
    if (name.empty())
        return -1;
    for (int i = 0; i < s_row_count; i++)
    {
        if (s_names[static_cast<std::size_t>(i)] == name)
            return i;
    }
    return -1;
}

bool set_attack_row(const items::WeaponAttack& attack)
{
    if (attack.index < 0)
        return false;
    const auto at = static_cast<std::size_t>(attack.index);
    grow_to(at + 1);
    s_row_count = std::max(s_row_count, attack.index + 1);
    AttackRow& row = s_table[at];
    row.reach = static_cast<std::int16_t>(attack.reach_q12);
    row.reach_start = static_cast<std::int16_t>(attack.reach_start_q12);
    row.power = attack.power;
    for (int i = 0; i < 6; i++)
        row.shape[i] = attack.shape[static_cast<std::size_t>(i)];
    row.knockback = static_cast<std::int16_t>(attack.knockback_q12);
    row.keyframe_start = attack.keyframe_start;
    row.keyframe_length = attack.keyframe_length;
    row.hit_type = attack.hit_type;
    row.hurt_sound = static_cast<std::uint8_t>(attack.hurt_sound);
    row.blood_splatter = attack.blood_splatter;
    s_weapon_rows[at] = attack;
    return true;
}

const items::WeaponAttack* weapon_attack(int index)
{
    if (index < 0 || static_cast<std::size_t>(index) >= s_weapon_rows.size() ||
        !s_weapon_rows[static_cast<std::size_t>(index)].has_value())
        return nullptr;
    return &*s_weapon_rows[static_cast<std::size_t>(index)];
}

}  // namespace sh::inventory::game
