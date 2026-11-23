// The title fog against its specification (the original's title fog): seed row, in-place averaging,
// scatter cadence and the black upper half; and the palette file's checks.

#include <string>

#include "engine/gameflow/flow_data.h"
#include "engine/gameflow/title_fog.h"

#include "framework/sh_test.h"

namespace gf = sh::gameflow;

namespace {

class Counter final : public gf::TitleFog::Random {
public:
    int calls = 0;
    std::int32_t value = 0;
    std::int32_t rand16() override {
        ++calls;
        return value;
    }
};

}  // namespace

SH_TEST(title_fog_waits_for_init) {
    gf::TitleFog f;
    Counter r;
    SH_CHECK(!f.frame(r));
    SH_CHECK(r.calls == 0);
}

SH_TEST(title_fog_seed_and_spread) {
    gf::TitleFog f;
    Counter r;  // every random number 0: angle steps by 4, hot and cold cells both land on column 0
    f.init(r);
    // 30 scatters, 1 + 16 + 9 random numbers each.
    SH_CHECK(r.calls == 30 * 26);
    // Seed row: base (sin(120) * 10 >> 12) - 122 = 1 - 122 as a byte, column 0 cold.
    const int seed = gf::TitleFog::kSide * gf::TitleFog::kSide;
    SH_CHECK(f.heat(seed) == 0);
    SH_CHECK(f.heat(seed + 1) == static_cast<std::uint8_t>(-121));
    // The bottom row heats up from the seed row; the top half is drawn black.
    SH_CHECK(f.heat(seed - 5) > 0);
    SH_CHECK(f.shade(0) == -1);
    SH_CHECK(f.shade(gf::TitleFog::kFirstDrawnCell) == f.heat(gf::TitleFog::kFirstDrawnCell));

    // A scatter on every 5th frame (counted from the first frame call).
    r.calls = 0;
    SH_CHECK(f.frame(r));
    SH_CHECK(r.calls == 26);
    for (int i = 0; i < 4; ++i) {
        f.frame(r);
    }
    SH_CHECK(r.calls == 26);
    f.frame(r);
    SH_CHECK(r.calls == 52);
}

SH_TEST(title_fog_palette_file) {
    std::string json = "{\"colors\": [";
    for (int i = 0; i < 256; ++i) {
        json += (i ? "," : "") + std::string("[") + std::to_string(i) + ",1,2]";
    }
    json += "]}";
    const auto p = gf::TitleFogPalette::parse(json);
    SH_CHECK(p);
    SH_CHECK(p.value().color(200).r == 200 && p.value().color(200).b == 2);
    SH_CHECK(!gf::TitleFogPalette::parse("{\"colors\": [[1,2,3]]}"));
    SH_CHECK(!gf::TitleFogPalette::parse("{\"colors\": [[1,2,300]]}"));
}
