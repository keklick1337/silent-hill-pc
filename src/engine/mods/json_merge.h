// Field-wise merge of a mod's JSON data file over the base one, so several mods patch the same file instead of the
// last one replacing it. The rules:
//
//   objects        merge key by key, recursively; a member set to null deletes the key; a new key is added.
//   arrays of      matched by a key field (MergeRules: the file's own key for that path, else the first of
//   objects        "id", "name" that every patch entry has): a matching entry merges recursively, a new one is
//                  appended (or placed with "$after": <key> / "$before": <key>), "$remove": true drops it. An
//                  array whose patch entries have no key field is replaced whole.
//   other arrays   replaced whole, unless the patch is {"$append": [...]} / {"$prepend": [...]} /
//                  {"$remove": [values]}.
//   scalars        replaced.
//   twins          "speed_q12" in a patch drops the base's "speed" (and the other way round) unless the patch
//                  sets both: the readers take the readable twin, so a stale one would hide the change.
//   "$replace"     {"$replace": true, ...} inside the patch: that object replaces the base value whole.
//                  At the top of a file it means "replace the whole file", which data_merge.h only honours for a
//                  path the mod lists in its manifest.
//   "$keys"        at the top of a patch: {"path": "field"} adds array keys for this patch ("objects": "name";
//                  nested arrays as "a[].b").
// Keys starting with '$' never reach the merged result.
#pragma once

#include "formats/common/json.h"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sh::mods {

struct MergeRules {
    // Path of an array -> the field its entries are matched by. Paths are object keys joined with '.', an array
    // level written as "[]": "objects", "spawn_variants.variants", "groups[].members". "" is the document root.
    // A compound key lists its fields with ',' ("group,slot"); an anchor of "$after" is then an object with them.
    std::vector<std::pair<std::string, std::string>> array_keys;
    // Tried in order when the path has no key of its own.
    std::vector<std::string> key_candidates{"id", "name"};
    // A value and its fixed-point twin ("speed" / "speed_q12", "x" / "x_q12", "road_max_y" / "road_max_y_q4")
    // are one value: a patch member sets it, and the base's copy of the twin the patch does not restate goes.
    bool drop_stale_twins = true;

    const std::string* key_for(std::string_view path) const;
};

// What a patch did, counted per top-level member, keyed array entry or array: "modified 2, added 1, removed 0".
struct MergeStats {
    int modified = 0;
    int added = 0;
    int removed = 0;

    bool changed() const { return modified + added + removed > 0; }
    std::string describe() const;
};

// Applies `patch` onto `base`. Problems that do not stop the merge (a "$remove" of an entry the base does not
// have, an unknown directive) are appended to `warnings`. A top-level "$replace" is not looked at here.
void merge_patch(json::Value& base, const json::Value& patch, const MergeRules& rules, MergeStats& stats,
                 std::vector<std::string>& warnings);

// The top-level {"$replace": true} of a whole-file replacement.
bool wants_replace(const json::Value& patch);

// The value without its '$' directives (recursively).
json::Value strip_directives(const json::Value& value);

}  // namespace sh::mods
