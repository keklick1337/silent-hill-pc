#include "engine/world/area_collision.h"

#include "formats/common/json.h"

#include <algorithm>
#include <array>
#include <climits>
#include <cstdint>
#include <map>
#include <cctype>
#include <cstring>
#include <string_view>
#include <vector>

namespace sh::world {

namespace {

using json::Value;

constexpr std::string_view kExtension = "SH_collision";
constexpr std::string_view kNodeSuffix = "/collision";

std::uint32_t read_u32(std::span<const std::uint8_t> b, std::size_t at) {
    return static_cast<std::uint32_t>(b[at]) | (static_cast<std::uint32_t>(b[at + 1]) << 8) |
           (static_cast<std::uint32_t>(b[at + 2]) << 16) | (static_cast<std::uint32_t>(b[at + 3]) << 24);
}

std::string upper(std::string_view text) {
    std::string out(text);
    for (char& c : out) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return out;
}

// Reads members of one extension object; the first problem is kept and reported with its member name.
class Reader {
public:
    Reader(const Value& root, std::string cell) : root_(root), cell_(std::move(cell)) {}

    bool ok() const { return error_.empty(); }
    const std::string& error() const { return error_; }

    std::int64_t integer(std::string_view key, std::int64_t min, std::int64_t max) {
        const Value* v = root_.find(key);
        if (v == nullptr || !v->is_integer()) {
            fail(std::string(key) + ": missing or not an integer");
            return 0;
        }
        return checked(*v, key, min, max);
    }

    // A table {"items": [[...], ...]} with `columns` integers per row.
    std::vector<std::vector<std::int64_t>> rows(std::string_view key, std::size_t columns) {
        std::vector<std::vector<std::int64_t>> out;
        const Value* items = table_items(key);
        if (items == nullptr) {
            return out;
        }
        for (const Value& row : items->array()) {
            if (!row.is_array() || row.array().size() != columns) {
                fail(std::string(key) + ": row of " + std::to_string(columns) + " integers expected");
                return {};
            }
            std::vector<std::int64_t> values;
            for (const Value& cell : row.array()) {
                if (!cell.is_integer()) {
                    fail(std::string(key) + ": integer expected");
                    return {};
                }
                values.push_back(cell.integer());
            }
            out.push_back(std::move(values));
        }
        return out;
    }

    // A byte table {"items": [b, ...]}.
    std::vector<std::uint8_t> bytes(std::string_view key) {
        std::vector<std::uint8_t> out;
        const Value* items = table_items(key);
        if (items == nullptr) {
            return out;
        }
        for (const Value& v : items->array()) {
            if (!v.is_integer() || v.integer() < 0 || v.integer() > 255) {
                fail(std::string(key) + ": byte expected");
                return {};
            }
            out.push_back(static_cast<std::uint8_t>(v.integer()));
        }
        return out;
    }

    // A hex string of exactly `size` bytes.
    std::vector<std::uint8_t> hex(std::string_view key, std::size_t size) {
        const Value* v = root_.find(key);
        std::vector<std::uint8_t> out;
        if (v == nullptr || !v->is_string()) {
            fail(std::string(key) + ": missing or not a string");
            return out;
        }
        std::string digits;
        for (char c : v->string()) {
            if (std::isxdigit(static_cast<unsigned char>(c))) {
                digits.push_back(c);
            }
        }
        if (digits.size() != size * 2) {
            fail(std::string(key) + ": " + std::to_string(size) + " hex bytes expected");
            return out;
        }
        for (std::size_t i = 0; i < size; ++i) {
            out.push_back(static_cast<std::uint8_t>(std::stoul(digits.substr(i * 2, 2), nullptr, 16)));
        }
        return out;
    }

    void fail(std::string message) {
        if (error_.empty()) {
            error_ = cell_ + ": " + kExtension.data() + "." + message;
        }
    }

    std::int64_t checked(const Value& v, std::string_view key, std::int64_t min, std::int64_t max) {
        if (v.integer() < min || v.integer() > max) {
            fail(std::string(key) + ": " + std::to_string(v.integer()) + " is outside " + std::to_string(min) + ".." +
                 std::to_string(max));
            return 0;
        }
        return v.integer();
    }

private:
    const Value* table_items(std::string_view key) {
        const Value* table = root_.find(key);
        const Value* items = table != nullptr ? table->find("items") : nullptr;
        if (items == nullptr || !items->is_array()) {
            fail(std::string(key) + ".items: missing or not an array");
            return nullptr;
        }
        return items;
    }

