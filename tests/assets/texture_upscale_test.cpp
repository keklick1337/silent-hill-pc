#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "engine/assets/texture_upscale.h"
#include "engine/assets/upscale_cache.h"
#include "framework/sh_test.h"

namespace {

namespace fs = std::filesystem;

void write(const fs::path& file, const std::vector<std::uint8_t>& bytes) {
    fs::create_directories(file.parent_path());
    std::ofstream f(file, std::ios::binary | std::ios::trunc);
    f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

std::string read_text(const fs::path& file) {
    std::ifstream f(file, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

}  // namespace

// A mod's picture (other bytes, any size) wins over the cache: it is drawn as the mod made it, never swapped for the
// upscaled imported one, and never named to the tool as wanted.
SH_TEST(upscale_mod_picture_wins_over_cache) {
    const fs::path gamedata = fs::temp_directory_path() / "sh_upscale_precedence_test";
    fs::remove_all(gamedata);
    const std::vector<std::uint8_t> imported = {1, 2, 3, 4}, other = {5, 6, 7};
    const std::vector<std::uint8_t> mod_hd = {9, 9, 9, 9, 9, 9, 9, 9}, upscaled = {42};
    write(gamedata / "convert/textures/BG/SIGN.png", imported);
    write(gamedata / "convert/textures/BG/WALL.png", other);
    write(gamedata / "convert/textures/BG/DOOR.png", other);
    write(sh::upscale::cache_file(sh::upscale::cache_root(gamedata), sh::upscale::kModel, "BG/SIGN", 4,
                                  sh::upscale::source_hash(imported)),
          upscaled);
    sh::upscale::set_gamedata(gamedata);
    sh::upscale::set_scale(4);

    const auto from_import = sh::upscale::find("convert/textures/BG/SIGN.png", imported);
    SH_CHECK(from_import.has_value() && *from_import == upscaled);
    SH_CHECK(!sh::upscale::find("mods/hd/convert/textures/BG/SIGN.png", mod_hd).has_value());
    SH_CHECK(!sh::upscale::find("mods/hd/convert/textures/BG/DOOR.png", mod_hd).has_value());
    SH_CHECK(!sh::upscale::find("convert/textures/BG/WALL.png", other).has_value());
    sh::upscale::publish_wanted();
    // Only the imported picture without its upscaled copy is wanted: the mod's SIGN and DOOR were not noted.
    SH_CHECK(read_text(sh::upscale::cache_root(gamedata) / sh::upscale::kWantedFile) == "BG/WALL\n");

    sh::upscale::set_scale(0);
    sh::upscale::set_gamedata({});
    fs::remove_all(gamedata);
}
