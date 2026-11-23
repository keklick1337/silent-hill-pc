// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "formats/game/model_file_check.h"

#include <array>
#include <cstdio>
#include <vector>

namespace sh::formats::game {

namespace {

constexpr std::uint8_t kMagic = 0x30;
constexpr std::uint8_t kVersion = 6;
constexpr std::uint8_t kVersionWide = 7;

// Record sizes in the file.
constexpr std::uint32_t kHeaderBytes = 20;
constexpr std::uint32_t kMaterialBytes = 24;
constexpr std::uint32_t kPartBytes = 16;
constexpr std::uint32_t kMeshBytes = 24;
constexpr std::uint32_t kPrimitiveBytes = 20;
constexpr std::uint32_t kNormalBytes = 4;

constexpr std::uint32_t kWindowSlots = 256;
constexpr std::uint32_t kSkeletalVertexSlots = 255;
constexpr std::uint32_t kSkeletonParts = 56;
constexpr std::uint8_t kTriangleMark = 0xFF;

// One array of a mesh: where it is, how long, and how much room the readers of its kind want behind it.
struct Array {
    const char* name;
    std::uint32_t at;
    std::uint32_t count;
    std::uint32_t element;
    std::uint32_t room_behind;
};

struct Mesh {
    std::uint32_t primitives = 0, vertices = 0, normals = 0, shades = 0;
    std::uint32_t primitives_at = 0, normals_at = 0;
    std::array<Array, 5> arrays{};
};

struct Part {
    std::string name;
    std::uint32_t vertex_slot = 0, normal_slot = 0;
    std::uint32_t far_kind = 0;
    std::vector<Mesh> meshes;
};

template <typename... Args>
ModelFileIssue issue(int rule, const char* format, Args... args) {
    char text[200];
    const int head = std::snprintf(text, sizeof(text), "V%d: ", rule);
    std::snprintf(text + head, sizeof(text) - static_cast<std::size_t>(head), format, args...);
    return {rule, text};
}

class File {
public:
    explicit File(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}

