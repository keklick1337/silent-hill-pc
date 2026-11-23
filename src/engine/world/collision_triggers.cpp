#include "engine/world/collision_triggers.h"

#include <cstdlib>
#include <limits>

#include "engine/world/fixed_trig.h"

namespace sh::world {

namespace {

constexpr std::int32_t kOne = 4096;
constexpr std::int32_t kNearbyMargin = 16 * kOne;

// The sweep starts with no trigger height: the original's constant (1048560.0 in Q.12) is out of
// the 32-bit range and converts to this value.
constexpr std::int32_t kNoHeight = std::numeric_limits<std::int32_t>::min();

constexpr std::int32_t metres(std::int32_t m) { return static_cast<std::int32_t>(static_cast<std::uint32_t>(m) << 12); }

// A trigger's height: half-metre steps below a 1.5 m base.
std::int32_t trigger_height(std::uint32_t steps) {
    return (-static_cast<std::int32_t>(steps * kOne) >> 1) - (3 * kOne / 2);
}

// Q.12 to the 16-bit Q.8 the crossing test takes (the shift wraps as on the console).
std::int16_t narrow(std::int32_t v) {
    return static_cast<std::int16_t>(static_cast<std::int32_t>(static_cast<std::uint32_t>(v) << 12) >> 16);
}

// Updates the sweep with one box; true when the move starts inside it.
bool sweep_box(TriggerSweep& s, const TriggerBox& box) {
    const std::int32_t min_x = metres(box.x);
    const std::int32_t max_x = metres(box.x + static_cast<std::int32_t>(box.size_x));
    const std::int32_t min_z = metres(box.z);
    const std::int32_t max_z = metres(box.z + static_cast<std::int32_t>(box.size_z));

    // Skipped only when the move's bounds miss the box on both axes.
    if ((min_x >= s.max_x || s.min_x >= max_x) && (min_z >= s.max_z || s.min_z >= max_z)) {
        return false;
    }

    if (s.from_x >= min_x && max_x >= s.from_x && s.from_z >= min_z && max_z >= s.from_z) {
        s.fraction = 0;
        s.height = trigger_height(box.height_steps);
        return true;
    }

    // The near corner in the direction of the move.
    const std::int32_t corner_x = s.move_x >= 0 ? min_x : max_x;
    const std::int32_t corner_z = s.move_z >= 0 ? min_z : max_z;
    const std::int32_t across_z = slab_crossing(narrow(s.move_x), narrow(s.move_z), narrow(corner_z - s.from_z),
                                                narrow(min_x - s.from_x), narrow(max_x - s.from_x));
    const std::int32_t across_x = slab_crossing(narrow(s.move_z), narrow(s.move_x), narrow(corner_x - s.from_x),
                                                narrow(min_z - s.from_z), narrow(max_z - s.from_z));
    const std::int32_t first = across_x >= across_z ? across_z : across_x;
    if (first < s.fraction) {
        s.fraction = first;
        s.height = trigger_height(box.height_steps);
    }
    return s.fraction == 0;
}

}  // namespace

std::int32_t slab_crossing(std::int16_t other_move, std::int16_t move, std::int16_t to_slab, std::int32_t low,
                           std::int32_t high) {
    // Bounds arrive as 16-bit values.
    low = static_cast<std::int16_t>(low);
    high = static_cast<std::int16_t>(high);
    if (move > 0) {
        if (to_slab < 0) {
            return kOne;
        }
    } else if (move == 0 || to_slab > 0) {
        return kOne;
    }
    if (to_slab == 0) {
        return low <= 0 && high >= 0 ? 0 : kOne;
    }
    if (std::abs(static_cast<int>(to_slab)) > std::abs(static_cast<int>(move))) {
        return kOne;
    }
    const std::int16_t fraction = static_cast<std::int16_t>(((static_cast<std::int32_t>(to_slab) << 16) >> 4) / move);
    const std::int32_t along = (other_move * fraction) >> 12;
    if (along < low || high < along) {
        return kOne;
    }
    return fraction;
}

bool is_list_end(PackedTrigger t) {
    return (t.bits & 1u) != 0;
}

TriggerBox unpack_trigger(PackedTrigger t) {
    TriggerBox b;
    b.x = static_cast<std::int32_t>(t.bits << 21) >> 22;
    b.z = static_cast<std::int32_t>(t.bits << 11) >> 22;
    b.size_x = (t.bits >> 21) & 0xFu;
    b.size_z = (t.bits >> 25) & 0xFu;
    b.height_steps = t.bits >> 29;
    return b;
}

ActiveTriggers& active_triggers() {
    static ActiveTriggers active;
    return active;
}

void find_nearby_triggers(ActiveTriggers& active, std::int32_t x, std::int32_t z, const PackedTrigger* list) {
    std::vector<TriggerBox> boxes;
    for (const PackedTrigger* t = list; !is_list_end(*t); ++t) {
        boxes.push_back(unpack_trigger(*t));
    }
    active.count = 0;
    for (std::size_t i : nearby_triggers(x, z, boxes)) {
        if (active.count < ActiveTriggers::kCapacity) {
            active.near[active.count++] = &list[i];
        }
    }
}

TriggerSweep sweep_active(const ActiveTriggers& active, std::int32_t from_x, std::int32_t from_z,
                          std::int32_t move_x, std::int32_t move_z) {
    std::vector<TriggerBox> boxes;
    for (int i = 0; i < active.count; ++i) {
        boxes.push_back(unpack_trigger(*active.near[i]));
    }
    return sweep_triggers(from_x, from_z, move_x, move_z, boxes);
}

std::vector<std::size_t> nearby_triggers(std::int32_t x, std::int32_t z, std::span<const TriggerBox> boxes) {
    std::vector<std::size_t> found;
    for (std::size_t i = 0; i < boxes.size(); ++i) {
        const TriggerBox& b = boxes[i];
        const std::int32_t min_x = metres(b.x) - kNearbyMargin;
        const std::int32_t max_x = metres(b.x + static_cast<std::int32_t>(b.size_x)) + kNearbyMargin;
        const std::int32_t min_z = metres(b.z) - kNearbyMargin;
        const std::int32_t max_z = metres(b.z + static_cast<std::int32_t>(b.size_z)) + kNearbyMargin;
        if (x >= min_x && max_x >= x && z >= min_z && max_z >= z) {
            found.push_back(i);
        }
    }
    return found;
}

TriggerSweep sweep_triggers(std::int32_t from_x, std::int32_t from_z, std::int32_t move_x, std::int32_t move_z,
                            std::span<const TriggerBox> boxes) {
    TriggerSweep s;
    s.from_x = from_x;
    s.from_z = from_z;
    s.move_x = move_x;
    s.move_z = move_z;
    s.to_x = from_x + move_x;
    s.to_z = from_z + move_z;
    s.fraction = kOne;
    s.height = kNoHeight;
    s.min_x = s.to_x >= s.from_x ? s.from_x : s.to_x;
    s.max_x = s.to_x >= s.from_x ? s.to_x : s.from_x;
    s.min_z = s.to_z >= s.from_z ? s.from_z : s.to_z;
    s.max_z = s.to_z >= s.from_z ? s.to_z : s.from_z;
    for (const TriggerBox& box : boxes) {
        if (sweep_box(s, box)) {
            break;
        }
    }
    return s;
}

SlopeForce slope_force(const TriggerSweep& s) {
    if (s.fraction == kOne) {
        return SlopeForce{32 * kOne, -16 * kOne};
    }
    const std::int64_t length = fixed_length3(s.move_x, 0, s.move_z);
    return SlopeForce{static_cast<std::int32_t>((length * s.fraction) >> 12), s.height};
}

}  // namespace sh::world
