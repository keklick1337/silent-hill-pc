#include "formats/convert/level/level_json.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <utility>
#include <vector>

namespace sh::convert::level {
namespace {

// Streaming writer: containers break lines, rows ("[1, 2, 3]") stay on one line.
class Json {
public:
    std::string take() {
        out_ += '\n';
        return std::move(out_);
    }

    void begin_object() { open('{'); }
    void end_object() { close('}'); }
    void begin_array() { open('['); }
    void end_array() { close(']'); }

    void key(std::string_view name) {
        prefix();
        string(name);
        out_ += ": ";
        after_key_ = true;
    }

    void value(std::int64_t v) { start_value(); out_ += std::to_string(v); }
    void value(std::uint64_t v) { start_value(); out_ += std::to_string(v); }
    void value(int v) { value(static_cast<std::int64_t>(v)); }
    void value(unsigned v) { value(static_cast<std::uint64_t>(v)); }
    void value(bool v) { start_value(); out_ += v ? "true" : "false"; }
    void value(std::string_view s) { start_value(); string(s); }
    void value(const char* s) { value(std::string_view(s)); }
    void raw(std::string_view json_text) { start_value(); out_.append(json_text); }

    template <typename T>
    void field(std::string_view name, const T& v) {
        key(name);
        value(v);
    }

    template <typename Range>
    void row(const Range& values) {
        start_value();
        out_ += '[';
        bool first = true;
        for (const auto& v : values) {
            if (!first) {
                out_ += ", ";
            }
            first = false;
            out_ += std::to_string(static_cast<std::int64_t>(v));
        }
        out_ += ']';
    }

    void row(std::initializer_list<std::int64_t> values) { row<std::initializer_list<std::int64_t>>(values); }

    void hex(std::string_view name, const std::uint8_t* data, std::size_t size) {
        static const char digits[] = "0123456789abcdef";
        std::string s;
        for (std::size_t i = 0; i < size; ++i) {
            s += digits[data[i] >> 4];
            s += digits[data[i] & 15];
        }
        key(name);
        value(std::string_view(s));
    }

    template <std::size_t N>
    void hex(std::string_view name, const std::array<std::uint8_t, N>& data) {
        hex(name, data.data(), N);
    }

private:
    void open(char c) {
        start_value();
        out_ += c;
        has_items_.push_back(false);
    }

    void close(char c) {
        const bool had_items = has_items_.back();
        has_items_.pop_back();
        if (had_items) {
            newline();
        }
        out_ += c;
    }

    // The comma and line break that precede the next element of the current container.
    void prefix() {
        if (has_items_.empty()) {
            return;
        }
        if (has_items_.back()) {
            out_ += ',';
        }
        has_items_.back() = true;
        newline();
    }

    void start_value() {
        if (after_key_) {
            after_key_ = false;
        } else {
            prefix();
        }
    }

    void newline() {
        out_ += '\n';
        out_.append(has_items_.size() * 2, ' ');
    }

    void string(std::string_view s) {
        out_ += '"';
        for (const char c : s) {
            if (c == '"' || c == '\\') {
                out_ += '\\';
                out_ += c;
            } else if (static_cast<unsigned char>(c) < 0x20 || static_cast<unsigned char>(c) >= 0x7F) {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned>(static_cast<unsigned char>(c)));
                out_ += buf;
            } else {
                out_ += c;
            }
        }
        out_ += '"';
    }

