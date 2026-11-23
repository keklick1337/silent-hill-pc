// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "input/glyphs/glyph_sheet.h"

#include "engine/app/pc_paths.h"
#include "engine/config/pc_config.h"
#include "engine/log/sh_log.h"
#include "render/textures/replacements.h"

#include <SDL3/SDL.h>

#include <cstdlib>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

namespace sh::input {
namespace {

static_assert(SDL_GAMEPAD_TYPE_STANDARD == 1 && SDL_GAMEPAD_TYPE_XBOX360 == 2 && SDL_GAMEPAD_TYPE_XBOXONE == 3 &&
              SDL_GAMEPAD_TYPE_PS3 == 4 && SDL_GAMEPAD_TYPE_PS4 == 5 && SDL_GAMEPAD_TYPE_PS5 == 6 &&
              SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO == 7 && SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR == 10,
              "glyph_table.cpp numbers controller types as SDL does");

// The picture, and the part of video memory it stands over: a 4-bit page is four texels a word, and the picture
// is twice the page's size.
constexpr int kPictureWidth = 256;
constexpr int kPictureHeight = 512;
constexpr int kPageY = 0;
constexpr int kPageWords = 32;
constexpr int kPageRows = 256;
constexpr int kPageBits = 4;

std::vector<unsigned char> file_bytes(const char* path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<unsigned char>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// Why the picture cannot be used, or nullptr once it is registered.
const char* register_sheet() {
    char path[1024];
    const std::vector<unsigned char> file = file_bytes(PcPaths_Asset(path, sizeof(path), "icons/input_glyphs.png"));
    if (file.empty()) {
        return "atlas assets/icons/input_glyphs.png missing";
    }
    const std::optional<sh::formats::texture::RgbaImage> picture =
        sh::textures::read_picture("icons/input_glyphs.png", {file.data(), file.size()});
    if (!picture) {
        return "atlas does not decode";
    }
    if (picture->width != kPictureWidth || picture->height != kPictureHeight) {
        return "atlas is not 256x512";
    }
    // Pixel art: point sampling, and pinned so that uploads to this part of video memory leave it alone.
    sh::textures::ReplacementStore& store = sh::textures::replacements();
    const sh::textures::PictureKey place{kGlyphSheetPageX, kPageY,          kPageWords, kPageRows,
                                         kGlyphSheetKeyX,  kGlyphSheetKeyY, kPageBits};
    if (!store.put(place, picture->pixels, kPictureWidth, kPictureHeight, true) || !store.pin(place, true)) {
        return "atlas registration failed";
    }
    return nullptr;
}

PadIdentity first_controller() {
    PadIdentity pad;
    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    if (ids != nullptr && count > 0) {
        pad.type = static_cast<int>(SDL_GetGamepadTypeForID(ids[0]));
        pad.vendor = SDL_GetJoystickVendorForID(ids[0]);
        pad.product = SDL_GetJoystickProductForID(ids[0]);
    }
    SDL_free(ids);
    const char* deck = std::getenv("SteamDeck");
    pad.on_steam_deck = deck != nullptr && std::string_view(deck) == "1";
    return pad;
}

}  // namespace

void GlyphSheet::load() {
    if (tried_) {
        return;
    }
    tried_ = true;
    const char* why = register_sheet();
    ready_ = why == nullptr;
    if (ready_) {
        SH_DBG("[GLYPHS] atlas registered");
    } else {
        SH_DBG("[GLYPHS] %s, drawing text", why);
    }
}

GlyphSet GlyphSheet::chosen_set() const { return glyph_set_from(g_PcConfig.gamepadIcons); }

GlyphSet GlyphSheet::active_set() {
    const GlyphSet chosen = chosen_set();
    if (chosen != GlyphSet::Auto) {
        return chosen;
    }
    const GlyphSet set = glyph_set_for(first_controller());
    if (static_cast<int>(set) != logged_auto_) {
        logged_auto_ = static_cast<int>(set);
        SH_DBG("[GLYPHS] auto: %s", glyph_set_label(set));
    }
    return set;
}

void GlyphSheet::step_chosen_set(int direction) {
    const int step = direction < 0 ? kGlyphSets - 1 : 1;
    g_PcConfig.gamepadIcons = ((g_PcConfig.gamepadIcons % kGlyphSets + kGlyphSets) + step) % kGlyphSets;
    PcConfig_SaveKeyValue("gamepad_icons", std::to_string(g_PcConfig.gamepadIcons).c_str());
}

GlyphSheet& glyph_sheet() {
    static GlyphSheet sheet;
    return sheet;
}

}  // namespace sh::input
