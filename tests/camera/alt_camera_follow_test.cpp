// How the PC alternate cameras (engine/camera/alt_camera.h) follow Harry and keep off walls, with the shipped
// tuning: what the rays around the third person eye graze brings the eye in over a moment while what stands
// between Harry and the eye brings it in at once, and the first person picture rides the head's sway eased.

#include "engine/camera/alt_camera.h"

#include "framework/sh_test.h"

#include <cmath>
#include <fstream>
#include <iterator>
#include <string>

namespace {

namespace cam = sh::camera;

cam::AltCameraTuning shipped() {
    std::ifstream in(std::string(SH_PROJECT_ROOT) + "/gamedata/config/alt_cameras.json", std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    auto parsed = cam::parse_alt_camera_tuning(text);
    if (!parsed) {
        SH_SKIP(parsed.error().c_str());
    }
    return parsed.value();
}

// A wall across the rays: `all` stops every ray after `hit` units, otherwise only the rays that do not end at the
// eye itself (the eye is the end of the first ray of each update's cone).
class Edge : public cam::AltCameraHost {
public:
    bool on = false;
    bool all = false;
    std::int32_t hit = 4096;
    int ray = 0;
    bool ray_hit(const cam::AltVec3&, const cam::AltVec3&, std::int32_t* distance) override {
        const bool centre = ray++ == 0;
        if (!on || (centre && !all)) return false;
        *distance = hit;
        return true;
    }
};

constexpr std::int32_t kFrame60 = 4096 / 60;

double arm_length(const cam::AltCameraOutput& out, const cam::AltCameraInput& in) {
    const double dx = out.eye.x - in.harry.x, dz = out.eye.z - in.harry.z;
    return std::sqrt(dx * dx + dz * dz);
}

}  // namespace

// The eye stands 2.5 m behind Harry. An edge that only the rays around the eye catch, 1 m from his head, does not
// throw the eye in on that frame (it goes less than half of the way to 0.75 m, where the edge's margin puts it),
// and has it within a metre in half a second. A wall that the ray to the eye itself hits has the eye in front of it on the same frame.
SH_TEST(camera_alt_grazed_edge_eases_in_and_a_wall_between_snaps) {
    cam::AltCamera c(shipped());
    Edge level;
    cam::AltCameraInput in;
    in.delta_time = kFrame60;
    in.options.follow_smoothing = 0.0f;
    in.options.recenter = false;
    in.options.rest_x = 0;
    level.ray = 0;
    cam::AltCameraOutput out = c.update(in, level);
    const double open = arm_length(out, in);
    SH_CHECK(open > 10000.0);

    level.on = true;
    level.ray = 0;
    out = c.update(in, level);
    const double first = arm_length(out, in);
    SH_CHECK(first < open && open - first < (open - 3072.0) * 0.5);
    for (int i = 0; i < 30; ++i) {
        level.ray = 0;
        out = c.update(in, level);
    }
    SH_CHECK(arm_length(out, in) < 4096.0);

    cam::AltCamera d(shipped());
    Edge wall;
    wall.ray = 0;
    d.update(in, wall);
    wall.on = wall.all = true;
    wall.ray = 0;
    out = d.update(in, wall);
    SH_CHECK(arm_length(out, in) < 4096.0);
}

// First person: the head bone moves in steps of 1/256 m. When it steps 3 steps (12 mm) to the side at once, the
// game's own eye takes the step whole on that update, as it always did; the picture's eye takes less than half of
// it on that update and has all of it within half a second.
SH_TEST(camera_alt_first_person_picture_eases_the_head_sway) {
    cam::AltCamera c(shipped());
    Edge level;
    cam::AltCameraInput in;
    in.style = cam::AltStyle::FirstPerson;
    in.delta_time = kFrame60;
    in.in_gameplay = true;
    in.options.head_tracking = false;
    in.options.eye_offset = cam::AltVec3{0, -6836, 919};
    in.head.ty = -6836 >> 4;
    cam::AltCameraOutput out = c.update(in, level);
    const std::int32_t rest = out.eye.x;
    SH_CHECK(std::fabs(out.basis.eye[0] - rest) < 1e-9);

    in.head.tx = 3;
    out = c.update(in, level);
    const std::int32_t stepped = out.eye.x - rest;
    SH_CHECK(stepped >= 46 && stepped <= 48);  // 3/256 m in Q12, less the reference's slow follow
    const double shown = out.basis.eye[0] - rest;
    SH_CHECK(shown > 0.0 && shown < stepped * 0.5);
    for (int i = 0; i < 30; ++i) out = c.update(in, level);
    SH_CHECK(std::fabs(out.basis.eye[0] - out.eye.x) < 1.5);
}
