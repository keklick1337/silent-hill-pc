// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/world/lm_materials.h"

#include <climits>
#include <cstring>

namespace sh::world {

namespace {

constexpr std::uint8_t kLmMagic = sh::formats::records::kLmMagic;

std::uint32_t word_at(const char* p, int i) {
    std::uint32_t w;
    std::memcpy(&w, p + i * 4, 4);
    return w;
}

LmPrimitive* primitives(LmMesh& mesh) { return static_cast<LmPrimitive*>(mesh.primitives); }

void set_low_byte(std::uint16_t& word, std::uint8_t value) {
    word = static_cast<std::uint16_t>((word & 0xFF00u) | value);
}
void set_high_byte(std::uint16_t& word, std::uint8_t value) {
    word = static_cast<std::uint16_t>((word & 0x00FFu) | (static_cast<unsigned>(value) << 8));
}

// Every primitive using `material` drawn untextured: its slot's page was handed to another chunk.
void model_material_untexture(LmModel& model, int material) {
    for (LmMesh* mesh = model.meshes; mesh < model.meshes + model.mesh_count; mesh++) {
        for (LmPrimitive* p = primitives(*mesh); p < primitives(*mesh) + mesh->primitive_count; p++) {
            if (p->material() == material) {
                p->set_page(kUntexturedPage);
            }
        }
    }
}

}  // namespace

bool same_name(const char* a, const char* b) { return word_at(a, 0) == word_at(b, 0) && word_at(a, 1) == word_at(b, 1); }

void name_copy(char* dst, const char* src) {
    std::memset(dst, 0, 8);
    if (src != nullptr) {
        std::strncpy(dst, src, 8);
    }
}

LmMaterial* lm_materials(LmHeader& lm) { return lm.materials; }

void lm_set_translucent(LmHeader& lm, bool translucent) {
    for (LmModel* model = lm.models; model < lm.models + lm.model_count; model++) {
        for (LmMesh* mesh = model->meshes; mesh < model->meshes + model->mesh_count; mesh++) {
            for (LmPrimitive* p = primitives(*mesh); p < primitives(*mesh) + mesh->primitive_count; p++) {
                p->set_translucent(translucent);
            }
        }
    }
}

void material_image_apply(LmMaterial& mat, const TextureImage& image, int blend) {
    // Texels per byte of the page's colour depth: 4-bit, 8-bit, 16-bit.
    int texels = 4;
    if (image.page[0] == 1) {
        texels = 2;
    } else if (image.page[0] == 2) {
        texels = 1;
    }
    set_low_byte(mat.uv_a, static_cast<std::uint8_t>(image.u * texels));
    set_high_byte(mat.uv_a, image.v);
    mat.tpage = static_cast<std::uint8_t>(((image.page[0] & 0x3) << 7) | ((blend & 0x3) << 5) | (image.page[1] & 0x10) |
                                          (image.page[1] & 0xF));
    mat.clut = static_cast<std::uint16_t>((image.palette_y << 6) | ((image.palette_x >> 4) & 0x3F));
}

bool lm_material_image_apply(LmHeader& lm, const char* name, const TextureImage& image, int blend) {
    for (LmMaterial* mat = lm.materials; mat < lm.materials + lm.material_count; mat++) {
        if (same_name(mat->name, name)) {
            mat->is_applied = 1;
            material_image_apply(*mat, image, blend);
            return true;
        }
    }
    return false;
}

void lm_material_changes_apply(LmHeader& lm, bool interior_sync, PaletteRestamp restamp) {
    LmMaterial* mat = lm.materials;
    for (int i = 0; i < lm.material_count; i++, mat++) {
        if (interior_sync && mat->texture == nullptr) {
            for (int j = 0; j < lm.model_count; j++) {
                if (lm.magic == kLmMagic) {
                    model_material_untexture(lm.models[j], i);
                }
            }
            continue;
        }
        int changes = mat->tpage != mat->tpage_extra ? kMaterialPageChanged : 0;
        if (mat->clut != mat->clut_extra) {
            changes |= kMaterialPaletteChanged;
        }
        if (mat->uv_a != mat->uv_b) {
            changes |= kMaterialTexelChanged;
        }
        // A resident material during a re-sync may have got its page back in the same slot: stamp it anyway so
        // primitives left untextured by the steal are textured again.
        if (interior_sync) {
            changes |= kMaterialPageChanged;
        }
        if (changes != 0) {
            for (int j = 0; j < lm.model_count; j++) {
                if (lm.magic == kLmMagic) {
                    model_material_apply(lm.models[j], i, *mat, changes, restamp);
                }
            }
            mat->tpage_extra = mat->tpage;
            mat->clut_extra = mat->clut;
            mat->uv_b = mat->uv_a;
        }
    }
}

void model_material_apply(LmModel& model, int material, const LmMaterial& mat, int changes, PaletteRestamp restamp) {
    for (LmMesh* mesh = model.meshes; mesh < model.meshes + model.mesh_count; mesh++) {
        for (LmPrimitive* p = primitives(*mesh); p < primitives(*mesh) + mesh->primitive_count; p++) {
            if (p->material() == -1) {
                p->set_page(kUntexturedPage);
            }
            if (p->material() != material) {
                continue;
            }
            if (changes & kMaterialPageChanged) {
                p->set_page(mat.tpage);
            }
            if (changes & kMaterialPaletteChanged) {
                p->clut = restamp(static_cast<std::uint16_t>(mat.clut + (p->clut - mat.clut_extra)), mat.clut, p->clut,
                                  mat.clut_extra, mat.name);
            }
            if (changes & kMaterialTexelChanged) {
                const std::uint16_t now = mat.uv_a;
                const std::uint16_t before = mat.uv_b;
                p->uv0 = static_cast<std::uint16_t>(now + (p->uv0 - before));
                p->uv1 = static_cast<std::uint16_t>(now + (p->uv1 - before));
                p->uv2 = static_cast<std::uint16_t>(now + (p->uv2 - before));
                p->uv3 = static_cast<std::uint16_t>(now + (p->uv3 - before));
            }
        }
    }
}

}  // namespace sh::world
