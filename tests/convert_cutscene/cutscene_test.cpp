#include "framework/sh_disc.h"
#include "framework/sh_test.h"
#include "formats/convert/cutscene/cutscene_converter.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace {

namespace cs = sh::convert::cutscene;
using Bytes = std::vector<std::uint8_t>;

void put16(Bytes& b, std::size_t at, int v) {
    b[at] = static_cast<std::uint8_t>(v);
    b[at + 1] = static_cast<std::uint8_t>(v >> 8);
}
void put32(Bytes& b, std::size_t at, std::uint32_t v) {
    for (std::size_t i = 0; i < 4; ++i) {
        b[at + i] = static_cast<std::uint8_t>(v >> (8 * i));
    }
}

// Header, one character "HERO" (2 keyframes, 1 hold), the camera (1 keyframe), 2 segments, padding with junk.
Bytes synthetic_dms() {
    Bytes b(160, 0);
    b[1] = 1;  // characters
    b[2] = 2;  // segments
    put32(b, 4, 77);
    put32(b, 8, 0x3C);   // segments
    put32(b, 12, 1000);  // origin x
    put32(b, 16, static_cast<std::uint32_t>(-256));
    put32(b, 20, 3);
    put32(b, 0x18, 0x2C);  // characters
    // camera entry at 0x1C
    put16(b, 0x1C, 1);
    b[0x1E] = 0;
    std::copy_n("%CAM", 4, b.begin() + 0x20);
    put32(b, 0x24, 0x44);
    put32(b, 0x28, 0x44);
    // character entry at 0x2C
    put16(b, 0x2C, 2);
    b[0x2E] = 1;
    b[0x2F] = 0x5F;
    std::copy_n("HERO", 4, b.begin() + 0x30);
    put32(b, 0x34, 0x54);
    put32(b, 0x38, 0x5A);
    // segments at 0x3C
    put16(b, 0x3C, 0);
    put16(b, 0x3E, 10);
    put16(b, 0x40, 10);
    put16(b, 0x42, 5);
    // camera keyframe at 0x44 (16 bytes)
    for (int i = 0; i < 8; ++i) {
        put16(b, 0x44 + static_cast<std::size_t>(i) * 2, 100 * (i + 1) - 400);
    }
    // hold at 0x54
    put16(b, 0x54, 3);
    put16(b, 0x56, 7);
    put16(b, 0x58, 1);
    // character keyframes at 0x5A (2 x 12 bytes)
    for (int i = 0; i < 12; ++i) {
        put16(b, 0x5A + static_cast<std::size_t>(i) * 2, i * 37 - 200);
    }
    b[0x80] = 0xEE;  // junk in the padding
    return b;
}

}  // namespace

SH_TEST(DmsJsonRoundTripIsLossless) {
    const Bytes raw = synthetic_dms();
    const auto dms = cs::parse_dms(raw);
    SH_CHECK(dms);
    if (!dms) {
        return;
    }
    SH_CHECK(dms.value().characters.size() == 1);
    SH_CHECK(dms.value().characters[0].keyframes.size() == 2);
    SH_CHECK(dms.value().characters[0].holds.size() == 1 && dms.value().characters[0].holds[0].end == 7);
    SH_CHECK(dms.value().camera.keyframes.size() == 1 && dms.value().camera.keyframes[0][7] == 400);
    SH_CHECK(dms.value().origin[1] == -256);

    const auto conv = cs::make_converter();
    SH_CHECK(conv->lossless());
    SH_CHECK(conv->accepts(raw, "ANIM/TEST.DMS"));
    SH_CHECK(!conv->accepts(raw, "ANIM/TEST.ANM"));
    SH_CHECK(!conv->accepts(Bytes(16, 0), "ANIM/TEST.DMS"));
    const auto out = conv->decode(raw, "ANIM/TEST.DMS");
    SH_CHECK(out && out.value().size() == 1);
    if (!out) {
        return;
    }
    SH_CHECK(out.value()[0].relative_path == "cutscenes/ANIM/TEST.dms.json");
    const std::string text(out.value()[0].bytes.begin(), out.value()[0].bytes.end());
    SH_CHECK(text.find("\"name\": \"HERO\"") != std::string::npos);
    SH_CHECK(text.find("\"frames\": 15") != std::string::npos);
    const auto back = conv->encode(out.value());
    SH_CHECK(back && back.value() == raw);

    // Damaged offsets are refused.
    Bytes bad = raw;
    put32(bad, 0x38, 5000);
    SH_CHECK(!cs::parse_dms(bad));
}

SH_TEST(EveryDmsOfTheImageRoundTrips) {
    const auto disc = sh::test::open_game_disc();
    SH_CHECK(disc.has_value());
    if (!disc) {
        return;
    }
    const auto conv = cs::make_converter();
    std::size_t seen = 0;
    const std::size_t converted = sh::test::round_trip_all(*disc, *conv, ".DMS", &seen);
    std::printf("  DMS: %zu files, %zu converted\n", seen, converted);
    SH_CHECK(seen == 46);
    SH_CHECK(converted == 45);  // TEST/TEST2.DMS has an older layout ("CAME" and "HERO" entries elsewhere); kept raw
}
