// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// How a footstep sounds, all of it data:
//   floor material byte of the collision  -> surface name   gamedata/world/surfaces.json
//   surface -> sound name                 (the character)   chara/<Name>/character.json "footsteps"
//   place rules of the map                (a spot, a room)  maps/<map>/footsteps.json
//   on_footstep(e) hook                   (Lua, last word)  chara/<Name>/actions.lua (the caller runs it)
//   sound name -> bank tone or own file                     sounds/sounds.json
// The surface also gives the two pitch jitters the game draws for a step (walk and run): base + rand % range,
// drawn by the caller from the game's random generator in the original's order.
//
// The shipped data reproduce the original choice exactly (same sound id, same jitter ranges on the same ticks): the
// place rules test the player's position even for another character's step, as the original did.

#pragma once

#include "engine/audio/sound_registry.h"
#include "engine/chara/chara_library.h"
#include "formats/common/status.h"

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sh::chara {

// One pitch jitter of a step: base + rand % range (range > 0).
struct StepPitch {
    int base = -16;
    int range = 32;
    bool operator==(const StepPitch&) const = default;
};

struct Surface {
    int material = 0;  // the collision's floor material byte
    std::string name;  // "concrete"
    StepPitch walk;
    StepPitch run{32, 64};
};

// gamedata/world/surfaces.json (+ every mods/<mod>/world/surfaces.json, merged by surface name).
class SurfaceTable {
public:
    static Result<SurfaceTable> load(const std::filesystem::path& gamedata_dir, const std::filesystem::path& mods_dir,
                                     std::vector<std::string>& warnings);
    // One file merged over the table (all-or-nothing).
    Result<int> apply(std::string_view json, std::string_view origin);

    // The surface of a material byte; materials the table does not list are the "fallback" surface.
    const Surface& of_material(int material) const;
    const Surface* find(std::string_view name) const;
    bool empty() const { return by_material_.empty(); }
    const std::map<int, Surface>& surfaces() const { return by_material_; }

private:
    std::map<int, Surface> by_material_;
    std::string fallback_ = "default";
    Surface none_;  // the fallback when even that is missing
};

// One place rule of a map: where it applies (all conditions optional) and the sound there.
struct PlaceRule {
    std::string name;
    std::vector<std::string> surfaces;      // any of these (empty = any surface)
    std::vector<std::string> not_surfaces;  // none of these
    int room = -1;                          // the map's room index (-1 = any)
    // Player position bounds, Q12 like the game's coordinates, inclusive; y_above is strict (y > it).
    std::optional<std::int32_t> x_min, x_max, z_min, z_max, y_above;
    std::string sound_name;
    int sound = -1;
};

struct StepPlace {
    std::int32_t x = 0, y = 0, z = 0;  // the player's position, Q12
    int room = -1;
};

// maps/<map>/footsteps.json. The rules apply in order and every match replaces the sound, so the last matching
// rule has the last word (the original's order of checks).
class PlaceRules {
public:
    static Result<PlaceRules> parse(std::string_view json, std::string_view origin, const audio::SoundRegistry& sounds);
    const PlaceRule* match(std::string_view surface, const StepPlace& place) const;
    const std::vector<PlaceRule>& rules() const { return rules_; }

private:
    std::vector<PlaceRule> rules_;
};

struct StepChoice {
    int sound = -1;
    const Surface* surface = nullptr;
    const PlaceRule* place = nullptr;  // the rule that chose the sound, nullptr for the character's table
};

// "map2_s00" for map index 10 (the game's map order); empty past the last map.
std::string map_name(int map_index);

class Footsteps {
public:
    Footsteps(const CharaLibrary& library, SurfaceTable surfaces, std::filesystem::path gamedata_dir,
              std::filesystem::path mods_dir);

    // The step of character `chara` on `material`: the character's table (Harry's when the character has none),
    // then the place rules of `map`. `fallback` when no table names the surface.
    StepChoice choose(int chara, int material, std::string_view map, const StepPlace& place, int fallback);
    // The character's table only (characters whose code has no place rules).
    StepChoice table(int chara, int material, int fallback) const;

    const SurfaceTable& surfaces() const { return surfaces_; }
    // The place rules of a map (loaded once; every mod's maps/<map>/footsteps.json patches the base file).
    const PlaceRules* places(std::string_view map);
    std::vector<std::string> take_warnings();

    static constexpr std::uint8_t kHarry = 1;

private:
    const CharaLibrary& library_;
    SurfaceTable surfaces_;
    std::filesystem::path gamedata_dir_, mods_dir_;
    std::map<std::string, std::optional<PlaceRules>, std::less<>> places_;
    std::vector<std::string> warnings_;
};

}  // namespace sh::chara
