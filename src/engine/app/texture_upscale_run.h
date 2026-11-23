#pragma once

// Running the texture upscaler (tools/sh-upscale, docs/RENDER.md section 12) beside the game: at start
// with texture_upscale on, and whenever Options changes the scale. The tool works in the background at a lower
// priority, never per frame, and skips what its cache already holds; its output goes to
// <gamedata>/cache/upscale/upscale.log. Each picture is drawn as soon as the tool has written it: the loaded
// textures whose better picture arrived are read and uploaded again a few at a time (scene::refresh_textures, the
// characters, the 2D pictures), and the tool is told which pictures the game is waiting for, so the current map and
// characters come first. A change of scale reads everything loaded again (scene::forget_assets).

namespace sh::app {

// Applies texture_upscale (0 off, 2, 4): the texture readers follow it, a pass for another scale stops, and
// a pass for this one starts a second later (poll_texture_upscale) when its cache is not complete. False when the tool
// is not beside the game: the original textures stay.
bool apply_texture_upscale(int scale);

// Once a frame: swaps in the pictures that arrived, tells the tool what is waited for, notices a finished pass.
void poll_texture_upscale();

// Upscaled pictures swapped in since the start: each swap reads and uploads a texture again.
unsigned long long texture_upscale_swaps();

// False when the tool is not beside the game (a build without ncnn): Options says so.
bool texture_upscale_available();

// The row text of Options: empty when the scale is ready or off, else "<done>/<total>" of the running pass,
// "x4 <done>/<total>" while a pass for x8 or x16 makes its x4 set, or "..." before it has counted.
const char* texture_upscale_status();

}  // namespace sh::app
