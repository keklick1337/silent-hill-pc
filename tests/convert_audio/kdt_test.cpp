#include "framework/sh_disc.h"
#include "framework/sh_test.h"
#include "formats/convert/audio/audio_converter.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace {

using Bytes = std::vector<std::uint8_t>;

// One track: wait 0, channel 2, program 5, tempo 59 (bpm 138), note 60 vel 100, wait 120, off, wait 0, end.
Bytes synthetic_kdt() {
    const Bytes track = {0x00, 0xC6, 0x02, 0x00, 0xC9, 0x05, 0x00, 0xC7, 0x3B, 0x00, 60, 100, 0x78,
                         0xCA, 0x00, 0xE3, 0x94, 0xFF, 0x00, 0xAA};
    Bytes b = {'K', 'D', 'T', '1', 0, 0, 0, 0, 0xE0, 0x01, 0, 0, 1, 0, 0, 0,
               static_cast<std::uint8_t>(track.size()), 0};
    b.insert(b.end(), track.begin(), track.end());
    b[4] = static_cast<std::uint8_t>(b.size());
    b.insert(b.end(), {0, 0, 0});  // padding behind the tracks
    return b;
}

}  // namespace

SH_TEST(KdtJsonRoundTripAndMidi) {
    const Bytes raw = synthetic_kdt();
    const auto conv = sh::convert::audio::make_kdt_converter();
    SH_CHECK(conv->lossless());
    SH_CHECK(conv->accepts(raw, "SND/T.KDT"));
    SH_CHECK(!conv->accepts(raw, "SND/T.VAB"));
    const auto out = conv->decode(raw, "SND/T.KDT");
    SH_CHECK(out && out.value().size() == 2);
    if (!out) {
        return;
    }
    SH_CHECK(out.value()[0].relative_path == "audio/seq/SND/T.kdt.json");
    SH_CHECK(out.value()[1].relative_path == "audio/seq/SND/T.mid");
    const std::string text(out.value()[0].bytes.begin(), out.value()[0].bytes.end());
    SH_CHECK(text.find("[\"note\", 60, 100]") != std::string::npos);
    SH_CHECK(text.find("[\"cc\", 99, 148]") != std::string::npos);  // 0xE3 0x94: loop start, no delta after
    const auto back = conv->encode(out.value());
    SH_CHECK(back && back.value() == raw);

    const Bytes& mid = out.value()[1].bytes;
    SH_CHECK(mid.size() > 22 && std::string(mid.begin(), mid.begin() + 4) == "MThd");
    SH_CHECK(mid[12] == 0x01 && mid[13] == 0xE0);  // division 480
    // Program change on channel 2, note on 60/100 on channel 2.
    const auto has = [&](std::initializer_list<std::uint8_t> seq) {
        return std::search(mid.begin(), mid.end(), seq.begin(), seq.end()) != mid.end();
    };
    SH_CHECK(has({0xC2, 0x05}));
    SH_CHECK(has({0x92, 60, 100}));
    SH_CHECK(has({0x78, 0x82, 60, 0}));  // 120 ticks later, the note off
}

SH_TEST(EveryKdtOfTheImageRoundTrips) {
    const auto disc = sh::test::open_game_disc();
    SH_CHECK(disc.has_value());
    if (!disc) {
        return;
    }
    const auto conv = sh::convert::audio::make_kdt_converter();
    std::size_t seen = 0;
    const std::size_t converted = sh::test::round_trip_all(*disc, *conv, ".KDT", &seen);
    std::printf("  KDT: %zu files, %zu converted\n", seen, converted);
    SH_CHECK(seen == 40 && converted == 40);
}
