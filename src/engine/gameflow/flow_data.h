#pragma once

// Game flow data from gamedata/config/: the door sound sets,
// the game-over tips and the title fog colours. Every mod's copy of the same file patches the base one (in load
// order, engine/mods/data_merge.h); the files hold sound and image names, never numbers.

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "formats/common/status.h"

namespace sh::gameflow {

// Reads <gamedata>/config/<name> or the highest-priority mod's mods/<id>/config/<name>.
// `source` gets the mod id ("" for the base file).
Result<std::string> read_config_file(const std::filesystem::path& gamedata_dir, const std::filesystem::path& mods_dir,
                                     std::string_view name, std::string* source, std::vector<std::string>& warnings);
// The same for any folder of gamedata/ (menu/, ...); only config/ files need a gameplay mod.
Result<std::string> read_data_file(const std::filesystem::path& gamedata_dir, const std::filesystem::path& mods_dir,
                                   std::string_view folder, std::string_view name, std::string* source,
                                   std::vector<std::string>& warnings);

// A rules type from config/<name>: T::builtin() when the file is absent, T::parse of its text otherwise.
// A parse failure keeps the built-ins and lands in `error`.
template <class T>
T load_config(const std::filesystem::path& gamedata_dir, const std::filesystem::path& mods_dir, std::string_view name,
              std::string* error) {
    std::vector<std::string> warnings;
    std::string source;
    const Result<std::string> text = read_config_file(gamedata_dir, mods_dir, name, &source, warnings);
    if (!text) return T::builtin();
    Result<T> parsed = T::parse(text.value());
    if (!parsed) {
        if (error != nullptr) *error = parsed.error();
        return T::builtin();
    }
    return parsed.value();
}

// One door sound set; the map event data selects a set by its index in the list.
struct DoorSoundSet {
    std::string open;           // sound name played when the door starts; "" = none
    std::string close;          // sound name played when the next room is shown; "" = none
    bool starts_loading = false;  // the transition raises the game's loading flag
};

class DoorSounds {
public:
    static Result<DoorSounds> parse(std::string_view json);

    std::size_t count() const { return sets_.size(); }
    // Sets outside the list are empty (no sounds).
    const DoorSoundSet& set(std::size_t index) const;

private:
    std::vector<DoorSoundSet> sets_;
};

struct GameOverTip {
    std::string image;  // image file name (without extension)
    bool dark = false;  // favoured in dark scenes instead of lit ones
};

// Picks the tip shown on the game-over screen. Unseen tips matching the scene (dark or lit) weigh
// 3, the others 1; a random number in [0, total weight) walks the unseen tips in order. When every
// tip has been seen the seen set starts over.
class GameOverTips {
public:
    static constexpr std::size_t kMaxTips = 16;  // the seen set is 16 bits

    static Result<GameOverTips> parse(std::string_view json);

    std::size_t count() const { return tips_.size(); }
    const GameOverTip& tip(std::size_t index) const { return tips_[index]; }

    // Clears `seen` when it holds every tip.
    void restart_if_all_seen(std::uint16_t& seen) const;
    // Total weight of the unseen tips.
    std::int32_t total_weight(std::uint16_t seen, bool dark_scene) const;
    // The tip that `roll` (0 <= roll < total_weight) lands on.
    std::size_t pick(std::uint16_t seen, bool dark_scene, std::int32_t roll) const;

    static void mark_seen(std::uint16_t& seen, std::size_t tip);

private:
    std::vector<GameOverTip> tips_;
};

// Colours of the title screen fog by heat (title_fog.h); exactly 256 entries.
class TitleFogPalette {
public:
    struct Rgb {
        std::uint8_t r = 0, g = 0, b = 0;
    };
    static constexpr std::size_t kColors = 256;

    static Result<TitleFogPalette> parse(std::string_view json);

    bool empty() const { return colors_.empty(); }
    // Black when the palette is empty.
    Rgb color(std::uint8_t heat) const { return colors_.empty() ? Rgb{} : colors_[heat]; }

private:
    std::vector<Rgb> colors_;
};

}  // namespace sh::gameflow
