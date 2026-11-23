#include "framework/sh_test.h"
#include "formats/common/crc32.h"
#include "import/crc32_stream.h"
#include "import/index.h"
#include "import/sha256.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace {

using sh::import::Sha256;

std::vector<std::uint8_t> pattern(std::size_t n) {
    std::vector<std::uint8_t> bytes(n);
    for (std::size_t i = 0; i < n; ++i) {
        bytes[i] = static_cast<std::uint8_t>((i * 7 + 3) & 255);
    }
    return bytes;
}

std::string hex_of(std::string_view text) {
    return sh::import::sha256_hex(text.data(), text.size());
}

}  // namespace

// FIPS 180-4 / NIST example messages.
SH_TEST(import_sha256_known_vectors) {
    SH_CHECK(hex_of("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    SH_CHECK(hex_of("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    SH_CHECK(hex_of("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
             "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    SH_CHECK(hex_of("abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu") ==
             "cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1");
}

SH_TEST(import_sha256_million_a) {
    Sha256 hash;
    const std::string block(1000, 'a');
    for (int i = 0; i < 1000; ++i) {
        hash.update(block.data(), block.size());
    }
    SH_CHECK(sh::import::to_hex(hash.finish()) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

// Lengths around the 56 and 64 byte padding boundaries; expected values are Python hashlib's.
SH_TEST(import_sha256_padding_boundaries) {
    struct Case {
        std::size_t size;
        const char* hex;
    };
    const Case cases[] = {
        {55, "e7313d333c272e639f790978283f9eb392e843d0f29b7016828bb1daa4aac70b"},
        {56, "4324d65f3c103567f5589c710bc08f8523f929a9272e3af36fc968e52abc6c27"},
        {57, "35df609437dcfea3279283ab79fd554e2bf78f8f7ae2de532d8ee300b09e8f73"},
        {63, "81c80242132f230c3bd41b3e63bbcff16107339549214a99614ff26664625055"},
        {64, "39e3d7b6b5d075d37d053ad89b24b41bef4f3c29760c84447cab3f3be1882241"},
        {65, "aacca6ff74fdbb296d165a45cecfa04e5127bc008770fbbdd48006f2d2fae95e"},
        {119, "9ce7368e4daf32341631b492e80359dc9f594b48453cd0dd5bf0b19279cc177e"},
        {120, "7836b787757e95e58b3ca5aec90b1b004e8deba1e50e9675af9cabf1a13a04b5"},
        {121, "1189a98a00c71bc1848ea8bdc9700b442bee0be7c3f45172303f1ab0b6f1617e"},
        {1000, "1e9bc38cbf860b9ec31918b065f9b52476c549a782e0e7990bed8ce3868d2371"},
    };
    for (const Case& c : cases) {
        const std::vector<std::uint8_t> bytes = pattern(c.size);
        SH_CHECK(sh::import::sha256_hex(bytes.data(), bytes.size()) == c.hex);
    }
}

SH_TEST(import_sha256_incremental_matches_one_shot) {
    const std::vector<std::uint8_t> bytes = pattern(1000);
    for (const std::size_t step : {1u, 3u, 63u, 64u, 65u, 500u}) {
        Sha256 hash;
        for (std::size_t at = 0; at < bytes.size(); at += step) {
            hash.update(bytes.data() + at, std::min(step, bytes.size() - at));
        }
        SH_CHECK(sh::import::to_hex(hash.finish()) == "1e9bc38cbf860b9ec31918b065f9b52476c549a782e0e7990bed8ce3868d2371");
    }
}

SH_TEST(import_crc32_stream_matches_module_crc32) {
    const std::vector<std::uint8_t> bytes = pattern(5000);
    sh::import::Crc32Stream stream;
    stream.update(bytes.data(), 1);
    stream.update(bytes.data() + 1, 4093);
    stream.update(bytes.data() + 4094, bytes.size() - 4094);
    SH_CHECK(stream.value() == sh::crc32(bytes.data(), bytes.size()));
    SH_CHECK(sh::import::Crc32Stream().value() == 0);
    SH_CHECK(sh::import::crc32_hex(0x2D30905C) == "2D30905C");
    SH_CHECK(sh::import::crc32_hex(0x1) == "00000001");
}

SH_TEST(import_index_text_round_trips) {
    std::vector<sh::import::IndexRow> rows(2);
    rows[0] = {"CHARA/HERO.TIM", 14592, 0x2D30905C, "SILENT:id=1422", "tim",
               {"textures/CHARA/HERO.png", "textures/CHARA/HERO.tim.json"}};
    rows[1] = {"SYSTEM.CNF", 68, 0x1F1A87CC, "ISO:file", "cnf", {}};
    const std::string text = sh::import::format_index(rows);
    SH_CHECK(text ==
             "path\tsize\tcrc32\tsource\tformat\tconverted\n"
             "CHARA/HERO.TIM\t14592\t2D30905C\tSILENT:id=1422\ttim\ttextures/CHARA/HERO.png;textures/CHARA/HERO.tim.json\n"
             "SYSTEM.CNF\t68\t1F1A87CC\tISO:file\tcnf\t\n");

    const auto parsed = sh::import::parse_index(text);
    SH_CHECK(parsed);
    if (parsed) {
        SH_CHECK(parsed.value().size() == 2);
        SH_CHECK(parsed.value()[0].path == rows[0].path && parsed.value()[0].size == rows[0].size);
        SH_CHECK(parsed.value()[0].crc32 == rows[0].crc32 && parsed.value()[0].converted == rows[0].converted);
        SH_CHECK(parsed.value()[1].converted.empty());
    }
    SH_CHECK(!sh::import::parse_index("path\tsize\n"));
    SH_CHECK(!sh::import::parse_index(text.substr(0, text.size() - 1)));
}
