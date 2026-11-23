#pragma once

// Options of sh-import: defaults, config.cfg keys and the command line.
// Priority, lowest first: defaults, --config FILE, command line.

#include "formats/common/status.h"

#include <array>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sh::import_tool {

// The groups of converters a user can switch on and off. The tables are not a converter group and cannot be
// switched off: the game requires them. They are written after the import (tables.h).
enum class Category : std::size_t { Textures, Models, Audio, Text, Levels, Data };
inline constexpr std::size_t kCategoryCount = 6;

// Name used by --only/--skip: "textures", "models", "audio", "text", "levels", "data".
std::string_view category_name(Category category);

struct Options {
    std::filesystem::path image;             // import_image
    std::filesystem::path out = "gamedata";  // gamedata_dir
    bool overwrite = false;                  // import_overwrite
    bool verify = true;                      // import_verify
    unsigned threads = 0;                    // import_threads, 0 = automatic
    std::array<bool, kCategoryCount> enabled{true, true, true, true, true, true};  // import_textures ... import_data
    // import_levels = unified (default): Levels on, only gamedata/maps/areas/*.glb (map_unified.h);
    // = glb: also the per-cell research views convert/levels/ (level_converter.h); = none: Levels off.
    bool level_views = false;
    // import_music = wav (default): the songs (SND/*.KDT + *.VAB) rendered by the engine's synthesizer to
    // convert/audio/music/; = none skips it. Part of the Audio group.
    bool music = true;
    std::optional<std::filesystem::path> config;  // --config
    // Where the glyph sheets of the game font are (<gamedata>/font of the base game data). Empty: `out`/font.
    std::filesystem::path font_dir;

    bool is_enabled(Category category) const { return enabled[static_cast<std::size_t>(category)]; }
    void set_enabled(Category category, bool on) { enabled[static_cast<std::size_t>(category)] = on; }
};

// What the command line said; fields it did not mention stay empty.
struct CliArgs {
    bool help = false;
    std::optional<std::filesystem::path> image;
    std::optional<std::filesystem::path> out;
    std::optional<std::filesystem::path> config;
    std::optional<bool> overwrite;
    std::optional<bool> verify;
    std::optional<unsigned> threads;
    std::optional<std::vector<Category>> only;
    std::vector<Category> skip;
};

// `args` excludes the program name. Accepts "--key value" and "--key=value". The one positional argument is the image.
Result<CliArgs> parse_args(std::span<const std::string> args);

// Applies `key = value` lines (blank lines, lines starting with '#' and text after " #" are comments). Unknown keys
// are not an error: each adds a line to `warnings`. A known key with a bad value, or a line without '=', is an error
// that names the line. A key given twice takes the last value.
Result<void> apply_config(Options& options, std::string_view text, std::vector<std::string>& warnings);

// Command line over the options already set from the config. --only replaces the set of enabled groups,
// then --skip removes from it.
void apply_cli(Options& options, const CliArgs& args);

// The text printed by --help.
std::string usage();

}  // namespace sh::import_tool
