#include "formats/convert/model/ilm.h"

#include <algorithm>
#include <cstring>

namespace sh::convert::model {
namespace {

constexpr std::size_t kHeaderBytes = 0x14;
constexpr std::size_t kMaterialBytes = 24;
constexpr std::size_t kPartBytes = 16;
constexpr std::size_t kMeshBytes = 24;
constexpr std::size_t kPrimBytes = 20;
constexpr std::uint8_t kTriangleSentinel = 0xFF;

class Cursor {
public:
    explicit Cursor(std::span<const std::uint8_t> data) : d_(data) {}

    bool has(std::size_t offset, std::size_t bytes) const {
        return offset <= d_.size() && bytes <= d_.size() - offset;
    }
    std::uint8_t u8(std::size_t o) const { return d_[o]; }
    std::uint16_t u16(std::size_t o) const { return static_cast<std::uint16_t>(d_[o] | (d_[o + 1] << 8)); }
    std::int16_t s16(std::size_t o) const { return static_cast<std::int16_t>(u16(o)); }
    std::uint32_t u32(std::size_t o) const {
        return std::uint32_t{d_[o]} | (std::uint32_t{d_[o + 1]} << 8) | (std::uint32_t{d_[o + 2]} << 16) |
               (std::uint32_t{d_[o + 3]} << 24);
    }
    // NUL-terminated within the 8-byte field.
    std::string name(std::size_t o) const {
        std::string s;
        for (std::size_t i = 0; i < 8 && d_[o + i] != 0; ++i) {
            const auto c = d_[o + i];
            s += (c >= 0x20 && c < 0x7F) ? static_cast<char>(c) : '?';
        }
        return s;
    }

private:
    std::span<const std::uint8_t> d_;
};

}  // namespace

int IlmPart::bone() const {
    if (name.size() >= 2 && name[0] >= '0' && name[0] <= '9' && name[1] >= '0' && name[1] <= '9') {
        return (name[0] - '0') * 10 + (name[1] - '0');
    }
    return 0;
}

bool looks_like_ilm(std::span<const std::uint8_t> raw) {
    if (raw.size() < kHeaderBytes || raw[0] != '0' || raw[1] != 6) {
        return false;
    }
    const Cursor c(raw);
    const std::uint32_t mats = c.u32(4);
    const std::uint32_t parts = c.u32(0xC);
    const std::uint32_t order = c.u32(0x10);
    return c.has(mats, std::size_t{raw[3]} * kMaterialBytes) && c.has(parts, std::size_t{raw[8]} * kPartBytes) &&
           c.has(order, raw[8]);
}

Result<Ilm> parse_ilm(std::span<const std::uint8_t> raw) {
    using Out = Result<Ilm>;
    if (!looks_like_ilm(raw)) {
        return Out::fail("not an ILM v6 (magic, version or header offsets)");
    }
    const Cursor c(raw);
    Ilm ilm;

    const std::size_t material_count = raw[3];
    const std::size_t part_count = raw[8];
    const std::uint32_t materials_at = c.u32(4);
    const std::uint32_t parts_at = c.u32(0xC);
    const std::uint32_t order_at = c.u32(0x10);

    for (std::size_t i = 0; i < material_count; ++i) {
        IlmMaterial m;
        m.offset = materials_at + static_cast<std::uint32_t>(i * kMaterialBytes);
        m.name = c.name(m.offset);
        std::memcpy(m.raw.data(), raw.data() + m.offset, kMaterialBytes);
        ilm.materials.push_back(std::move(m));
    }

    ilm.order.assign(raw.begin() + order_at, raw.begin() + order_at + part_count);
    {
        std::vector<std::uint8_t> sorted = ilm.order;
        std::sort(sorted.begin(), sorted.end());
        for (std::size_t i = 0; i < sorted.size(); ++i) {
            if (sorted[i] != i) {
                ilm.order_is_permutation = false;
                break;
            }
        }
        if (!ilm.order_is_permutation) {
            for (std::size_t i = 0; i < part_count; ++i) {
                ilm.order[i] = static_cast<std::uint8_t>(i);
            }
        }
    }

    for (std::size_t pi = 0; pi < part_count; ++pi) {
        const std::size_t base = parts_at + pi * kPartBytes;
        IlmPart part;
        part.offset = static_cast<std::uint32_t>(base);
        part.name = c.name(base);
        const std::size_t mesh_count = c.u8(base + 8);
        part.vertex_offset = c.u8(base + 9);
        part.normal_offset = c.u8(base + 0xA);
        part.bits = c.u8(base + 0xB);
        const std::uint32_t meshes_at = c.u32(base + 0xC);
        if (!c.has(meshes_at, mesh_count * kMeshBytes)) {
            return Out::fail("part " + std::to_string(pi) + ": mesh headers outside the file");
        }

        for (std::size_t mi = 0; mi < mesh_count; ++mi) {
            const std::size_t mb = meshes_at + mi * kMeshBytes;
            IlmMesh mesh;
            mesh.offset = static_cast<std::uint32_t>(mb);
            const std::size_t prim_count = c.u8(mb);
            const std::size_t vertex_count = c.u8(mb + 1);
            const std::size_t normal_count = c.u8(mb + 2);
            mesh.unknown_count = c.u8(mb + 3);
            mesh.prims_offset = c.u32(mb + 4);
            mesh.xy_offset = c.u32(mb + 8);
            mesh.z_offset = c.u32(mb + 0xC);
            mesh.normals_offset = c.u32(mb + 0x10);
            mesh.unknown_offset = c.u32(mb + 0x14);

            const std::string where = "part " + std::to_string(pi) + " mesh " + std::to_string(mi) + ": ";
            if (!c.has(mesh.xy_offset, vertex_count * 4) || !c.has(mesh.z_offset, vertex_count * 2)) {
                return Out::fail(where + "vertex arrays outside the file");
            }
            if (!c.has(mesh.normals_offset, normal_count * 4)) {
                return Out::fail(where + "normal array outside the file");
            }
            if (!c.has(mesh.prims_offset, prim_count * kPrimBytes)) {
                return Out::fail(where + "primitives outside the file");
            }

            for (std::size_t v = 0; v < vertex_count; ++v) {
                mesh.vertices.push_back({c.s16(mesh.xy_offset + v * 4), c.s16(mesh.xy_offset + v * 4 + 2),
                                         c.s16(mesh.z_offset + v * 2)});
            }
            for (std::size_t n = 0; n < normal_count; ++n) {
                const std::size_t o = mesh.normals_offset + n * 4;
                mesh.normals.push_back({static_cast<std::int8_t>(c.u8(o)), static_cast<std::int8_t>(c.u8(o + 1)),
                                        static_cast<std::int8_t>(c.u8(o + 2)), c.u8(o + 3)});
            }
            for (std::size_t p = 0; p < prim_count; ++p) {
                const std::size_t o = mesh.prims_offset + p * kPrimBytes;
                IlmPrimitive prim;
                prim.offset = static_cast<std::uint32_t>(o);
                std::memcpy(prim.raw.data(), raw.data() + o, kPrimBytes);
                prim.uv[0] = {c.u8(o), c.u8(o + 1)};
                prim.clut = c.u16(o + 2);
                prim.uv[1] = {c.u8(o + 4), c.u8(o + 5)};
                prim.flags = c.u16(o + 6);
                prim.uv[2] = {c.u8(o + 8), c.u8(o + 9)};
                prim.uv[3] = {c.u8(o + 0xA), c.u8(o + 0xB)};
                for (std::size_t k = 0; k < 4; ++k) {
                    prim.vertex[k] = c.u8(o + 0xC + k);
                    prim.normal[k] = c.u8(o + 0x10 + k);
                }
                prim.triangle = prim.vertex[3] == kTriangleSentinel;
                mesh.prims.push_back(prim);
            }
            part.meshes.push_back(std::move(mesh));
        }
        ilm.parts.push_back(std::move(part));
    }
    return Out::ok(std::move(ilm));
}

// The game copies each part's vertices into the pool in draw order and draws the primitives of a mesh right after
// that mesh's vertices went in. A corner therefore sees the latest writer of its slot at that moment.
ResolvedIlm resolve_pool(const Ilm& ilm) {
    ResolvedIlm out;
    out.prims.resize(ilm.parts.size());
    for (std::size_t p = 0; p < ilm.parts.size(); ++p) {
        out.prims[p].resize(ilm.parts[p].meshes.size());
        for (std::size_t m = 0; m < ilm.parts[p].meshes.size(); ++m) {
            out.prims[p][m].resize(ilm.parts[p].meshes[m].prims.size());
        }
    }

    std::array<PoolRef, 256> vertex_pool{};
    std::array<PoolRef, 256> normal_pool{};
    for (const std::uint8_t part_index : ilm.order) {
        const IlmPart& part = ilm.parts[part_index];
        for (std::size_t m = 0; m < part.meshes.size(); ++m) {
            const IlmMesh& mesh = part.meshes[m];
            for (std::size_t j = 0; j < mesh.vertices.size(); ++j) {
                const std::size_t slot = std::size_t{part.vertex_offset} + j;
                if (slot < vertex_pool.size()) {
                    vertex_pool[slot] = {part_index, static_cast<int>(m), static_cast<int>(j)};
                }
            }
            for (std::size_t j = 0; j < mesh.normals.size(); ++j) {
                const std::size_t slot = std::size_t{part.normal_offset} + j;
                if (slot < normal_pool.size()) {
                    normal_pool[slot] = {part_index, static_cast<int>(m), static_cast<int>(j)};
                }
            }
            for (std::size_t i = 0; i < mesh.prims.size(); ++i) {
                const IlmPrimitive& prim = mesh.prims[i];
                ResolvedPrim& rp = out.prims[part_index][m][i];
                for (int k = 0; k < prim.corners(); ++k) {
                    rp.vertex[static_cast<std::size_t>(k)] = vertex_pool[prim.vertex[static_cast<std::size_t>(k)]];
                    rp.normal[static_cast<std::size_t>(k)] = normal_pool[prim.normal[static_cast<std::size_t>(k)]];
                    if (!rp.vertex[static_cast<std::size_t>(k)].valid()) {
                        ++out.dangling_vertex_corners;
                    }
                    if (!rp.normal[static_cast<std::size_t>(k)].valid()) {
                        ++out.dangling_normal_corners;
                    }
                }
            }
        }
    }
    return out;
}

}  // namespace sh::convert::model
