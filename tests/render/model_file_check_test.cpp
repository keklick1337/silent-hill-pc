// The check of a mod's model file (formats/game/model_file_check.h): one small valid file, then one broken rule at
// a time, each reported under its own number and never under a later one.

#include "framework/sh_test.h"
#include "formats/game/model_file_check.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {

namespace game = sh::formats::game;
using Bytes = std::vector<std::uint8_t>;

void put32(Bytes& f, std::size_t at, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) f[at + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(v >> (8 * i));
}

// Where the pieces of the test file are.
constexpr std::size_t kMaterials = 20, kParts = 44, kOrder = 76, kMeshes = 80;
constexpr std::size_t kPrims0 = 128, kXy0 = 148, kZ0 = 164, kNormals0 = 172;
constexpr std::size_t kPrims1 = 188, kXy1 = 208, kZ1 = 220, kNormals1 = 228;
constexpr std::size_t kArraysEnd = 240, kFileEnd = 256;

std::size_t part(int i) { return kParts + static_cast<std::size_t>(i) * 16; }
std::size_t mesh(int i) { return kMeshes + static_cast<std::size_t>(i) * 24; }

// Two parts of one mesh each. "00BODY": a quad on vertex and normal slots 0..3. "01HEAD": a triangle on slots
// 4..6. One material, the parts drawn in file order, 16 spare bytes behind the last array.
Bytes model() {
    Bytes f(kFileEnd, 0);
    f[0] = 0x30;
    f[1] = 6;
    f[3] = 1;
    put32(f, 4, kMaterials);
    f[8] = 2;
    put32(f, 12, kParts);
    put32(f, 16, kOrder);
    f[kOrder] = 0;
    f[kOrder + 1] = 1;

    std::memcpy(&f[part(0)], "00BODY", 6);
    f[part(0) + 8] = 1;  // meshes
    put32(f, part(0) + 12, static_cast<std::uint32_t>(mesh(0)));
    std::memcpy(&f[part(1)], "01HEAD", 6);
    f[part(1) + 8] = 1;
    f[part(1) + 9] = 4;   // first vertex slot
    f[part(1) + 10] = 4;  // first normal slot
    put32(f, part(1) + 12, static_cast<std::uint32_t>(mesh(1)));

    // A mesh: counts of primitives, vertices, normals, shade values; then the five array offsets.
    const auto mesh_at = [&](std::size_t at, std::uint8_t vertices, std::size_t prims, std::size_t xy, std::size_t z,
                             std::size_t normals) {
        f[at] = 1;
        f[at + 1] = vertices;
        f[at + 2] = vertices;
        put32(f, at + 4, static_cast<std::uint32_t>(prims));
        put32(f, at + 8, static_cast<std::uint32_t>(xy));
        put32(f, at + 12, static_cast<std::uint32_t>(z));
        put32(f, at + 16, static_cast<std::uint32_t>(normals));
    };
    mesh_at(mesh(0), 4, kPrims0, kXy0, kZ0, kNormals0);
    mesh_at(mesh(1), 3, kPrims1, kXy1, kZ1, kNormals1);

    // A primitive's vertex slots are bytes 12..15, its normal slots 16..19; 0xFF in the fourth corner is a triangle.
    const std::uint8_t quad[4] = {0, 1, 2, 3};
    std::memcpy(&f[kPrims0 + 12], quad, 4);
    std::memcpy(&f[kPrims0 + 16], quad, 4);
    const std::uint8_t triangle[4] = {4, 5, 6, 0xFF};
    std::memcpy(&f[kPrims1 + 12], triangle, 4);
    std::memcpy(&f[kPrims1 + 16], triangle, 4);
    return f;
}

game::ModelFileRules skeletal() {
    game::ModelFileRules r;
    r.skeletal = true;
    return r;
}

int rule_of(const Bytes& file, const game::ModelFileRules& rules) {
    const auto broken = game::check_model_file(file, rules);
    if (!broken) return 0;
    // The text names its rule.
    SH_CHECK(broken->text.rfind("V" + std::to_string(broken->rule) + ":", 0) == 0);
    return broken->rule;
}

}  // namespace

