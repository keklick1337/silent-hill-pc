#include "engine/mods/asset_files_c.h"
#include "engine/mods/asset_names.h"
#include "engine/mods/registry.h"
#include "framework/sh_test.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>
#include "framework/mod_fixture.h"

namespace fs = std::filesystem;
using namespace sh::mods;

namespace {

class Scratch {
public:
    explicit Scratch(const std::string& name) : path_(fs::path(SH_PROJECT_ROOT) / ".tmp" / "mods_assets" / name) {
        std::error_code ec;
        fs::remove_all(path_, ec);
        fs::create_directories(path_, ec);
    }
    ~Scratch() {
        std::error_code ec;
        fs::remove_all(path_, ec);
    }
    Scratch(const Scratch&) = delete;
    Scratch& operator=(const Scratch&) = delete;

    const fs::path& path() const { return path_; }

    void write(const std::string& relative, std::string_view text) const {
        const fs::path file = path_ / relative;
        fs::create_directories(file.parent_path());
        std::ofstream out(file, std::ios::binary);
        out << text;
        out.close();
        sh::test::mod_file_written(file);
    }

private:
    fs::path path_;
};

// A find over a fixed set of names (lower-case compare, as the registry does).
FindFile set_find(std::set<std::string> names) {
    return [names = std::move(names)](std::string_view rel) -> std::optional<fs::path> {
        if (names.count(lower_ascii(rel)) != 0) {
            return fs::path("/mods/x") / std::string(rel);
        }
        return std::nullopt;
    };
}

std::string read_all(FILE* f) {
    std::string out;
    char buf[64];
    size_t n = 0;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) {
        out.append(buf, n);
    }
    std::fclose(f);
    return out;
}

}  // namespace

SH_TEST(mods_assets_tim_names) {
    SH_CHECK(tim_texture_stem("CHARA/HERO.TIM") == "convert/textures/CHARA/HERO");
    SH_CHECK(tim_texture_stem("\\BG\\ER_AQSH1.TIM") == "convert/textures/BG/ER_AQSH1");
    SH_CHECK(tim_texture_stem("1ST/FONT16") == "convert/textures/1ST/FONT16");
    SH_CHECK(tim_texture_stem("CHARA/").empty());
    SH_CHECK(tim_texture_stem("").empty());

    const auto whole = tim_whole_names("CHARA/HERO.TIM");
    SH_CHECK(whole[0] == "convert/textures/CHARA/HERO.dds" && whole[1] == "convert/textures/CHARA/HERO.png");
    SH_CHECK(tim_row_names("CHARA/HERO.TIM", 0) == whole);
    const auto row3 = tim_row_names("CHARA/HERO.TIM", 3);
    SH_CHECK(row3[0] == "convert/textures/CHARA/HERO.p03.dds" && row3[1] == "convert/textures/CHARA/HERO.p03.png");
    SH_CHECK(tim_row_names("CHARA/HERO.TIM", 15)[1] == "convert/textures/CHARA/HERO.p15.png");
    SH_CHECK(tim_row_names("CHARA/HERO.TIM", 16)[0].empty());
    SH_CHECK(tim_row_names("CHARA/HERO.TIM", -1)[0].empty());
}

