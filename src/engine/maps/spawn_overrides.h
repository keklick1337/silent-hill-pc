#pragma once

// Story-driven spawns: the generic selector over the unified map.json's optional
// "spawn_overrides" section (parsed into sh::convert::mapdata::SpawnOverride by the mapdata
// reader). Nothing map-specific lives here or in the adapter: a condition is difficulty bounds
// and event-flag names evaluated through an injected lookup, and the output is the ordered list
// of {character name, slot, flag names} writes the game adapter performs through its existing
// Chara_SpawnFlagsSet path.

#include "formats/convert/mapdata/mapdata.h"

#include <functional>
#include <string>
#include <vector>

namespace sh::maps {

/* Event-flag lookup by name: the adapter resolves the name through its registry and reads the
 * save's flag. An unknown name is reported through the lookup (false) — the caller logs it. */
using SpawnFlagGet = std::function<bool(const std::string& name)>;

/* A condition matches when: no difficulty bound or difficulty <= difficulty_max, every name in
 * flags_set is set, every name in flags_clear is clear, every "all" sub-condition matches and at
 * least one "any" sub-condition matches (an empty "any" list matches, like an empty flags list). */
bool spawn_condition_matches(const sh::convert::mapdata::SpawnCondition& condition, int difficulty,
                             const SpawnFlagGet& flag_get);

/* Every set entry of every matching override, in file order: later entries overwrite earlier
 * ones. */
std::vector<const sh::convert::mapdata::SpawnFlagSet*> select_spawn_overrides(
    const std::vector<sh::convert::mapdata::SpawnOverride>& overrides, int difficulty,
    const SpawnFlagGet& flag_get);

/* Spawn variants (whole alternative tables): the first choice whose condition matches, nullptr when
 * none does. The caller sets the choice's set_flags, copies its variant's rows and moves the killed
 * state words; this function only decides, so the tests and the game run the same decision. */
const sh::convert::mapdata::SpawnVariantChoice* select_spawn_variant(
    const sh::convert::mapdata::SpawnVariants& variants, int difficulty, const SpawnFlagGet& flag_get);

/* The variant a choice names (the reader guarantees it exists), nullptr for an unknown name. */
const sh::convert::mapdata::SpawnVariant* find_spawn_variant(const sh::convert::mapdata::SpawnVariants& variants,
                                                             const std::string& name);

}  // namespace sh::maps
