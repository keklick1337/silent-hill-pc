#include "framework/sh_test.h"
#include "engine/gameflow/collision_zones.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

// The collision trigger zones (src/engine/gameflow/collision_zones.*): the shipped data equals the built-in boxes,
// the bounds are inclusive, and only the named maps are affected.

namespace {

namespace gf = sh::gameflow;

std::string read_file(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

}  // namespace

SH_TEST(collision_zones_shipped_equals_builtin) {
    const auto r = gf::CollisionZones::parse(
        read_file(std::filesystem::path(SH_PROJECT_ROOT) / "gamedata/config/collision_zones.json"));
    SH_CHECK(static_cast<bool>(r));
    const gf::CollisionZones b = gf::CollisionZones::builtin();
    SH_CHECK(r.value().zones().size() == b.zones().size());
    for (std::size_t i = 0; i < b.zones().size(); ++i) {
        const gf::CollisionZone &x = r.value().zones()[i], &y = b.zones()[i];
        SH_CHECK(x.maps == y.maps && x.min_x == y.min_x && x.max_x == y.max_x && x.min_z == y.min_z &&
                 x.max_z == y.max_z && x.clear == y.clear && x.set == y.set);
    }
}

SH_TEST(collision_zones_apply) {
    const gf::CollisionZones z = gf::CollisionZones::builtin();
    // Inside the town box: Objects (2) off, AltObjects (4) on; the other bits stay.
    SH_CHECK(z.apply("THR", 784793, -393216, 0x0B) == 0x0D);
    SH_CHECK(z.apply("THR", 814284, -369868, 0x03) == 0x05);
    SH_CHECK(z.apply("THR", 784792, -393216, 0x03) == 0x03);
    SH_CHECK(z.apply("THR", 784793, -369867, 0x03) == 0x03);
    SH_CHECK(z.apply("SC", 784793, -393216, 0x03) == 0x03);
    SH_CHECK(z.apply("SPU", -400000, -260000, 0x02) == 0x04);
    SH_CHECK(z.apply("", -400000, -260000, 0x02) == 0x02);
    SH_CHECK(!static_cast<bool>(gf::CollisionZones::parse("{\"zones\": [{\"maps\": [\"THR\"]}]}")));
    SH_CHECK(!static_cast<bool>(gf::CollisionZones::parse(
        "{\"zones\": [{\"maps\": [\"THR\"], \"min_x\": 0, \"max_x\": 1, \"min_z\": 0, \"max_z\": 1, \"set\": [\"Bogus\"]}]}")));
}
