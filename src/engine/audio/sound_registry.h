// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Sound effects by readable name: gamedata/sounds/sounds.json lists "footstep_concrete" and says which bank files and
// converter WAVs hold it; the id the game plays for the name (1311) comes from the default numbering
// (set_default_numbering), the data carries no numbers. Characters, map scripts and mods name sounds through it; the
// game itself plays ids, and a name always resolves to the same id.
//
// Mods: every mods/<mod>/sounds/*.json (same shape) is merged over the base by name, in
// ascending mod priority: "my_name": "footstep_concrete" is another name of that sound, an
// existing name with another "id" rebinds every user of that name (character slots, scripts).
// An entry's "aliases" are its former names: they resolve to the same sound (old mods and
// scripts keep working), while name(id) stays the entry's own name.
//
// Two kinds of entry: a tone of an original bank (id/slot/program/note - the game keys that
// program, 1:1 with the original) or an own file, {"file": "sounds/step.wav", "volume",
// "pitch", "loop", "pan"}: every play of its id plays that WAV instead (the bridge in the
// game's sound calls asks file_sound()). A file entry without "id" that names a new sound gets
// a free id from kFileIdFirst..kFileIdLast; on an existing name it keeps that name's id, so a
// mod can turn "footstep_concrete" into its own WAV without touching anything else.

#pragma once

#include "formats/common/status.h"

#include <algorithm>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace sh::audio {

struct SoundInfo {
    std::string name;                // "footstep_concrete"
    int id = -1;                     // e_SfxId
    std::string enum_name;           // "Sfx_FootstepConcrete" (may be empty)
    std::vector<std::string> banks;  // "BASE", "MAP100", ...
    std::vector<std::string> files;  // "audio/vab/BASE/005.wav" (under convert/)

    // Own file (empty = the bank tone plays). `file` as written, `file_path` resolved against
    // the directory of the data that named it (gamedata, or the mod's folder).
    std::string file;
    std::filesystem::path file_path;
    float volume = 1.0f;  // gain on top of the game's volume for the call
    float pitch = 0.0f;   // semitones; the game's own pitch offset (steps) adds to it
    float pan = 0.0f;     // -1 left .. 1 right, on top of the call's balance
    bool loop = false;    // loops until the game stops the id

    bool has_file() const { return !file.empty(); }

    // The channel levels of a file sound. The play path folds volume and pan
    // first, the live volume path multiplies left-to-right; float rounding is
    // not associative, so the two orders are kept apart.
    void play_levels(int left, int right, int* out_left, int* out_right) const {
        const float l = volume * std::min(1.0f, 1.0f - pan);
        const float r = volume * std::min(1.0f, 1.0f + pan);
        *out_left = static_cast<int>(left * l);
        *out_right = static_cast<int>(right * r);
    }

    void volume_levels(int left, int right, int* out_left, int* out_right) const {
        *out_left = static_cast<int>(left * volume * std::min(1.0f, 1.0f - pan));
        *out_right = static_cast<int>(right * volume * std::min(1.0f, 1.0f + pan));
    }

    // The play pitch: the registry's semitones plus the caller's live cents.
    float pitch_semitones(int pitch_cents) const { return pitch + static_cast<float>(pitch_cents) / 100.0f; }
};

class SoundRegistry {
public:
    // <gamedata_dir>/sounds/*.json (sounds.json first, the rest by file name), then every
    // mods/<mod>/sounds/*.json by ascending priority. A missing base file is an empty
    // registry, not an error: names then simply do not resolve.
    static Result<SoundRegistry> load(const std::filesystem::path& gamedata_dir,
                                      const std::filesystem::path& mods_dir,
                                      std::vector<std::string>& warnings);

    // One file's text merged over the registry (all-or-nothing: a bad file changes nothing).
    // Returns the number of names it set.
    // `root`: the directory "file" paths are relative to (empty = as written).
    Result<int> apply(std::string_view json, std::string_view origin, const std::filesystem::path& root = {});

    // Ids given to new file-only names: past the game's sound table (1281..1699) and inside the
    // range the game's sound command plays (SD_Call 1280..1791), so every play path reaches them.
    static constexpr int kFileIdFirst = 1700;
    static constexpr int kFileIdLast = 1791;

    // The entry whose own file plays for `id`, nullptr when the id plays its bank tone.
    const SoundInfo* file_sound(int id) const;
    bool has_file_sounds() const { return !file_ids_.empty(); }

    // "footstep_concrete", the enum name "Sfx_FootstepConcrete" or a number "1311" -> 1311;
    // -1 when unknown.
    int id(std::string_view name) const;
    // The first (base) name of an id, nullptr when the id has none.
    const std::string* name(int id) const;
    const SoundInfo* find(std::string_view name) const;

    std::size_t size() const { return sounds_.size(); }
    const std::map<std::string, SoundInfo, std::less<>>& sounds() const { return sounds_; }

    // The registry of the running game: loaded once from the given directories (the first
    // call wins; later calls with other directories load and cache those too).
    static const SoundRegistry& shared(const std::filesystem::path& gamedata_dir,
                                       const std::filesystem::path& mods_dir);

    // The number of a base game's sound name, -1 when the base game has none. gamedata/ names
    // sounds only; the engine (ids/game_numbers.h, by place in sounds.json) knows the e_SfxId behind each
    // name and installs itself before main(). Without it (tools, tests) entries need an "id".
    using Numbering = int (*)(std::string_view name);
    static void set_default_numbering(Numbering numbering);

private:
    static int default_number(std::string_view name);

    std::vector<std::string> unresolved_;  // entries apply() skipped; load() reports them
    std::map<std::string, SoundInfo, std::less<>> sounds_;
    std::map<int, std::string> names_;  // id -> first name given
    std::map<std::string, int, std::less<>> enums_;
    std::map<int, std::string> file_ids_;  // id -> the name whose file plays for it
};

}  // namespace sh::audio
