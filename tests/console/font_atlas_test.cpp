#include "framework/sh_test.h"

#include "engine/console/font_atlas.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace fs = std::filesystem;
using namespace sh::console;

namespace {

fs::path font_path() {
    return fs::path(SH_PROJECT_ROOT) / "third_party/fonts/jetbrains-mono/JetBrainsMonoNL-Regular.ttf";
}

std::vector<std::uint8_t> font_bytes() {
    std::ifstream in(font_path(), std::ios::binary);
    return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

FontAtlas build(float px) {
    const auto bytes = font_bytes();
    auto r = FontAtlas::build(bytes, px);
    if (!r) throw sh::test::Skip{"font build failed: " + r.error()};
    return r.value();
}

std::uint32_t decode(const std::string& s, std::size_t* end = nullptr) {
    std::size_t pos = 0;
    const char32_t cp = next_code_point(s, pos);
    if (end) *end = pos;
    return cp;
}

}  // namespace

SH_TEST(font_atlas_metrics_and_ascii) {
    const FontAtlas a = build(18.0f);
    SH_CHECK(a.cell_width() >= 1.0f);
    SH_CHECK(a.cell_height() >= 18.0f);
    SH_CHECK(a.ascent() > 0.0f && a.ascent() < a.cell_height());
    SH_CHECK(a.width() <= 4096 && a.height() <= 4096);
    SH_CHECK(a.pixels().size() == static_cast<std::size_t>(a.width()) * a.height());
    for (char32_t c = 0x20; c <= 0x7E; ++c) {
        SH_CHECK(a.has(c));
        const Glyph& g = a.glyph(c);
        SH_CHECK(g.u0 >= 0.0f && g.u1 <= 1.0f && g.v0 >= 0.0f && g.v1 <= 1.0f);
        SH_CHECK(g.u0 <= g.u1 && g.v0 <= g.v1);
        SH_CHECK(g.w <= a.cell_width() + 2.0f);  // monospace: no bitmap is wider than the cell (plus overhang)
    }
    SH_CHECK(a.glyph(U' ').w == 0.0f);
    const Glyph& A = a.glyph(U'A');
    SH_CHECK(A.w > 0.0f && A.h > 0.0f);
    int sum = 0;
    const auto y0 = static_cast<std::uint32_t>(A.v0 * a.height() + 0.5f);
    const auto y1 = static_cast<std::uint32_t>(A.v1 * a.height() + 0.5f);
    const auto x0 = static_cast<std::uint32_t>(A.u0 * a.width() + 0.5f);
    const auto x1 = static_cast<std::uint32_t>(A.u1 * a.width() + 0.5f);
    for (std::uint32_t y = y0; y < y1; ++y)
        for (std::uint32_t x = x0; x < x1; ++x) sum += a.pixels()[static_cast<std::size_t>(y) * a.width() + x];
    SH_CHECK(sum > 0);
}

SH_TEST(font_atlas_fallback_and_ranges) {
    const FontAtlas a = build(18.0f);
    SH_CHECK(!a.has(0x4E2D));
    const Glyph& q = a.glyph(U'?');
    const Glyph& unknown = a.glyph(0x4E2D);
    SH_CHECK(&q == &unknown);
    SH_CHECK(a.glyph(U'\n').w == 0.0f);
    SH_CHECK(a.has(0x0416));  // Cyrillic
    SH_CHECK(a.glyph(0x0416).w > 0.0f);
    SH_CHECK(a.has(0x00E9));
}

SH_TEST(font_atlas_sizes_grow) {
    const FontAtlas s = build(14.0f), m = build(18.0f), l = build(24.0f);
    SH_CHECK(s.cell_width() < m.cell_width() && m.cell_width() < l.cell_width());
    SH_CHECK(s.cell_height() < m.cell_height() && m.cell_height() < l.cell_height());
}

SH_TEST(font_atlas_solid_block) {
    const FontAtlas a = build(18.0f);
    const Glyph s = a.solid();
    SH_CHECK(s.u0 < s.u1 && s.v0 < s.v1 && s.u0 >= 0.0f && s.u1 <= 1.0f);
    const auto px = [&](float u, float v) {
        return a.pixels()[static_cast<std::size_t>(v * a.height()) * a.width() + static_cast<std::size_t>(u * a.width())];
    };
    SH_CHECK(px(s.u0, s.v0) == 255);
    SH_CHECK(px((s.u0 + s.u1) / 2, (s.v0 + s.v1) / 2) == 255);
}

SH_TEST(font_atlas_bad_font) {
    const std::vector<std::uint8_t> junk(64, 7);
    SH_CHECK(!FontAtlas::build(junk, 18.0f));
}

SH_TEST(font_atlas_utf8) {
    SH_CHECK(decode("A") == 'A');
    SH_CHECK(decode("\xC3\xA9") == 0xE9);
    SH_CHECK(decode("\xD0\x96") == 0x416);
    SH_CHECK(decode("\xE2\x80\xA6") == 0x2026);
    SH_CHECK(decode("\xF0\x9F\x98\x80") == 0x1F600);
    std::size_t end = 0;
    SH_CHECK(decode("\xC0\x80", &end) == 0xFFFD && end == 1);  // overlong
    SH_CHECK(decode("\xE0\x80\x80", &end) == 0xFFFD && end == 1);
    SH_CHECK(decode("\xED\xA0\x80", &end) == 0xFFFD && end == 1);  // surrogate
    SH_CHECK(decode("\xE2\x80", &end) == 0xFFFD && end == 1);      // truncated
    SH_CHECK(decode("\x80", &end) == 0xFFFD && end == 1);          // stray continuation
    SH_CHECK(decode("\xFF", &end) == 0xFFFD && end == 1);
    SH_CHECK(decode("\xC3\x28", &end) == 0xFFFD && end == 1);
    const std::string s = "a\xD0\x96z";
    std::size_t pos = 0;
    SH_CHECK(next_code_point(s, pos) == 'a' && pos == 1);
    SH_CHECK(next_code_point(s, pos) == 0x416 && pos == 3);
    SH_CHECK(next_code_point(s, pos) == 'z' && pos == 4);
}

SH_TEST(font_atlas_candidates) {
    const fs::path dir = fs::temp_directory_path() / "sh_font_atlas_test";
    fs::create_directories(dir);
    const fs::path cfg = dir / "cfg.ttf", bundled = dir / "bundled.ttf", missing = dir / "none.ttf";
    std::ofstream(cfg) << "x";
    std::ofstream(bundled) << "y";
    auto c = font_candidates(cfg, bundled);
    SH_CHECK(c.size() >= 2 && c.front() == cfg);
    std::size_t bi = c.size();
    for (std::size_t i = 0; i < c.size(); ++i)
        if (c[i] == bundled) bi = i;
    SH_CHECK(bi < c.size() && bi >= 1);
    auto none = font_candidates(missing, bundled);
    SH_CHECK(!none.empty() && none.front() != missing);
    // Garbage files are skipped; the real font is taken.
    std::vector<fs::path> list{cfg, font_path()};
    fs::path used;
    auto r = load_first_font(list, &used);
    SH_CHECK(r && used == font_path());
    SH_CHECK(!load_first_font(std::vector<fs::path>{cfg}));
    SH_CHECK(!load_first_font(std::vector<fs::path>{}));
    std::error_code ec;
    fs::remove_all(dir, ec);
}