    std::string out_;
    std::vector<bool> has_items_;
    bool after_key_ = false;
};

using Row = std::initializer_list<std::int64_t>;

void write_fields(Json& j, std::initializer_list<const char*> names) {
    j.key("fields");
    j.begin_array();
    for (const char* name : names) {
        j.value(name);
    }
    j.end_array();
}

// Common shape of a table: where it sits in the file, the column names, then one row per element.
template <typename T, typename RowFn>
void write_table(Json& j, std::string_view name, std::uint32_t offset, std::initializer_list<const char*> columns,
                 const std::vector<T>& items, RowFn make_row) {
    j.key(name);
    j.begin_object();
    j.field("offset", offset);
    j.field("count", static_cast<std::uint64_t>(items.size()));
    write_fields(j, columns);
    j.key("items");
    j.begin_array();
    for (const T& item : items) {
        make_row(item);
    }
    j.end_array();
    j.end_object();
}

void write_layout(Json& j, const FileLayout& layout) {
    std::uint64_t covered = 0;
    for (const Span& s : layout.spans) {
        covered += s.size;
    }
    j.key("layout");
    j.begin_object();
    j.field("file_size", layout.file_size);
    j.field("structure_count", static_cast<std::uint64_t>(layout.spans.size()));
    j.field("covered_bytes", covered - layout.overlap_bytes);
    j.field("overlap_bytes", layout.overlap_bytes);
    j.field("trailing_bytes", layout.trailing_bytes);
    write_table(j, "gaps", 0, {"offset", "size", "all_zero"}, layout.gaps, [&](const Gap& g) {
        j.row(Row{g.offset, g.size, g.all_zero ? 1 : 0});
    });
    j.end_object();
}

void write_lm(Json& j, const LmFile& lm) {
    j.begin_object();
    j.field("offset", lm.offset);
    j.field("magic", lm.magic);
    j.field("version", lm.version);
    j.field("is_loaded", lm.is_loaded);
    j.key("pad_9");
    j.row(lm.pad_9);
    j.field("materials_offset", lm.materials_offset);
    j.field("model_headers_offset", lm.model_headers_offset);
    j.field("model_order_offset", lm.model_order_offset);
    j.key("model_order");
    j.row(lm.model_order);

    j.key("materials");
    j.begin_array();
    for (const LmMaterial& m : lm.materials) {
        j.begin_object();
        j.field("offset", m.offset);
        j.field("name", m.name);
        j.hex("name_raw", m.name_raw);
        j.field("texture_pointer", m.texture_pointer);
        j.field("applied", m.applied);
        j.field("pad_d", m.pad_d);
        j.field("tpage", m.tpage);
        j.field("tpage_base", m.tpage_base);
        j.field("clut", m.clut);
        j.field("clut_base", m.clut_base);
        j.field("uv", m.uv);
        j.field("uv_base", m.uv_base);
        j.end_object();
    }
    j.end_array();

    j.key("models");
    j.begin_array();
    for (const LmModel& model : lm.models) {
        j.begin_object();
        j.field("offset", model.offset);
        j.field("name", model.name);
        j.hex("name_raw", model.name_raw);
        j.field("vertex_offset", model.vertex_offset);
        j.field("normal_offset", model.normal_offset);
        j.field("flags", model.flags);
        j.field("mesh_headers_offset", model.mesh_headers_offset);
        j.key("meshes");
        j.begin_array();
        for (const LmMesh& mesh : model.meshes) {
            j.begin_object();
            j.field("offset", mesh.offset);
            write_table(j, "primitives", mesh.primitives_offset,
                        {"u0", "v0", "u1", "v1", "u2", "v2", "u3", "v3", "clut", "flags", "vertex0", "vertex1", "vertex2",
                         "vertex3", "shade_slot0", "shade_slot1", "shade_slot2", "shade_slot3"},
                        mesh.primitives, [&](const LmPrimitive& p) {
                            j.row(Row{p.uv[0][0], p.uv[0][1], p.uv[1][0], p.uv[1][1], p.uv[2][0], p.uv[2][1], p.uv[3][0],
                                      p.uv[3][1], p.clut, p.flags, p.vertex[0], p.vertex[1], p.vertex[2], p.vertex[3],
                                      p.shade_slot[0], p.shade_slot[1], p.shade_slot[2], p.shade_slot[3]});
                        });
            j.field("vertices_z_offset", mesh.vertices_z_offset);
            write_table(j, "vertices", mesh.vertices_xy_offset, {"x", "y", "z"}, mesh.vertices,
                        [&](const Vec3s& v) { j.row(Row{v.x, v.y, v.z}); });
            write_table(j, "normals", mesh.normals_offset, {"x", "y", "z", "count"}, mesh.normals,
                        [&](const LmNormal& n) { j.row(Row{n.x, n.y, n.z, n.count}); });
            j.key("shade");
            j.begin_object();
            j.field("offset", mesh.shade_offset);
            j.key("items");
            j.row(mesh.shade);
            j.end_object();
            j.end_object();
        }
        j.end_array();
        j.end_object();
    }
    j.end_array();
    j.end_object();
}

void write_collision(Json& j, const IpdCollision& c) {
    j.begin_object();
    j.field("position_x", c.position_x);
    j.field("position_z", c.position_z);
    j.field("subcell_size", c.subcell_size);
    j.field("subcell_count_x", c.subcell_count_x);
    j.field("subcell_count_z", c.subcell_count_z);
    j.field("surface_lut_size", c.table_2c_size);
    j.field("subcell_check_count", c.subcell_check_count);
    j.hex("pad_31", c.pad_31);
    j.hex("subcell_check_idx", c.subcell_check_idx);
    write_table(j, "split_vertices", c.split_vertices_offset, {"x", "y", "z"}, c.split_vertices,
                [&](const Vec3s& v) { j.row(Row{v.x, v.y, v.z}); });
    write_table(j, "surfaces", c.surfaces_offset,
                {"offset_x", "base_ground_height", "offset_z", "flags", "tilt_angle_x", "tilt_angle_z"}, c.surfaces,
                [&](const CollSurface& s) { j.row(Row{s.offset_x, s.base_ground_height, s.offset_z, s.flags, s.tilt_angle_x, s.tilt_angle_z}); });
    write_table(j, "subcells", c.subcells_offset,
                {"offset_x_packed", "offset_y_packed", "offset_z", "split_vertex_0", "split_vertex_1", "surface_0", "surface_1"}, c.subcells,
                [&](const CollSubcell& s) {
                    j.row(Row{s.offset_x_packed, s.offset_y_packed, s.offset_z, s.split_vertex_index_0, s.split_vertex_index_1,
                              s.surface_index_0, s.surface_index_1});
                });
    write_table(j, "elements18", c.elements18_offset, {"flags", "x", "y", "z", "radius"}, c.elements18,
                [&](const CollElement18& e) { j.row(Row{e.flags, e.offset.x, e.offset.y, e.offset.z, e.radius}); });
    write_table(j, "ranges", c.ranges_offset, {"shape_lut_index", "surface_lut_index"}, c.ranges,
                [&](const CollRange& r) { j.row(Row{r.shape_start, r.surface_start}); });
    j.key("index_pool");
    j.begin_object();
    j.field("offset", c.index_pool_offset);
    j.key("items");
    j.row(c.index_pool);
    j.end_object();
    j.key("table_2c");
    j.begin_object();
    j.field("offset", c.table_2c_offset);
    j.key("items");
    j.row(c.table_2c);
    j.end_object();
    j.end_object();
}

void write_head(Json& j, std::string_view kind, std::string_view game_path) {
    j.field("format", "silent-hill-level");
    j.field("version", 1);
    j.field("kind", kind);
    j.field("path", game_path);
    j.field("units",
            "Q7.8 positions (256 = 1 world unit), Y down; rotation Q12; one cell = 40 units; offsets are absolute file offsets");
}

}  // namespace

