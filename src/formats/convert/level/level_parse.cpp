#include "formats/convert/level/level_parse.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <utility>

namespace sh::convert::level {
namespace {

constexpr std::uint32_t kIpdHeaderSize = 0x188;  // 0x54 header + 308 bytes of collision data
constexpr std::uint32_t kCollisionBase = 0x54;
constexpr std::uint8_t kIpdMagic = 0x14;
constexpr std::uint8_t kLmMagic = 0x30;
constexpr std::uint8_t kLmVersion = 6;

constexpr std::uint32_t kLmHeaderSize = 20;
constexpr std::uint32_t kMaterialSize = 24;
constexpr std::uint32_t kModelSize = 16;
constexpr std::uint32_t kMeshSize = 24;
constexpr std::uint32_t kPrimitiveSize = 20;

class Reader {
public:
    explicit Reader(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}

    bool ok() const { return error_.empty(); }
    const std::string& error() const { return error_; }
    std::uint32_t size() const { return static_cast<std::uint32_t>(bytes_.size()); }

    // The range [offset, offset + length) must lie in the file. Records the first failure.
    bool fits(std::uint64_t offset, std::uint64_t length, const char* what) {
        if (offset > bytes_.size() || length > bytes_.size() - offset) {
            char text[160];
            std::snprintf(text, sizeof(text), "%s: 0x%llX..0x%llX is outside the file (size 0x%zX)", what,
                          static_cast<unsigned long long>(offset), static_cast<unsigned long long>(offset + length),
                          bytes_.size());
            fail(text);
            return false;
        }
        return true;
    }

    void fail(std::string message) {
        if (error_.empty()) {
            error_ = std::move(message);
        }
    }

    std::uint8_t u8(std::uint64_t o) {
        return fits(o, 1, "read") ? bytes_[o] : 0;
    }
    std::uint16_t u16(std::uint64_t o) {
        return fits(o, 2, "read") ? static_cast<std::uint16_t>(bytes_[o] | (bytes_[o + 1] << 8)) : 0;
    }
    std::uint32_t u32(std::uint64_t o) {
        return fits(o, 4, "read") ? static_cast<std::uint32_t>(bytes_[o]) | (static_cast<std::uint32_t>(bytes_[o + 1]) << 8) |
                                        (static_cast<std::uint32_t>(bytes_[o + 2]) << 16) |
                                        (static_cast<std::uint32_t>(bytes_[o + 3]) << 24)
                                  : 0;
    }
    std::int8_t s8(std::uint64_t o) { return static_cast<std::int8_t>(u8(o)); }
    std::int16_t s16(std::uint64_t o) { return static_cast<std::int16_t>(u16(o)); }
    std::int32_t s32(std::uint64_t o) { return static_cast<std::int32_t>(u32(o)); }

