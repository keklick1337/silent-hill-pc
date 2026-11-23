#include "framework/sh_test.h"

#include "engine/gconfig/map_info.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using sh::gconfig::MapInfoLibrary;

namespace {

std::string shipped_text() {
    std::ifstream in(fs::path(SH_PROJECT_ROOT) / "gamedata" / "config" / "map_info.json", std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

}  // namespace

SH_TEST(map_info_shipped_file_loads) {
    std::vector<std::string> warnings;
    const auto lib = MapInfoLibrary::load(fs::path(SH_PROJECT_ROOT) / "gamedata", {}, warnings);
    SH_CHECK(static_cast<bool>(lib));
    SH_CHECK((lib.value().maps().size()) == (16u));
    SH_CHECK((lib.value().environments().size()) == (21u));
    SH_CHECK((lib.value().maps()[0].tag) == (std::string("THR")));
    SH_CHECK((lib.value().maps()[1].flags) == (sh::gconfig::kMapTwoActiveChunks | sh::gconfig::kMapInterior));
    const auto* speed = lib.value().speed_zones("SP");
    SH_CHECK(speed != nullptr);
    SH_CHECK((speed->size()) == (4u));
    SH_CHECK((lib.value().environments()[3].fog_distance) == (163840));  // Q12(40)
    SH_CHECK(lib.value().water_zones("WaterLight1") != nullptr);
}

SH_TEST(map_info_rejects_bad_data) {
    std::string text = shipped_text();
    const std::size_t at = text.find("\"TwoActiveChunks\"");
    SH_CHECK(at != std::string::npos);
    std::string bad_flag = text;
    bad_flag.replace(at, 17, "\"Basement\"");
    SH_CHECK(!static_cast<bool>(MapInfoLibrary::parse(bad_flag)));

    std::string bad_set = text;
    const std::size_t sp = bad_set.find("\"speed_zones\": \"THR\"");
    SH_CHECK(sp != std::string::npos);
    bad_set.replace(sp, 20, "\"speed_zones\": \"NOPE\"");
    SH_CHECK(!static_cast<bool>(MapInfoLibrary::parse(bad_set)));

    SH_CHECK(!static_cast<bool>(MapInfoLibrary::parse("{}")));
}
