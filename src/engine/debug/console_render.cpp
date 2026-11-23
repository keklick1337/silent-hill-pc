// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/debug/console_render.h"

#include "render/overlay/overlay.h"

namespace sh::console {

void Renderer::upload_atlas(const FontAtlas& atlas) {
    auto& store = overlay::overlay().textures();
    const auto w = static_cast<std::int32_t>(atlas.width()), h = static_cast<std::int32_t>(atlas.height());
    if (texture_ && store.find(texture_))
        store.update(texture_, w, h, atlas.pixels().data());
    else
        texture_ = store.create(w, h, overlay::Format::Alpha8, overlay::Filter::Linear, atlas.pixels().data());
}

void Renderer::draw(const DrawList& list) {
    if (list.quads.empty() || !texture_) return;
    overlay::Overlay& o = overlay::overlay();
    for (const Quad& q : list.quads) {
        const overlay::Color c{float(q.rgba >> 24 & 0xFF) / 255.f, float(q.rgba >> 16 & 0xFF) / 255.f,
                               float(q.rgba >> 8 & 0xFF) / 255.f, float(q.rgba & 0xFF) / 255.f};
        o.quad(overlay::Space::Picture, {q.x, q.y, q.w, q.h}, c, texture_, {q.u0, q.v0, q.u1 - q.u0, q.v1 - q.v0});
    }
}

}  // namespace sh::console
