// The movement setup, the ray record and the line-of-sight point checks of the world module
// (engine/world/mover_setup.h, ray_query.h report_ray, line_of_sight.h).

#include "engine/world/line_of_sight.h"
#include "engine/world/mover_setup.h"
#include "engine/world/ray_query.h"

#include "framework/sh_test.h"

namespace {

namespace wd = sh::world;

constexpr std::int32_t kOne = 4096;

struct RecordingRay {
    wd::Vec3 from;
    wd::Vec3 offset;
    int calls = 0;
    wd::SightOutcome answer;
    wd::SightOutcome operator()(const wd::Vec3& f, const wd::Vec3& o, bool) {
        ++calls;
        from = f;
        offset = o;
        return answer;
    }
};

}  // namespace

SH_TEST(world_mover_cylinder) {
    wd::MoverShape shape;
    shape.position = wd::Vec3{10 * kOne, -2 * kOne, 3 * kOne};
    shape.offset_x = 100;
    shape.offset_z = -50;
    shape.top = -6000;
    shape.bottom = 0;
    shape.radius = 1200;
    shape.collision_state = 3;
    const wd::MoveCylinder c = wd::mover_cylinder(shape);
    SH_CHECK(c.position.x == 10 * kOne + 100 && c.position.z == 3 * kOne - 50);
    SH_CHECK(c.position.y == -2 * kOne - 81);  // lifted 0.02
    SH_CHECK(c.top == -6000 && c.bottom == 0 && c.radius == 1200 && c.collision_state == 3);
}

SH_TEST(world_mover_off_map) {
    wd::MoverRules holds;
    SH_CHECK(wd::off_map_answer(holds, 12345).ground_height == 12345);
    wd::MoverRules drops;
    drops.off_map_drop = true;
    const wd::OffMapAnswer a = wd::off_map_answer(drops, 12345);
    SH_CHECK(a.ground_height == 8 * kOne && a.ceiling_height == -16 * kOne);
}

SH_TEST(world_mover_slowdown_and_ceiling) {
    const wd::CollisionStates states{0, 1, 4};
    SH_CHECK(wd::slows_mover(2, 3, states));
    SH_CHECK(!wd::slows_mover(3, 3, states));  // only a lower state gives way
    SH_CHECK(!wd::slows_mover(0, 3, states));  // ignored
    SH_CHECK(!wd::slows_mover(1, 3, states));  // the player's state
    const wd::CeilingBox c = wd::trigger_ceiling(wd::TriggerBox{-3, 2, 4, 1, 3});
    SH_CHECK(c.min_x == -3 * kOne && c.max_x == kOne && c.min_z == 2 * kOne && c.max_z == 3 * kOne);
    SH_CHECK(c.height == -(3 * kOne) / 2 - kOne * 3 / 2);
    SH_CHECK(wd::clear_groups(0xFFFF, 0xFFFF) == 1 && wd::clear_groups(0x0006, 0x0004) == 0x0003);
}

SH_TEST(world_ray_report) {
    wd::RayHit miss;
    miss.length = 300;
    const wd::RayReport m = wd::report_ray(miss, wd::Vec3{1, 2, 3}, wd::Vec3{10, 20, 30}, 77);
    SH_CHECK(!m.hit && m.point.x == 11 && m.point.y == 22 && m.point.z == 33);
    SH_CHECK(m.distance == 300 * 16 && m.ground_height == 7680 && m.angle == 0 && m.target == -1);
    miss.rejected = true;
    SH_CHECK(wd::report_ray(miss, {}, {}, 77).distance == 77 * 16);
    wd::RayHit hit;
    hit.hit = true;
    hit.point = wd::Vec3{5, 6, 7};
    hit.distance = 900;
    hit.ground_height = -400;
    hit.normal_x = 0;
    hit.normal_z = 256;
    hit.ground_type = 3;
    hit.target = 1;
    const wd::RayReport h = wd::report_ray(hit, {}, {}, 0);
    SH_CHECK(h.hit && h.point.y == 6 && h.distance == 900 && h.ground_height == -400 && h.ground_type == 3);
    SH_CHECK(h.angle == 0 && h.target == 1);
    hit.normal_x = 256;
    hit.normal_z = 0;
    SH_CHECK(wd::report_ray(hit, {}, {}, 0).angle == 1024);
}

SH_TEST(world_los_point_checks) {
    const wd::Vec3 from{kOne, -kOne, 2 * kOne};
    RecordingRay ray;
    ray.answer = wd::SightOutcome{true, false};
    SH_CHECK(wd::wall_toward_point(from, wd::Vec3{3 * kOne, 0, 2 * kOne}, ray));
    SH_CHECK(ray.offset.x == 2 * kOne && ray.offset.y == kOne && ray.offset.z == 0);
    // A character in the way is not a wall.
    ray.answer = wd::SightOutcome{true, true};
    SH_CHECK(!wd::wall_toward_body(from, 5 * kOne, 2 * kOne, ray));
    SH_CHECK(ray.offset.x == 4 * kOne && ray.offset.y == -409 && ray.offset.z == 0);
    SH_CHECK(wd::blocked_ahead(from, kOne, 0, ray));
    SH_CHECK(ray.offset.x == 0 && ray.offset.y == 0 && ray.offset.z == kOne);
    ray.answer = wd::SightOutcome{false, false};
    SH_CHECK(!wd::wall_ahead(from, kOne, 1024, ray));
    SH_CHECK(ray.offset.x == kOne && ray.offset.y == 0 && ray.calls == 4);
}
