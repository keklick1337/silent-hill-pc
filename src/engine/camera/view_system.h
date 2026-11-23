#pragma once

// The view system: the eye frame the camera places
// every frame, the reference view under it, the eye's world matrix, the world-to-view rotation and
// the renderer's world-screen matrix made from them, the frame hierarchy walk the models use and
// the screen tests. The arithmetic is view_space.h; this is the state and the order of the steps.

#include <cstdint>

#include "engine/camera/camera_types.h"
#include "engine/camera/view_space.h"

namespace sh::camera {

// A frame of the world hierarchy in the layout the model records share while they live (bones,
// the view frame): a "world is current" counter, the local and world matrices, the user data and
// the parent and child links.
struct FrameRecord {
    std::uint32_t world_current;
    Matrix local;
    Matrix world;
    void* user;
    FrameRecord* parent;
    FrameRecord* child;
};

// Q12 angles with the spare 16-bit word of the records that carry them.
struct PackedAngles {
    std::int16_t x, y, z, spare;
};

// The eye at `eye` looking at `target` (geometry units), under `parent`.
struct ReferenceView {
    Vec3 eye;
    Vec3 target;
    std::int32_t roll;
    FrameRecord* parent;
};

// The engine's walk over FrameRecords. A link that is not a canonical heap/stack address (a
// truncated or uninitialised pointer) ends the chain instead of crashing.
class FrameRecords final : public FrameHost {
public:
    static FrameRecord* rec(Frame f) { return static_cast<FrameRecord*>(f); }
    static bool canonical(const void* p);

    bool usable(Frame f) const override { return canonical(f); }
    Frame parent(Frame f) override;
    bool world_current(Frame f) const override { return rec(f)->world_current != 0; }
    void mark_world_current(Frame f) override { rec(f)->world_current++; }
    void set_child(Frame f, Frame child) override { rec(f)->child = rec(child); }
    Frame child(Frame f) const override { return rec(f)->child; }
    Matrix local(Frame f) const override { return rec(f)->local; }
    Matrix world(Frame f) const override { return rec(f)->world; }
    void set_world(Frame f, const Matrix& m) override { rec(f)->world = m; }
    void unusable_root(Frame f) override;
};

class ViewSystem {
public:
    // The view frame back to an identity root at the origin, the reference view straight ahead.
    void init();

    FrameRecord* frame() { return &frame_; }
    const Vec3& position() const { return position_; }
    const PackedAngles& angles() const { return angles_; }

    // The view frame's local matrix and parent; its world becomes stale.
    void set_frame(FrameRecord* parent, const Matrix& local);
    // The world-screen matrices from the reference view, then the eye's position and angles.
    void update();
    // The eye's world matrix, the world-to-view rotation and the renderer's world-screen matrix,
    // from the world of `eye_frame`.
    void set_world_screen(FrameRecord* eye_frame);
    void set_reference_view(const ReferenceView& view);

    const Matrix& eye() const { return eye_; }
    const Matrix& world_to_view() const { return world_to_view_; }
    // eye, world_to_view and the renderer's world-screen matrix.
    WorldScreen world_screen() const;

private:
    ReferenceView reference_{};
    FrameRecord frame_{};
    Vec3 position_;
    PackedAngles angles_{};
    Matrix eye_;
    Matrix world_to_view_;
};

ViewSystem& view_system();

// The world matrix of a frame (frames above it recomputed when stale).
Matrix frame_world(FrameRecord* frame);
// A fresh frame under `parent`: identity, no links; a root (null parent) counts as computed, so the hierarchy walk
// stops at it.
void init_frame(FrameRecord& frame, FrameRecord* parent);
// The walk over FrameRecords for code that drives view_space.h itself.
FrameHost& frame_records();

// The projection registers in force and the whole-map far flag.
ScreenProjection projection_now();
// The distance to the screen of the projection.
void set_projection_distance(std::int16_t h);

}  // namespace sh::camera
