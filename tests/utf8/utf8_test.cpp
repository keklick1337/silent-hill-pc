// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// UTF-8 core tests: strict decode with byte offsets, counting, view advance,
// and the default codepoint -> font-cell mapping.

#include "framework/sh_test.h"

#include "engine/utf8/utf8.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#ifndef SH_PROJECT_ROOT
#define SH_PROJECT_ROOT "."
#endif

namespace fs = std::filesystem;

namespace {

struct Scratch {
    fs::path root;

    explicit Scratch(const char* name) : root(fs::path(SH_PROJECT_ROOT) / ".tmp" / "utf8" / name) {
        fs::remove_all(root);
        fs::create_directories(root);
    }
    ~Scratch() { std::error_code ec; fs::remove_all(root, ec); }
};

bool error_mentions(const sh::Result<std::vector<char32_t>>& result, std::string_view part) {
    return !result && result.error().find(part) != std::string::npos;
}

}  // namespace

SH_TEST(utf8_ascii_round_trip) {
    Scratch scratch("ascii");
    const std::string_view text = "Silent Hill";
    const auto decoded = sh::utf8::decode(text);
    SH_CHECK(decoded);
    SH_CHECK(decoded.value().size() == text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        SH_CHECK(decoded.value()[i] == static_cast<char32_t>(text[i]));
    }
    SH_CHECK(sh::utf8::valid(text));
    SH_CHECK(sh::utf8::codepoint_count(text) == text.size());
}

SH_TEST(utf8_cyrillic_privet) {
    Scratch scratch("cyrillic");
    const std::string_view text = "\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82";  // "Privet" in Cyrillic
    SH_CHECK(text.size() == 12);
    SH_CHECK(sh::utf8::valid(text));
    SH_CHECK(sh::utf8::codepoint_count(text) == 6);
    const auto decoded = sh::utf8::decode(text);
    SH_CHECK(decoded);
    SH_CHECK(decoded.value().size() == 6);
    SH_CHECK(decoded.value()[0] == U'\u041F');
    SH_CHECK(decoded.value()[5] == U'\u0442');
}

SH_TEST(utf8_cjk_seijaku) {
    Scratch scratch("cjk");
    const std::string_view text = "\xE9\x9D\x99\xE5\xAF\x82";  // 静寂
    SH_CHECK(text.size() == 6);
    SH_CHECK(sh::utf8::valid(text));
    SH_CHECK(sh::utf8::codepoint_count(text) == 2);
    const auto decoded = sh::utf8::decode(text);
    SH_CHECK(decoded);
    SH_CHECK(decoded.value()[0] == U'\u9759');
    SH_CHECK(decoded.value()[1] == U'\u5BC2');
}

SH_TEST(utf8_invalid_byte_reports_offset) {
    Scratch scratch("invalid");
    const std::string bad = std::string("ok") + std::string("\xFF", 1);
    const auto decoded = sh::utf8::decode(bad);
    SH_CHECK(error_mentions(decoded, "2"));
    SH_CHECK(!sh::utf8::valid(bad));
}

SH_TEST(utf8_overlong_rejected) {
    Scratch scratch("overlong");
    const std::string_view overlong = "\xC0\x80";  // NUL in two bytes
    SH_CHECK(!sh::utf8::decode(overlong));
    SH_CHECK(!sh::utf8::valid(overlong));
}

SH_TEST(utf8_truncated_rejected) {
    Scratch scratch("truncated");
    const std::string_view truncated = "\xD0";  // lead byte without continuation
    SH_CHECK(!sh::utf8::decode(truncated));
    SH_CHECK(!sh::utf8::valid(truncated));
}

SH_TEST(utf8_advance_over_mixed) {
    Scratch scratch("advance");
    std::string_view s = "a\xD0\xB1\xE9\x9D\x99z";  // 'a', U+0431, U+9759, 'z'
    SH_CHECK(sh::utf8::codepoint_count(s) == 4);
    SH_CHECK(sh::utf8::advance(s, 1));
    SH_CHECK(s.front() == '\xD0');
    SH_CHECK(sh::utf8::advance(s, 2));
    SH_CHECK(s.front() == 'z');
    SH_CHECK(sh::utf8::advance(s, 1));
    SH_CHECK(s.empty());
}

SH_TEST(utf8_advance_past_end_fails) {
    Scratch scratch("past_end");
    std::string_view s = "\xD0\xB0" "b";  // U+0430, b
    SH_CHECK(!sh::utf8::advance(s, 3));
    SH_CHECK(s.size() == 3);  // unchanged on failure
    std::string_view empty;
    SH_CHECK(!sh::utf8::advance(empty, 1));
    SH_CHECK(empty.empty());
}

SH_TEST(utf8_glyphmap_default_identity) {
    Scratch scratch("glyphmap");
    const sh::utf8::GlyphMap map;
    SH_CHECK(map.cell_of(U'a') == 'a');
    SH_CHECK(map.cell_of(U'\x7F') == 0x7F);
    SH_CHECK(map.cell_of(U'\x80') == -1);
    SH_CHECK(map.cell_of(U'\u041F') == -1);
}
