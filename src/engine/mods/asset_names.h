// Where a mod keeps the replacement of a disc file. A raw disc file is replaced at its disc
// path ("CHARA/HERO.TIM"). A TIM's picture is replaced under the importer's names below convert/textures/:
//   whole picture   convert/textures/CHARA/HERO.dds, then .png (the importer's HERO.png: every texel in the colour of
//                   the palette row the game draws it with), registered for every CLUT row;
//   per palette row convert/textures/CHARA/HERO.pNN.dds, then .pNN.png for row NN >= 1, and HERO.dds / HERO.png
//                   for row 0 (tim_converter.h, level_textures.h palette_png).
// A mod that ships any row NN >= 1 makes the set per-row: rows it lacks keep the disc art. Otherwise HERO.png (or
// .dds) alone is the whole picture.
#pragma once

#include <array>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace sh::mods {

// Highest palette row a TIM picture replacement is looked up for (the game's TIMs carry at most 16).
inline constexpr int kMaxTimRows = 16;

// "CHARA/HERO.TIM" -> "convert/textures/CHARA/HERO" (backslashes and a leading slash allowed); "" when the path has
// no file name.
std::string tim_texture_stem(std::string_view disc_path);

// The names a mod file for row `row` (0..kMaxTimRows-1) may have, preferred first (.dds, then .png). Row 0 is the
// whole-picture name. Empty strings for a bad path or row.
std::array<std::string, 2> tim_row_names(std::string_view disc_path, int row);
inline std::array<std::string, 2> tim_whole_names(std::string_view disc_path) { return tim_row_names(disc_path, 0); }

// A found picture: the host file and whether it is a BC7 .dds (else a PNG or any image stb reads).
struct ImageFile {
    std::filesystem::path path;
    bool dds = false;
};

// Looks a mod-relative path up ("" / nothing = no mod has it); Registry::find in the game.
using FindFile = std::function<std::optional<std::filesystem::path>(std::string_view)>;

// The first existing name of `names`.
std::optional<ImageFile> find_image(const std::array<std::string, 2>& names, const FindFile& find);

// True when some row NN >= 1 is shipped: the set is per-row.
bool tim_is_per_row(std::string_view disc_path, const FindFile& find);

// What the mods replace of a TIM picture.
struct TimImages {
    bool per_row = false;
    std::optional<ImageFile> whole;                          // set only when !per_row
    std::array<std::optional<ImageFile>, kMaxTimRows> rows;  // set only when per_row
    bool any() const { return per_row || whole.has_value(); }
};
TimImages find_tim_images(std::string_view disc_path, const FindFile& find);

std::string lower_ascii(std::string_view text);

// The folder `relative` ("CHARA", "XA/voices") below `root`, every component matched case-insensitively (an exact
// match first); nothing when some component is missing.
std::optional<std::filesystem::path> find_dir_nocase(const std::filesystem::path& root, std::string_view relative);

}  // namespace sh::mods
