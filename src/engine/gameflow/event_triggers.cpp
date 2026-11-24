#include "engine/gameflow/event_triggers.h"

#include "engine/world/fixed_trig.h"

namespace sh::gameflow {

namespace {

// 32-bit two's complement arithmetic: the segment test multiplies distances that can exceed the
// signed range, and the result has to be the wrapped one the game computed.
constexpr std::int32_t wrap_mul(std::int32_t a, std::int32_t b) {
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) * static_cast<std::uint32_t>(b));
}
constexpr std::int32_t wrap_add(std::int32_t a, std::int32_t b) {
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) + static_cast<std::uint32_t>(b));
}
constexpr std::int32_t wrap_sub(std::int32_t a, std::int32_t b) {
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) - static_cast<std::uint32_t>(b));
}
constexpr std::int32_t abs32(std::int32_t v) { return v < 0 ? wrap_sub(0, v) : v; }

// Q8 angle (256 per turn) to Q12 (4096 per turn).
constexpr std::int32_t q8_angle(std::uint8_t a) { return static_cast<std::int32_t>(a) << 4; }

constexpr std::int32_t kQuarterMetre = 1024;     // 0.25 m in Q12
constexpr std::int32_t kHalfTurn = 2048;         // 180 degrees in Q12
constexpr std::int32_t kFullTurn = 4096;
constexpr std::int32_t kFacingCone = 341;        // 30 degrees in Q12, truncated
constexpr std::uint32_t kOrientedBoxDepth = 4 * 4096;  // 4 m in Q12

bool overlaps(std::int32_t lo_ref, std::int32_t hi_ref, std::int32_t a, std::int32_t b) {
    // The reference interval [min(v, 0), max(v, 0)] against the segment [a, b] in either order.
    const std::int32_t seg_min = b >= a ? a : b;
    const std::int32_t seg_max = a >= b ? a : b;
    return !(hi_ref < seg_min) && lo_ref <= seg_max;
}

}  // namespace

void EventTriggers::clear_slots() {
    for (ItemSlot& slot : slots_) {
        slot.item = -1;
        slot.event = -1;
        slot.trigger = EventTrigger{};
    }
}

bool EventTriggers::facing_point(const TriggerFrame& frame, const TriggerPoint& p) {
    if (frame.tick > probe_tick_) {
        probe_x_ = frame.player_x - (sh::world::fixed_sin(frame.player_heading) >> 3);
        probe_z_ = frame.player_z - (sh::world::fixed_cos(frame.player_heading) >> 3);
        probe_tick_ = frame.tick;
    }

    const std::int32_t reach = frame.facing_reach;
    const std::int32_t dx = p.x - probe_x_;
    if (abs32(dx) > reach) {
        return false;
    }
    const std::int32_t dz = p.z - probe_z_;
    if (abs32(dz) > reach) {
        return false;
    }
    if (wrap_add(dx * dx, dz * dz) > reach * reach) {
        return false;
    }

    std::int32_t turn = frame.player_heading - sh::world::fixed_atan2(dx, dz);
    if (turn >= kHalfTurn) {
        turn -= kFullTurn;
    }
    return abs32(turn) <= kFacingCone;
}

bool EventTriggers::facing_segment(const TriggerFrame& frame, const TriggerPoint& p) {
    // The facing ray is the half-length unit vector (sin/2, cos/2) from the player; the segment is
    // centred on the point, perpendicular to the param0 direction, param1/16 m long per side.
    const std::int32_t ray_x = sh::world::fixed_sin(frame.player_heading) >> 1;
    const std::int32_t half_x = (-sh::world::fixed_cos(q8_angle(p.param0)) * p.param1) >> 4;
    const std::int32_t rel_x = p.x - frame.player_x;
    const std::int32_t ax = rel_x - half_x;
    const std::int32_t bx = rel_x + half_x;

    if (!overlaps(ray_x < 0 ? ray_x : 0, ray_x > 0 ? ray_x : 0, ax, bx)) {
        return false;
    }

    const std::int32_t ray_z = sh::world::fixed_cos(frame.player_heading) >> 1;
    const std::int32_t half_z = (sh::world::fixed_sin(q8_angle(p.param0)) * p.param1) >> 4;
    const std::int32_t rel_z = p.z - frame.player_z;
    const std::int32_t az = rel_z - half_z;
    const std::int32_t bz = rel_z + half_z;

    if (!overlaps(ray_z < 0 ? ray_z : 0, ray_z > 0 ? ray_z : 0, az, bz)) {
        return false;
    }

    // Segment endpoints on opposite sides of the ray, and the ray's origin and tip on opposite
    // sides of the segment (cross products, wrapped like the game's 32-bit products).
    if (wrap_sub(wrap_mul(ax, ray_z), wrap_mul(ray_x, az)) > 0) {
        return false;
    }
    if (wrap_sub(wrap_mul(bx, ray_z), wrap_mul(ray_x, bz)) < 0) {
        return false;
    }
    const std::int32_t seg_x = wrap_sub(bx, ax);
    const std::int32_t seg_z = wrap_sub(bz, az);
    if (wrap_add(wrap_mul(wrap_sub(0, ax), seg_z), wrap_mul(seg_x, az)) < 0) {
        return false;
    }
    return wrap_sub(wrap_mul(wrap_sub(ray_x, ax), seg_z), wrap_mul(seg_x, wrap_sub(ray_z, az))) < 1;
}

