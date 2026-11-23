// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Materials and texture slots of the loaded LM models (RENDER.md section 6): which texture image a material uses, how
// its texture page, palette and texel origin are packed into the material and stamped into the primitives that use
// it, and the pool of texture slots the map's chunk models share. The file reads and the pool's video memory live
// in the game (src/engine/runtime/lm_runtime.cpp); this module is the bookkeeping only.
//
// The records keep the layout of the game's material, primitive and texture records.

#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/world/skeleton.h"
#include "formats/game/model_records.h"

namespace sh::world {

using sh::formats::records::LmMaterial;
using sh::formats::records::LmMesh;

// One primitive of a model mesh: four texel words, a palette word and the draw word (low byte the material's
// texture page byte, bits 8..14 the signed material index, bit 15 translucent).
struct LmPrimitive {
    std::uint16_t uv0;
    std::uint16_t clut;
    std::uint16_t uv1;
    std::uint16_t draw;
    std::uint16_t uv2;
    std::uint16_t uv3;
    std::uint8_t extra[8];

    int material() const { return static_cast<std::int8_t>(static_cast<std::uint8_t>(draw >> 8) << 1) >> 1; }
    void set_page(std::uint8_t page) { draw = static_cast<std::uint16_t>((draw & 0xFF00u) | page); }
    void set_translucent(bool on) { draw = static_cast<std::uint16_t>((draw & 0x7FFFu) | (on ? 0x8000u : 0u)); }
};

// Where a texture image sits in video memory (page bytes, texel origin, palette position).
struct TextureImage {
    std::uint8_t page[2];
    std::uint8_t u;
    std::uint8_t v;
    std::int16_t palette_x;
    std::int16_t palette_y;
};

// A texture slot of a pool: the image place, the texture name it holds, its read ticket (-1: never read) and how
// many materials use it.
struct TextureSlot {
    TextureImage image;
    alignas(4) char name[8];
    std::uint32_t ticket;
    std::int32_t ref_count;  // materials that use it (a mod's cell may share one texture past a byte)
};

inline constexpr int kActiveTexturesMax = 8 + 192;
inline constexpr std::uint32_t kNoTicket = 0xFFFFFFFFu;

// The slots a material may take its texture from, in claim order.
struct ActiveTextures {
    std::int32_t count;
    TextureSlot* textures[kActiveTexturesMax];
};

// Palette rows from here on are virtual pool slots: no row of video memory has such a number.
inline constexpr std::int16_t kVirtualPaletteRow = 512;

// Bits of a material change (material_changes / model_material_apply).
inline constexpr int kMaterialPageChanged = 1 << 0;
inline constexpr int kMaterialPaletteChanged = 1 << 1;
inline constexpr int kMaterialTexelChanged = 1 << 2;

// The untextured value of a primitive's page byte.
inline constexpr std::uint8_t kUntexturedPage = 32;

// A material's validation of a moved palette word (the hires pool): new, base, old, old base, material name.
using PaletteRestamp = std::uint16_t (*)(std::uint16_t, std::uint16_t, std::uint16_t, std::uint16_t, const char*);

// 8-character names compared as two 32-bit words, as the game compares them.
bool same_name(const char* a, const char* b);
// `dst` (8 bytes) cleared, then at most 8 characters of `src` (none for null).
void name_copy(char* dst, const char* src);

LmMaterial* lm_materials(LmHeader& lm);

// Every primitive of every model translucent or not.
void lm_set_translucent(LmHeader& lm, bool translucent);
// Packs `image` (and the blend mode) into the material: texel origin (u scaled by the page's colour depth),
// page byte, palette word.
void material_image_apply(LmMaterial& mat, const TextureImage& image, int blend);
// The first material named `name` gets `image` and is marked applied; false when none is named so.
bool lm_material_image_apply(LmHeader& lm, const char* name, const TextureImage& image, int blend);
// Stamps every changed material's page, palette and texel origin into the primitives that use it. While
// `interior_sync` (the chunk pool is being re-synced) a material without a texture is drawn untextured and a
// resident one always re-stamps its page.
void lm_material_changes_apply(LmHeader& lm, bool interior_sync, PaletteRestamp restamp);
void model_material_apply(LmModel& model, int material, const LmMaterial& mat, int changes, PaletteRestamp restamp);

// A slot for `mat`: the slot already holding its texture (`holds` true), else the unused slot read longest ago
// (virtual slots first unless `needs_vram`), null when every slot is used.
struct SlotChoice {
    TextureSlot* slot = nullptr;
    bool holds = false;
};

}  // namespace sh::world
