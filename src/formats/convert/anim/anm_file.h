#pragma once

#include "formats/common/status.h"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

// ANM: skeletal animation of Silent Hill.
//
//   0x00 u16 data offset    u8 rotation slots   u8 translation slots   u16 keyframe size   u8 bone count   s8 pad
//   0x08 u32 active bones   0x0C u32 file size  0x10 u16 keyframe count  u8 scale log2  u8 root Y offset
//   0x14 bone table, 6 bytes per bone: s8 parent, s8 rotation slot, s8 translation slot, s8 x, y, z at bind
//   data offset: keyframes, `keyframe size` bytes each: translation slots (3 x s8) then rotation slots (9 x s8,
//   a row-major 3x3 matrix, value << 5 is Q12)
//
// Harry's map files (ANIM/HB_M0S00.ANM ...) have no header: they are more keyframes in HB_BASE.ANM's layout,
// loaded behind its keyframes, so their first keyframe has the index HB_BASE's keyframe count.
namespace sh::convert::anim {

struct AnmHeader {
    std::uint16_t data_offset = 0;
    std::uint8_t rotation_slots = 0;
    std::uint8_t translation_slots = 0;
    std::uint16_t keyframe_size = 0;
    std::uint8_t bone_count = 0;
    std::uint8_t pad7 = 0;
    std::uint32_t active_bones = 0;
    std::uint32_t file_size = 0;
    std::uint16_t keyframe_count = 0;
    std::uint8_t scale_log2 = 0;
    std::uint8_t root_y_offset = 0;
};

struct AnmBone {
    int parent = -1;
    int rotation = -1;     // rotation slot, -1: identity
    int translation = -1;  // translation slot, -1: `bind` << scale_log2
    std::array<int, 3> bind{0, 0, 0};
};

// Values of one keyframe as stored (s8).
struct AnmKeyframe {
    std::vector<std::array<std::int8_t, 3>> translations;  // one per translation slot
    std::vector<std::array<std::int8_t, 9>> rotations;     // one per rotation slot
};

// Pose of one bone: 3x3 row-major rotation in Q12 and translation in PS1 geometry units (Q8 metres), the
// coordinate matrix of the game's bone coordinate record.
struct Pose {
    std::array<std::int32_t, 9> m{4096, 0, 0, 0, 4096, 0, 0, 0, 4096};
    std::array<std::int32_t, 3> t{0, 0, 0};
};

class AnmFile {
public:
    // A file with its own header.
    static Result<AnmFile> parse(std::span<const std::uint8_t> raw);
    // A headerless keyframe file (HB_M*.ANM) in the layout of `base` (HB_BASE.ANM).
    static Result<AnmFile> parse_extension(std::span<const std::uint8_t> raw, const AnmFile& base);
    // True when the bytes start with a header whose arithmetic holds.
    static bool has_header(std::span<const std::uint8_t> raw);

    bool extension() const { return extension_; }
    const AnmHeader& header() const { return header_; }
    const std::vector<AnmBone>& bones() const { return bones_; }
    const std::vector<AnmKeyframe>& keyframes() const { return keyframes_; }
    // Bytes between the bone table and the data offset, and after the last whole keyframe (padding, or a cut
    // keyframe: the padded file size of a few files ends inside their last keyframe).
    const std::vector<std::uint8_t>& gap() const { return gap_; }
    const std::vector<std::uint8_t>& tail() const { return tail_; }

    // The bytes this was parsed from, rebuilt from the fields.
    std::vector<std::uint8_t> serialize() const;

    // Builds a file from fields (the JSON reader). Checks the same invariants as parse().
    static Result<AnmFile> from_parts(bool extension, const AnmHeader& header, std::vector<AnmBone> bones,
                                      std::vector<std::uint8_t> gap, std::vector<AnmKeyframe> keyframes,
                                      std::vector<std::uint8_t> tail);

    // Local bone poses between keyframes k0 and k1 at `alpha` (Q12), with the arithmetic of Anim_BonesInit and
    // Anim_BonesUpdate: every bone counts as active, bone 0 is left at the identity (the game puts the character's
    // own coordinate there). Keyframe indices out of range are clamped.
    std::vector<Pose> local_poses(std::size_t k0, std::size_t k1, std::int32_t alpha) const;

    // Model-space poses: W = W_parent * L with the GTE's shift (GsGetLws), parent first.
    std::vector<Pose> world_poses(const std::vector<Pose>& local) const;

private:
    bool extension_ = false;
    AnmHeader header_;
    std::vector<AnmBone> bones_;
    std::vector<std::uint8_t> gap_;
    std::vector<AnmKeyframe> keyframes_;
    std::vector<std::uint8_t> tail_;
};

}  // namespace sh::convert::anim
