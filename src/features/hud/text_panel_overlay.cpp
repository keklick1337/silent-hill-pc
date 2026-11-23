// SPDX-License-Identifier: GPL-3.0-or-later
//
// The panels of text_panel.h on the engine's overlay, and the font they share.
#include "features/hud/text_panel.h"

#include "engine/log/sh_log.h"
#include "render/overlay/overlay.h"

extern "C" const char* PcPort_GetGameDataPath(void);

namespace sh::hud {

PanelFont& shared_font() {
    static PanelFont font;
    return font;
}

void load_shared_font() {
    static bool tried = false;
    if (tried) {
        return;
    }
    tried = true;
    const std::filesystem::path file =
        std::filesystem::path(PcPort_GetGameDataPath()) / "font" / "BarlowSemiCondensed-Regular.ttf";
    if (!shared_font().load(file)) {
        SH_LOG_WARN(RENDER, "text panels: %s is not a usable font, the panels are not drawn", file.string().c_str());
    }
}

bool TextPanel::set(std::span<const std::string> rows, int pixel_height) {
    std::string text;
    for (const std::string& row : rows) {
        text += row;
        text += '\n';
    }
    if (texture_ != 0 && pixel_height == pixel_height_ && text == text_) {
        return true;
    }
    sh::overlay::TextureStore& textures = sh::overlay::overlay().textures();
    if (texture_ != 0) {
        textures.destroy(texture_);
        texture_ = 0;
    }
    pixel_height_ = pixel_height;
    text_ = std::move(text);
    const PanelBitmap bitmap = shared_font().render(rows, static_cast<float>(pixel_height));
    if (bitmap.empty()) {
        return false;
    }
    width_ = bitmap.width;
    height_ = bitmap.height;
    // Nearest: the text is rendered at its final size, one texel a pixel.
    texture_ = textures.create(bitmap.width, bitmap.height, sh::overlay::Format::Rgba8, sh::overlay::Filter::Nearest,
                               bitmap.rgba.data());
    return texture_ != 0;
}

void TextPanel::draw(Box box, float padding) const {
    if (texture_ == 0) {
        return;
    }
    sh::overlay::Overlay& overlay = sh::overlay::overlay();
    const float w = static_cast<float>(width_);
    const float h = static_cast<float>(height_);
    overlay.quad(sh::overlay::Space::Picture, {box.x, box.y, box_width(padding), box_height(padding)},
                 {0.0f, 0.0f, 0.0f, 0.45f});
    overlay.quad(sh::overlay::Space::Picture, {box.x + padding, box.y + padding, w, h}, {1.0f, 1.0f, 1.0f, 0.95f},
                 texture_);
}

}  // namespace sh::hud
