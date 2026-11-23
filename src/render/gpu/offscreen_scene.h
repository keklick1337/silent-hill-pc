// Scene pictures drawn off screen (the editor's model viewer): a scene frame the caller builds
// (posed meshes and library models in its own view space) is drawn by a scene renderer of its own into an RGBA
// target of the asked size, after the frame's present (a frame-end listener of the frame driver), and read back for the caller to show (an overlay texture).
// One job at a time: a new request replaces the one not yet drawn. Nothing runs while nobody asks.
#pragma once

#include "render/scene/scene.h"

#include <array>
#include <cstdint>
#include <vector>

namespace sh::gpu {

struct OffscreenScene {
    scene::Frame frame;            // camera, light and the meshes; frame.h and the centre are in target pixels
    int width = 0, height = 0;     // the target, pixels (clamped to 16..2048)
    std::array<float, 4> clear{0.12f, 0.12f, 0.14f, 1.0f};
    bool wireframe = false;
    float depth_far = 65535.0f;    // view depth at the far end of the depth buffer
};

void request_offscreen_scene(OffscreenScene job);
// The last picture drawn: true once, when a new one came back since the last call (rows top first).
bool take_offscreen_picture(std::vector<std::uint8_t>* rgba, int* width, int* height);

}  // namespace sh::gpu
