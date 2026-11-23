// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/desktop/window_icon.h"

#include "engine/desktop/desktop_entry.h"
#include "formats/png/sh_png.h"

#include <SDL3/SDL.h>

#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace sh::desktop {

namespace {

SDL_Surface* load_icon(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        return nullptr;
    }
    const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const auto picture = sh::png::decode_rgba8(bytes);
    if (!picture || picture.value().width == 0 || picture.value().height == 0) {
        return nullptr;
    }
    const sh::png::Rgba8& p = picture.value();
    SDL_Surface* surface =
        SDL_CreateSurface(static_cast<int>(p.width), static_cast<int>(p.height), SDL_PIXELFORMAT_RGBA32);
    if (!surface) {
        return nullptr;
    }
    for (std::uint32_t y = 0; y < p.height; ++y) {
        SDL_memcpy(static_cast<std::uint8_t*>(surface->pixels) + static_cast<std::size_t>(y) * surface->pitch,
                   p.pixels.data() + static_cast<std::size_t>(y) * p.width * 4, static_cast<std::size_t>(p.width) * 4);
    }
    return surface;
}

}  // namespace

void set_app_identity(const char* version) {
    SDL_SetAppMetadata(kAppName, version, kAppId);
    SDL_SetAppMetadataProperty(SDL_PROP_APP_METADATA_TYPE_STRING, "game");
}

bool set_window_icon(SDL_Window* window, const std::filesystem::path& icon_dir) {
    if (!window) {
        return false;
    }
    // The main picture is a large one; the others go with it as alternates for the platforms that take several
    // sizes and pick the nearest for each place the icon is shown.
    SDL_Surface* icon = load_icon(icon_dir / "app_128.png");
    if (!icon) {
        return false;
    }
    std::vector<SDL_Surface*> alternates;
    for (const int size : {16, 32, 48, 64, 256}) {
        if (SDL_Surface* other = load_icon(icon_dir / ("app_" + std::to_string(size) + ".png"))) {
            SDL_AddSurfaceAlternateImage(icon, other);
            alternates.push_back(other);
        }
    }
    // A video driver without window icons (offscreen) refuses; there is nothing to show the icon on then.
    SDL_SetWindowIcon(window, icon);
    for (SDL_Surface* other : alternates) {
        SDL_DestroySurface(other);
    }
    SDL_DestroySurface(icon);
    return true;
}

}  // namespace sh::desktop
