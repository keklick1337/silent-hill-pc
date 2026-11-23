// Animation slots on a fake host: buffer chaining, reuse, copy from another group, requests, overlap
// clearing and the bone-coordinate chain, checked against hand-computed slot states.

#include <string>
#include <vector>

#include "engine/gameflow/anim_slots.h"

#include "framework/sh_test.h"

namespace gf = sh::gameflow;

namespace {

char g_mem[0x100000];
int g_coords[100];  // bone coordinate "objects"; one int each

class Host final : public gf::AnimSlotHost {
public:
    std::vector<std::string> log;
    int bones = 5;
    int groupChara[4] = {0, 7, 8, 9};
    std::int32_t anim_file_size(int chara) const override { return chara * 0x800; }
    int bone_count(const void*) const override { return bones; }
    void* coord_advance(void* c, int n) const override { return static_cast<int*>(c) + n; }
    bool coord_past_end(const void* c, int b) const override { return static_cast<const int*>(c) + b + 1 >= g_coords + 40; }
    void* coord_buffer_start() const override { return g_coords; }
    void* coord_buffer_spill() const override { return g_coords + 80; }
    void bind_bones(void*, void* c) override { log.push_back("bind " + std::to_string(static_cast<int*>(c) - g_coords)); }
    void move_bytes(void* d, void* s, std::int32_t n) override {
        log.push_back("move " + std::to_string(static_cast<char*>(d) - g_mem) + " " + std::to_string(static_cast<char*>(s) - g_mem) + " " + std::to_string(n));
    }
    void queue_read(int slot, int chara, void* d, void*) override {
        log.push_back("read " + std::to_string(slot) + " " + std::to_string(chara) + " " + std::to_string(static_cast<char*>(d) - g_mem));
    }
    void set_chara_slot(int chara, int slot) override { log.push_back("slot " + std::to_string(chara) + "->" + std::to_string(slot)); }
    int group_chara(int g) const override { return groupChara[g]; }
};

}  // namespace

SH_TEST(anim_slots_chain_and_request) {
    std::vector<gf::AnimSlot> slots(6);
    slots[0].alloc_chara = slots[0].active_chara = 1;
    slots[0].alloc_addr = g_mem;
    slots[0].alloc_size = 0x1000;
    Host h;
    gf::AnimSlots s(slots, 4, h);

    s.alloc(1, 7, nullptr, nullptr);  // buffer follows slot 0
    SH_CHECK(slots[1].alloc_addr == g_mem + 0x1000 && slots[1].alloc_size == 7 * 0x800);
    SH_CHECK(slots[1].active_chara == 0 && slots[1].active_header == nullptr);
    SH_CHECK((h.log == std::vector<std::string>{"read 1 7 4096"}));

    h.log.clear();
    s.update(1, 7, slots[1].alloc_addr, nullptr);  // arrives: coords at the buffer start
    SH_CHECK(slots[1].bone_coords == g_coords && slots[1].active_size == 7 * 0x800);
    SH_CHECK((h.log == std::vector<std::string>{"bind 0", "slot 7->1"}));

    h.log.clear();
    s.alloc(2, 8, nullptr, nullptr);
    s.update(2, 8, slots[2].alloc_addr, nullptr);  // after slot 1's 5 bones + 1
    SH_CHECK(slots[2].bone_coords == g_coords + 6);
    SH_CHECK(s.slot_of(8) == 2 && s.slot_of(9) == 0);
}

SH_TEST(anim_slots_reuse_copy_and_overlap) {
    std::vector<gf::AnimSlot> slots(6);
    slots[0].alloc_addr = g_mem;
    slots[0].alloc_size = 0x1000;
    Host h;
    gf::AnimSlots s(slots, 4, h);
    s.alloc(1, 7, g_mem + 0x4000, nullptr);
    s.update(1, 7, g_mem + 0x4000, nullptr);
    s.alloc(2, 8, g_mem + 0x2000, nullptr);
    s.update(2, 8, g_mem + 0x2000, nullptr);
    h.log.clear();

    // Same character, buffer moved down: the loaded data moves with it (slot 1 never moves).
    s.alloc(2, 8, g_mem + 0x2000 - 0x100, nullptr);
    SH_CHECK(slots[2].alloc_addr == g_mem + 0x2000 - 0x100 && slots[2].active_header == g_mem + 0x2000 - 0x100);
    SH_CHECK(h.log[0] == "move 7936 8192 16384");

    h.log.clear();
    s.alloc(3, 8, g_mem + 0x20000, nullptr);  // same character in another group: copied, no read
    SH_CHECK(h.log.size() == 3 && h.log[0] == "move 131072 7936 16384");

    // Reserving bytes under slot 1's loaded ones clears slot 1 and nothing else.
    s.alloc(3, 9, g_mem + 0x6000, nullptr);
    SH_CHECK(slots[1].active_chara == 0 && slots[1].alloc_addr == nullptr && slots[2].active_chara == 8);
}

SH_TEST(anim_slots_refresh_and_spill) {
    std::vector<gf::AnimSlot> slots(6);
    Host h;
    gf::AnimSlots s(slots, 4, h);
    slots[1].bone_coords = g_coords + 10;
    slots[1].active_header = g_mem;
    slots[2].active_header = g_mem + 1;
    s.refresh_bones();
    SH_CHECK(slots[2].bone_coords == g_coords + 16);
    h.groupChara[1] = 0;  // an empty group leaves the next one alone
    slots[2].bone_coords = nullptr;
    s.refresh_bones();
    SH_CHECK(slots[2].bone_coords == g_coords + 16 || slots[2].bone_coords == nullptr);
    slots[1].bone_coords = g_coords + 35;  // runs out of the shared buffer: spill
    h.groupChara[1] = 7;
    s.refresh_bones();
    SH_CHECK(slots[2].bone_coords == g_coords + 80);
}