SH_TEST(model_file_check_accepts_a_sound_file) {
    SH_CHECK(rule_of(model(), skeletal()) == 0);
    SH_CHECK(rule_of(model(), {}) == 0);
}

SH_TEST(model_file_check_header) {
    Bytes f = model();
    f[1] = 5;
    SH_CHECK(rule_of(f, {}) == 1);
    f = model();
    f[2] = 1;  // already converted in memory: the offsets are no longer offsets
    SH_CHECK(rule_of(f, {}) == 1);
    f = model();
    f[0] = 0x31;
    SH_CHECK(rule_of(f, {}) == 1);
    f = model();
    f.resize(19);
    SH_CHECK(rule_of(f, {}) == 1);
    SH_CHECK(rule_of({}, {}) == 1);

    // The wide format of version 7 is refused by name.
    f = model();
    f[1] = 7;
    const auto wide = game::check_model_file(f, {});
    SH_CHECK(wide && wide->rule == 1 && wide->text.find("version 7") != std::string::npos);
}

SH_TEST(model_file_check_sections_inside_the_file) {
    Bytes f = model();
    put32(f, 4, kFileEnd - 23);  // 24 bytes of material from here end one byte past the file
    SH_CHECK(rule_of(f, {}) == 2);
    f = model();
    put32(f, 12, 0xFFFFFFF0u);  // must not wrap around
    SH_CHECK(rule_of(f, {}) == 2);
    f = model();
    put32(f, 16, kFileEnd - 1);  // two order bytes from the last byte
    SH_CHECK(rule_of(f, {}) == 2);
    f = model();
    put32(f, part(1) + 12, kFileEnd - 8);  // the mesh record of a part
    SH_CHECK(rule_of(f, {}) == 2);
    f = model();
    put32(f, mesh(0) + 16, kFileEnd - 12);  // four normals of 4 bytes
    SH_CHECK(rule_of(f, {}) == 2);
    f = model();
    put32(f, mesh(1) + 8, kXy1 + 2);  // inside the file but not on a 4-byte boundary
    SH_CHECK(rule_of(f, {}) == 2);

    // An array of no elements may point anywhere.
    f = model();
    put32(f, mesh(0) + 20, 0xFFFFFFFFu);  // shade values: none
    SH_CHECK(rule_of(f, skeletal()) == 0);
}

SH_TEST(model_file_check_part_count_and_order) {
    // 57 parts without meshes: more than a skeleton holds, fine for a prop.
    Bytes many(20 + 57 * 16 + 57, 0);
    many[0] = 0x30;
    many[1] = 6;
    many[8] = 57;
    put32(many, 12, 20);
    put32(many, 16, 20 + 57 * 16);
    SH_CHECK(rule_of(many, skeletal()) == 3);
    SH_CHECK(rule_of(many, {}) == 0);

    Bytes f = model();
    f[kOrder + 1] = 2;
    SH_CHECK(rule_of(f, {}) == 4);
    // A part named twice is drawn twice; the head's corners are then never written.
    f = model();
    f[kOrder + 1] = 0;
    SH_CHECK(rule_of(f, {}) == 0);
}

SH_TEST(model_file_check_slot_windows) {
    // Vertices 253..255: a skeletal model keeps slot 255 free, a prop may use it.
    Bytes f = model();
    f[part(1) + 9] = 253;
    SH_CHECK(rule_of(f, skeletal()) == 5);
    SH_CHECK(rule_of(f, {}) == 0);
    f[part(1) + 9] = 254;
    SH_CHECK(rule_of(f, {}) == 5);

    f = model();
    f[part(1) + 10] = 254;  // normals 254..256
    SH_CHECK(rule_of(f, {}) == 6);

    // 255 shade values from slot 4. The array itself is in the file (the first 255 bytes will do).
    f = model();
    f[mesh(1) + 3] = 255;
    put32(f, mesh(1) + 20, 0);
    SH_CHECK(rule_of(f, {}) == 7);

    // Every normal of the body asks for 100 shade values.
    f = model();
    for (int k = 0; k < 4; ++k) f[kNormals0 + static_cast<std::size_t>(k) * 4 + 3] = 100;
    SH_CHECK(rule_of(f, {}) == 8);
    // A normal that asks for none still takes a slot: 3 normals from slot 254 would be V6 first, so count on the
    // body with its 4 normals from slot 253.
    f = model();
    f[part(0) + 10] = 252;
    SH_CHECK(rule_of(f, {}) == 0);  // 252 + 4 = 256 normals and 256 shade slots
    f[kNormals0 + 3] = 2;           // one normal takes two: 257
    SH_CHECK(rule_of(f, {}) == 8);
}

