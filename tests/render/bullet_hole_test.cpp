// The generated bullet holes (render/decals/bullet_hole.h): the sheet and the look of a hit.

#include "framework/sh_test.h"
#include "render/decals/bullet_hole.h"

#include <cmath>
#include <set>

namespace {

namespace r = sh::render;

const std::uint8_t* texel(const std::vector<std::uint8_t>& sheet, int cell, int x, int y) {
    const int sx = (cell % r::kBulletHoleGrid) * r::kBulletHoleCell + x;
    const int sy = (cell / r::kBulletHoleGrid) * r::kBulletHoleCell + y;
    return &sheet[static_cast<std::size_t>((sy * r::kBulletHoleSheet + sx) * 4)];
}

}  // namespace

SH_TEST(bullet_hole_sheet_is_deterministic_and_seeded) {
    const std::vector<std::uint8_t> a = r::bullet_hole_sheet(1);
    SH_CHECK(a.size() == static_cast<std::size_t>(r::kBulletHoleSheet * r::kBulletHoleSheet * 4));
    SH_CHECK(a == r::bullet_hole_sheet(1));
    SH_CHECK(a != r::bullet_hole_sheet(2));
}

SH_TEST(bullet_hole_cells_are_holes_that_fade_before_their_edge) {
    const std::vector<std::uint8_t> sheet = r::bullet_hole_sheet(0x5348B011u);
    const int n = r::kBulletHoleCell;
    for (int cell = 0; cell < r::kBulletHoleVariants; ++cell) {
        // Nothing on the border: neighbours cannot bleed into each other under a filter.
        for (int i = 0; i < n; ++i) {
            SH_CHECK(texel(sheet, cell, i, 0)[3] == 0 && texel(sheet, cell, i, n - 1)[3] == 0);
            SH_CHECK(texel(sheet, cell, 0, i)[3] == 0 && texel(sheet, cell, n - 1, i)[3] == 0);
        }
        // The middle is the punched hole: covered, almost black, flat at its bottom.
        const std::uint8_t* core = texel(sheet, cell, n / 2, n / 2);
        SH_CHECK(core[3] > 240 && core[2] < 16);
        SH_CHECK(std::abs(core[0] - 128) < 12 && std::abs(core[1] - 128) < 12);
        // The crater's wall tilts the normal toward the hole on both sides of it (the relief the light picks out),
        // and bares material lighter than the hole.
        int left = 0, right = 255, lightest = 0;
        for (int x = 0; x < n; ++x) {
            const std::uint8_t* t = texel(sheet, cell, x, n / 2);
            if (x < n / 2 && t[0] > left) left = t[0];
            if (x > n / 2 && t[0] < right) right = t[0];
            if (t[3] > 128 && t[2] > lightest) lightest = t[2];
        }
        SH_CHECK(left > 128 + 20 && right < 128 - 20);
        SH_CHECK(lightest > 48);
    }
}

SH_TEST(bullet_hole_cells_differ) {
    const std::vector<std::uint8_t> sheet = r::bullet_hole_sheet(0x5348B011u);
    const int n = r::kBulletHoleCell;
    std::set<std::vector<std::uint8_t>> seen;
    for (int cell = 0; cell < r::kBulletHoleVariants; ++cell) {
        std::vector<std::uint8_t> alpha;
        for (int y = 0; y < n; ++y) {
            for (int x = 0; x < n; ++x) alpha.push_back(texel(sheet, cell, x, y)[3]);
        }
        seen.insert(std::move(alpha));
    }
    SH_CHECK(seen.size() == static_cast<std::size_t>(r::kBulletHoleVariants));
}

SH_TEST(bullet_hole_look_depends_on_the_hit_point_alone) {
    const r::BulletHoleLook a = r::bullet_hole_look(573440, -5600, 75400);
    const r::BulletHoleLook b = r::bullet_hole_look(573440, -5600, 75400);
    SH_CHECK(a.variant == b.variant && a.angle == b.angle && a.scale == b.scale && a.mirrored == b.mirrored);
    std::set<int> variants;
    float lo = 10.f, hi = 0.f;
    bool turned = false;
    for (int i = 0; i < 400; ++i) {
        const r::BulletHoleLook l = r::bullet_hole_look(573440 + i * 37, -5600 + i, 75400 - i * 11);
        SH_CHECK(l.variant >= 0 && l.variant < r::kBulletHoleVariants);
        SH_CHECK(l.angle >= 0.f && l.angle < 6.2832f);
        variants.insert(l.variant);
        lo = l.scale < lo ? l.scale : lo;
        hi = l.scale > hi ? l.scale : hi;
        turned = turned || std::fabs(l.angle - a.angle) > 1.f;
    }
    SH_CHECK(variants.size() == static_cast<std::size_t>(r::kBulletHoleVariants));
    SH_CHECK(lo >= 0.8f && hi <= 1.36f && hi - lo > 0.4f && turned);
}
