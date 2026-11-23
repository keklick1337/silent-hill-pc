// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Gameplay parameters as data: gamedata/config/gameplay.json — player HP
// and saturation caps, health-item restore amounts, and the enemy-grab escape input counts
// per difficulty. The shipped file carries the original's values, so nothing changes until
// a mod or the user edits it. The game reaches these through the C accessors of
// gamedata_config.h; every accessor falls back to the compiled-in value without the data.

#pragma once

#include "formats/common/status.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace sh::gconfig {

// Grab situations, in the order player_control.cpp uses them. The string keys of
// gameplay.json's grab_escape name the same situations.
enum class Grab : std::uint8_t {
    OnFloor = 0,
    PinnedFront,
    PinnedBack,
    TorsoFront,
    TorsoBack,
    LegsFront,
    LegsBack,
    NeckFront,
    NeckBack,
    Count
};
inline constexpr std::array<std::string_view, static_cast<std::size_t>(Grab::Count)> kGrabNames = {
    "on_floor", "grab_pinned_front", "grab_pinned_back", "grab_torso_front", "grab_torso_back",
    "grab_legs_front", "grab_legs_back", "grab_neck_front", "grab_neck_back"};

struct GameplayParams {
    double max_health = 100.0;
    double max_saturation = 300.0;
    double heal_health_drink = 40.0;
    double heal_first_aid_kit = 80.0;
    double heal_ampoule = 100.0;
    // Every "health_items" row by name (an item's "heal" names one): the three above and a mod's own rows.
    std::map<std::string, double, std::less<>> heal_amounts{
        {"health_drink", 40.0}, {"first_aid_kit", 80.0}, {"ampoule", 100.0}};
    std::array<std::array<std::int32_t, 3>, static_cast<std::size_t>(Grab::Count)> grab_escape{};  // [easy, normal, hard]

    bool operator==(const GameplayParams&) const = default;
};

struct LoadSource {
    std::string mod_id;
    bool mod_gameplay = false;
};

class GameplayLibrary {
public:
    // params() carries the original's values until the data overrides them.
    GameplayLibrary();
    // <gamedata_dir>/config/gameplay.json; the highest-priority mod's copy replaces it.
    static Result<GameplayLibrary> load(const std::filesystem::path& gamedata_dir,
                                        const std::filesystem::path& mods_dir,
                                        std::vector<std::string>& warnings, LoadSource* source = nullptr);

    static Result<GameplayLibrary> parse(std::string_view json, std::vector<std::string>& warnings);

    const GameplayParams& params() const { return params_; }

    // The grab count of `grab` for difficulty -1/0/+1 (e_GameDifficulty) clamped to an index.
    std::int32_t grab_escape(Grab grab, int difficulty) const;

    std::vector<std::string> check_consistency() const;

private:
    GameplayParams params_;
};

}  // namespace sh::gconfig
