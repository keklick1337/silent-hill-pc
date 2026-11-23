#include "engine/world/chara_volume.h"

#include <cstdint>

namespace sh::world {
namespace {

// Q.12 multiply, 64-bit product, the low part kept (the game's macro wraps there).
std::int32_t mult_q12(std::int32_t a, std::int32_t b) {
    return static_cast<std::int32_t>((static_cast<std::int64_t>(a) * b) >> 12);
}

// Q.12 -> Q.8 with the truncating shift the game uses, then the 16-bit wrap of its state fields.
constexpr std::int16_t to_q8(std::int32_t v) {
    return static_cast<std::int16_t>(v >> 4);
}

}  // namespace

CharaVolume build_chara_volume(const CharaVolumeSource& source, const VolumeInflate& inflate) {
    const bool cylinder = source.path == VolumePath::Cylinder;
    const std::int32_t radius_q12 = cylinder ? source.cylinder_radius : source.box_radius;
    const std::int32_t offset_x = cylinder ? source.cylinder_offset_x : source.box_offset_x;
    const std::int32_t offset_z = cylinder ? source.cylinder_offset_z : source.box_offset_z;
    std::int32_t top_q12 = source.y + (cylinder ? source.box_bottom : source.box_height);
    std::int32_t ground_q12 = source.y + source.box_top;
    std::int16_t radius_q8 = to_q8(radius_q12);

    if (inflate.active && !cylinder) {
        const std::int32_t band = mult_q12(radius_q12, inflate.vert_mul);
        top_q12 += band;
        ground_q12 -= band;
        radius_q8 = static_cast<std::int16_t>(radius_q8 + to_q8(mult_q12(radius_q12, inflate.rad_mul)));
    }

    return CharaVolume{
        (source.x + offset_x) >> 4,
        (source.z + offset_z) >> 4,
        to_q8(ground_q12),
        to_q8(top_q12),
        radius_q8,
    };
}

}  // namespace sh::world
