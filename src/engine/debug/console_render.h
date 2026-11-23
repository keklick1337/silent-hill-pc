// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The console's drawing: the FontAtlas as an alpha-only overlay texture and a DrawList of picture-space quads queued
// on the engine overlay (render/overlay/overlay.h), which blends them over the frame on any RHI backend.

#pragma once

#include "engine/console/console_view.h"
#include "engine/console/font_atlas.h"

#include <cstdint>

namespace sh::console {

class Renderer {
public:
    // Replaces the atlas texture (call when the font is created and whenever it is rebuilt).
    void upload_atlas(const FontAtlas& atlas);
    // Queues `list` (pixels of the overlay picture) on the overlay.
    void draw(const DrawList& list);

private:
    std::uint32_t texture_ = 0;
};

}  // namespace sh::console
