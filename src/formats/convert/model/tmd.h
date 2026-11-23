#pragma once

#include "formats/common/status.h"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// TMD: the standard PlayStation model format (Sony libgs), as used by ITEM/*.TMD and TEST/*.TMD.
//
//   0x00 u32 id (0x41), u32 flags (bit 0: absolute addresses; every file of the game has 0), u32 object count
//   0x0C object table, 28 bytes per object: vertex offset, vertex count, normal offset, normal count,
//        primitive offset, primitive count, s32 scale (vertices are multiplied by 2^scale)
//
// Offsets are relative to the start of the object table (file offset 0x0C). A vertex is 4 x s16 (x, y, z, pad), a
// normal is 4 x s16 in Q12. A primitive packet is a 4-byte header (output words, input words, flag, mode) followed by
// `input words` 32-bit words:
//   mode: bits 5..7 packet kind (1 = polygon), bit 4 gouraud, bit 3 quad, bit 2 textured, bit 1 semi-transparent
//   flag: bit 0 light calculation off (the packet carries colours instead of normals), bit 1 double sided
namespace sh::convert::model {

struct TmdPrimitive {
    std::uint32_t offset = 0;  // file offset of the packet header
    std::uint8_t flag = 0;
    std::uint8_t mode = 0;
    bool quad = false;
    bool textured = false;
    bool gouraud = false;
    bool semi_transparent = false;
    bool double_sided = false;
    bool unlit = false;                      // flag bit 0: colours in the packet, no normals
    int corners = 3;
    std::array<std::uint16_t, 4> vertex{};
    std::array<std::uint16_t, 4> normal{};   // valid when !unlit
    std::array<std::array<std::uint8_t, 3>, 4> rgb{};  // per corner; flat prims repeat corner 0
    bool has_rgb = false;                    // false for lit textured packets (white)
    std::array<std::array<std::uint8_t, 2>, 4> uv{};
    std::uint16_t clut = 0;
    std::uint16_t tpage = 0;
};

struct TmdObject {
    std::uint32_t table_offset = 0;
    std::int32_t scale = 0;
    std::vector<std::array<std::int16_t, 3>> vertices;
    std::vector<std::array<std::int16_t, 3>> normals;
    std::vector<TmdPrimitive> prims;
    std::uint32_t vertex_offset = 0, normal_offset = 0, prim_offset = 0;  // as stored (relative)
};

struct Tmd {
    std::uint32_t flags = 0;
    std::vector<TmdObject> objects;
};

// The TIM and CLUT row an inventory item model (ITEM/IT_00x.TMD, ITEM/UNQxx.TMD, ITEM/FOOK.TMD) samples for one
// packet's texture page and CLUT word. TMD packets address VRAM; the game uploads the item TIMs to fixed places that
// ignore the TIM headers (the original's s_FsImageDesc table of the USA build; x = page * 64 + u words, y = v):
//   page 15, CLUT (176, 0)   ITEM/TIM00.TIM   common items, always loaded
//   page 14, CLUT (160, 0)   ITEM/TIM01..06   key items of the current map group (GameFs_MapItemsModelLoad); which
//                            one belongs to which IT_00x pack is the same switch as GameFs_MapItemsTextureLoad
//   page 13 + 32 words, CLUT (240, 0)   ITEM/TIM07.TIM   always loaded with the packs
//   page 5, v 128, CLUT (240, 8)        ITEM/FOOK.TIM    loaded with FOOK.TMD only (map5_s01)
// A UNQ model is drawn with whichever key TIM the map group loaded; it is resolved only when every group whose pack
// holds the same object gives its texels the same colours. Empty when the packet cannot be resolved.
struct ItemTexture {
    std::string tim;       // "ITEM/TIM02.TIM"
    int palette = 0;       // CLUT row inside the TIM
    int u0 = 0, v0 = 0;    // texel of the texture page where the TIM picture starts
    int width = 0, height = 0;  // TIM picture size in texels
};
std::optional<ItemTexture> resolve_item_texture(std::string_view game_path, std::uint16_t tpage, std::uint16_t clut);

// The packet's picture on the key items' sheet while `key_tim` ("ITEM/TIM02.TIM") is the TIM resident there: what the
// import cannot know for a model drawn on every map group (a UNQ model whose groups' TIMs differ). Empty when the page
// and CLUT are not the key sheet's.
std::optional<ItemTexture> resolve_key_sheet(std::uint16_t tpage, std::uint16_t clut, std::string_view key_tim);

bool looks_like_tmd(std::span<const std::uint8_t> raw);

// Fails on a truncated file, an unsupported flag, a packet kind other than a polygon, or a packet whose size does
// not match its kind. The message names the object and the packet.
Result<Tmd> parse_tmd(std::span<const std::uint8_t> raw);

}  // namespace sh::convert::model
