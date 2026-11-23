// SPDX-License-Identifier: GPL-3.0-or-later
//
// The scene of a frame presented between two game updates. The game describes a
// scene once an update; above the update rate the frames in between show the scene blended from the last two
// updates: the camera, the posed meshes and the world objects move along the straight line between them. Nothing
// here feeds back into the game.
#pragma once

#include "render/scene/scene.h"

namespace sh::scene {

// Beyond these two updates are a cut, not a motion, and the newer one is shown as it is.
struct BlendLimits {
    double camera_move = 512.0;     // camera position step (world units, 256 per metre)
    double camera_turn_cos = 0.9;   // cosine of the camera's turn
    double mesh_move = 192.0;       // step of a posed mesh's centre or of a world object
    double object_turn_cos = 0.0;   // cosine of a world object's turn (a swung weapon turns fast)
};

// `out` becomes the scene `t` (0..1) of the way from `from` to `to`. It has the lists of `to` (what the newer update
// drew), so the frame's scene items keep their numbers; a mesh or an object with no counterpart in `from` (another
// vertex count, texture or model, or farther than the limit) stands where `to` has it.
void blend_frames(const Frame& from, const Frame& to, double t, Frame& out, const BlendLimits& limits = {});

// The flashlight of an update, in the view space of its frame's camera (what the per-pixel light and its shadow map
// are drawn with).
struct Beam {
    bool on = false;
    std::array<float, 3> position{};
    std::array<float, 3> shadow_position{};
    std::array<float, 3> direction{};
};

// The beam `t` of the way from `from` to `to`, in the view space of `out_frame` (the blend of the two frames): it
// moves through the world with its holder instead of staying glued to the picture between two updates.
Beam blend_beam(const Frame& from_frame, const Beam& from, const Frame& to_frame, const Beam& to,
                const Frame& out_frame, double t, const BlendLimits& limits = {});

// The scenes of the last two updates.
class TickFrames {
public:
    // An update's scene is about to be presented.
    void record(const Frame& frame, const Beam& beam);
    // The next recorded scene has no predecessor (the frames in between were not presented one after another).
    void reset();
    bool has_pair() const { return count_ >= 2; }
    bool has_last() const { return count_ >= 1; }
    // The scene `t` of the way from the update before the last to the last one; the last one alone without a pair.
    void show(double t, Frame& out, Beam& beam) const;

private:
    Frame previous_;
    Frame last_;
    Beam previous_beam_;
    Beam last_beam_;
    int count_ = 0;
};

TickFrames& tick_frames();

}  // namespace sh::scene