    template <std::size_t N>
    std::array<std::uint8_t, N> bytes(std::uint64_t o) {
        std::array<std::uint8_t, N> out{};
        if (fits(o, N, "read")) {
            std::copy_n(bytes_.begin() + static_cast<std::ptrdiff_t>(o), N, out.begin());
        }
        return out;
    }

private:
    std::span<const std::uint8_t> bytes_;
    std::string error_;
};

std::string name_of(const std::array<std::uint8_t, 8>& raw) {
    std::string name;
    for (const std::uint8_t c : raw) {
        if (c == 0) {
            break;
        }
        name.push_back(static_cast<char>(c));
    }
    return name;
}

void add_span(std::vector<Span>& spans, std::uint64_t offset, std::uint64_t size, const char* label) {
    if (size != 0) {
        spans.push_back({static_cast<std::uint32_t>(offset), static_cast<std::uint32_t>(size), label});
    }
}

// Sorts the spans, then finds what no span covers and what two spans share.
void finish_layout(FileLayout& layout, std::span<const std::uint8_t> file) {
    std::sort(layout.spans.begin(), layout.spans.end(), [](const Span& a, const Span& b) {
        return a.offset != b.offset ? a.offset < b.offset : a.size < b.size;
    });
    layout.file_size = static_cast<std::uint32_t>(file.size());
    std::uint32_t covered_to = 0;
    for (const Span& span : layout.spans) {
        if (span.offset > covered_to) {
            Gap gap{covered_to, span.offset - covered_to, true};
            gap.all_zero = std::all_of(file.begin() + gap.offset, file.begin() + gap.offset + gap.size,
                                       [](std::uint8_t b) { return b == 0; });
            layout.gaps.push_back(gap);
        } else if (span.offset < covered_to) {
            layout.overlap_bytes += std::min(covered_to, span.offset + span.size) - span.offset;
        }
        covered_to = std::max(covered_to, span.offset + span.size);
    }
    if (covered_to < layout.file_size) {
        Gap gap{covered_to, layout.file_size - covered_to, true};
        gap.all_zero = std::all_of(file.begin() + gap.offset, file.end(), [](std::uint8_t b) { return b == 0; });
        layout.gaps.push_back(gap);
        layout.trailing_bytes = gap.size;
    }
}

LmMaterial read_material(Reader& r, std::uint32_t at) {
    LmMaterial m;
    m.offset = at;
    m.name_raw = r.bytes<8>(at);
    m.name = name_of(m.name_raw);
    m.texture_pointer = r.u32(at + 8);
    m.applied = r.u8(at + 0xC);
    m.pad_d = r.u8(at + 0xD);
    m.tpage = r.u8(at + 0xE);
    m.tpage_base = r.u8(at + 0xF);
    m.clut = r.u16(at + 0x10);
    m.clut_base = r.u16(at + 0x12);
    m.uv = r.u16(at + 0x14);
    m.uv_base = r.u16(at + 0x16);
    return m;
}

LmPrimitive read_primitive(Reader& r, std::uint32_t at) {
    LmPrimitive p;
    constexpr std::uint32_t kUvAt[4] = {0, 4, 8, 0xA};
    for (int c = 0; c < 4; ++c) {
        p.uv[c] = {r.u8(at + kUvAt[c]), r.u8(at + kUvAt[c] + 1)};
    }
    p.clut = r.u16(at + 2);
    p.flags = r.u16(at + 6);
    p.vertex = r.bytes<4>(at + 0xC);
    p.shade_slot = r.bytes<4>(at + 0x10);
    return p;
}

LmMesh read_mesh(Reader& r, std::uint32_t base, std::uint32_t at, std::vector<Span>& spans) {
    LmMesh mesh;
    mesh.offset = at;
    const std::uint32_t primitive_count = r.u8(at);
    const std::uint32_t vertex_count = r.u8(at + 1);
    const std::uint32_t normal_count = r.u8(at + 2);
    const std::uint32_t shade_count = r.u8(at + 3);
    const std::uint64_t primitives = static_cast<std::uint64_t>(base) + r.u32(at + 4);
    const std::uint64_t vertices_xy = static_cast<std::uint64_t>(base) + r.u32(at + 8);
    const std::uint64_t vertices_z = static_cast<std::uint64_t>(base) + r.u32(at + 12);
    const std::uint64_t normals = static_cast<std::uint64_t>(base) + r.u32(at + 16);
    const std::uint64_t shade = static_cast<std::uint64_t>(base) + r.u32(at + 20);
    if (!r.ok()) {
        return mesh;
    }
    mesh.primitives_offset = static_cast<std::uint32_t>(primitives);
    mesh.vertices_xy_offset = static_cast<std::uint32_t>(vertices_xy);
    mesh.vertices_z_offset = static_cast<std::uint32_t>(vertices_z);
    mesh.normals_offset = static_cast<std::uint32_t>(normals);
    mesh.shade_offset = static_cast<std::uint32_t>(shade);

    if (primitive_count != 0 && r.fits(primitives, primitive_count * kPrimitiveSize, "LM primitives")) {
        mesh.primitives.reserve(primitive_count);
        for (std::uint32_t i = 0; i < primitive_count; ++i) {
            mesh.primitives.push_back(read_primitive(r, static_cast<std::uint32_t>(primitives) + i * kPrimitiveSize));
        }
        add_span(spans, primitives, primitive_count * kPrimitiveSize, "lm.primitives");
    }
    if (vertex_count != 0 && r.fits(vertices_xy, vertex_count * 4, "LM vertices (xy)") &&
        r.fits(vertices_z, vertex_count * 2, "LM vertices (z)")) {
        mesh.vertices.reserve(vertex_count);
        for (std::uint32_t i = 0; i < vertex_count; ++i) {
            mesh.vertices.push_back({r.s16(vertices_xy + i * 4), r.s16(vertices_xy + i * 4 + 2), r.s16(vertices_z + i * 2)});
        }
        add_span(spans, vertices_xy, vertex_count * 4, "lm.vertices_xy");
        add_span(spans, vertices_z, vertex_count * 2, "lm.vertices_z");
    }
    if (normal_count != 0 && r.fits(normals, normal_count * 4, "LM normals")) {
        mesh.normals.reserve(normal_count);
        for (std::uint32_t i = 0; i < normal_count; ++i) {
            const std::uint64_t n = normals + i * 4;
            mesh.normals.push_back({r.s8(n), r.s8(n + 1), r.s8(n + 2), r.u8(n + 3)});
        }
        add_span(spans, normals, normal_count * 4, "lm.normals");
    }
    if (shade_count != 0 && r.fits(shade, shade_count, "LM shade stream")) {
        mesh.shade.reserve(shade_count);
        for (std::uint32_t i = 0; i < shade_count; ++i) {
            mesh.shade.push_back(r.u8(shade + i));
        }
        add_span(spans, shade, shade_count, "lm.shade");
    }
    return mesh;
}

// The LM header at `base` and everything it points at. All offsets inside an LM are relative to `base`.
void read_lm(Reader& r, std::uint32_t base, LmFile& lm, std::vector<Span>& spans) {
    if (!r.fits(base, kLmHeaderSize, "LM header")) {
        return;
    }
    lm.offset = base;
    lm.magic = r.u8(base);
    lm.version = r.u8(base + 1);
    lm.is_loaded = r.u8(base + 2);
    if (lm.magic != kLmMagic) {
        r.fail("LM: bad magic");
        return;
    }
    if (lm.version != kLmVersion) {
        r.fail("LM: unsupported version " + std::to_string(lm.version) + " (only 6 is read)");
        return;
    }
    const std::uint32_t material_count = r.u8(base + 3);
    const std::uint64_t materials = static_cast<std::uint64_t>(base) + r.u32(base + 4);
    const std::uint32_t model_count = r.u8(base + 8);
    lm.pad_9 = r.bytes<3>(base + 9);
    const std::uint64_t models = static_cast<std::uint64_t>(base) + r.u32(base + 12);
    const std::uint64_t order = static_cast<std::uint64_t>(base) + r.u32(base + 16);
    if (!r.ok()) {
        return;
    }
    lm.materials_offset = static_cast<std::uint32_t>(materials);
    lm.model_headers_offset = static_cast<std::uint32_t>(models);
    lm.model_order_offset = static_cast<std::uint32_t>(order);
    add_span(spans, base, kLmHeaderSize, "lm.header");

    if (material_count != 0 && r.fits(materials, material_count * kMaterialSize, "LM materials")) {
        for (std::uint32_t i = 0; i < material_count; ++i) {
            lm.materials.push_back(read_material(r, static_cast<std::uint32_t>(materials) + i * kMaterialSize));
        }
        add_span(spans, materials, material_count * kMaterialSize, "lm.materials");
    }
    if (model_count != 0 && r.fits(order, model_count, "LM model order")) {
        for (std::uint32_t i = 0; i < model_count; ++i) {
            lm.model_order.push_back(r.u8(order + i));
        }
        add_span(spans, order, model_count, "lm.model_order");
    }
    if (model_count == 0 || !r.fits(models, model_count * kModelSize, "LM model headers")) {
        return;
    }
    add_span(spans, models, model_count * kModelSize, "lm.model_headers");
    for (std::uint32_t i = 0; i < model_count && r.ok(); ++i) {
        const std::uint32_t at = static_cast<std::uint32_t>(models) + i * kModelSize;
        LmModel model;
        model.offset = at;
        model.name_raw = r.bytes<8>(at);
        model.name = name_of(model.name_raw);
        const std::uint32_t mesh_count = r.u8(at + 8);
        model.vertex_offset = r.u8(at + 9);
        model.normal_offset = r.u8(at + 10);
        model.flags = r.u8(at + 11);
        const std::uint64_t meshes = static_cast<std::uint64_t>(base) + r.u32(at + 12);
        if (!r.ok()) {
            break;
        }
        model.mesh_headers_offset = static_cast<std::uint32_t>(meshes);
        if (mesh_count != 0 && r.fits(meshes, mesh_count * kMeshSize, "LM mesh headers")) {
            add_span(spans, meshes, mesh_count * kMeshSize, "lm.mesh_headers");
            for (std::uint32_t j = 0; j < mesh_count && r.ok(); ++j) {
                model.meshes.push_back(read_mesh(r, base, static_cast<std::uint32_t>(meshes) + j * kMeshSize, spans));
            }
        }
        lm.models.push_back(std::move(model));
    }
}

template <typename T, typename ReadFn>
void read_array(Reader& r, std::vector<T>& out, std::uint64_t offset, std::uint64_t count, std::uint64_t stride,
                const char* what, const char* label, std::vector<Span>& spans, ReadFn read) {
    if (count == 0 || !r.fits(offset, count * stride, what)) {
        return;
    }
    out.reserve(static_cast<std::size_t>(count));
    for (std::uint64_t i = 0; i < count; ++i) {
        out.push_back(read(static_cast<std::uint32_t>(offset + i * stride)));
    }
    add_span(spans, offset, count * stride, label);
}

void read_collision(Reader& r, IpdCollision& c, std::vector<Span>& spans) {
    constexpr std::uint32_t k = kCollisionBase;
    c.position_x = r.s32(k);
    c.position_z = r.s32(k + 4);
    c.split_vertex_count = r.u8(k + 8);
    c.surface_count = r.u8(k + 9);
    c.subcell_count = r.u8(k + 10);
    c.element18_count = r.u8(k + 11);
    c.split_vertices_offset = k + r.u32(k + 0xC);
    c.surfaces_offset = k + r.u32(k + 0x10);
    c.subcells_offset = k + r.u32(k + 0x14);
    c.elements18_offset = k + r.u32(k + 0x18);
    c.subcell_size = r.s16(k + 0x1C);
    c.subcell_count_x = r.u8(k + 0x1E);
    c.subcell_count_z = r.u8(k + 0x1F);
    c.ranges_offset = k + r.u32(k + 0x20);
    c.index_pool_size = r.u16(k + 0x24);
    c.table_2c_size = r.u16(k + 0x26);
    c.index_pool_offset = k + r.u32(k + 0x28);
    c.table_2c_offset = k + r.u32(k + 0x2C);
    c.subcell_check_count = r.u8(k + 0x30);
    c.pad_31 = r.bytes<3>(k + 0x31);
    c.subcell_check_idx = r.bytes<256>(k + 0x34);
    if (!r.ok()) {
        return;
    }

    read_array(r, c.split_vertices, c.split_vertices_offset, c.split_vertex_count, 6, "IPD collision split vertices",
               "coll.split_vertices", spans, [&](std::uint32_t at) { return Vec3s{r.s16(at), r.s16(at + 2), r.s16(at + 4)}; });
    read_array(r, c.surfaces, c.surfaces_offset, c.surface_count, 12, "IPD collision surfaces", "coll.surfaces", spans,
               [&](std::uint32_t at) {
                   return CollSurface{r.s16(at),     r.s16(at + 2), r.s16(at + 4),
                                      r.u16(at + 6), r.s16(at + 8), r.s16(at + 10)};
               });
    read_array(r, c.subcells, c.subcells_offset, c.subcell_count, 10, "IPD collision subcells", "coll.subcells", spans,
               [&](std::uint32_t at) {
                   return CollSubcell{r.u16(at), r.u16(at + 2), r.s16(at + 4), r.u8(at + 6),
                                      r.u8(at + 7), r.u8(at + 8), r.u8(at + 9)};
               });
    read_array(r, c.elements18, c.elements18_offset, c.element18_count, 10, "IPD collision elements", "coll.elements18",
               spans, [&](std::uint32_t at) {
                   return CollElement18{r.u16(at), Vec3s{r.s16(at + 2), r.s16(at + 4), r.s16(at + 6)}, r.s16(at + 8)};
               });
    const std::uint32_t grid = static_cast<std::uint32_t>(c.subcell_count_x) * c.subcell_count_z;
    read_array(r, c.ranges, c.ranges_offset, grid != 0 ? grid + 1 : 0, 4, "IPD collision ranges", "coll.ranges", spans,
               [&](std::uint32_t at) { return CollRange{r.s16(at), r.s16(at + 2)}; });
    read_array(r, c.index_pool, c.index_pool_offset, c.index_pool_size, 1, "IPD collision index pool", "coll.index_pool",
               spans, [&](std::uint32_t at) { return r.u8(at); });
    read_array(r, c.table_2c, c.table_2c_offset, c.table_2c_size, 1, "IPD collision table 2C", "coll.table_2c", spans,
               [&](std::uint32_t at) { return r.u8(at); });
}

}  // namespace

Result<IpdFile> parse_ipd(std::span<const std::uint8_t> file) {
    Reader r(file);
    if (file.size() < kIpdHeaderSize) {
        return Result<IpdFile>::fail("IPD: file is smaller than the 0x188 byte header");
    }
    IpdFile ipd;
    ipd.magic = r.u8(0);
    if (ipd.magic != kIpdMagic) {
        return Result<IpdFile>::fail("IPD: bad magic");
    }
    ipd.is_loaded = r.u8(1);
    ipd.cell_x = r.s8(2);
    ipd.cell_z = r.s8(3);
    ipd.lm_offset = r.u32(4);
    ipd.model_info_count = r.u8(8);
    ipd.model_buffer_count = r.u8(9);
    ipd.model_order_count = r.u8(10);
    ipd.pad_0b = r.bytes<9>(0xB);
    ipd.model_info_offset = r.u32(0x14);
    ipd.model_buffers_offset = r.u32(0x18);
    for (std::size_t i = 0; i < ipd.order_groups.size(); ++i) {
        ipd.order_groups[i] = {r.u8(0x1C + 2 * i), r.u8(0x1D + 2 * i)};
    }
    ipd.pad_4e = r.bytes<2>(0x4E);
    ipd.model_order_offset = r.u32(0x50);

    std::vector<Span>& spans = ipd.layout.spans;
    add_span(spans, 0, kIpdHeaderSize, "header");

    read_array(r, ipd.model_infos, ipd.model_info_offset, ipd.model_info_count, 16, "IPD model infos", "model_infos", spans,
               [&](std::uint32_t at) {
                   IpdModelInfo info;
                   info.offset = at;
                   info.is_global_plm = r.u8(at);
                   info.pad_1 = r.bytes<3>(at + 1);
                   info.name_raw = r.bytes<8>(at + 4);
                   info.name = name_of(info.name_raw);
                   info.model_header_offset = r.u32(at + 12);
                   return info;
               });
    read_array(r, ipd.model_order, ipd.model_order_offset, ipd.model_order_count, 1, "IPD model order", "model_order", spans,
               [&](std::uint32_t at) { return r.u8(at); });

    read_array(r, ipd.model_buffers, ipd.model_buffers_offset, ipd.model_buffer_count, 24, "IPD model buffers",
               "model_buffers", spans, [&](std::uint32_t at) {
                   IpdModelBuffer b;
                   b.offset = at;
                   const std::uint32_t instance_count = r.u8(at);
                   b.billboard_count = r.u8(at + 1);
                   const std::uint32_t bound_count = r.u8(at + 2);
                   b.pad_3 = r.u8(at + 3);
                   b.min_x = r.s16(at + 4);
                   b.max_x = r.s16(at + 6);
                   b.min_z = r.s16(at + 8);
                   b.max_z = r.s16(at + 10);
                   b.instances_offset = r.u32(at + 12);
                   b.billboards_offset = r.u32(at + 16);
                   b.subcell_bounds_offset = r.u32(at + 20);
                   read_array(r, b.instances, b.instances_offset, instance_count, 36, "IPD model instances", "instances",
                              spans, [&](std::uint32_t i) {
                                  IpdModelInstance m;
                                  m.offset = i;
                                  m.model_info_index = r.u32(i);
                                  for (int e = 0; e < 9; ++e) {
                                      m.rotation[e] = r.s16(i + 4 + 2 * e);
                                  }
                                  m.rotation_pad = r.s16(i + 22);
                                  for (int e = 0; e < 3; ++e) {
                                      m.translation[e] = r.s32(i + 24 + 4 * e);
                                  }
                                  return m;
                              });
                   auto vec4 = [&](std::uint32_t i) {
                       return IpdVec4{r.s16(i), r.s16(i + 2), r.s16(i + 4), r.s16(i + 6)};
                   };
                   read_array(r, b.billboards, b.billboards_offset, b.billboard_count, 8, "IPD billboards", "billboards", spans, vec4);
                   read_array(r, b.subcell_bounds, b.subcell_bounds_offset, bound_count, 8, "IPD subcell bounds",
                              "subcell_bounds", spans, vec4);
                   return b;
               });

    read_collision(r, ipd.collision, spans);

    if (r.ok()) {
        read_lm(r, ipd.lm_offset, ipd.lm, spans);
    }
    if (!r.ok()) {
        return Result<IpdFile>::fail("IPD: " + r.error());
    }
    for (const IpdModelBuffer& buffer : ipd.model_buffers) {
        for (const IpdModelInstance& instance : buffer.instances) {
            if (instance.model_info_index >= ipd.model_infos.size()) {
                return Result<IpdFile>::fail("IPD: instance at offset " + std::to_string(instance.offset) +
                                             " names model info " + std::to_string(instance.model_info_index) + " of " +
                                             std::to_string(ipd.model_infos.size()));
            }
        }
    }
    finish_layout(ipd.layout, file);
    return Result<IpdFile>::ok(std::move(ipd));
}

Result<LmFile> parse_plm(std::span<const std::uint8_t> file) {
    Reader r(file);
    LmFile lm;
    read_lm(r, 0, lm, lm.layout.spans);
    if (!r.ok()) {
        return Result<LmFile>::fail("PLM: " + r.error());
    }
    finish_layout(lm.layout, file);
    return Result<LmFile>::ok(std::move(lm));
}

}  // namespace sh::convert::level