bool EventTriggers::oriented_box(const TriggerFrame& frame, const TriggerPoint& p) {
    const std::int32_t half_width = static_cast<std::int32_t>(p.param1) << 8;

    const std::int32_t dx = frame.player_x - p.x;
    if ((static_cast<std::int32_t>(p.param1) << 9) < abs32(dx)) {
        return false;
    }
    const std::int32_t dz = frame.player_z - p.z;
    if (half_width * 2 < abs32(dz)) {
        return false;
    }

    const std::int32_t angle = -q8_angle(p.param0);
    // Depth along the box axis: in front of the point by up to 4 m (negative depths wrap to large
    // unsigned values and fail).
    const std::uint32_t depth = static_cast<std::uint32_t>(
        wrap_add(wrap_mul(wrap_sub(0, dx), sh::world::fixed_sin(angle)), wrap_mul(dz, sh::world::fixed_cos(angle))) >> 12);
    if (depth > kOrientedBoxDepth) {
        return false;
    }
    const std::int32_t side =
        wrap_add(wrap_mul(dx, sh::world::fixed_cos(angle)), wrap_mul(dz, sh::world::fixed_sin(angle))) >> 12;
    return abs32(side) <= half_width;
}

TriggerResult EventTriggers::update(const TriggerFrame& frame, std::span<const EventTrigger> events, bool has_events,
                                    EventHost& host) {
    TriggerResult out;

    // The item used on a slot last frame starts that slot's event.
    if (frame.used_item != 0) {
        out.consume_used_item = true;
        int slot = 0;
        while (slot < kItemSlots && frame.used_item != slots_[slot].item) {
            ++slot;
        }
        if (slot < kItemSlots) {
            const int index = slots_[slot].event;
            const EventTrigger& ev = slots_[slot].trigger;
            out.has_event = true;
            out.event = index;
            out.item_slot = slot;
            out.has_event_item = true;
            out.event_item = frame.used_item;
            out.has_state = true;
            out.state = ev.state;
            out.has_param = true;
            out.param = ev.param;
        }
        clear_slots();
        return out;
    }

    clear_slots();
    out.has_event_item = true;
    out.event_item = 0;

    if (!has_events) {
        return out;
    }

    const std::size_t count = events.size() < static_cast<std::size_t>(kMaxEvents) ? events.size() : kMaxEvents;
    for (std::size_t i = 0; i < count; ++i) {
        const EventTrigger& ev = events[i];
        const int index = static_cast<int>(i);

        if (ev.required_flag != 0 && !host.flag(ev.required_flag)) {
            continue;
        }
        if (ev.blocking_flag != 0 && host.flag(ev.blocking_flag) &&
            (!rules_.soft_blocking || !rules_.soft_blocking(ev.blocking_flag) || ev.activation == Activation::Exclusive ||
             ev.state == static_cast<std::uint8_t>(GameState::EventSetFlag))) {
            continue;
        }

        if (ev.shape == TriggerShape::Always) {
            out.has_event = true;
            out.event = index;
            out.has_state = true;
            out.state = ev.state;
            out.has_param = true;
            out.param = ev.param;
            return out;
        }

        if (ev.activation == Activation::Button &&
            (!frame.action_pressed || frame.buttons_disabled || frame.player_busy)) {
            continue;
        }

        bool inside = true;
        switch (ev.shape) {
            case TriggerShape::Box: {
                const TriggerPoint p = host.point(ev.point);
                inside = abs32(frame.player_x - p.x) <= p.param0 * kQuarterMetre &&
                         abs32(frame.player_z - p.z) <= p.param1 * kQuarterMetre;
                break;
            }
            case TriggerShape::FacingPoint:
                inside = facing_point(frame, host.point(ev.point));
                break;
            case TriggerShape::FacingSegment:
                inside = facing_segment(frame, host.point(ev.point));
                break;
            case TriggerShape::OrientedBox:
                inside = oriented_box(frame, host.point(ev.point));
                break;
            default:
                break;
        }
        if (!inside) {
            continue;
        }

        if (ev.activation == Activation::Exclusive && index == frame.current_event) {
            out.has_state = true;
            out.state = static_cast<std::uint8_t>(GameState::None);
            return out;
        }

        if (ev.activation == Activation::Item) {
            int slot = 0;
            while (slot < kItemSlots && slots_[slot].item != -1) {
                ++slot;
            }
            if (slot < kItemSlots) {
                slots_[slot].item = ev.item;
                slots_[slot].event = index;
                slots_[slot].trigger = ev;
            } else {
                ++slot_overflows_;
            }
            continue;
        }

        if (ev.activation == Activation::Button && frame.dark_without_light &&
            ev.state != static_cast<std::uint8_t>(GameState::LoadMap) &&
            ev.state != static_cast<std::uint8_t>(GameState::LoadRoom) && ev.param > 1) {
            continue;
        }

        if (ev.state == static_cast<std::uint8_t>(GameState::EventSetFlag)) {
            host.set_flag(ev.blocking_flag);
            out.flag_set = ev.blocking_flag;
            break;
        }

        out.has_event = true;
        out.event = index;
        out.has_state = true;
        out.state = ev.state;
        out.has_param = true;
        out.param = ev.param;
        return out;
    }

    out.has_event = true;
    out.event = -1;
    out.has_state = true;
    out.state = static_cast<std::uint8_t>(GameState::None);
    out.has_param = true;
    out.param = 0;
    return out;
}

