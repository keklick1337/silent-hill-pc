#pragma once

// The one seam between the FMV player and the platform renderer: show a decoded frame as the next presented
// frame. The player knows nothing of the RHI device; the host (fmv_present_gpu.cpp, on the engine
// renderer) draws it with FmvRenderer and presents it.

#include <cstdint>

namespace sh::fmv {

// `pixels`: width x height, tightly packed RGB24 (rgba = false) or RGBA32, top row first. Always presents, so the
// player's pacing and the window stay as they are even when the picture cannot be drawn.
void present_frame(const std::uint8_t* pixels, int width, int height, bool rgba);

// Releases the host's device objects.
void release_presenter();

}  // namespace sh::fmv