    const Value& root_;
    std::string cell_;
    std::string error_;
};

std::int16_t s16(std::int64_t v) { return static_cast<std::int16_t>(v); }
std::uint8_t u8(std::int64_t v) { return static_cast<std::uint8_t>(v); }

// The 14-bit signed value in the low bits of a stored direction word.
std::int16_t direction_of(std::uint16_t word) {
    return static_cast<std::int16_t>(static_cast<std::int16_t>(static_cast<std::uint16_t>(word << 2)) >> 2);
}

Result<CellCollision> build_cell(const Value& ext, std::string name) {
    Reader r(ext, name);
    CellCollision cell;
    cell.name = std::move(name);
    cell.origin_x = static_cast<std::int32_t>(r.integer("position_x", INT32_MIN, INT32_MAX));
    cell.origin_z = static_cast<std::int32_t>(r.integer("position_z", INT32_MIN, INT32_MAX));
    cell.square_size = s16(r.integer("subcell_size", INT16_MIN, INT16_MAX));
    cell.columns = u8(r.integer("subcell_count_x", 0, 255));
    cell.rows = u8(r.integer("subcell_count_z", 0, 255));
    cell.floor_pool_size_field = static_cast<std::uint16_t>(r.integer("surface_lut_size", 0, 65535));
    cell.stored_pass = u8(r.integer("subcell_check_count", 0, 255));
    const std::vector<std::uint8_t> marks = r.hex("subcell_check_idx", 256);
    if (marks.size() == cell.stored_marks.size()) {
        std::copy(marks.begin(), marks.end(), cell.stored_marks.begin());
    }

    for (const auto& v : r.rows("split_vertices", 3)) {
        cell.vertices.push_back(Point3{s16(v[0]), s16(v[1]), s16(v[2])});
    }
    for (const auto& v : r.rows("surfaces", 6)) {
        FloorPlane f;
        f.anchor_x = s16(v[0]);
        f.base_height = s16(v[1]);
        f.anchor_z = s16(v[2]);
        f.packed_flags = static_cast<std::uint16_t>(v[3]);
        f.ground_type = static_cast<std::uint8_t>(f.packed_flags & 0x1F);
        f.height_disabled = static_cast<std::uint8_t>((f.packed_flags >> 5) & 7);
        f.side_slot = static_cast<std::uint8_t>((f.packed_flags >> 8) & 7);
        f.enable_group = static_cast<std::uint8_t>((f.packed_flags >> 11) & 0xF);
        f.slope_x = s16(v[4]);
        f.slope_z = s16(v[5]);
        cell.floors.push_back(f);
    }
    for (const auto& v : r.rows("subcells", 7)) {
        WallEdge e;
        e.packed_x = static_cast<std::uint16_t>(v[0]);
        e.packed_z = static_cast<std::uint16_t>(v[1]);
        e.dir_x = direction_of(e.packed_x);
        e.group_high = static_cast<std::uint8_t>(e.packed_x >> 14);
        e.dir_z = direction_of(e.packed_z);
        e.group_low = static_cast<std::uint8_t>(e.packed_z >> 14);
        e.length = s16(v[2]);
        e.end_vertex = u8(v[3]);
        e.start_vertex = u8(v[4]);
        e.floor_left = u8(v[5]);
        e.floor_right = u8(v[6]);
        cell.edges.push_back(e);
    }
    for (const auto& v : r.rows("elements18", 5)) {
        Pillar p;
        p.packed_flags = static_cast<std::uint16_t>(v[0]);
        p.ground_type = static_cast<std::uint8_t>(p.packed_flags & 0x1F);
        p.height_disabled = static_cast<std::uint8_t>((p.packed_flags >> 5) & 7);
        p.enable_group = static_cast<std::uint8_t>((p.packed_flags >> 8) & 0xF);
        p.position = Point3{s16(v[1]), s16(v[2]), s16(v[3])};
        p.radius = s16(v[4]);
        cell.pillars.push_back(p);
    }
    for (const auto& v : r.rows("ranges", 2)) {
        cell.squares.push_back(SquareLists{s16(v[0]), s16(v[1])});
    }
    cell.edge_pool = r.bytes("index_pool");
    cell.floor_pool = r.bytes("table_2c");

    if (!r.ok()) {
        return Result<CellCollision>::fail(r.error());
    }
    // Indices the queries follow without a check must be in range; a file that breaks this is rejected here.
    if (cell.edges.size() > 255 || cell.floors.size() > 255 || cell.vertices.size() > 255 || cell.pillars.size() > 255) {
        return Result<CellCollision>::fail(cell.name + ": more than 255 entries in a collision table");
    }
    const std::size_t grid = static_cast<std::size_t>(cell.columns) * cell.rows;
    if (!cell.empty() && cell.squares.size() < grid + 1) {
        return Result<CellCollision>::fail(cell.name + ": " + std::to_string(cell.squares.size()) +
                                           " square lists for a grid of " + std::to_string(grid));
    }
    for (const SquareLists& s : cell.squares) {
        if (s.first < 0 || static_cast<std::size_t>(s.first) > cell.edge_pool.size() || s.first_floor < 0 ||
            static_cast<std::size_t>(s.first_floor) > cell.floor_pool.size()) {
            return Result<CellCollision>::fail(cell.name + ": a square list points outside its pool");
        }
    }
    const std::size_t elements = cell.edges.size() + cell.pillars.size();
    for (std::uint8_t index : cell.edge_pool) {
        if (index >= elements && index >= cell.edges.size()) {
            // The game reads such an entry as a pillar past the table; no cell on the disc has one.
            return Result<CellCollision>::fail(cell.name + ": edge pool entry " + std::to_string(index) +
                                               " is neither an edge nor a pillar");
        }
    }
    for (std::uint8_t index : cell.floor_pool) {
        if (index >= cell.floors.size()) {
            return Result<CellCollision>::fail(cell.name + ": floor pool entry " + std::to_string(index) +
                                               " is not a floor");
        }
    }
    for (const WallEdge& e : cell.edges) {
        if (e.end_vertex >= cell.vertices.size() || e.start_vertex >= cell.vertices.size() ||
            (e.floor_left != 255 && e.floor_left >= cell.floors.size()) ||
            (e.floor_right != 255 && e.floor_right >= cell.floors.size())) {
            return Result<CellCollision>::fail(cell.name + ": a wall edge points outside the vertex or floor table");
        }
    }
    for (const FloorPlane& f : cell.floors) {
        if (f.side_slot > 3) {
            return Result<CellCollision>::fail(cell.name + ": floor side slot " + std::to_string(f.side_slot) +
                                               " is outside 0..3");
        }
    }
    return Result<CellCollision>::ok(std::move(cell));
}

}  // namespace

Result<CellCollision> parse_cell_collision(std::string_view json_text, std::string name) {
    Result<Value> doc = json::parse(json_text);
    if (!doc) {
        return Result<CellCollision>::fail(name + ": " + doc.error());
    }
    return build_cell(doc.value(), std::move(name));
}

Result<AreaMaterials> read_area_materials(std::span<const std::uint8_t> glb) {
    if (glb.size() < 20 || std::memcmp(glb.data(), "glTF", 4) != 0 || read_u32(glb, 4) != 2) {
        return Result<AreaMaterials>::fail("not a glTF 2.0 binary file");
    }
    const std::uint32_t json_length = read_u32(glb, 12);
    if (read_u32(glb, 16) != 0x4E4F534Au || json_length > glb.size() - 20) {
        return Result<AreaMaterials>::fail("the first chunk is not a complete JSON chunk");
    }
    Result<Value> doc = json::parse(std::string_view(reinterpret_cast<const char*>(glb.data() + 20), json_length));
    if (!doc) {
        return Result<AreaMaterials>::fail(doc.error());
    }
    AreaMaterials out;
    const Value* nodes = doc.value().find("nodes");
    if (nodes == nullptr || !nodes->is_array()) {
        return Result<AreaMaterials>::ok(std::move(out));
    }
    for (const Value& node : nodes->array()) {
        const Value* name = node.find("name");
        const Value* extras = node.find("extras");
        const Value* list = extras != nullptr ? extras->find("materials") : nullptr;
        if (name == nullptr || !name->is_string() || list == nullptr || !list->is_array()) {
            continue;
        }
        std::vector<std::array<char, 8>>& names = out[upper(name->string())];
        for (const Value& m : list->array()) {
            if (!m.is_string() || m.string().size() > 8) {
                return Result<AreaMaterials>::fail("node " + std::string(name->string()) +
                                                   ": a material is not a name of up to 8 characters");
            }
            std::array<char, 8> n{};
            std::memcpy(n.data(), m.string().data(), m.string().size());
            names.push_back(n);
        }
    }
    return Result<AreaMaterials>::ok(std::move(out));
}

Result<AreaCollision> read_area_collision(std::span<const std::uint8_t> glb) {
    if (glb.size() < 20 || std::memcmp(glb.data(), "glTF", 4) != 0 || read_u32(glb, 4) != 2) {
        return Result<AreaCollision>::fail("not a glTF 2.0 binary file");
    }
    const std::uint32_t json_length = read_u32(glb, 12);
    if (read_u32(glb, 16) != 0x4E4F534Au || json_length > glb.size() - 20) {
        return Result<AreaCollision>::fail("the first chunk is not a complete JSON chunk");
    }
    const std::string_view text(reinterpret_cast<const char*>(glb.data() + 20), json_length);
    Result<Value> doc = json::parse(text);
    if (!doc) {
        return Result<AreaCollision>::fail(doc.error());
    }
    AreaCollision area;
    const Value* nodes = doc.value().find("nodes");
    if (nodes == nullptr || !nodes->is_array()) {
        return Result<AreaCollision>::ok(std::move(area));
    }
    // The lamp glows on the cell roots (importer 6), by cell.
    std::map<std::string, std::vector<CellCollision::Glow>> glows;
    for (const Value& node : nodes->array()) {
        const Value* name = node.find("name");
        const Value* extras = node.find("extras");
        const Value* list = extras != nullptr ? extras->find("glows") : nullptr;
        if (name == nullptr || !name->is_string() || list == nullptr || !list->is_array()) {
            continue;
        }
        std::vector<CellCollision::Glow>& out = glows[upper(name->string())];
        for (const Value& g : list->array()) {
            if (!g.is_array() || g.array().size() != 4) {
                return Result<AreaCollision>::fail("cell " + std::string(name->string()) + ": a glow is not [x, y, z, kind]");
            }
            std::array<std::int16_t, 4> v{};
            for (std::size_t i = 0; i < 4; ++i) {
                const Value& n = g.array()[i];
                if (!n.is_integer() || n.integer() < INT16_MIN || n.integer() > INT16_MAX) {
                    return Result<AreaCollision>::fail("cell " + std::string(name->string()) + ": a glow value is not a 16-bit integer");
                }
                v[i] = static_cast<std::int16_t>(n.integer());
            }
            out.push_back(CellCollision::Glow{Point3{v[0], v[1], v[2]}, v[3]});
        }
    }
    for (const Value& node : nodes->array()) {
        const Value* extensions = node.find("extensions");
        const Value* ext = extensions != nullptr ? extensions->find(kExtension) : nullptr;
        const Value* name = node.find("name");
        if (ext == nullptr || name == nullptr || !name->is_string()) {
            continue;
        }
        std::string_view node_name = name->string();
        if (node_name.size() > kNodeSuffix.size() && node_name.ends_with(kNodeSuffix)) {
            node_name.remove_suffix(kNodeSuffix.size());
        }
        Result<CellCollision> cell = build_cell(*ext, upper(node_name));
        if (!cell) {
            return Result<AreaCollision>::fail(cell.error());
        }
        std::string key = cell.value().name;
        if (auto it = glows.find(key); it != glows.end()) {
            cell.value().glows = std::move(it->second);
        }
        area[std::move(key)] = std::make_shared<const CellCollision>(std::move(cell.value()));
    }
    return Result<AreaCollision>::ok(std::move(area));
}

}  // namespace sh::world