int EventTriggers::button_event_in_reach(const TriggerFrame& frame, std::span<const EventTrigger> events,
                                         const EventHost& host) const {
    if (frame.buttons_disabled || frame.player_busy) {
        return -1;
    }
    EventTriggers probe = *this;  // facing_point refreshes its cached probe
    const std::size_t count = events.size() < static_cast<std::size_t>(kMaxEvents) ? events.size() : kMaxEvents;
    for (std::size_t i = 0; i < count; ++i) {
        const EventTrigger& ev = events[i];
        if (ev.activation != Activation::Button || ev.shape == TriggerShape::Always) {
            continue;
        }
        if (ev.required_flag != 0 && !host.flag(ev.required_flag)) {
            continue;
        }
        if (ev.blocking_flag != 0 && host.flag(ev.blocking_flag) &&
            (!rules_.soft_blocking || !rules_.soft_blocking(ev.blocking_flag) ||
             ev.state == static_cast<std::uint8_t>(GameState::EventSetFlag))) {
            continue;
        }
        bool inside = false;
        switch (ev.shape) {
            case TriggerShape::Box: {
                const TriggerPoint p = host.point(ev.point);
                inside = abs32(frame.player_x - p.x) <= p.param0 * kQuarterMetre &&
                         abs32(frame.player_z - p.z) <= p.param1 * kQuarterMetre;
                break;
            }
            case TriggerShape::FacingPoint:
                inside = probe.facing_point(frame, host.point(ev.point));
                break;
            case TriggerShape::FacingSegment:
                inside = facing_segment(frame, host.point(ev.point));
                break;
            case TriggerShape::OrientedBox:
                inside = oriented_box(frame, host.point(ev.point));
                break;
            default:
                inside = true;
                break;
        }
        if (!inside) {
            continue;
        }
        if (frame.dark_without_light && ev.state != static_cast<std::uint8_t>(GameState::LoadMap) &&
            ev.state != static_cast<std::uint8_t>(GameState::LoadRoom) && ev.param > 1) {
            continue;
        }
        return static_cast<int>(i);
    }
    return -1;
}

}  // namespace sh::gameflow
