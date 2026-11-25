#include "engine/camera/view_system.h"

#include <cstddef>

#include "engine/camera/camera_host.h"
#include "engine/camera/precise_view.h"
#include "engine/game_state/session.h"
#include "engine/log/sh_log.h"
#include "engine/math/geometry_state.h"
#include "render/gpu/render_state.h"

namespace sh::camera {

// The character slots' bone records (sh::entities::BoneCoord) and the particle anchors of the
// session are walked as frames, so they share this layout.
static_assert(offsetof(FrameRecord, local) == 4);
static_assert(sizeof(Matrix) == 32);
static_assert(sizeof(PackedAngles) == 8);

bool FrameRecords::canonical(const void* p) {
    // Widened so a 32-bit build compares the same bounds (there the upper one always holds).
    const std::uint64_t a = reinterpret_cast<std::uintptr_t>(p);
    return a >= 0x10000ULL && a < 0x0000800000000000ULL;
}

FrameHost::Frame FrameRecords::parent(Frame f) {
    FrameRecord* up = rec(f)->parent;
    if (up != nullptr && !canonical(up)) {
        static bool logged = false;
        if (!logged) {
            logged = true;
            SH_DBG("[COORD] non-canonical super=%p on coord=%p — treating as root", static_cast<void*>(up), f);
        }
        return nullptr;
    }
    return up;
}

void FrameRecords::unusable_root(Frame f) {
    static bool logged = false;
    if (!logged) {
        logged = true;
        SH_DBG("[COORD] non-canonical rootCoord=%p — identity", f);
    }
}

FrameHost& frame_records() {
    static FrameRecords frames;
    return frames;
}

void init_frame(FrameRecord& frame, FrameRecord* parent) {
    frame = FrameRecord{};
    frame.local.m[0][0] = 4096;
    frame.local.m[1][1] = 4096;
    frame.local.m[2][2] = 4096;
    frame.parent = parent;
    frame.world_current = parent == nullptr ? 1 : 0;
}

Matrix frame_world(FrameRecord* frame) {
    return sh::camera::frame_world(frame_records(), frame);
}

void ViewSystem::init() {
    reference_.eye = Vec3{0, 0, 0};
    reference_.target = Vec3{0, 0, 4096};
    reference_.roll = 0;
    reference_.parent = &frame_;

    // A root frame with an identity matrix, its world already current.
    frame_ = FrameRecord{};
    frame_.local = Matrix{{{4096, 0, 0}, {0, 4096, 0}, {0, 0, 4096}}, {0, 0, 0}};
    frame_.world_current = 1;
    update();
}

void ViewSystem::set_frame(FrameRecord* parent, const Matrix& local) {
    frame_.world_current = 0;
    frame_.parent = parent;
    frame_.local = local;
}

void ViewSystem::set_world_screen(FrameRecord* eye_frame) {
    const WorldScreen ws = world_screen_from_eye(frame_world(eye_frame));
    eye_ = ws.eye;
    world_to_view_ = ws.world_to_view;
    camera_host().world_screen() = ws.world_screen;
    publish_precise_view(ws.eye, ws);
}

void ViewSystem::set_reference_view(const ReferenceView& view) {
    // The reference view is a frame of its own under its parent: the walk links the parent's child
    // link to it, as it always did.
    FrameRecord frame{};
    frame.parent = view.parent;
    frame.local = reference_view_local(view.eye, view.target);
    set_world_screen(&frame);
}

void ViewSystem::update() {
    set_reference_view(reference_);
    const ViewPose pose = pose_of(frame_.world);
    position_ = pose.position;
    angles_.x = pose.angles.x;
    angles_.y = pose.angles.y;
    angles_.z = pose.angles.z;
}

WorldScreen ViewSystem::world_screen() const {
    WorldScreen ws;
    ws.eye = eye_;
    ws.world_to_view = world_to_view_;
    ws.world_screen = camera_host().world_screen();
    return ws;
}

ViewSystem& view_system() {
    static ViewSystem view;
    return view;
}

ScreenProjection projection_now() {
    const sh::math::ScreenSetup& screen = sh::math::geometry().screen();
    ScreenProjection p;
    p.ofx = screen.ofx;
    p.ofy = screen.ofy;
    p.h = screen.h;
    p.whole_map_far = sh::render::whole_town().reproject_far != 0;
    return p;
}

void set_projection_distance(std::int16_t h) {
    sh::math::geometry().set_screen_distance(h);
}

}  // namespace sh::camera
