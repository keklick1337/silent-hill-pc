#pragma once

#include "formats/convert/converter.h"

#include <memory>

// TIM <-> PNG, without loss.
//
// decode("CHARA/HERO.TIM") returns:
//   textures/CHARA/HERO.png       the picture in true colour. 4-bit and 8-bit TIMs: RGBA of CLUT row 0 (a later
//                                 pass, the texture bake, rewrites this file with the rows the geometry draws, so
//                                 encode() never reads it for these depths). 16-bit TIMs: RGBA. 24-bit TIMs: RGB.
//   indexed/CHARA/HERO.png        4-bit and 8-bit TIMs only: an 8-bit indexed PNG whose pixels are the TIM's palette
//                                 indices and whose PLTE and tRNS are CLUT row 0 (16 or 256 words). The lossless
//                                 source of the pixels and the source of runtime CLUT effects; not meant for editing.
//   textures/CHARA/HERO.tim.json  everything else, among it every CLUT row.
// A TIM without a CLUT is shown with a gray ramp in both PNGs; encode() does not read the ramp.
// Further CLUT rows are never written as pictures: png/palette_variant.h builds "HERO.p03.png" from the indexed
// file plus row 3 of the JSON on demand.
//
// Colours. A CLUT or 16-bit word is bit 15 STP, then 5 bits each of B, G, R. In the PNG a 5-bit value v is shown
// as (v << 3) | (v >> 2), so 31 becomes 255. The word 0x0000 is transparent (alpha 0); every other word is opaque
// (alpha 255), the same rule the game's own texture loader uses. The STP bit is not shown in the PNG: an editor
// would draw a texture with STP set half see-through or opaque, and neither is what the picture looks like.
//
// What encode() reads back (the JSON is found by its suffix; the indexed PNG is png::indexed_path_for of the
// JSON's textures/ name):
//   from the PNG   4/8-bit: the pixel indices and the colours of row 0 (PLTE and tRNS) of indexed/.../HERO.png,
//                  which must be an indexed PNG with the size in the JSON; 4-bit TIMs need indices 0..15. A PLTE
//                  shorter than the palette keeps the JSON colours for the missing entries. 16/24-bit: the
//                  RGBA/RGB pixels of textures/.../HERO.png (any PNG stb_image reads).
//   from the JSON  the VRAM rectangles, the depth, the unused tail of the file, the STP bit of every palette entry
//                  (and of every 16-bit pixel), and the words of CLUT rows 1 and up.
//   A palette colour in a PNG is quantised to 5 bits by rounding, v5 = (v * 31 + 127) / 255. That is the exact
//   inverse of the expansion above, so untouched colours come back unchanged, and a colour edited to any 8-bit
//   value goes to the nearest of the 32 levels. Alpha 0 is the word 0x0000; any other alpha is opaque. An opaque
//   colour that quantises to black gets STP set (word 0x8000), because 0x0000 would be transparent.
//   Not read: the "game_path" and "source" values of the JSON, the row 0 words of the JSON (the indexed PLTE gives
//   them), textures/.../HERO.png of a 4/8-bit TIM, and any HERO.pNN.png or other variant picture.
//
// Files that this converter does not take (accepts() is false, decode() fails): not a TIM by the header, a block
// length that differs from 12 + 2 * width * height, an empty picture or palette, a 24-bit width that is not a
// multiple of 3 words, a 4-/8-bit CLUT whose size is not a whole number of palettes. The importer then keeps the
// raw file only. All 996 TIMs of the NTSC-U image are taken.
namespace sh::convert::tim {

std::unique_ptr<sh::convert::Converter> make_converter();

}  // namespace sh::convert::tim
