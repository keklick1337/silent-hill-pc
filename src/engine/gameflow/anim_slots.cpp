#include "engine/gameflow/anim_slots.h"

#include <cstring>

namespace sh::gameflow {

AnimSlots::AnimSlots(std::vector<AnimSlot>& slots, int groups, AnimSlotHost& host)
    : slots_(slots), groups_(groups), host_(host) {}

void AnimSlots::ensure(int idx) {
    if (idx >= 0 && static_cast<std::size_t>(idx) >= slots_.size()) {
        slots_.resize(static_cast<std::size_t>(idx) + 1, AnimSlot{});
    }
}

void AnimSlotTable::set_chara_slot(int chara, int slot) {
    if (chara < 0) {
        return;
    }
    if (static_cast<std::size_t>(chara) >= chara_slot.size()) {
        chara_slot.resize(static_cast<std::size_t>(chara) + 1, -1);
    }
    chara_slot[static_cast<std::size_t>(chara)] = slot;
}

bool AnimSlots::overlaps(int a, int b) const {
    const auto alloc = reinterpret_cast<std::uintptr_t>(slots_[a].alloc_addr);
    const auto active = reinterpret_cast<std::uintptr_t>(slots_[b].active_header);
    return !(alloc >= active + static_cast<std::uintptr_t>(static_cast<std::int64_t>(slots_[b].active_size)) ||
             active >= alloc + static_cast<std::uintptr_t>(static_cast<std::int64_t>(slots_[a].alloc_size)));
}

int AnimSlots::slot_of(int chara) const {
    for (int i = 1; i < groups_; ++i)
        if (slots_[i].active_chara == chara) return i;
    return 0;
}

void AnimSlots::alloc(int idx, int chara, void* header, void* coord) {
    ensure(idx);
    AnimSlot& slot = slots_[idx];
    if (chara == 0) return;

    // No explicit buffer: right after the nearest earlier slot that has one.
    for (const AnimSlot* prev = &slot - 1; header == nullptr; --prev)
        header = static_cast<char*>(prev->alloc_addr) + prev->alloc_size;

    if (slot.active_chara == chara) {
        if (idx == 1 || header == slot.active_header) {
            update(idx, chara, slot.active_header, coord);
            return;
        }
        if (header < slot.active_header) {
            slot.alloc_addr = header;
            host_.move_bytes(header, slot.active_header, slot.active_size);
            update(idx, chara, header, coord);
            return;
        }
    }

    slot.bone_coords = host_.coord_buffer_start();
    slot.active_chara = 0;
    slot.active_header = nullptr;
    slot.active_size = 0;
    slot.alloc_chara = chara;
    slot.alloc_addr = header;
    slot.alloc_size = host_.anim_file_size(chara);

    const int donor = slot_of(chara);
    if (donor > 0) {
        host_.move_bytes(slot.alloc_addr, slots_[donor].active_header, slots_[donor].active_size);
        update(idx, chara, slot.alloc_addr, coord);
    } else {
        host_.queue_read(idx, chara, header, coord);
    }

    for (int i = 1; i < groups_; ++i)
        if (i != idx && slots_[i].active_chara != 0 && overlaps(idx, i))
            std::memset(&slots_[i], 0, sizeof(AnimSlot));
}

void AnimSlots::update(int idx, int chara, void* header, void* coord) {
    ensure(idx);
    AnimSlot& slot = slots_[idx];

    if (coord == nullptr) {
        if (idx == 1) {
            coord = host_.coord_buffer_start();
        } else if (idx >= 2) {
            const AnimSlot& prev = slots_[idx - 1];
            // The previous group's character may never have finished loading.
            if (prev.active_header == nullptr) {
                coord = host_.coord_buffer_start();
            } else {
                coord = host_.coord_advance(prev.bone_coords, host_.bone_count(prev.active_header) + 1);
            }
            if (host_.coord_past_end(coord, host_.bone_count(header))) coord = host_.coord_buffer_spill();
        }
    }

    slot.active_chara = chara;
    slot.active_header = header;
    slot.active_size = host_.anim_file_size(chara);
    slot.bone_coords = coord;

    host_.bind_bones(header, coord);
    host_.set_chara_slot(chara, idx);
}

void AnimSlots::refresh_bones() {
    for (int i = 1; i < groups_ - 1; ++i) {
        if (host_.group_chara(i) == 0) continue;
        void* header = slots_[i + 1].active_header;
        void* coord = host_.coord_advance(slots_[i].bone_coords, host_.bone_count(slots_[i].active_header) + 1);
        if (host_.coord_past_end(coord, host_.bone_count(header))) coord = host_.coord_buffer_spill();
        slots_[i + 1].bone_coords = coord;
        host_.bind_bones(header, coord);
    }
}

}  // namespace sh::gameflow
