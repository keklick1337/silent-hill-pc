// The one loader of camera.json (shared_camera_config): base file or a mod's copy, read once.
// It keeps its first result for the whole process, so this file is its only caller in the tests.

#include <filesystem>
#include <fstream>
#include <string>

#include "engine/camera/camera_config.h"

#include "framework/sh_test.h"
#include "framework/mod_fixture.h"

namespace cam = sh::camera;
namespace fs = std::filesystem;

namespace {

void write_text(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << text;
    sh::test::mod_file_written(path);
}

}  // namespace

SH_TEST(camera_config_loaded_once_from_the_mod_copy) {
    const fs::path root = fs::temp_directory_path() / "sh_camera_config_load_test";
    fs::remove_all(root);
    write_text(root / "gamedata/config/camera.json", R"({"view": {"draw_cull_margin": 70}})");
    write_text(root / "mods/wide/mod.json", R"({"id": "wide", "version": "1.0.0", "priority": 10, "gameplay": true})");
    write_text(root / "mods/wide/config/camera.json",
               R"({"view": {"draw_cull_margin": 71}, "map_rules": {"tight_doorways": ["SC"]}})");

    std::string error;
    const cam::CameraConfig* first = &cam::shared_camera_config(root / "gamedata", root / "mods", &error);
    SH_CHECK(error.empty());
    SH_CHECK(first->draw_cull_margin == 71);
    SH_CHECK(first->map_rules.tight_doorway_types.size() == 1 && first->map_rules.tight_doorway_types[0].tag == "SC");
    SH_CHECK(first->map_rules.wide_shots.size() == 1);  // the lists the mod does not give stay

    // Later calls return the same config whatever they pass.
    const cam::CameraConfig* again = &cam::shared_camera_config(root / "nowhere", "", &error);
    SH_CHECK(again == first && again->draw_cull_margin == 71);
    fs::remove_all(root);
}
