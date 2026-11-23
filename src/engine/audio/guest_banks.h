#pragma once

// Sound banks of a character spawned off its maps. A monster's sounds are
// programs of the map's ambient bank (slot 2), so on a map that does not own the monster its sound ids key a tone of
// another bank or nothing. The engine's spawn loads one of the monster's own ambient banks into a free slot and
// redirects only the sounds the current bank lacks to it; the map's bank stays where it is. Pure part: the plan and the
// redirect table; the game side (src/audio/game/guest_sound_banks.cpp) gathers the data and loads the banks.

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace sh::audio {

// One ambient sound a character plays.
struct GuestSound {
    std::uint16_t id = 0;
    int program = 0;
    int note = 0;
    std::string name;                // registry name, for messages
    std::vector<std::string> banks;  // the banks that hold it (registry); empty: the character's own banks
};

struct GuestPlan {
    struct Load {
        std::string bank;
        std::vector<std::uint16_t> ids;
    };
    std::vector<Load> loads;               // in order; a bank already in a guest slot only gets its ids added
    std::vector<std::string> unplayable;   // missing sounds no candidate bank has a tone for
};

// `current`: the bank in the ambient slot (upper case, "MEP000"); `own_banks`: the character's banks in preference
// order (the ambient banks of its maps); `has_tone(bank, program, note)`: the bank keys a tone for it.
GuestPlan plan_guest_banks(const std::string& current, const std::vector<GuestSound>& sounds,
                           const std::vector<std::string>& own_banks,
                           const std::function<bool(const std::string&, int, int)>& has_tone);

class GuestBanks {
public:
    static constexpr int kFirstSlot = 4;  // 0 base, 1 weapon, 2 ambient, 3 music
    static constexpr int kLastSlot = 15;

    // The slot a guest bank sits in, or a new one (-1 when all are taken). `fresh` is set for a new slot.
    int slot_of(const std::string& bank, bool* fresh);
    void redirect(std::uint16_t id, int slot) { redirect_[id] = slot; }
    // The slot sound `id` keys instead of `slot`.
    int slot_for(std::uint16_t id, int slot) const {
        const auto it = redirect_.find(id);
        return it != redirect_.end() ? it->second : slot;
    }
    bool redirected(std::uint16_t id) const { return redirect_.count(id) != 0; }
    // Forgets everything; returns the slots that held banks.
    std::vector<int> reset();
    bool empty() const { return slots_.empty(); }

private:
    std::map<std::string, int> slots_;       // bank -> slot
    std::map<std::uint16_t, int> redirect_;  // sound id -> slot
};

}  // namespace sh::audio
