#pragma once

// The upscaled textures (texture_upscale, docs/RENDER.md section 12): the readers of the imported PNGs
// (the scene's map textures, the glTF characters, the 2D pictures of the TIM files) ask here for the upscaled picture
// of what they read. The pictures are written one by one by tools/sh-upscale (engine/app/texture_upscale_run.h
// starts it); a picture not yet in the cache, a mod's own picture and everything with the option off read the
// original, and improved() tells the readers when a better picture of one they hold has arrived.

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sh::upscale {

// 0 off, 2 or 4.
void set_scale(int scale);
int scale();
void set_gamedata(const std::filesystem::path& gamedata);

// The upscaled picture of `source`, the bytes read from `path` (a path under convert/textures/): the one of the
// current scale, else the largest smaller one already there; nullopt when there is none, and for bytes that are not
// those of gamedata/convert/textures/ (a mod's picture, at any size, is drawn as the mod made it). Every other call
// is noted, so improved() can name the picture when a better one is written.
std::optional<std::vector<std::uint8_t>> find(std::string_view path, std::span<const std::uint8_t> source);

// The keys (upscale_cache.h texture_key) of pictures find() gave out, or found missing, that now have a better file
// on disk; at most `limit`, each once per improvement, the most recently asked first. Cheap when nothing changed:
// the waiting pictures are looked up only after the tool's progress moved.
std::vector<std::string> improved(std::size_t limit);

// Writes the tool's wanted file (upscale_cache.h) with the pictures the game holds without their final scale, the
// most recently asked first, when that list changed.
void publish_wanted();

// The state of the cache of `scale`: complete, or the pictures done of the total of a pass (both 0 before one).
struct Progress {
    bool complete = false;
    int done = 0;
    int total = 0;
};
Progress progress(int scale);

}  // namespace sh::upscale
