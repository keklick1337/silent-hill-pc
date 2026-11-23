// SPDX-License-Identifier: GPL-3.0-or-later
//
// A model file of the disc (.LM: materials, models, their meshes) made usable in the buffer it was read into.
// The file addresses its parts by 32-bit offsets from its start; the engine's records (model_records.h) hold
// pointers. convert() reads the file's header, materials, models and mesh headers, builds the records for them and
// puts the header record over the start of the file; the vertex, normal and primitive data stay where they are in
// the buffer and the records point at them.
#include "formats/game/model_records.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "engine/log/sh_log.h"
#include "formats/game/model_file_check.h"

namespace {

using namespace sh::formats::records;

// Sizes of the file's records.
constexpr std::size_t kFileHeaderBytes = 20;
constexpr std::size_t kFileModelBytes = 16;
constexpr std::size_t kFileMeshBytes = 24;
constexpr std::size_t kFileMaterialBytes = 24;

// A record of the file: little-endian fields at byte offsets.
class Fields {
public:
    explicit Fields(const std::uint8_t* at) : at_(at) {}

    std::uint8_t byte(std::size_t offset) const { return at_[offset]; }
    std::uint16_t word(std::size_t offset) const {
        return static_cast<std::uint16_t>(at_[offset] | at_[offset + 1] << 8);
    }
    std::uint32_t dword(std::size_t offset) const {
        return static_cast<std::uint32_t>(at_[offset]) | static_cast<std::uint32_t>(at_[offset + 1]) << 8 |
               static_cast<std::uint32_t>(at_[offset + 2]) << 16 | static_cast<std::uint32_t>(at_[offset + 3]) << 24;
    }
    const std::uint8_t* bytes(std::size_t offset) const { return at_ + offset; }

private:
    const std::uint8_t* at_;
};

// Allocated apart from the buffer: the records are larger than the file's own and do not fit in their place.
template <class Record>
Record* zeroed(std::size_t count) {
    return count != 0 ? static_cast<Record*>(std::calloc(count, sizeof(Record))) : nullptr;
}

LmMesh mesh_from(Fields f, std::uint8_t* file) {
    LmMesh mesh{};
    mesh.primitive_count = f.byte(0);
    mesh.vertex_count = f.byte(1);
    mesh.normal_count = f.byte(2);
    mesh.extra_count = f.byte(3);
    mesh.primitives = file + f.dword(4);
    mesh.vertices_xy = file + f.dword(8);
    mesh.vertices_z = file + f.dword(12);
    mesh.normals = file + f.dword(16);
    mesh.extra = file + f.dword(20);
    return mesh;
}

LmModel model_from(Fields f, std::uint8_t* file) {
    LmModel model{};
    std::memcpy(model.name, f.bytes(0), sizeof(model.name));
    model.mesh_count = f.byte(8);
    model.vertex_offset = f.byte(9);
    model.normal_offset = f.byte(10);
    model.draw_bits = f.byte(11);  // the record's bit fields are this byte, in the file's bit order
    LmMesh* meshes = zeroed<LmMesh>(model.mesh_count);
    const std::uint8_t* first = file + f.dword(12);
    for (std::size_t i = 0; i < model.mesh_count; ++i) {
        meshes[i] = mesh_from(Fields(first + i * kFileMeshBytes), file);
    }
    model.meshes = meshes;
    return model;
}

LmMaterial material_from(Fields f) {
    LmMaterial material{};
    std::memcpy(material.name, f.bytes(0), sizeof(material.name));
    material.texture = nullptr;  // bound when the model's textures are looked up
    material.is_applied = f.byte(12);
    material.pad_d = f.byte(13);
    material.tpage = f.byte(14);
    material.tpage_extra = f.byte(15);
    material.clut = f.word(16);
    material.clut_extra = f.word(18);
    material.uv_a = f.word(20);
    material.uv_b = f.word(22);
    return material;
}

}  // namespace

void sh::formats::records::lm_convert(void* buffer) {
    auto* const file = static_cast<std::uint8_t*>(buffer);
    auto* const header = static_cast<LmHeader*>(buffer);
    const Fields f(file);

    // The third byte is the "converted" mark in the file and in the record alike.
    constexpr std::size_t kLoadedMark = 2;
    if (f.byte(kLoadedMark) == 1) {
        return;
    }
    const std::uint8_t magic = f.byte(0);
    const std::uint8_t version = f.byte(1);
    // Not a model (a buffer never filled), or the wide variant of another port's mods, which is not drawn here
    // (mods bring high-detail characters as glb): marked, so it is not looked at again, and left as it is.
    if (magic != kLmMagic || version == kLmVersionWide) {
        header->is_loaded = 1;
        return;
    }

    const std::size_t material_count = f.byte(3);
    const std::size_t model_count = f.byte(8);
    const std::uint8_t* const file_materials = file + f.dword(4);
    const std::uint8_t* const file_models = file + f.dword(12);
    std::uint8_t* const model_order = file + f.dword(16);

    // Everything is read out before the header record goes over the file's start: the record is larger than the
    // file's header and covers the first of what follows it.
    LmMaterial* materials = zeroed<LmMaterial>(material_count);
    for (std::size_t i = 0; i < material_count; ++i) {
        materials[i] = material_from(Fields(file_materials + i * kFileMaterialBytes));
    }
    LmModel* models = zeroed<LmModel>(model_count);
    for (std::size_t i = 0; i < model_count; ++i) {
        models[i] = model_from(Fields(file_models + i * kFileModelBytes), file);
    }

    std::memset(header, 0, sizeof(LmHeader));
    header->magic = magic;
    header->version = version;
    header->is_loaded = 1;
    header->material_count = static_cast<std::uint8_t>(material_count);
    header->materials = materials;
    header->model_count = static_cast<std::uint8_t>(model_count);
    header->models = models;
    header->model_order = model_order;
}

// A mod's file that replaced a model of the disc, checked when the loader has read it: a report in the log only,
// so a file the drawing code copes with is never refused. Other kinds of files fail the header test and are not
// looked at. The buffer is readable for some bytes past the file (kModelBufferTail), as the check is told.
extern "C" void Lm_LooseValidateDiag(const void* raw, std::uint32_t fileSize, const char* name) {
    const auto* bytes = static_cast<const std::uint8_t*>(raw);
    const bool model = fileSize >= kFileHeaderBytes && bytes[0] == kLmMagic &&
                       (bytes[1] == kLmVersion || bytes[1] == kLmVersionWide) && bytes[2] == 0;
    if (!model) {
        return;
    }
    sh::formats::game::ModelFileRules rules;
    rules.readable_tail = sh::formats::game::kModelBufferTail;
    if (const auto broken = sh::formats::game::check_model_file({bytes, fileSize}, rules)) {
        SH_LOG_DEBUG(MODEL, "%s: a mod's model file fails the check: %s", name, broken->text.c_str());
    }
}
