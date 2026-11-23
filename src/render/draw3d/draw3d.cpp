#include "render/draw3d/draw3d.h"

namespace sh::draw3d {

CommandRing::CommandRing(std::size_t capacity_log2) : slots_(std::size_t{1} << capacity_log2) {}

CommandId CommandRing::record(const Command& command) {
    const CommandId id = next_;
    next_ = (next_ + 1) & kIdMask;
    if (next_ == 0) next_ = 1;
    Slot& slot = slots_[id & (slots_.size() - 1)];
    slot.id = id;
    slot.command = command;
    ++recorded_;
    return id;
}

const Command* CommandRing::find(CommandId id) const {
    if (id == 0 || id > kIdMask) return nullptr;
    const Slot& slot = slots_[id & (slots_.size() - 1)];
    return slot.id == id ? &slot.command : nullptr;
}

CommandRing& frame_commands() {
    static CommandRing ring;
    return ring;
}

CommandId submit(const Polygon& polygon, frame::OrderKey key, std::uint32_t depth_max) {
    frame::state().note_item_depth(depth_max);
    const CommandId id = frame_commands().record(polygon);
    frame::submit_keyed(frame::Kind::Draw3D, id, key);
    return id;
}

CommandId submit_projected(Polygon polygon, const std::array<std::uint16_t, 4>& newest, frame::OrderKey key) {
    polygon.depth = projection_depth(newest, polygon.count);
    std::uint32_t mx = 0;
    for (const std::uint16_t z : newest) mx = z > mx ? z : mx;
    return submit(polygon, key, mx);
}

CommandId submit_placed(Polygon polygon, const std::array<std::uint16_t, 4>& newest, frame::OrderKey key) {
    if (!polygon.depth) return submit_projected(polygon, newest, key);
    std::uint32_t mx = 0;
    for (int k = 0; k < polygon.count && k < 4; ++k) {
        const std::uint32_t z = polygon.depth->sz[static_cast<std::size_t>(k)];
        mx = z > mx ? z : mx;
    }
    return submit(polygon, key, mx);
}

}  // namespace sh::draw3d
