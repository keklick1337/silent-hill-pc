#pragma once

// The picture work of sh-upscale (docs/RENDER.md section 12), apart from the network: what is done to
// the colour before it and to the alpha beside it. Tested in tests/upscale/upscaler_test.cpp.

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sh::upscale {

struct Picture {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;  // width * height * 4
};

// Transparent texels take the colour of their solid neighbours, ring by ring, so the network does not pull the
// (usually black) colour under a cut-out into the edge of what is drawn.
void bleed_colour(Picture& p);

// The alpha of `src` at `scale`, nearest: cut-out edges stay hard and the three alpha classes of the scene (0, STP,
// solid) stay exactly what they were.
void scale_alpha_nearest(const Picture& src, int scale, Picture& dst);

// Runs `infer` over tiles of at most `tile` texels per side with `pad` texels of context on each side (the edge
// texels repeated past the picture), and puts the centre of each result in `dst`. `infer` gets a tight RGB tile
// (w * h * 3) and returns w * scale * h * scale * 3 bytes, or false on failure.
using TileInfer =
    std::function<bool(const std::vector<std::uint8_t>& rgb, int w, int h, std::vector<std::uint8_t>& out)>;
bool upscale_tiled(const Picture& src, int scale, int tile, int pad, const TileInfer& infer, Picture& dst);

// The colour of `p` averaged over factor x factor blocks (a model that only runs at x4 asked for x2); the alpha is
// set again by scale_alpha_nearest from the source.
void shrink_colour(Picture& p, int factor);

// The colour of `src` at `scale` through a Lanczos-3 filter: lettering stays as the original draws it, where the
// network repaints small letters into other shapes. The alpha is set again by scale_alpha_nearest.
void scale_lanczos(const Picture& src, int scale, Picture& dst);

// A rectangle of a texture in source texels.
struct Rect {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
};

// Puts the colour of `sharp` into `dst` (both at `scale`) inside `rects`, fading out over one source texel around
// each, so the seam to the network's result does not show.
void paste_rects(const Picture& sharp, const std::vector<Rect>& rects, int scale, Picture& dst);

// What of a texture has lettering (gamedata/config/texture_upscale.json): the whole picture, or rectangles of it.
struct Lettering {
    std::vector<Rect> rects;  // empty: the whole picture
    // The cache remembers this beside each picture it made with lettering, so a changed entry makes it again.
    std::string signature() const;
};
using LetteringList = std::map<std::string, Lettering>;  // by texture name ("TIM/MP_0TOWN")

// The "lettering" array of the file; an error names the entry that is wrong.
std::optional<LetteringList> parse_lettering(std::string_view json, std::string& error);

// The entry of cache key `key` ("BG/SPR06F.alt1"): its own, else the one of its picture without the palette label
// ("BG/SPR06F"), whose variants share the layout. Names compare without case.
const Lettering* lettering_for(const LetteringList& list, std::string_view key);

}  // namespace sh::upscale
