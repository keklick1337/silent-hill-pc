// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The replacement pictures of the running game (replacement_store.h): the store over the renderer's textures, and
// the reading of a mod's picture file into it.

#pragma once

#include <optional>
#include <span>
#include <string>

#include "formats/texture/bc7_dds.h"
#include "render/textures/replacement_store.h"

namespace sh::textures {

// The store of the running game. Without a renderer no texture can be made: every put fails and nothing is found.
ReplacementStore& replacements();

// The numbers of the sheets the engine keeps.
inline constexpr int kBulletHoleSheet = 255;

// A picture file of a mod (PNG, BC7 DDS or TIM) as RGBA; a file that cannot be read is named in the log with the
// reason, once per call.
std::optional<sh::formats::texture::RgbaImage> read_picture(const std::string& name,
                                                            std::span<const std::uint8_t> file);

}  // namespace sh::textures
