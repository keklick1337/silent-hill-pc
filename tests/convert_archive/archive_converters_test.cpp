#include "framework/sh_disc.h"
#include "framework/sh_test.h"
#include "formats/convert/archive/archive_converters.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace {

namespace ar = sh::convert::archive;
using Bytes = std::vector<std::uint8_t>;

}  // namespace

SH_TEST(LzssRoundTrip) {
    Bytes data;
    for (int i = 0; i < 5000; ++i) {
        data.push_back(static_cast<std::uint8_t>((i % 7) * 13 + (i / 300)));
    }
    data.insert(data.end(), 100, 0);
    const Bytes packed = ar::lzss_encode(data);
    SH_CHECK(packed.size() < data.size());
    std::size_t used = 0;
    const auto back = ar::lzss_decode(packed, &used);
    SH_CHECK(back && back.value() == data);
    SH_CHECK(used == packed.size());
    SH_CHECK(ar::lzss_decode(ar::lzss_encode({})).value().empty());
    SH_CHECK(!ar::lzss_decode(Bytes{0x10, 0, 0, 0, 0xFF, 1}));  // ends before 16 bytes
}

SH_TEST(CmpConverterKeepsTheSectorTail) {
    Bytes tim = {0x10, 0, 0, 0, 2, 0, 0, 0};
    for (int i = 0; i < 300; ++i) {
        tim.push_back(static_cast<std::uint8_t>(i * 3));
    }
    Bytes raw = ar::lzss_encode(tim);
    raw.insert(raw.end(), {0xDE, 0xAD, 0xBE});
    const auto conv = ar::make_cmp_converter();
    SH_CHECK(conv->accepts(raw, "TEST/X.CMP"));
    const auto out = conv->decode(raw, "TEST/X.CMP");
    SH_CHECK(out && out.value().size() == 2);
    if (!out) {
        return;
    }
    SH_CHECK(out.value()[0].relative_path == "unpacked/TEST/X.tim" && out.value()[0].bytes == tim);
    SH_CHECK(out.value()[1].relative_path == "unpacked/TEST/X.cmp.json");
    const auto back = conv->encode(out.value());
    SH_CHECK(back && back.value() == raw);
}

SH_TEST(DemoConverterRoundTrip) {
    const auto conv = ar::make_demo_converter();
    Bytes state(2048, 0);
    state[0x1F] = 16;
    state[0x1C] = 0xF5;  // s8 -11
    state[0x150] = 0x42;
    state[0x7F8] = 0x34;
    state[0x7F9] = 0x12;
    state[0x7FC] = 0x99;
    SH_CHECK(conv->accepts(state, "MISC/DEMO0001.DAT"));
    SH_CHECK(!conv->accepts(state, "MISC/OTHER.DAT"));
    auto out = conv->decode(state, "MISC/DEMO0001.DAT");
    SH_CHECK(out);
    if (out) {
        const std::string text(out.value()[0].bytes.begin(), out.value()[0].bytes.end());
        SH_CHECK(text.find("\"screen_position_x\": -11") != std::string::npos);
        SH_CHECK(text.find("\"frame_count\": 4660") != std::string::npos);
        const auto back = conv->encode(out.value());
        SH_CHECK(back && back.value() == state);
    }
    Bytes play(4 * 16);
    for (int f = 0; f < 4; ++f) {
        const std::uint8_t frame[16] = {0, 0x73, 0xFF, static_cast<std::uint8_t>(0xFF - f), 0x7F, 0x6F, 0x81, 0x74,
                                        0x0B, 1, 0x2C, 0x2C, static_cast<std::uint8_t>(f), 0x1E, 0, 0x80};
        std::copy(frame, frame + 16, play.begin() + f * 16);
    }
    SH_CHECK(conv->accepts(play, "MISC/PLAY0001.DAT"));
    out = conv->decode(play, "MISC/PLAY0001.DAT");
    SH_CHECK(out);
    if (out) {
        SH_CHECK(out.value()[0].relative_path == "demo/MISC/PLAY0001.dat.json");
        const auto back = conv->encode(out.value());
        SH_CHECK(back && back.value() == play);
    }
}

SH_TEST(EveryCmpAndDemoOfTheImageRoundTrips) {
    const auto disc = sh::test::open_game_disc();
    SH_CHECK(disc.has_value());
    if (!disc) {
        return;
    }
    std::size_t cmp_seen = 0, dat_seen = 0;
    const std::size_t cmp = sh::test::round_trip_all(*disc, *ar::make_cmp_converter(), ".CMP", &cmp_seen);
    const std::size_t dat = sh::test::round_trip_all(*disc, *ar::make_demo_converter(), ".DAT", &dat_seen);
    std::printf("  CMP: %zu of %zu, DAT: %zu of %zu\n", cmp, cmp_seen, dat, dat_seen);
    SH_CHECK(cmp == 5 && cmp_seen == 5);
    SH_CHECK(dat == 32 && dat_seen == 32);
    // The unpacked CMP is the TIM next to it (the TIM file is the same bytes plus sector padding).
    const auto packed = disc->read("TEST/RESIDENT.CMP");
    const auto tim = disc->read("TEST/RESIDENT.TIM");
    SH_CHECK(packed && tim);
    if (packed && tim) {
        const auto unpacked = ar::lzss_decode(*packed);
        SH_CHECK(unpacked && unpacked.value().size() == 99872);
        SH_CHECK(unpacked && std::equal(unpacked.value().begin(), unpacked.value().end(), tim->begin()));
    }
}
