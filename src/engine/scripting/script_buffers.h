// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The numbered file buffers the scripts load into (cutscene tracks, scene models, pictures): sh.* binds name a
// buffer by its number. Each number is a work buffer (engine/memory/work_buffers.h).

#pragma once

#include <array>
#include <optional>

#include "engine/memory/work_buffers.h"

namespace sh::scripting {

// File buffer `n` (0..35, no 23); nullptr for any other number.
inline void* file_buffer(int n) {
    using R = sh::memory::WorkBuffer;
    static constexpr std::array<std::optional<R>, 36> kBuffers = {
        R::PlayerBaseAnim, R::FileStaging,    R::PaperMapSheet,  R::TitleImage,    R::PlayerMapAnim,
        R::CloseUpModel,   R::SaveScreenData, R::SaveLoadImage,  R::InventoryModels, R::MapTexture,
        R::HeldItemImage,  R::Cutscene11,     R::WeaponAnim,     R::Cutscene13,    R::Cutscene14,
        R::Cutscene15,     R::Cutscene16,     R::Cutscene17,     R::Cutscene18,    R::Cutscene19,
        R::Cutscene20,     R::EventModel,     R::MapWork22,      std::nullopt,     R::Cutscene24,
        R::MapWork25,      R::MapWork26,      R::MapWork27,      R::MapWork28,     R::CharaAnim29,
        R::CharaAnim30,    R::CharaModel31,   R::CharaModel32,   R::CharaAnim33,   R::CharaModel34,
        R::MapWork35,
    };
    if (n < 0 || n >= static_cast<int>(kBuffers.size()) || !kBuffers[n]) {
        return nullptr;
    }
    return sh::memory::work_buffers().data(*kBuffers[n]);
}

// The character model buffer 2 (the scenes' second model).
inline void* model_buffer(int n) {
    return n == 2 ? sh::memory::work_buffers().data(sh::memory::WorkBuffer::SceneModel) : nullptr;
}

// Image staging buffers 4 and 5 (loading screen, event pictures).
inline void* image_buffer(int n) {
    using R = sh::memory::WorkBuffer;
    return n == 4 ? sh::memory::work_buffers().data(R::LoadingImage)
           : n == 5 ? sh::memory::work_buffers().data(R::EventImage)
                    : nullptr;
}

}  // namespace sh::scripting
