#pragma once

// Character animation slots: which character's animation file
// sits in which slot of the animation memory, where its bone coordinates live, and the order in
// which a slot is claimed, moved, refreshed or cleared. Slot 0 is the player, slots 1..group-1 are
// the map's character groups, the slots after them are free for extra characters that bring their
// own buffer.
//
// The table is a view over records the game owns: AnimSlot has the byte layout of the game's slot
// record, so the bridge hands over the game's array and nothing is copied. Addresses and bone
// coordinates are opaque to the engine; everything that touches memory, files or the bone
// hierarchy goes through AnimSlotHost.

#include <cstdint>
#include <vector>

namespace sh::gameflow {

struct AnimSlot {
    std::int32_t alloc_chara;   // character the buffer is reserved for
    std::int32_t active_chara;  // character whose animations the buffer holds now; 0 = none
    void* alloc_addr;          // start of the reserved buffer
    void* active_header;       // header of the animation file that was loaded into it
    std::int32_t alloc_size;   // reserved bytes
    std::int32_t active_size;  // loaded bytes (sector-aligned file size)
    void* bone_coords;         // this slot's bone coordinates
};

class AnimSlotHost {
public:
    virtual ~AnimSlotHost() = default;

    virtual std::int32_t anim_file_size(int chara) const = 0;       // sector-aligned size of the character's animation file
    virtual int bone_count(const void* header) const = 0;           // bones of a loaded animation file (0 for null)
    virtual void* coord_advance(void* coord, int count) const = 0;  // coord + count bone coordinates
    // True when `bones` coordinates starting at `coord`, plus one spare, would reach the end of the
    // shared NPC bone buffer.
    virtual bool coord_past_end(const void* coord, int bones) const = 0;
    virtual void* coord_buffer_start() const = 0;                   // start of the shared NPC bone buffer
    virtual void* coord_buffer_spill() const = 0;                   // the map's own buffer used when the shared one is full
    virtual void bind_bones(void* header, void* coord) = 0;         // builds the bone hierarchy of an animation over coordinates
    virtual void move_bytes(void* dest, void* src, std::int32_t bytes) = 0;  // moves a loaded animation file
    virtual void queue_read(int slot, int chara, void* dest, void* coord) = 0;  // asks for the character's animation file
    virtual void set_chara_slot(int chara, int slot) = 0;           // the character's animations now live in this slot
    virtual int group_chara(int group) const = 0;                   // character of the map's group, 0 = none
};

class AnimSlots {
public:
    // The first `groups` of `slots` are the player and the map's groups; a slot index past the end grows the list.
    AnimSlots(std::vector<AnimSlot>& slots, int groups, AnimSlotHost& host);

    // Slot `idx` (1-based group slot, or an extra one) is reserved for `chara`. A null `header`
    // takes the buffer right after the nearest earlier reserved one. A character that is already
    // in the slot is reused (moved down when its buffer moved down), a character loaded in another
    // group slot is copied, otherwise its file is requested; reserved buffers that overlap another
    // slot's loaded animations clear that slot.
    void alloc(int idx, int chara, void* header, void* coord);

    // The animation file of `chara` has arrived in `header`: marks the slot loaded and binds the
    // bones; a null `coord` takes the next free coordinates after the previous slot's.
    void update(int idx, int chara, void* header, void* coord);

    // Re-binds the bones of every group slot behind a used one (the groups moved when an earlier
    // character changed).
    void refresh_bones();

    // First group slot (not the player's) where `chara`'s animations are loaded; 0 when none.
    int slot_of(int chara) const;

    // True when the reserved buffer of `a` overlaps the loaded bytes of `b`.
    bool overlaps(int a, int b) const;

private:
    void ensure(int idx);

    std::vector<AnimSlot>& slots_;
    int groups_;
    AnimSlotHost& host_;
};

// The running game's slots: the player's, the map's three character groups, then one per character type; and each
// character type's slot index (-1 = none). Both grow with the character numbers in use (a mod's types take numbers
// past the base game's 0..44 without a limit).
struct AnimSlotTable {
    static constexpr int kGroupSlots = 4;
    std::vector<AnimSlot> slots;
    std::vector<std::int32_t> chara_slot;

    // The slot of character type `chara`, -1 when it has none (or is no type number).
    int chara_slot_of(int chara) const {
        const auto at = static_cast<std::size_t>(chara);
        return chara >= 0 && at < chara_slot.size() ? chara_slot[at] : -1;
    }
    void set_chara_slot(int chara, int slot);
    // Whether `index` names a slot of the list.
    bool has_slot(int index) const { return index >= 0 && static_cast<std::size_t>(index) < slots.size(); }
};
AnimSlotTable& anim_slot_table();
// The slot logic over that table (src/engine/gameflow/game/anim_slots_host.cpp).
AnimSlots& anim_slots();

}  // namespace sh::gameflow
