#include "engine/maps/spawn_overrides.h"

namespace sh::maps {

bool spawn_condition_matches(const sh::convert::mapdata::SpawnCondition& condition, int difficulty,
                             const SpawnFlagGet& flag_get)
{
    using Cond = sh::convert::mapdata::SpawnCondition;
    if (condition.difficulty != Cond::kDifficultyUnset && difficulty != condition.difficulty)
    {
        return false;
    }
    if (condition.difficulty_min != Cond::kDifficultyUnset && difficulty < condition.difficulty_min)
    {
        return false;
    }
    if (condition.difficulty_max != Cond::kDifficultyUnset && difficulty > condition.difficulty_max)
    {
        return false;
    }
    for (const std::string& name : condition.flags_set)
    {
        if (!flag_get(name))
        {
            return false;
        }
    }
    for (const std::string& name : condition.flags_clear)
    {
        if (flag_get(name))
        {
            return false;
        }
    }
    for (const sh::convert::mapdata::SpawnCondition& sub : condition.all)
    {
        if (!spawn_condition_matches(sub, difficulty, flag_get))
        {
            return false;
        }
    }
    for (const sh::convert::mapdata::SpawnCondition& sub : condition.any)
    {
        if (spawn_condition_matches(sub, difficulty, flag_get))
        {
            return true;
        }
    }
    return condition.any.empty();
}

std::vector<const sh::convert::mapdata::SpawnFlagSet*> select_spawn_overrides(
    const std::vector<sh::convert::mapdata::SpawnOverride>& overrides, int difficulty,
    const SpawnFlagGet& flag_get)
{
    std::vector<const sh::convert::mapdata::SpawnFlagSet*> out;
    for (const sh::convert::mapdata::SpawnOverride& override : overrides)
    {
        if (!spawn_condition_matches(override.when, difficulty, flag_get))
        {
            continue;
        }
        for (const sh::convert::mapdata::SpawnFlagSet& set : override.set)
        {
            out.push_back(&set);
        }
    }
    return out;
}

const sh::convert::mapdata::SpawnVariantChoice* select_spawn_variant(
    const sh::convert::mapdata::SpawnVariants& variants, int difficulty, const SpawnFlagGet& flag_get)
{
    for (const sh::convert::mapdata::SpawnVariantChoice& choice : variants.choices)
    {
        if (spawn_condition_matches(choice.when, difficulty, flag_get))
        {
            return &choice;
        }
    }
    return nullptr;
}

const sh::convert::mapdata::SpawnVariant* find_spawn_variant(const sh::convert::mapdata::SpawnVariants& variants,
                                                             const std::string& name)
{
    for (const sh::convert::mapdata::SpawnVariant& variant : variants.variants)
    {
        if (variant.name == name)
        {
            return &variant;
        }
    }
    return nullptr;
}

}  // namespace sh::maps
