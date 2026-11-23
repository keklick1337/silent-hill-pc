// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Event flags by name: gamedata/flags/*.json declare the base
// game's flags by area ("M1S03_ShotgunShells0" == "silenthill:M1S03_ShotgunShells0"), every
// mods/<mod>/flags.json or mods/<mod>/flags/*.json of the same shape adds the mod's own
// ("<mod folder>:door_opened"). The registry knows names only. Bits are handed out by assign_bits:
// the bits of the original save layout come from the caller (original_order.json), any other flag
// gets a bit none of them uses, the same bit for the same set of mods. Saves store names, so a bit
// is never persisted.

#pragma once

#include "formats/common/status.h"

#include <cstdint>
#include <filesystem>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sh::flags {

struct FlagInfo {
    std::string id;           // "silenthill:M1S03_ShotgunShells0", "mymod:door_opened"
    std::string description;  // what the data says about it (may be empty)
    std::string origin;       // the file that declared it
    // Former full ids that still name this flag everywhere ("silenthill:Flag47").
    std::vector<std::string> aliases;
    // Former full ids that name another flag now: they mean this one only in saves written before the rename.
    std::vector<std::string> old_save_names;
    // A paper map mark (declared in a file of "kind": "paper_map_mark"): the event that sets it may run again.
    bool paper_map_mark = false;
};

class FlagRegistry {
public:
    // <gamedata_dir>/flags/*.json by file name (a gamedata without that directory: its flags.json),
    // then each mod's flags.json and flags/*.json by ascending priority. No base file is an empty
    // base, not an error.
    static Result<FlagRegistry> load(const std::filesystem::path& gamedata_dir,
                                     const std::filesystem::path& mods_dir,
                                     std::vector<std::string>& warnings);

    // One file's flags (a mod's or the base's) merged in; `ns` is its namespace. A short name
    // that is a base flag patches its description; any other short name is a new flag of `ns`.
    Result<int> apply(std::string_view json, std::string_view ns, std::string_view origin,
                      std::vector<std::string>& warnings);

    // Declared flags in declaration order (base first, then each mod's by name).
    const std::vector<FlagInfo>& flags() const { return flags_; }
    const FlagInfo* find(std::string_view id) const;

    // id -> bit: the flags of `base_bits` (full ids) at their bits, every other declared flag at the
    // highest bit below `bit_count` that none of them and no earlier one uses, so the bits of the
    // original layout stay free. A flag that finds no bit is left out with a warning.
    std::map<std::string, int, std::less<>> assign_bits(const std::map<std::string, int, std::less<>>& base_bits,
                                                        int bit_count, std::vector<std::string>& warnings) const;

private:
    std::vector<FlagInfo> flags_;
    std::map<std::string, std::size_t, std::less<>> index_;
};

// Saves keep the set flags by name. `name_of(bit)` is the full id of a
// bit or nullptr; `number_of(name)` the bit of a name or -1.
using FlagNameOf = const char* (*)(int bit);
using FlagNumberOf = int (*)(const char* name);

// The names of the set bits of `words` (bit 0 is "no flag" and never listed); a set bit without
// a name is not listed (it lives on in the numeric copy of the save).
std::vector<std::string> set_flag_names(std::span<const std::uint32_t> words, FlagNameOf name_of);

// Makes every bit of `words` that has a name set exactly when the name is in `names` (names win
// over the numbers); bits without a name keep their value. Names this build does not know go to
// `unknown`. Returns whether a bit changed.
bool apply_flag_names(std::span<std::uint32_t> words, const std::vector<std::string>& names, FlagNumberOf number_of,
                      FlagNameOf name_of, std::vector<std::string>& unknown);

}  // namespace sh::flags
