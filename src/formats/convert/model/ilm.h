#pragma once

#include "formats/common/status.h"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

// ILM: the skeletal model container of Silent Hill (PS1, version 6). Every offset is a file offset.
//
//   0x00 u8 '0', u8 6, u8 loaded flag, u8 material count
//   0x04 u32 material table (24 bytes each)   0x08 u8 part count
//   0x0C u32 part headers (16 bytes each)     0x10 u32 draw-order table (one byte per part)
//
// A "part" is a rigid piece of the body; its first two name characters are the decimal bone index. Each part has
// meshes; each mesh has its own vertices (s16 x, y, z in two arrays), normals (s8 x, y, z, count) and 20-byte
// primitives (triangle or quad). Primitive corners index a per-character *pool*, not the mesh: the vertices of every
// part are copied to pool[part.vertex_offset + i] in draw order, so a corner may read a vertex of a neighbouring
// part (that is how joint seams stay closed). resolve_pool() replays that.
namespace sh::convert::model {

struct IlmMaterial {
    std::uint32_t offset = 0;
    std::string name;  // texture name, 8 characters at most
    std::array<std::uint8_t, 24> raw{};
    std::uint16_t clut_base() const { return static_cast<std::uint16_t>(raw[0x10] | (raw[0x11] << 8)); }
};

struct IlmPrimitive {
    std::uint32_t offset = 0;
    std::array<std::uint8_t, 20> raw{};
    std::array<std::array<std::uint8_t, 2>, 4> uv{};  // u, v per corner
    std::uint16_t clut = 0;
    std::uint16_t flags = 0;                  // bits 8..14 material index, bit 15 semi-transparent
    std::array<std::uint8_t, 4> vertex{};     // pool slots
    std::array<std::uint8_t, 4> normal{};     // pool slots
    bool triangle = false;                    // fourth vertex byte is 0xFF
    int material() const { return (flags >> 8) & 0x7F; }
    bool semi_transparent() const { return (flags >> 15) & 1; }
    int corners() const { return triangle ? 3 : 4; }
};

struct IlmNormal {
    std::int8_t x = 0, y = 0, z = 0;
    std::uint8_t count = 0;
};

struct IlmMesh {
    std::uint32_t offset = 0;
    std::uint32_t prims_offset = 0, xy_offset = 0, z_offset = 0, normals_offset = 0, unknown_offset = 0;
    std::uint8_t unknown_count = 0;
    std::vector<std::array<std::int16_t, 3>> vertices;
    std::vector<IlmNormal> normals;
    std::vector<IlmPrimitive> prims;
};

struct IlmPart {
    std::uint32_t offset = 0;
    std::string name;
    std::uint8_t vertex_offset = 0;
    std::uint8_t normal_offset = 0;
    std::uint8_t bits = 0;
    std::vector<IlmMesh> meshes;
    // The first two name characters when they are digits; otherwise 0, as the game does.
    int bone() const;
};

// Where a pool slot's content comes from: part, mesh and index inside the mesh.
struct PoolRef {
    int part = -1;
    int mesh = -1;
    int index = -1;
    bool valid() const { return part >= 0; }
};

struct Ilm {
    std::vector<IlmMaterial> materials;
    std::vector<IlmPart> parts;
    std::vector<std::uint8_t> order;  // draw order; a permutation of part indices
    bool order_is_permutation = true; // false: the table was not a permutation and file order is used
};

// Per primitive corner references after replaying the pool, parallel to Ilm::parts[p].meshes[m].prims[i].
struct ResolvedPrim {
    std::array<PoolRef, 4> vertex;
    std::array<PoolRef, 4> normal;
};
struct ResolvedIlm {
    // [part][mesh][prim]
    std::vector<std::vector<std::vector<ResolvedPrim>>> prims;
    std::size_t dangling_vertex_corners = 0;  // corners that read a pool slot nobody wrote yet
    std::size_t dangling_normal_corners = 0;
};

// True when the bytes start like an ILM v6 (magic '0', version 6) and the header offsets are inside the file.
bool looks_like_ilm(std::span<const std::uint8_t> raw);

Result<Ilm> parse_ilm(std::span<const std::uint8_t> raw);

ResolvedIlm resolve_pool(const Ilm& ilm);

}  // namespace sh::convert::model
