// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The LM model material and texture-slot entry points the game's model and chunk loaders call
// (engine/world/lm_materials.h, RENDER.md section 6): the bookkeeping is the engine's, the texture reads go through the
// engine's file queue and file table. The names are the ones the game's code links against.

#include <cstdint>
#include <cstring>

#include "engine/app/loader/game_files.h"
#include "engine/config/pc_config.h"
#include "engine/loader/queue/disc_files.h"
#include "engine/log/sh_log.h"
#include "engine/world/lm_materials.h"

namespace sw = sh::world;
using sw::LmHeader;
using sw::LmMaterial;
using sw::TextureImage;
using sw::TextureSlot;

static_assert(sizeof(TextureImage) == sizeof(sh::loader::ImagePlace), "image place layout");

namespace {

constexpr std::uint8_t kLmMagic = sh::formats::records::kLmMagic;

// A header a stale or reused buffer left behind: its walks would follow garbage bounds and pointers.
bool usable(const LmHeader* lm, const char* what) {
    if (lm != nullptr && lm->magic == kLmMagic) {
        return true;
    }
    static int logged = 0;
    if (logged < 8) {
        logged++;
        SH_DBG("[LM-GARBAGE] %s on invalid header %p (magic=0x%02X) -- skipped", what, static_cast<const void*>(lm),
               lm != nullptr ? static_cast<unsigned>(lm->magic) : 0u);
    }
    return false;
}

std::uint16_t restamp(std::uint16_t clut, std::uint16_t base, std::uint16_t old_clut, std::uint16_t old_base,
                      const char* name) {
    // The moved word is taken as it is: a word that names a virtual slot is not checked against a texture,
    // because nothing draws from those slots (the scene draws the models from the imported files).
    (void)base;
    (void)old_clut;
    (void)old_base;
    (void)name;
    return clut;
}

}  // namespace

extern "C" {

void LmHeader_FixOffsets(LmHeader* lm) { sh::formats::records::lm_convert(lm); }

bool Lm_MaterialFsImageApply(LmHeader* lm, char* name, TextureImage* image, std::int32_t blend) {
    return sw::lm_material_image_apply(*lm, name, *image, blend);
}

// The material named like disc file `file` (its name up to the extension) gets `image`.
void Lm_MaterialFileIdxApply(LmHeader* lm, std::int32_t file, TextureImage* image, std::int32_t blend) {
    char name[8] = {};
    const std::string full = sh::loader::disc_files().name(file);
    for (std::size_t i = 0; i < sizeof name && i < full.size() && full[i] != '.'; i++) {
        name[i] = full[i];
    }
    Lm_MaterialFsImageApply(lm, name, image, blend);
}

void Lm_MaterialFsImageApply1(LmHeader* lm, char* name, TextureImage* image, std::int32_t blend) {
    char copy[8];
    sw::name_copy(copy, name);
    Lm_MaterialFsImageApply(lm, copy, image, blend);
}

void Material_FsImageApply(LmMaterial* mat, TextureImage* image, std::int32_t blend) {
    sw::material_image_apply(*mat, *image, blend);
}

void Lm_MaterialFlagsApply(LmHeader* lm) {
    if (!usable(lm, "Lm_MaterialFlagsApply")) {
        return;
    }
    sw::lm_material_changes_apply(*lm, false, restamp);
}

}  // extern "C"
