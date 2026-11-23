// The title screen's fog on the engine (src/engine/gameflow/title_fog.h): the
// fog runs on the game's random numbers, its colours come from gamedata/config/title_fog.json, and the lower half
// of the grid is drawn as shaded quads over the title picture.

#include <cstdint>

#include "engine/gameflow/game/game_link.h"
#include "engine/gameflow/title_fog.h"
#include "engine/loop/game_random.h"
#include "render/draw2d/draw2d.h"

extern "C" std::uint32_t GameFlow_TitleFogColor(std::int32_t heat);

namespace {

namespace gf = sh::gameflow;
namespace link = sh::gameflow::game;
namespace d2 = sh::draw2d;

// Quads of 16 x 24 pixels from the screen's top left, semi-transparent Gouraud quads (primitive code 0x3A) on
// ordering entry 6, blended additively (page word 42).
constexpr int kQuadWidth = 16;
constexpr int kQuadHeight = 24;
constexpr int kLeft = -176;
constexpr int kTop = -208;
constexpr std::uint32_t kQuadCode = 0x3Au << 24;
constexpr int kOtEntry = 6;
constexpr int kBlendPage = 42;

class GameRandom final : public gf::TitleFog::Random {
public:
    std::int32_t rand16() override { return sh::loop::game_random().next15(); }
};

gf::TitleFog& fog() {
    static gf::TitleFog f;
    return f;
}

std::uint32_t cell_color(int cell) {
    const int shade = fog().shade(cell);
    return kQuadCode | (shade < 0 ? 0u : GameFlow_TitleFogColor(shade));
}

d2::Rgb rgb(std::uint32_t word) {
    return {static_cast<std::uint8_t>(word), static_cast<std::uint8_t>(word >> 8), static_cast<std::uint8_t>(word >> 16)};
}

d2::Point point(int x, int y) { return {static_cast<std::int16_t>(x), static_cast<std::int16_t>(y)}; }

void draw() {
    constexpr int side = gf::TitleFog::kSide;
    // Rows 10..20 against the row above each; the first column only feeds the quads' left edges.
    for (int row = 10; row < side; ++row) {
        std::uint32_t top_right = cell_color(side * (row - 1));
        std::uint32_t bottom_right = cell_color(side * row);
        for (int col = 1; col < side; ++col) {
            const std::uint32_t top_left = top_right;
            const std::uint32_t bottom_left = bottom_right;
            top_right = cell_color(col + side * (row - 1));
            bottom_right = cell_color(col + side * row);

            const int x = kLeft + kQuadWidth * col;
            const int y = (row - 1) * kQuadHeight + kTop;
            d2::Polygon poly;
            poly.count = 4;
            poly.gouraud = true;
            // The first colour word carries the primitive code in its top byte (bit 1: translucent, bit 0: raw).
            const std::uint32_t code = top_left >> 24;
            poly.shade = d2::Shade{(code & 2) != 0, (code & 1) != 0};
            poly.color[0] = rgb(top_left);
            poly.color[1] = rgb(top_right);
            poly.color[2] = rgb(bottom_left);
            poly.color[3] = rgb(bottom_right);
            // The code in every colour word lands in the fog bytes.
            poly.fog.amount[1] = static_cast<std::uint8_t>(top_right >> 24);
            poly.fog.amount[2] = static_cast<std::uint8_t>(bottom_left >> 24);
            poly.fog.amount[3] = static_cast<std::uint8_t>(bottom_right >> 24);
            poly.xy[0] = point(x, y);
            poly.xy[1] = point(x + kQuadWidth, y);
            poly.xy[2] = point(x, y + kQuadHeight);
            poly.xy[3] = point(x + kQuadWidth, y + kQuadHeight);
            link::submit_2d(link::OtList::Main, kOtEntry, poly, link::Packet::GouraudQuad);
        }
    }

    // No texture window: the mode packet's second word is a no-op, so it is the page switch alone.
    d2::DrawMode mode;
    mode.page = d2::TexturePage::from_word(kBlendPage);
    mode.dither = true;
    link::submit_2d(link::OtList::Main, kOtEntry, mode, link::Packet::Mode);
}

}  // namespace

extern "C" void Title_FogInit(void) {
    GameRandom rng;
    fog().init(rng);
}

extern "C" void MainMenu_FogUpdate(void) {
    GameRandom rng;
    if (fog().frame(rng)) {
        draw();
    }
}