SH_TEST(mods_assets_tim_whole_vs_rows) {
    // HERO.png alone: the whole picture, for every row.
    TimImages only_png = find_tim_images("CHARA/HERO.TIM", set_find({"convert/textures/chara/hero.png"}));
    SH_CHECK(!only_png.per_row && only_png.whole && !only_png.whole->dds);
    SH_CHECK(only_png.whole->path.generic_string() == "/mods/x/convert/textures/CHARA/HERO.png");

    // .dds wins over .png.
    TimImages both = find_tim_images("CHARA/HERO.TIM",
                                     set_find({"convert/textures/chara/hero.png", "convert/textures/chara/hero.dds"}));
    SH_CHECK(both.whole && both.whole->dds);

    // Any row >= 1 makes the set per-row; HERO.png is then row 0.
    TimImages rows = find_tim_images("CHARA/HERO.TIM", set_find({"convert/textures/chara/hero.png",
                                                                 "convert/textures/chara/hero.p02.dds"}));
    SH_CHECK(rows.per_row && !rows.whole);
    SH_CHECK(rows.rows[0] && !rows.rows[0]->dds);
    SH_CHECK(!rows.rows[1]);
    SH_CHECK(rows.rows[2] && rows.rows[2]->dds);

    // A row without row 0 is still per-row: row 0 keeps the disc art.
    TimImages no_zero = find_tim_images("CHARA/HERO.TIM", set_find({"convert/textures/chara/hero.p01.png"}));
    SH_CHECK(no_zero.per_row && !no_zero.rows[0] && no_zero.rows[1]);

    // The old names are not read.
    TimImages old = find_tim_images("CHARA/HERO.TIM", set_find({"chara/hero.tim.png", "chara/hero.png",
                                                                "chara/hero.tim.p00.png", "chara/hero_reference.png"}));
    SH_CHECK(!old.any());
}

SH_TEST(mods_assets_find_dir_nocase) {
    Scratch dir("nocase");
    dir.write("Mod/Chara/Sub/a.txt", "a");
    SH_CHECK(find_dir_nocase(dir.path() / "Mod", "CHARA").has_value());
    SH_CHECK(find_dir_nocase(dir.path() / "Mod", "chara/SUB").has_value());
    SH_CHECK(find_dir_nocase(dir.path() / "Mod", "Chara\\Sub").has_value());
    SH_CHECK(!find_dir_nocase(dir.path() / "Mod", "chara/none").has_value());
    SH_CHECK(!find_dir_nocase(dir.path() / "Mod", "chara/sub/a.txt").has_value());  // a file, not a folder
    SH_CHECK(!find_dir_nocase(dir.path() / "Mod", "../Mod").has_value());
}

