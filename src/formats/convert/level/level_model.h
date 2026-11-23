#pragma once

// Data model of the map files of Silent Hill (PS1, USA 1.1): IPD (one map cell) and LM (the model library inside an
// IPD, and the stand-alone PLM files). Every structure mirrors the on-disc layout; nothing is merged or dropped.
//
// Conventions:
//  * `offset` fields are absolute byte offsets in the file the structure was read from. On disc the sub-offsets of an
//    LM are relative to the LM header, those of the IPD collision data to the byte 0x54 of the IPD, and all others to
//    the start of the IPD; the model stores them already resolved, so any `offset` can be used against the file bytes.
//  * Q7.8 means 256 = 1 world unit. Rotation matrices are Q12 (4096 = 1). The world is Y-down. One map cell is
//    40 world units (10240 in Q8) wide.
//  * Fields whose meaning is unknown are named after the offset they sit at (`pad_9`, ...).

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace sh::convert::level {

struct Vec3s {
    std::int16_t x = 0;
    std::int16_t y = 0;
    std::int16_t z = 0;
};

// A region of the file that some structure occupies. Used to prove that the parse accounts for every byte.
struct Span {
    std::uint32_t offset = 0;
    std::uint32_t size = 0;
    const char* label = "";
};

// Bytes no structure points at. All of them are alignment padding (zero) or the tail of the last disc block.
struct Gap {
    std::uint32_t offset = 0;
    std::uint32_t size = 0;
    bool all_zero = true;
};

struct FileLayout {
    std::uint32_t file_size = 0;
    std::vector<Span> spans;           // sorted by offset
    std::vector<Gap> gaps;             // sorted by offset; the last one may reach file_size (disc block tail)
    std::uint32_t overlap_bytes = 0;   // bytes claimed by more than one structure
    std::uint32_t trailing_bytes = 0;  // size of the gap that ends at file_size, 0 if none
};

// ---- LM: model library -------------------------------------------------------------------------------------------

// 24 bytes. The texture pointer and the GPU words are filled in by the loader at run time; the file holds the
// values the tool chain left there. Each of tpage, clut and uv has a `_base` twin: the value the model primitives were
// last rebased on. The loader rewrites the primitives by (value - base) and then copies the value into the base.
struct LmMaterial {
    std::uint32_t offset = 0;
    std::string name;                       // texture sheet name, up to the first NUL
    std::array<std::uint8_t, 8> name_raw{};
    std::uint32_t texture_pointer = 0;      // +8
    std::uint8_t applied = 0;               // +0xC: set once the texture's tpage and blend mode are in place
    std::uint8_t pad_d = 0;                 // unused
    std::uint8_t tpage = 0;                 // +0xE: GPU tpage / blend flags after loading
    std::uint8_t tpage_base = 0;            // +0xF
    std::uint16_t clut = 0;                 // +0x10: CLUT word: (clutY << 6) | ((clutX >> 4) & 0x3F) after loading
    std::uint16_t clut_base = 0;            // +0x12
    std::uint16_t uv = 0;                   // +0x14: two bytes: image u, v
    std::uint16_t uv_base = 0;              // +0x16
};

// 20 bytes. Quads only in the whole image (no triangle is stored in a map file), but the triangle marker is kept:
// vertex[3] == 0xFF.
struct LmPrimitive {
    std::array<std::array<std::uint8_t, 2>, 4> uv{};  // [corner][u, v], texel units of the texture page; at +0, +4, +8, +0xA
    std::uint16_t clut = 0;                           // +2
    std::uint16_t flags = 0;                          // +6: bits 0-7 tpage (the material's), bits 8-14 material index, bit 15 transparent
    std::array<std::uint8_t, 4> vertex{};             // +0xC: index into the vertex window of the model (vertex_offset + n)
    std::array<std::uint8_t, 4> shade_slot{};         // +0x10: slot in the shade stream (normal_offset + n)

    bool is_triangle() const { return vertex[3] == 0xFF; }
    int material_index() const { return (flags >> 8) & 0x7F; }
    bool transparent() const { return (flags >> 15) & 1; }
};

// 4 bytes. Signed bytes, about 1/127 per unit. `count` is the number of shade-stream entries this normal covers.
struct LmNormal {
    std::int8_t x = 0;
    std::int8_t y = 0;
    std::int8_t z = 0;
    std::uint8_t count = 0;
};

// 24 bytes. `shade` is a stream of vertex indices, grouped by normal: normal k owns the next normals[k].count entries.
// A primitive corner names its slot in that stream (LmPrimitive::shade_slot, minus the model's normal_offset), and the
// entry at the slot is the corner's own vertex index. This holds for every corner of every map file.
struct LmMesh {
    std::uint32_t offset = 0;
    std::uint32_t primitives_offset = 0;
    std::uint32_t vertices_xy_offset = 0;
    std::uint32_t vertices_z_offset = 0;
    std::uint32_t normals_offset = 0;
    std::uint32_t shade_offset = 0;
    std::vector<LmPrimitive> primitives;
    std::vector<Vec3s> vertices;  // x, y from the XY array, z from the separate Z array; Q7.8, model space
    std::vector<LmNormal> normals;
    std::vector<std::uint8_t> shade;
};

// 16 bytes. A model is a list of meshes drawn with one vertex/normal window: primitive indices are absolute in the
// window, local index = index - vertex_offset.
struct LmModel {
    std::uint32_t offset = 0;
    std::string name;
    std::array<std::uint8_t, 8> name_raw{};
    std::uint8_t vertex_offset = 0;
    std::uint8_t normal_offset = 0;
    std::uint8_t flags = 0;  // +0xB: bit 0 lit, bits 1-3 order bucket, bits 4-5 far drawer, bits 6-7 unused
    std::uint32_t mesh_headers_offset = 0;
    std::vector<LmMesh> meshes;

    // Drawn through the lit (light-shaded palette) chain instead of the flat one.
    int lit() const { return flags & 1; }
    // Selects the ordering-table slot the model is sorted into (0 -> 2, 1 -> 0, 2 -> 4, 3 -> 33, 4 -> 66, 5 -> 99).
    int order_bucket() const { return (flags >> 1) & 7; }
    // Non-zero sends the model through the far-model drawer instead of the lit/flat chains.
    int far_drawer() const { return (flags >> 4) & 3; }
};

// 20 bytes header. Only version 6 (the PS1 format) is read.
struct LmFile {
    std::uint32_t offset = 0;  // where the LM header sits in the file: 0 for a PLM, IpdFile::lm_offset inside an IPD
    std::uint8_t magic = 0;    // '0'
    std::uint8_t version = 0;
    std::uint8_t is_loaded = 0;
    std::array<std::uint8_t, 3> pad_9{};
    std::uint32_t materials_offset = 0;
    std::uint32_t model_headers_offset = 0;
    std::uint32_t model_order_offset = 0;
    std::vector<LmMaterial> materials;
    std::vector<LmModel> models;
    std::vector<std::uint8_t> model_order;  // draw order, one byte per model
    FileLayout layout;                      // filled for a stand-alone PLM; empty for an LM inside an IPD (see IpdFile)
};

// ---- IPD: one map cell ------------------------------------------------------------------------------------------

// 16 bytes. Names a model used by the instances of this cell. `is_global_plm` is 0 when the model is in this file's LM,
// 1 when it lives in the area's global PLM (BG/<AREA>_GLB.PLM). `model_header_offset` is not used on disc.
struct IpdModelInfo {
    std::uint32_t offset = 0;
    std::uint8_t is_global_plm = 0;
    std::array<std::uint8_t, 3> pad_1{};
    std::string name;
    std::array<std::uint8_t, 8> name_raw{};
    std::uint32_t model_header_offset = 0;
};

// 36 bytes. Placement of one model: a Q12 rotation and a Q8 translation relative to the cell origin.
struct IpdModelInstance {
    std::uint32_t offset = 0;
    std::uint32_t model_info_index = 0;       // index into IpdFile::model_infos
    std::array<std::int16_t, 9> rotation{};   // row-major m[r][c], Q12
    std::int16_t rotation_pad = 0;
    std::array<std::int32_t, 3> translation{};  // Q8
};

// 8 bytes (four int16). In the first array `w` selects the billboard effect type (0 or 1) drawn at (x, y, z); in the
// second the four values are a visibility box in cell space: min x, max x, min z, max z.
struct IpdVec4 {
    std::int16_t x = 0;
    std::int16_t y = 0;
    std::int16_t z = 0;
    std::int16_t w = 0;
};

// 24 bytes. One draw batch of instances with its cell-space bounds (Q8).
struct IpdModelBuffer {
    std::uint32_t offset = 0;
    std::uint8_t billboard_count = 0;
    std::uint8_t pad_3 = 0;
    std::int16_t min_x = 0;
    std::int16_t max_x = 0;
    std::int16_t min_z = 0;
    std::int16_t max_z = 0;
    std::uint32_t instances_offset = 0;
    std::uint32_t billboards_offset = 0;
    std::uint32_t subcell_bounds_offset = 0;
    std::vector<IpdModelInstance> instances;
    std::vector<IpdVec4> billboards;
    std::vector<IpdVec4> subcell_bounds;
};

// The 26 bytes at +0x1C of the header: 25 (start, count) ranges that split the model order list into consecutive
// groups. start[i] is the sum of the earlier counts; the sum of all counts is the model order count.
struct IpdOrderGroup {
    std::uint8_t start = 0;
    std::uint8_t count = 0;
};

// 12 bytes. Collision surface of a subcell. The first three values are relative to the cell, Q7.8.
struct CollSurface {
    std::int16_t offset_x = 0;
    std::int16_t base_ground_height = 0;
    std::int16_t offset_z = 0;
    std::uint16_t flags = 0;  // bits 0-4 ground type, 5-7 disable height, 8-10 surface cache slot, 11-14 trigger flag, 15 unused
    std::int16_t tilt_angle_x = 0;
    std::int16_t tilt_angle_z = 0;

    int ground_type() const { return flags & 0x1F; }
    int disable_height() const { return (flags >> 5) & 7; }
    int surface_cache_index() const { return (flags >> 8) & 7; }
    int trigger_flag_index() const { return (flags >> 11) & 0xF; }
};

// 10 bytes. Subcell split line: two 14-bit signed Q7.8 values with the two halves of a 4-bit trigger flag index, a
// third Q7.8 value, two indices into the split vertices and two into the surfaces.
struct CollSubcell {
    std::uint16_t offset_x_packed = 0;  // bits 0-13 signed Q7.8, bits 14-15 trigger flag index (high half)
    std::uint16_t offset_y_packed = 0;  // bits 0-13 signed Q7.8, bits 14-15 trigger flag index (low half)
    std::int16_t offset_z = 0;
    std::uint8_t split_vertex_index_0 = 0;
    std::uint8_t split_vertex_index_1 = 0;
    std::uint8_t surface_index_0 = 0;
    std::uint8_t surface_index_1 = 0;

    int offset_x() const { return static_cast<std::int16_t>(static_cast<std::uint16_t>(offset_x_packed << 2)) >> 2; }
    int offset_y() const { return static_cast<std::int16_t>(static_cast<std::uint16_t>(offset_y_packed << 2)) >> 2; }
    int trigger_flag_high() const { return offset_x_packed >> 14; }
    int trigger_flag_low() const { return offset_y_packed >> 14; }
};

// 10 bytes. A collision cylinder (the array at +0x18 of the collision header).
struct CollElement18 {
    std::uint16_t flags = 0;  // bits 0-4 ground type, 5-7 disable height, 8-11 trigger flag index, 12-15 unused
    Vec3s offset;             // Q7.8
    std::int16_t radius = 0;  // Q7.8

    int ground_type() const { return flags & 0x1F; }
    int disable_height() const { return (flags >> 5) & 7; }
};

// The shape lists of one grid square: each start runs to the next range's start.
struct CollRange {
    std::int16_t shape_start = 0;    // start in the byte table `index_pool` (indices into subcells, then elements18)
    std::int16_t surface_start = 0;  // start in the byte table `table_2c` (indices into surfaces)
};

// The 308 bytes at 0x54 and the arrays they point at (offsets relative to 0x54 on disc, absolute here).
// The grid is subcell_count_x * subcell_count_z cells of `subcell_size` (Q7.8); `ranges` has one entry per cell plus
// one closing entry.
struct IpdCollision {
    std::int32_t position_x = 0;  // Q23.8, the cell origin (0 when the cell has no collision)
    std::int32_t position_z = 0;
    std::uint8_t split_vertex_count = 0;
    std::uint8_t surface_count = 0;
    std::uint8_t subcell_count = 0;
    std::uint8_t element18_count = 0;
    std::uint32_t split_vertices_offset = 0;
    std::uint32_t surfaces_offset = 0;
    std::uint32_t subcells_offset = 0;
    std::uint32_t elements18_offset = 0;
    std::int16_t subcell_size = 0;
    std::uint8_t subcell_count_x = 0;
    std::uint8_t subcell_count_z = 0;
    std::uint32_t ranges_offset = 0;
    std::uint16_t index_pool_size = 0;   // +0x24
    std::uint16_t table_2c_size = 0;     // +0x26: size of the byte table at table_2c_offset
    std::uint32_t index_pool_offset = 0;  // ptr_28
    std::uint32_t table_2c_offset = 0;    // ptr_2C
    std::uint8_t subcell_check_count = 0;
    std::array<std::uint8_t, 3> pad_31{};
    std::array<std::uint8_t, 256> subcell_check_idx{};
    std::vector<Vec3s> split_vertices;
    std::vector<CollSurface> surfaces;
    std::vector<CollSubcell> subcells;
    std::vector<CollElement18> elements18;
    std::vector<CollRange> ranges;
    std::vector<std::uint8_t> index_pool;
    std::vector<std::uint8_t> table_2c;
};

struct IpdFile {
    std::uint8_t magic = 0;  // 0x14
    std::uint8_t is_loaded = 0;
    std::int8_t cell_x = 0;
    std::int8_t cell_z = 0;
    std::uint32_t lm_offset = 0;
    std::uint8_t model_info_count = 0;
    std::uint8_t model_buffer_count = 0;
    std::uint8_t model_order_count = 0;
    std::array<std::uint8_t, 9> pad_0b{};  // unused padding
    std::uint32_t model_info_offset = 0;
    std::uint32_t model_buffers_offset = 0;
    std::array<IpdOrderGroup, 25> order_groups{};
    std::array<std::uint8_t, 2> pad_4e{};  // unknown
    std::uint32_t model_order_offset = 0;
    std::vector<IpdModelInfo> model_infos;
    std::vector<IpdModelBuffer> model_buffers;
    // One byte each, an index into model_buffers. Every buffer appears at least once and may appear several times
    // (verified on all 491 cells that have buffers). order_groups cut this list into groups.
    std::vector<std::uint8_t> model_order;
    IpdCollision collision;
    LmFile lm;
    FileLayout layout;  // every structure of the file, LM included
};

}  // namespace sh::convert::level
