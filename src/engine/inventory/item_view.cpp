// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/inventory/item_view.h"


namespace sh::inventory {

void ItemView::count_link()
{
    if (linked < kViewModels)
        ++linked;
}

ItemView& item_view()
{
    static ItemView s_view;
    return s_view;
}

ItemScreenState& item_screen()
{
    static ItemScreenState s_screen;
    return s_screen;
}

void open_carousel(ItemView& view, std::int32_t selected, std::int32_t slot_count)
{
    view.shown.fill(kNoModelSlot);
    const std::int32_t first = (selected - 3 + slot_count) % slot_count;
    for (int i = 0; i < 7; ++i)
        view.shown[i] = (first + i) % slot_count;
}

std::int32_t slot_of_item(const Slots& slots, std::int32_t slot_count, int item)
{
    for (std::int32_t i = 0; i < slot_count && i < static_cast<std::int32_t>(slots.size()); ++i)
    {
        if (slots[i].item == item)
            return i;
    }
    return kNoModelSlot;
}

std::int32_t pack_index(std::span<const std::int32_t> pack, int item)
{
    for (std::size_t k = 0; k < kPackSearch && k < pack.size(); ++k)
    {
        if (pack[k] == item)
            return static_cast<std::int32_t>(k);
    }
    return -1;
}

void fit_to_screen(ScreenMatrix& ls, int display, const ScreenFit& fit)
{
    // The picture was laid out for a 4:3 display; a stretched one widens the model, so the x row and
    // the x translation shrink by the ratio of the two aspects.
    const float screen_aspect = 320.0f / 240.0f;
    if (fit.stretched && fit.display_aspect > screen_aspect)
    {
        const float narrow = screen_aspect / fit.display_aspect;
        for (int j = 0; j < 3; ++j)
            ls.m[0][j] = static_cast<std::int16_t>(static_cast<float>(ls.m[0][j]) * narrow);
        ls.t[0] = static_cast<std::int32_t>(static_cast<float>(ls.t[0]) * narrow);
    }

    // Only the size row: the model gets taller in place, an off-centre model does not move.
    if (fit.square && fit.frame_width > 0)
    {
        const float taller = (4.0f / 3.0f) *
                             (static_cast<float>(fit.frame_height) / static_cast<float>(fit.frame_width)) *
                             (static_cast<float>(fit.square_percent) / 100.0f);
        for (int j = 0; j < 3; ++j)
            ls.m[1][j] = static_cast<std::int16_t>(static_cast<float>(ls.m[1][j]) * taller);
    }

    // The offsets are fractions of the frame they were tuned on, so they land alike on any frame.
    if (fit.in_menu)
    {
        const std::int32_t frame = fit.frame_height > 0 ? fit.frame_height : kOffsetFrameHeight;
        if (display == 7)
            ls.t[1] += (fit.hand_offset * frame) / kOffsetFrameHeight;
        else if (display < 7)
            ls.t[1] += (fit.row_offset * frame) / kOffsetFrameHeight;
        if (display < 7 && fit.row_spread != 1.0f)
            ls.t[0] = static_cast<std::int32_t>(static_cast<float>(ls.t[0]) * fit.row_spread);
    }
}

std::int32_t carousel_dim(int display, std::int32_t z, std::int32_t strength)
{
    if (display >= 7 || strength <= 0)
        return 256;
    // Depth past the centre model (which stands at -4 m, Q8): about 1567 one slot off, 2896 two, 3784 three.
    const std::int32_t depth = z + 4 * 256;
    const std::int32_t off = depth < 800 ? 0 : depth < 2200 ? 1 : depth < 3300 ? 2 : 3;
    std::int32_t percent = off * strength;
    if (percent > 100)
        percent = 100;
    return 256 - (256 * percent) / 100;
}

namespace {

constexpr std::size_t kPrimBytes = 28;
constexpr std::uint32_t kBottleVertices = 59;
constexpr std::uint32_t kBottlePrims = 220;

// Bytes 6, 7 (palette) and 10, 11 (texture page) may have been moved by the loader.
bool relocatable(std::size_t i) { return i == 6 || i == 7 || i == 10 || i == 11; }

bool matches(const std::uint8_t* prim, const std::array<std::uint8_t, kPrimBytes>& stock)
{
    for (std::size_t i = 0; i < kPrimBytes; ++i)
    {
        if (!relocatable(i) && prim[i] != stock[i])
            return false;
    }
    return true;
}

using Prim = std::array<std::uint8_t, kPrimBytes>;

// The four faces that sample (128,255) and their corrected texture coordinates (three u,v pairs).
constexpr std::array<std::uint16_t, 4> kStrayFaces = {1, 109, 118, 214};
constexpr std::array<Prim, 4> kStrayStock = {{
    {9, 6, 0, 52, 255, 170, 0, 0, 128, 255, 0, 0, 128, 255, 0, 0, 2, 0, 3, 0, 3, 0, 4, 0, 3, 0, 5, 0},
    {9, 6, 0, 54, 128, 255, 0, 0, 255, 170, 0, 0, 255, 170, 0, 0, 91, 0, 4, 0, 14, 0, 3, 0, 14, 0, 7, 0},
    {9, 6, 0, 54, 255, 170, 0, 0, 255, 170, 0, 0, 128, 255, 0, 0, 2, 0, 7, 0, 2, 0, 3, 0, 3, 0, 5, 0},
    {9, 6, 0, 54, 128, 255, 0, 0, 128, 255, 0, 0, 255, 170, 0, 0, 91, 0, 5, 0, 91, 0, 4, 0, 14, 0, 7, 0},
}};
constexpr std::array<std::array<std::uint8_t, 6>, 4> kStrayUvs = {{
    {255, 191, 247, 191, 247, 168},
    {247, 191, 255, 191, 255, 168},
    {255, 168, 255, 191, 247, 168},
    {247, 168, 247, 191, 255, 168},
}};

// The liquid faces that share the shell's blending: they become one opaque volume.
constexpr std::array<std::uint16_t, 16> kLiquidFaces = {108, 109, 110, 111, 112, 113, 114, 115,
                                                        118, 119, 120, 121, 214, 215, 216, 217};
constexpr std::array<Prim, 16> kLiquidStock = {{
    {9, 6, 0, 54, 247, 168, 0, 0, 255, 191, 0, 0, 255, 168, 0, 0, 90, 0, 1, 0, 52, 0, 0, 0, 52, 0, 2, 0},
    {9, 6, 0, 54, 128, 255, 0, 0, 255, 170, 0, 0, 255, 170, 0, 0, 91, 0, 4, 0, 14, 0, 3, 0, 14, 0, 7, 0},
    {9, 6, 0, 54, 247, 191, 0, 0, 255, 191, 0, 0, 255, 168, 0, 0, 52, 0, 0, 0, 91, 0, 4, 0, 91, 0, 5, 0},
    {9, 6, 0, 54, 247, 168, 0, 0, 255, 191, 0, 0, 255, 168, 0, 0, 92, 0, 3, 0, 92, 0, 6, 0, 92, 0, 7, 0},
    {9, 6, 0, 54, 247, 168, 0, 0, 255, 191, 0, 0, 255, 168, 0, 0, 9, 0, 7, 0, 9, 0, 2, 0, 9, 0, 5, 0},
    {9, 6, 0, 54, 247, 168, 0, 0, 255, 191, 0, 0, 255, 168, 0, 0, 93, 0, 0, 0, 93, 0, 3, 0, 94, 0, 4, 0},
    {9, 6, 0, 54, 247, 191, 0, 0, 255, 191, 0, 0, 255, 168, 0, 0, 93, 0, 3, 0, 93, 0, 0, 0, 95, 0, 1, 0},
    {9, 6, 0, 54, 247, 191, 0, 0, 255, 191, 0, 0, 255, 168, 0, 0, 9, 0, 2, 0, 9, 0, 7, 0, 9, 0, 6, 0},
    {9, 6, 0, 54, 255, 170, 0, 0, 255, 170, 0, 0, 128, 255, 0, 0, 2, 0, 7, 0, 2, 0, 3, 0, 3, 0, 5, 0},
    {9, 6, 0, 54, 255, 168, 0, 0, 255, 191, 0, 0, 247, 168, 0, 0, 3, 0, 5, 0, 3, 0, 4, 0, 0, 0, 2, 0},
    {9, 6, 0, 54, 255, 168, 0, 0, 255, 191, 0, 0, 247, 168, 0, 0, 8, 0, 1, 0, 6, 0, 0, 0, 8, 0, 6, 0},
    {9, 6, 0, 54, 255, 168, 0, 0, 255, 191, 0, 0, 247, 168, 0, 0, 5, 0, 6, 0, 5, 0, 7, 0, 5, 0, 1, 0},
    {9, 6, 0, 54, 128, 255, 0, 0, 128, 255, 0, 0, 255, 170, 0, 0, 91, 0, 5, 0, 91, 0, 4, 0, 14, 0, 7, 0},
    {9, 6, 0, 54, 247, 168, 0, 0, 247, 191, 0, 0, 255, 168, 0, 0, 52, 0, 2, 0, 52, 0, 0, 0, 91, 0, 5, 0},
    {9, 6, 0, 54, 247, 168, 0, 0, 247, 191, 0, 0, 255, 168, 0, 0, 95, 0, 6, 0, 93, 0, 3, 0, 95, 0, 1, 0},
    {9, 6, 0, 54, 247, 168, 0, 0, 247, 191, 0, 0, 255, 168, 0, 0, 9, 0, 1, 0, 9, 0, 2, 0, 9, 0, 6, 0},
}};

}  // namespace

bool fix_liquid_uvs(std::span<std::uint8_t> prims, std::uint32_t vertex_count, std::uint32_t prim_count)
{
    if (vertex_count != kBottleVertices || prim_count != kBottlePrims || prims.size() < kBottlePrims * kPrimBytes)
        return false;
    // Every primitive must be a textured triangle (length 6) before any index is trusted.
    for (std::size_t p = 0; p < kBottlePrims; ++p)
    {
        if (prims[p * kPrimBytes + 1] != 6)
            return false;
    }
    for (std::size_t f = 0; f < kStrayFaces.size(); ++f)
    {
        if (!matches(&prims[kStrayFaces[f] * kPrimBytes], kStrayStock[f]))
            return false;
    }
    for (std::size_t f = 0; f < kLiquidFaces.size(); ++f)
    {
        if (!matches(&prims[kLiquidFaces[f] * kPrimBytes], kLiquidStock[f]))
            return false;
    }
    // Mode bit 1 is the blending: the liquid is opaque, the shell keeps its own.
    for (const std::uint16_t face : kLiquidFaces)
        prims[face * kPrimBytes + 3] &= static_cast<std::uint8_t>(~2u);
    for (std::size_t f = 0; f < kStrayFaces.size(); ++f)
    {
        std::uint8_t* prim = &prims[kStrayFaces[f] * kPrimBytes];
        for (int v = 0; v < 3; ++v)
        {
            prim[4 + v * 4] = kStrayUvs[f][v * 2];
            prim[5 + v * 4] = kStrayUvs[f][v * 2 + 1];
        }
    }
    return true;
}

}  // namespace sh::inventory
