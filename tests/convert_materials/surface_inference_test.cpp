// The import's proposed surface materials (src/formats/convert/materials/surface_inference.h): a page of floor tiles
// is recognised by its grout grid, a page that is mostly flat filler is left neutral, and so is plain noise.

#include "formats/convert/materials/surface_inference.h"

#include "framework/sh_test.h"

#include <cstdint>
#include <vector>

namespace cm = sh::convert::materials;

namespace {

std::vector<unsigned char> page(unsigned size, unsigned char (*texel)(unsigned x, unsigned y)) {
    std::vector<unsigned char> rgba(static_cast<std::size_t>(size) * size * 4);
    for (unsigned y = 0; y < size; ++y)
        for (unsigned x = 0; x < size; ++x) {
            unsigned char* p = &rgba[(static_cast<std::size_t>(y) * size + x) * 4];
            p[0] = p[1] = p[2] = texel(x, y);
            p[3] = 255;
        }
    return rgba;
}

}  // namespace

SH_TEST(surface_inference_tiles_by_their_grout) {
    // 16-texel tiles with dark grout lines and a little texture inside.
    const auto rgba = page(128, [](unsigned x, unsigned y) -> unsigned char {
        if (x % 16 == 0 || y % 16 == 0) return 40;
        return static_cast<unsigned char>(170 + ((x * 7 + y * 13) % 11));
    });
    const cm::PictureStats s = cm::picture_stats(rgba.data(), 128, 128);
    SH_CHECK(s.grid_period == 16 && s.grid > 0.7f);
    SH_CHECK(cm::classify(s) == cm::SurfaceKind::Tiles);
}

SH_TEST(surface_inference_filler_and_noise_stay_neutral) {
    const auto flat = page(64, [](unsigned, unsigned) -> unsigned char { return 90; });
    SH_CHECK(cm::classify(cm::picture_stats(flat.data(), 64, 64)) == cm::SurfaceKind::None);
    const auto noise = page(64, [](unsigned x, unsigned y) -> unsigned char {
        std::uint32_t h = x * 374761393u + y * 668265263u;
        h = (h ^ (h >> 13)) * 1274126177u;
        return static_cast<unsigned char>(h >> 24);
    });
    SH_CHECK(cm::classify(cm::picture_stats(noise.data(), 64, 64)) == cm::SurfaceKind::None);
}
