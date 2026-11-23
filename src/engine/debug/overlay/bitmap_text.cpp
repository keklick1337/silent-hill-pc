// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/debug/overlay/bitmap_text.h"

#include <algorithm>
#include <array>

namespace sh::debug {

namespace {

constexpr int kFirstGlyph = 0x20;

// The public-domain IBM PC 8x8 character set, printable ASCII only. One 64-bit word per character: byte y is
// row y (top first), bit x of a row is column x (left first).
constexpr std::array<std::uint64_t, 95> kGlyphs = {
    0x0000000000000000ULL, 0x00180018183C3C18ULL, 0x0000000000003636ULL, 0x0036367F367F3636ULL,  //  !"#
    0x000C1F301E033E0CULL, 0x0063660C18336300ULL, 0x006E333B6E1C361CULL, 0x0000000000030606ULL,  // $%&'
    0x00180C0606060C18ULL, 0x00060C1818180C06ULL, 0x0000663CFF3C6600ULL, 0x00000C0C3F0C0C00ULL,  // ()*+
    0x060C0C0000000000ULL, 0x000000003F000000ULL, 0x000C0C0000000000ULL, 0x000103060C183060ULL,  // ,-./
    0x003E676F7B73633EULL, 0x003F0C0C0C0C0E0CULL, 0x003F33061C30331EULL, 0x001E33301C30331EULL,  // 0123
    0x0078307F33363C38ULL, 0x001E3330301F033FULL, 0x001E33331F03061CULL, 0x000C0C0C1830333FULL,  // 4567
    0x001E33331E33331EULL, 0x000E18303E33331EULL, 0x000C0C00000C0C00ULL, 0x060C0C00000C0C00ULL,  // 89:;
    0x00180C0603060C18ULL, 0x00003F00003F0000ULL, 0x00060C1830180C06ULL, 0x000C000C1830331EULL,  // <=>?
    0x001E037B7B7B633EULL, 0x0033333F33331E0CULL, 0x003F66663E66663FULL, 0x003C66030303663CULL,  // @ABC
    0x001F36666666361FULL, 0x007F46161E16467FULL, 0x000F06161E16467FULL, 0x007C66730303663CULL,  // DEFG
    0x003333333F333333ULL, 0x001E0C0C0C0C0C1EULL, 0x001E333330303078ULL, 0x006766361E366667ULL,  // HIJK
    0x007F66460606060FULL, 0x0063636B7F7F7763ULL, 0x006363737B6F6763ULL, 0x001C36636363361CULL,  // LMNO
    0x000F06063E66663FULL, 0x00381E3B3333331EULL, 0x006766363E66663FULL, 0x001E33380E07331EULL,  // PQRS
    0x001E0C0C0C0C2D3FULL, 0x003F333333333333ULL, 0x000C1E3333333333ULL, 0x0063777F6B636363ULL,  // TUVW
    0x0063361C1C366363ULL, 0x001E0C0C1E333333ULL, 0x007F664C1831637FULL, 0x001E06060606061EULL,  // XYZ[
    0x00406030180C0603ULL, 0x001E18181818181EULL, 0x0000000063361C08ULL, 0xFF00000000000000ULL,  // \]^_ (backslash first)
    0x0000000000180C0CULL, 0x006E333E301E0000ULL, 0x003B66663E060607ULL, 0x001E3303331E0000ULL,  // `abc
    0x006E33333E303038ULL, 0x001E033F331E0000ULL, 0x000F06060F06361CULL, 0x1F303E33336E0000ULL,  // defg
    0x006766666E360607ULL, 0x001E0C0C0C0E000CULL, 0x1E33333030300030ULL, 0x0067361E36660607ULL,  // hijk
    0x001E0C0C0C0C0C0EULL, 0x00636B7F7F330000ULL, 0x00333333331F0000ULL, 0x001E3333331E0000ULL,  // lmno
    0x0F063E66663B0000ULL, 0x78303E33336E0000ULL, 0x000F06666E3B0000ULL, 0x001F301E033E0000ULL,  // pqrs
    0x00182C0C0C3E0C08ULL, 0x006E333333330000ULL, 0x000C1E3333330000ULL, 0x00367F7F6B630000ULL,  // tuvw
    0x0063361C36630000ULL, 0x1F303E3333330000ULL, 0x003F260C193F0000ULL, 0x00380C0C070C0C38ULL,  // xyz{
    0x0018181800181818ULL, 0x00070C0C380C0C07ULL, 0x0000000000003B6EULL,                         // |}~
};

}  // namespace

bool glyph_pixel(char character, int x, int y) {
    const int index = static_cast<unsigned char>(character) - kFirstGlyph;
    if (index < 0 || index >= static_cast<int>(kGlyphs.size())) return false;
    if (x < 0 || x >= kGlyphSize || y < 0 || y >= kGlyphSize) return false;
    return ((kGlyphs[static_cast<std::size_t>(index)] >> (y * 8 + x)) & 1u) != 0;
}

void TextPanel::draw(std::span<const std::string> lines, Ink ink) {
    std::fill(pixels_.begin(), pixels_.end(), std::uint8_t{0});
    const std::size_t stride = static_cast<std::size_t>(width()) * 4;
    const int rows = std::min(rows_, static_cast<int>(lines.size()));
    for (int row = 0; row < rows; ++row) {
        const std::string& text = lines[static_cast<std::size_t>(row)];
        const int columns = std::min(columns_, static_cast<int>(text.size()));
        for (int column = 0; column < columns; ++column) {
            const char character = text[static_cast<std::size_t>(column)];
            for (int y = 0; y < kGlyphSize; ++y) {
                std::uint8_t* out = pixels_.data() + static_cast<std::size_t>(row * kGlyphSize + y) * stride +
                                    static_cast<std::size_t>(column * kGlyphSize) * 4;
                for (int x = 0; x < kGlyphSize; ++x, out += 4) {
                    if (!glyph_pixel(character, x, y)) continue;
                    out[0] = ink.r;
                    out[1] = ink.g;
                    out[2] = ink.b;
                    out[3] = ink.a;
                }
            }
        }
    }
}

}  // namespace sh::debug
