#include "render/draw2d/draw2d.h"

namespace sh::draw2d {

namespace {

// Texture page word: column in bits 0-3, row bit 0 in bit 4 and row bit 1 in bit 11, blend in 5-6, depth in
// 7-8. Bits 9, 10 and 12-15 belong to no field.
constexpr std::uint16_t kPageFieldBits = 0x09FF;

}  // namespace

TexturePage TexturePage::from_word(std::uint16_t word) {
    TexturePage page;
    page.column = static_cast<std::uint8_t>(word & 0xF);
    page.row = static_cast<std::uint8_t>(((word >> 4) & 1) | (((word >> 11) & 1) << 1));
    page.blend = static_cast<Blend>((word >> 5) & 3);
    page.depth = static_cast<Depth>((word >> 7) & 3);
    page.other_bits = static_cast<std::uint16_t>(word & ~kPageFieldBits);
    return page;
}

std::uint16_t TexturePage::word() const {
    return static_cast<std::uint16_t>((column & 0xF) | ((row & 1) << 4) | ((row & 2) << 10) |
                                      ((static_cast<unsigned>(blend) & 3) << 5) |
                                      ((static_cast<unsigned>(depth) & 3) << 7) | (other_bits & ~kPageFieldBits));
}

Palette Palette::from_word(std::uint16_t word) {
    Palette palette;
    palette.column = static_cast<std::uint8_t>(word & 0x3F);
    palette.row = static_cast<std::uint16_t>(word >> 6);
    return palette;
}

std::uint16_t Palette::word() const { return static_cast<std::uint16_t>((column & 0x3F) | (row << 6)); }

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

void submit(const Command& command, frame::OrderKey key, std::uint32_t depth_max) {
    frame::state().note_item_depth(depth_max);
    if (const auto* area = std::get_if<DrawArea>(&command)) {
        frame::submit_keyed(frame::Kind::Area, 0, key, nullptr, frame::Rect{area->x, area->y, area->w, area->h});
        return;
    }
    frame::submit_keyed(frame::Kind::Draw2D, frame_commands().record(command), key);
}

}  // namespace sh::draw2d