SH_TEST(model_file_check_room_behind_the_arrays) {
    // The file ends with its last array: no room in the file, 16 bytes in a buffer of the game.
    Bytes f = model();
    f.resize(kArraysEnd);
    SH_CHECK(rule_of(f, {}) == 9);
    game::ModelFileRules in_buffer;
    in_buffer.readable_tail = game::kModelBufferTail;
    SH_CHECK(rule_of(f, in_buffer) == 0);
    // Seven spare bytes are one short for the normals.
    f.resize(kArraysEnd + 7);
    SH_CHECK(rule_of(f, {}) == 9);
    f.resize(kArraysEnd + 8);
    SH_CHECK(rule_of(f, {}) == 0);
}

SH_TEST(model_file_check_corners_read_written_slots) {
    // The body's quad reads vertex slot 5, which the head writes after the body is drawn.
    Bytes f = model();
    f[kPrims0 + 12 + 1] = 5;
    SH_CHECK(rule_of(f, skeletal()) == 11);
    SH_CHECK(rule_of(f, {}) == 0);  // a held item takes such corners from its carrier
    // Drawn the other way round, the slot is written in time.
    f[kOrder] = 1;
    f[kOrder + 1] = 0;
    SH_CHECK(rule_of(f, skeletal()) == 0);

    f = model();
    f[kPrims1 + 16 + 2] = 9;  // a normal slot nobody writes
    SH_CHECK(rule_of(f, skeletal()) == 11);
    // The fourth corner of a triangle is not a corner.
    f = model();
    f[kPrims1 + 16 + 3] = 200;
    SH_CHECK(rule_of(f, skeletal()) == 0);
}

SH_TEST(model_file_check_far_parts_and_bones) {
    Bytes f = model();
    f[part(1) + 11] = 1 << 4;  // far kind 1 on a part that starts at slot 4
    SH_CHECK(rule_of(f, {}) == 12);
    f = model();
    f[part(0) + 11] = 2 << 4;  // the body starts at slot 0
    SH_CHECK(rule_of(f, {}) == 0);

    // "01HEAD" is on bone 1.
    game::ModelFileRules rules;
    rules.animation_bones = 1;
    SH_CHECK(rule_of(model(), rules) == 13);
    rules.animation_bones = 2;
    SH_CHECK(rule_of(model(), rules) == 0);
    // A name without two digits is on bone 0.
    f = model();
    std::memcpy(&f[part(1)], "HEAD\0\0", 6);
    rules.animation_bones = 1;
    SH_CHECK(rule_of(f, rules) == 0);
}

SH_TEST(model_file_check_lowest_rule_first) {
    // Broken four ways: the order entry (V4), the head's vertices (V5), its normals (V6), its far kind (V12).
    Bytes f = model();
    f[kOrder + 1] = 9;
    f[part(1) + 9] = 254;
    f[part(1) + 10] = 254;
    f[part(1) + 11] = 1 << 4;
    SH_CHECK(rule_of(f, {}) == 4);
    f[kOrder + 1] = 1;
    SH_CHECK(rule_of(f, {}) == 5);
    f[part(1) + 9] = 4;
    SH_CHECK(rule_of(f, {}) == 6);
    f[part(1) + 10] = 4;
    SH_CHECK(rule_of(f, {}) == 12);
}