std::string ipd_to_json(const IpdFile& ipd, std::string_view game_path, std::string_view glb_section) {
    Json j;
    j.begin_object();
    write_head(j, "ipd", game_path);

    j.key("header");
    j.begin_object();
    j.field("magic", ipd.magic);
    j.field("is_loaded", ipd.is_loaded);
    j.field("cell_x", ipd.cell_x);
    j.field("cell_z", ipd.cell_z);
    j.field("lm_offset", ipd.lm_offset);
    j.field("model_info_count", ipd.model_info_count);
    j.field("model_buffer_count", ipd.model_buffer_count);
    j.field("model_order_count", ipd.model_order_count);
    j.hex("pad_0b", ipd.pad_0b);
    j.field("model_info_offset", ipd.model_info_offset);
    j.field("model_buffers_offset", ipd.model_buffers_offset);
    write_table(j, "order_groups", 0x1C, {"start", "count"},
                std::vector<IpdOrderGroup>(ipd.order_groups.begin(), ipd.order_groups.end()),
                [&](const IpdOrderGroup& g) { j.row(Row{g.start, g.count}); });
    j.hex("pad_4e", ipd.pad_4e);
    j.field("model_order_offset", ipd.model_order_offset);
    j.end_object();

    j.key("model_order");
    j.row(ipd.model_order);

    j.key("model_infos");
    j.begin_array();
    for (const IpdModelInfo& m : ipd.model_infos) {
        j.begin_object();
        j.field("offset", m.offset);
        j.field("is_global_plm", m.is_global_plm);
        j.hex("pad_1", m.pad_1);
        j.field("name", m.name);
        j.hex("name_raw", m.name_raw);
        j.field("model_header_offset", m.model_header_offset);
        j.end_object();
    }
    j.end_array();

    j.key("model_buffers");
    j.begin_array();
    for (const IpdModelBuffer& b : ipd.model_buffers) {
        j.begin_object();
        j.field("offset", b.offset);
        j.field("pad_3", b.pad_3);
        j.key("bounds");
        j.row(Row{b.min_x, b.max_x, b.min_z, b.max_z});
        write_table(j, "instances", b.instances_offset,
                    {"model_info", "m00", "m01", "m02", "m10", "m11", "m12", "m20", "m21", "m22", "pad", "tx", "ty", "tz"},
                    b.instances, [&](const IpdModelInstance& m) {
                        j.row(Row{m.model_info_index, m.rotation[0], m.rotation[1], m.rotation[2], m.rotation[3],
                                  m.rotation[4], m.rotation[5], m.rotation[6], m.rotation[7], m.rotation[8],
                                  m.rotation_pad, m.translation[0], m.translation[1], m.translation[2]});
                    });
        write_table(j, "billboards", b.billboards_offset, {"x", "y", "z", "kind"}, b.billboards,
                    [&](const IpdVec4& v) { j.row(Row{v.x, v.y, v.z, v.w}); });
        write_table(j, "subcell_bounds", b.subcell_bounds_offset, {"min_x", "max_x", "min_z", "max_z"}, b.subcell_bounds,
                    [&](const IpdVec4& v) { j.row(Row{v.x, v.y, v.z, v.w}); });
        j.end_object();
    }
    j.end_array();

    j.key("collision");
    write_collision(j, ipd.collision);

    j.key("lm");
    write_lm(j, ipd.lm);
    write_layout(j, ipd.layout);
    if (!glb_section.empty()) {
        j.key("glb");
        j.raw(glb_section);
    }
    j.end_object();
    return j.take();
}

std::string collision_to_json(const IpdCollision& collision) {
    Json j;
    write_collision(j, collision);
    return j.take();
}

std::string plm_to_json(const LmFile& plm, std::string_view game_path) {
    Json j;
    j.begin_object();
    write_head(j, "plm", game_path);
    j.key("lm");
    write_lm(j, plm);
    write_layout(j, plm.layout);
    j.end_object();
    return j.take();
}

}  // namespace sh::convert::level