SH_TEST(mods_assets_c_abi) {
    Scratch dir("c_abi");
    // No active mod: nothing at all.
    registry().configure(RegistryConfig{});
    SH_CHECK(ShModAsset_Open("CHARA/HERO.TIM", "rb") == nullptr);
    SH_CHECK(!ShModAsset_TimHasImage("CHARA/HERO.TIM"));

    dir.write("mods/low/mod.json", R"({"id": "low", "version": "1.0.0", "priority": 1})");
    dir.write("mods/low/chara/hero.tim", "low-tim");
    dir.write("mods/low/convert/textures/CHARA/HERO.png", "low-png");
    dir.write("mods/low/text_overrides.txt", "low");
    dir.write("mods/low/texturepacks/a/x.png", "x");
    dir.write("mods/low/chara/HERO_CJ.glb", "glb");
    dir.write("mods/high/mod.json", R"({"id": "high", "version": "1.0.0", "priority": 2})");
    dir.write("mods/high/convert/textures/CHARA/BOS.p01.png", "row1");
    dir.write("mods/high/text_overrides.txt", "high");
    dir.write("mods/high/TexturePacks/b.zip", "zip");
    dir.write("mods/high/chara/hero_cj.glb", "glb2");
    dir.write("mods/high/chara/HERO_X.gltf", "gltf");
    RegistryConfig config;
    config.dirs = {dir.path() / "mods"};
    registry().configure(config);
    SH_CHECK(registry().active().size() == 2);

    // A raw disc file at its disc path, case-insensitive.
    FILE* f = ShModAsset_Open("CHARA/HERO.TIM", "rb");
    SH_CHECK(f != nullptr);
    if (f != nullptr) {
        SH_CHECK(read_all(f) == "low-tim");
    }

    char out[1024];
    int dds = -1;
    SH_CHECK(ShModAsset_TimHasImage("CHARA/HERO.TIM"));
    SH_CHECK(!ShModAsset_TimPerRow("CHARA/HERO.TIM"));
    SH_CHECK(ShModAsset_TimWhole("CHARA/HERO.TIM", out, sizeof out, &dds) && dds == 0);
    SH_CHECK(fs::path(out).filename() == "HERO.png");

    SH_CHECK(ShModAsset_TimPerRow("CHARA/BOS.TIM"));
    SH_CHECK(!ShModAsset_TimWhole("CHARA/BOS.TIM", out, sizeof out, &dds));
    SH_CHECK(ShModAsset_TimRow("CHARA/BOS.TIM", 1, out, sizeof out, &dds) && dds == 0);
    SH_CHECK(!ShModAsset_TimRow("CHARA/BOS.TIM", 0, out, sizeof out, &dds));
    SH_CHECK(!ShModAsset_TimHasImage("CHARA/DOB.TIM"));

    // Every mod's copy, the last loaded (winning) one first.
    std::vector<std::string> seen;
    auto collect = [](const char* host, void* ctx) {
        std::FILE* file = std::fopen(host, "rb");
        static_cast<std::vector<std::string>*>(ctx)->push_back(file ? read_all(file) : "?");
    };
    SH_CHECK(ShModAsset_FindEach("text_overrides.txt", collect, &seen) == 2);
    SH_CHECK(seen.size() == 2 && seen[0] == "high" && seen[1] == "low");

    std::vector<std::string> dirs;
    auto dir_names = [](const char* host, void* ctx) {
        static_cast<std::vector<std::string>*>(ctx)->push_back(fs::path(host).parent_path().filename().string());
    };
    SH_CHECK(ShModAsset_DirEach("texturepacks", dir_names, &dirs) == 2);
    SH_CHECK(dirs.size() == 2 && dirs[0] == "high" && dirs[1] == "low");
    SH_CHECK(ShModAsset_DirEach("texturepacks", nullptr, nullptr) == 2);
    SH_CHECK(ShModAsset_DirEach("nothing", nullptr, nullptr) == 0);

    std::vector<std::string> names;
    auto name_list = [](const char* name, void* ctx) { static_cast<std::vector<std::string>*>(ctx)->push_back(name); };
    SH_CHECK(ShModAsset_ListDir("CHARA", name_list, &names) == 3);  // hero.tim, HERO_CJ.glb (once), HERO_X.gltf
    SH_CHECK(names.size() == 3 && names[0] == "hero.tim" && names[1] == "HERO_CJ.glb" && names[2] == "HERO_X.gltf");

    registry().configure(RegistryConfig{});
    SH_CHECK(ShModAsset_Open("CHARA/HERO.TIM", "rb") == nullptr);
}

// The layout of an HD texture mod with an old mod.cfg: HERO.png replaces the picture of CHARA/HERO.TIM.
SH_TEST(mods_assets_hd_texture_mod_cfg) {
    Scratch dir("hd_cfg");
    dir.write("mods/HD_TEST/mod.cfg", "id = HD_TEST\nname = HD test\nversion = 1\npriority = 10\ngameplay = 0\n");
    dir.write("mods/HD_TEST/convert/textures/CHARA/HERO.png", "png");
    RegistryConfig config;
    config.dirs = {dir.path() / "mods"};
    registry().configure(config);
    char out[1024];
    int dds = -1;
    SH_CHECK(ShModAsset_TimHasImage("CHARA/HERO.TIM"));
    SH_CHECK(!ShModAsset_TimPerRow("CHARA/HERO.TIM"));
    SH_CHECK(ShModAsset_TimWhole("CHARA/HERO.TIM", out, sizeof out, &dds) && dds == 0);
    SH_CHECK(fs::path(out) == dir.path() / "mods/HD_TEST/convert/textures/CHARA/HERO.png");
    SH_CHECK(ShModAsset_Open("CHARA/HERO.TIM", "rb") == nullptr);  // the disc TIM itself still loads
    registry().configure(RegistryConfig{});
}
