#include "engine/world/mover_setup.h"

#include "engine/world/fixed_math.h"

namespace sh::world {

namespace {

constexpr std::int32_t kOne = 4096;
constexpr std::int32_t kFootLift = 81;  // 0.02 in Q.12, truncated as the game's constant is

}  // namespace

MoveCylinder mover_cylinder(const MoverShape& shape) {
    MoveCylinder c;
    c.position = Vec3{wrap_add(shape.position.x, shape.offset_x), wrap_add(shape.position.y, -kFootLift),
                      wrap_add(shape.position.z, shape.offset_z)};
    c.top = shape.top;
    c.bottom = shape.bottom;
    c.radius = shape.radius;
    c.collision_state = shape.collision_state;
    return c;
}

OffMapAnswer off_map_answer(const MoverRules& rules, std::int32_t mover_height) {
    OffMapAnswer a;
    a.ground_height = rules.off_map_drop ? rules.off_map_ground : mover_height;
    return a;
}

bool slows_mover(int other_state, int mover_state, const CollisionStates& states) {
    return other_state != states.ignore && other_state != states.player && other_state < mover_state;
}

CeilingBox trigger_ceiling(const TriggerBox& box) {
    const auto metres = [](std::int64_t m) { return static_cast<std::int32_t>(static_cast<std::uint32_t>(m) << 12); };
    CeilingBox c;
    c.min_x = metres(box.x);
    c.max_x = metres(static_cast<std::int64_t>(box.x) + box.size_x);
    c.min_z = metres(box.z);
    c.max_z = metres(static_cast<std::int64_t>(box.z) + box.size_z);
    c.height = ((-static_cast<std::int32_t>(box.height_steps * kOne)) >> 1) - (kOne + kOne / 2);
    return c;
}

std::uint16_t clear_groups(std::uint16_t mask, std::int32_t bits) {
    return static_cast<std::uint16_t>((mask & ~bits) | kMapGeometryGroup);
}

}  // namespace sh::world
