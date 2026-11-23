#include "engine/camera/camera_map_rules.h"

#include "engine/camera/camera_math.h"

namespace sh::camera {

namespace {

bool at_place(const std::string& map, int room, const MapPlace& place) {
    return !place.map.empty() && place.map == map && place.room == room;
}

}  // namespace

bool map_type_matches(const MapTypeSelector& selector, const MapPlace& place) {
    if (place.map_type.empty() || place.map_type != selector.tag) {
        return false;
    }
    return selector.occurrence == 0 || selector.occurrence == place.map_type_occurrence;
}

bool self_view_blocked(const MapCameraRules& rules, const MapPlace& place, std::int32_t x, std::int32_t z) {
    for (const SelfViewBlock& b : rules.self_view_blocks) {
        if (map_type_matches(b.map_type, place) && x >= b.min_x && x <= b.max_x && z >= b.min_z && z <= b.max_z) {
            return true;
        }
    }
    return false;
}

bool tight_doorways(const MapCameraRules& rules, const MapPlace& place) {
    for (const MapTypeSelector& s : rules.tight_doorway_types) {
        if (map_type_matches(s, place)) {
            return true;
        }
    }
    return false;
}

std::vector<RoadAnchorShift> road_anchor_shifts(const MapCameraRules& rules, const MapPlace& place,
                                                const std::vector<CameraRoad>& roads) {
    std::vector<RoadAnchorShift> out;
    for (const RoadAnchorRule& r : rules.road_anchor_shifts) {
        if (!at_place(r.map, r.room, place)) {
            continue;
        }
        for (std::size_t i = 0; i < roads.size(); ++i) {
            const Area& a = roads[i].road_area;
            if (a.min_x == r.road_x && a.max_x == r.road_x && a.min_z == r.road_z && a.max_z == r.road_z) {
                RoadAnchorShift shift;
                shift.road = static_cast<RoadRef>(i);
                shift.x_shift = r.shift_x;
                out.push_back(shift);
            }
        }
    }
    return out;
}

WideShot wide_shot(const MapCameraRules& rules, const MapPlace& place, const HorPlusWidths& widths,
                   std::int32_t geom_screen_dist) {
    WideShot w;
    for (const WideShotRule& r : rules.wide_shots) {
        if (!at_place(r.map, r.room, place)) {
            continue;
        }
        // Written as "not wider" rather than "narrower" so a NaN width still goes on.
        if (!widths.valid || widths.half_wide <= widths.half_43 || geom_screen_dist <= 0) {
            return w;
        }
        const std::int32_t depth = fx::wrap_mul(geom_screen_dist, 4096);
        const std::int32_t a43 = fx::atan2(static_cast<std::int32_t>(widths.half_43 * 4096.0f), depth);
        const std::int32_t a_wide = fx::atan2(static_cast<std::int32_t>(widths.half_wide * 4096.0f), depth);
        w.enabled = true;
        w.anchor_x = r.anchor_x;
        w.anchor_z = r.anchor_z;
        w.tolerance = r.tolerance;
        w.yaw_delta = a_wide - a43;
        return w;
    }
    return w;
}

}  // namespace sh::camera