    std::uint32_t size() const { return static_cast<std::uint32_t>(bytes_.size()); }
    std::uint8_t byte(std::uint32_t at) const { return bytes_[at]; }
    std::uint32_t word(std::uint32_t at) const {
        return static_cast<std::uint32_t>(bytes_[at]) | static_cast<std::uint32_t>(bytes_[at + 1]) << 8 |
               static_cast<std::uint32_t>(bytes_[at + 2]) << 16 | static_cast<std::uint32_t>(bytes_[at + 3]) << 24;
    }
    // `count` is a byte and `element` a record size, so the product is small; the offset can be anything.
    bool holds(std::uint32_t at, std::uint32_t count, std::uint32_t element) const {
        return at <= size() && count * element <= size() - at;
    }
    std::string name(std::uint32_t at) const {
        std::string out;
        for (std::uint32_t i = 0; i < 8 && bytes_[at + i] != 0; ++i) out += static_cast<char>(bytes_[at + i]);
        return out;
    }

private:
    std::span<const std::uint8_t> bytes_;
};

Mesh read_mesh(const File& f, std::uint32_t at) {
    Mesh m;
    m.primitives = f.byte(at);
    m.vertices = f.byte(at + 1);
    m.normals = f.byte(at + 2);
    m.shades = f.byte(at + 3);
    m.primitives_at = f.word(at + 4);
    m.normals_at = f.word(at + 16);
    m.arrays = {{
        {"primitives", m.primitives_at, m.primitives, kPrimitiveBytes, 0},
        {"vertex xy", f.word(at + 8), m.vertices, 4, 8},
        {"vertex z", f.word(at + 12), m.vertices, 2, 8},
        {"normals", m.normals_at, m.normals, kNormalBytes, 8},
        {"shade", f.word(at + 20), m.shades, 1, 3},
    }};
    return m;
}

// The bone a part's name starts with ("07RLEG" is bone 7), or -1 for a name without two leading digits.
int named_bone(const std::string& name) {
    const auto digit = [](char c) { return c >= '0' && c <= '9'; };
    if (name.size() < 2 || !digit(name[0]) || !digit(name[1])) return -1;
    return (name[0] - '0') * 10 + (name[1] - '0');
}

}  // namespace

std::optional<ModelFileIssue> check_model_file(std::span<const std::uint8_t> bytes, const ModelFileRules& rules) {
    const File f(bytes);

    if (f.size() >= 2 && f.byte(0) == kMagic && f.byte(1) == kVersionWide) {
        return issue(1, "a wide model (version 7) is not read: a mod's character model is a glb");
    }
    if (f.size() < kHeaderBytes || f.byte(0) != kMagic || f.byte(1) != kVersion || f.byte(2) != 0) {
        return issue(1, "not a model file as it is on the disc (magic 0x%02X, version %d, converted %d, %u bytes)",
                     f.size() > 0 ? f.byte(0) : 0, f.size() > 1 ? f.byte(1) : 0, f.size() > 2 ? f.byte(2) : 0,
                     f.size());
    }

    const std::uint32_t material_count = f.byte(3);
    const std::uint32_t materials_at = f.word(4);
    const std::uint32_t part_count = f.byte(8);
    const std::uint32_t parts_at = f.word(12);
    const std::uint32_t order_at = f.word(16);

    // V2: the sections of the file first, so the walk over the parts reads only bytes known to be there.
    const struct {
        const char* name;
        std::uint32_t at, count, element;
    } sections[] = {
        {"materials", materials_at, material_count, kMaterialBytes},
        {"parts", parts_at, part_count, kPartBytes},
        {"part order", order_at, part_count, 1},
    };
    for (const auto& s : sections) {
        if (!f.holds(s.at, s.count, s.element)) {
            return issue(2, "the %s lie outside the file (offset 0x%X, 0x%X bytes, file 0x%X)", s.name, s.at,
                         s.count * s.element, f.size());
        }
    }

    std::vector<Part> parts(part_count);
    for (std::uint32_t i = 0; i < part_count; ++i) {
        const std::uint32_t at = parts_at + i * kPartBytes;
        Part& part = parts[i];
        part.name = f.name(at);
        part.vertex_slot = f.byte(at + 9);
        part.normal_slot = f.byte(at + 10);
        part.far_kind = (f.byte(at + 11) >> 4) & 3;
        const std::uint32_t mesh_count = f.byte(at + 8);
        const std::uint32_t meshes_at = f.word(at + 12);
        if (!f.holds(meshes_at, mesh_count, kMeshBytes)) {
            return issue(2, "part %s: the meshes lie outside the file (offset 0x%X, 0x%X bytes, file 0x%X)",
                         part.name.c_str(), meshes_at, mesh_count * kMeshBytes, f.size());
        }
        for (std::uint32_t j = 0; j < mesh_count; ++j) {
            part.meshes.push_back(read_mesh(f, meshes_at + j * kMeshBytes));
            // An array of no elements is never read, wherever its offset points.
            for (const Array& a : part.meshes.back().arrays) {
                if (a.count == 0) continue;
                if (!f.holds(a.at, a.count, a.element)) {
                    return issue(2, "part %s mesh %u: the %s lie outside the file (offset 0x%X, 0x%X bytes, file 0x%X)",
                                 part.name.c_str(), j, a.name, a.at, a.count * a.element, f.size());
                }
                if (a.at % 4 != 0) {
                    return issue(2, "part %s mesh %u: the %s do not start on a 4-byte boundary (offset 0x%X)",
                                 part.name.c_str(), j, a.name, a.at);
                }
            }
        }
    }

    if (rules.skeletal && part_count > kSkeletonParts) {
        return issue(3, "%u parts, a skeleton holds %u", part_count, kSkeletonParts);
    }

    // V4: a part may be named twice (it is drawn twice), but every entry must name one.
    for (std::uint32_t i = 0; i < part_count; ++i) {
        if (f.byte(order_at + i) >= part_count) {
            return issue(4, "part order entry %u is %u, there are %u parts", i, f.byte(order_at + i), part_count);
        }
    }

    // V5..V8, each over the whole file before the next, so the lowest rule is the one reported.
    const std::uint32_t vertex_slots = rules.skeletal ? kSkeletalVertexSlots : kWindowSlots;
    for (const Part& part : parts) {
        for (std::size_t j = 0; j < part.meshes.size(); ++j) {
            if (part.vertex_slot + part.meshes[j].vertices > vertex_slots) {
                return issue(5, "part %s mesh %zu: vertices %u+%u do not fit %u slots", part.name.c_str(), j,
                             part.vertex_slot, part.meshes[j].vertices, vertex_slots);
            }
        }
    }
    for (const Part& part : parts) {
        for (std::size_t j = 0; j < part.meshes.size(); ++j) {
            if (part.normal_slot + part.meshes[j].normals > kWindowSlots) {
                return issue(6, "part %s mesh %zu: normals %u+%u do not fit %u slots", part.name.c_str(), j,
                             part.normal_slot, part.meshes[j].normals, kWindowSlots);
            }
        }
    }
    for (const Part& part : parts) {
        for (std::size_t j = 0; j < part.meshes.size(); ++j) {
            if (part.normal_slot + part.meshes[j].shades > kWindowSlots) {
                return issue(7, "part %s mesh %zu: shade values %u+%u do not fit %u slots", part.name.c_str(), j,
                             part.normal_slot, part.meshes[j].shades, kWindowSlots);
            }
        }
    }
    // V8: the fourth byte of a normal is how many shade values it takes; a normal that asks for none still takes
    // one slot.
    for (const Part& part : parts) {
        for (std::size_t j = 0; j < part.meshes.size(); ++j) {
            const Mesh& m = part.meshes[j];
            std::uint32_t taken = 0;
            for (std::uint32_t k = 0; k < m.normals; ++k) {
                const std::uint32_t wanted = f.byte(m.normals_at + k * kNormalBytes + 3);
                taken += wanted != 0 ? wanted : 1;
            }
            if (part.normal_slot + taken > kWindowSlots) {
                return issue(8, "part %s mesh %zu: the normals take %u+%u shade slots of %u", part.name.c_str(), j,
                             part.normal_slot, taken, kWindowSlots);
            }
        }
    }

    // V9: the room is counted to the end of the buffer, not of the file: disc files end an array at the last byte.
    for (const Part& part : parts) {
        for (std::size_t j = 0; j < part.meshes.size(); ++j) {
            for (const Array& a : part.meshes[j].arrays) {
                if (a.count == 0 || a.room_behind == 0) continue;
                const std::uint32_t room = f.size() + rules.readable_tail - (a.at + a.count * a.element);
                if (room < a.room_behind) {
                    return issue(9, "part %s mesh %zu: the %s need %u bytes behind them, there are %u",
                                 part.name.c_str(), j, a.name, a.room_behind, room);
                }
            }
        }
    }

    // V11: the parts in drawing order; a part writes the slots of all its meshes, then its primitives read.
    if (rules.skeletal) {
        std::array<bool, kWindowSlots> vertex_written{};
        std::array<bool, kWindowSlots> normal_written{};
        for (std::uint32_t i = 0; i < part_count; ++i) {
            const Part& part = parts[f.byte(order_at + i)];
            for (const Mesh& m : part.meshes) {
                for (std::uint32_t k = 0; k < m.vertices; ++k) vertex_written[part.vertex_slot + k] = true;
                for (std::uint32_t k = 0; k < m.normals; ++k) normal_written[part.normal_slot + k] = true;
            }
            std::uint32_t number = 0;
            for (const Mesh& m : part.meshes) {
                for (std::uint32_t k = 0; k < m.primitives; ++k, ++number) {
                    const std::uint32_t vertices_at = m.primitives_at + k * kPrimitiveBytes + 12;
                    const std::uint32_t normals_at = vertices_at + 4;
                    const std::uint32_t corners = f.byte(vertices_at + 3) == kTriangleMark ? 3 : 4;
                    for (std::uint32_t c = 0; c < corners; ++c) {
                        if (!vertex_written[f.byte(vertices_at + c)]) {
                            return issue(11, "part %s primitive %u corner %u reads vertex slot %u, which nothing wrote",
                                         part.name.c_str(), number, c, f.byte(vertices_at + c));
                        }
                        if (!normal_written[f.byte(normals_at + c)]) {
                            return issue(11, "part %s primitive %u corner %u reads normal slot %u, which nothing wrote",
                                         part.name.c_str(), number, c, f.byte(normals_at + c));
                        }
                    }
                }
            }
        }
    }

    for (const Part& part : parts) {
        if (part.far_kind != 0 && (part.vertex_slot != 0 || part.normal_slot != 0)) {
            return issue(12, "part %s is of far kind %u and must start at slot 0 (vertices at %u, normals at %u)",
                         part.name.c_str(), part.far_kind, part.vertex_slot, part.normal_slot);
        }
    }

    if (rules.animation_bones > 0) {
        for (const Part& part : parts) {
            if (named_bone(part.name) >= rules.animation_bones) {
                return issue(13, "part %s is on bone %d, the animation has %d", part.name.c_str(),
                             named_bone(part.name), rules.animation_bones);
            }
        }
    }
    return std::nullopt;
}

}  // namespace sh::formats::game
