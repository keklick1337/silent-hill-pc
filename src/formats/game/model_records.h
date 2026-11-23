// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The in-memory records the LM (model) and IPD (map chunk) converters build over a loaded file buffer: the
// 64-bit layout of the game's own model and chunk records, which the draw and collision code walk. The disc
// files carry the same records with 4-byte pointers; model_file_convert.cpp reads those bytes and
// writes these in place.

#pragma once

#include <cstddef>
#include <cstdint>

namespace sh::formats::records {

// --- LM: a model file (materials, models, their meshes) ---

struct LmMaterial {
    alignas(4) char name[8];   // texture name
    void* texture;             // set when the material's image is applied
    std::uint8_t is_applied;   // image page and blend mode are set
    std::uint8_t pad_d;
    std::uint8_t tpage;        // packed texture page and blend mode
    std::uint8_t tpage_extra;
    std::uint16_t clut;        // packed palette position
    std::uint16_t clut_extra;
    std::uint16_t uv_a;
    std::uint16_t uv_b;
};

struct LmMesh {
    std::uint8_t primitive_count;
    std::uint8_t vertex_count;
    std::uint8_t normal_count;
    std::uint8_t extra_count;
    void* primitives;
    void* vertices_xy;
    void* vertices_z;
    void* normals;
    void* extra;
};

struct LmModel {
    alignas(4) char name[8];
    std::uint8_t mesh_count;
    std::uint8_t vertex_offset;
    std::uint8_t normal_offset;
    std::uint8_t draw_bits;  // bit fields of the game's record (draw kind and order bucket), kept as one byte
    LmMesh* meshes;
};

struct LmHeader {
    std::uint8_t magic;
    std::uint8_t version;
    std::uint8_t is_loaded;  // at byte 2 in the file layout too, so it doubles as the "already converted" mark
    std::uint8_t material_count;
    LmMaterial* materials;
    std::uint8_t model_count;
    std::int8_t pad_9[3];
    LmModel* models;
    std::uint8_t* model_order;
};

constexpr std::uint8_t kLmMagic = '0';
constexpr std::uint8_t kLmVersion = 6;
constexpr std::uint8_t kLmVersionWide = 7;  // a mod format of high-poly models

// --- IPD: a map chunk (its models and their collision) ---

struct ShortVec4 {
    std::int16_t x, y, z, pad;
};

// A 3x3 Q12 rotation and a translation, as the console's matrix stores them.
struct Transform {
    std::int16_t m[3][3];
    std::int32_t t[3];
};


// Converts the LM file loaded at `buffer` into an LmHeader in place (sub-records on the heap). A buffer already
// converted is left alone; a buffer without the LM magic is only marked loaded.
void lm_convert(void* buffer);

}  // namespace sh::formats::records
