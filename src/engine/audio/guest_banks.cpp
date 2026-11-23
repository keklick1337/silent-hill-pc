#include "engine/audio/guest_banks.h"

#include <algorithm>

namespace sh::audio {
namespace {

bool contains(const std::vector<std::string>& v, const std::string& s) {
    return std::find(v.begin(), v.end(), s) != v.end();
}

}  // namespace

GuestPlan plan_guest_banks(const std::string& current, const std::vector<GuestSound>& sounds,
                           const std::vector<std::string>& own_banks,
                           const std::function<bool(const std::string&, int, int)>& has_tone) {
    GuestPlan plan;
    // A sound is the map's when its bank is in the slot: then the map plays it already.
    std::vector<const GuestSound*> missing;
    for (const GuestSound& s : sounds) {
        const std::vector<std::string>& owners = s.banks.empty() ? own_banks : s.banks;
        if (!contains(owners, current)) {
            missing.push_back(&s);
        }
    }
    std::vector<std::string> candidates;
    for (const std::string& b : own_banks) {
        if (b != current && !contains(candidates, b)) {
            candidates.push_back(b);
        }
    }
    for (const GuestSound* s : missing) {
        for (const std::string& b : s->banks) {
            if (b != current && !contains(candidates, b)) {
                candidates.push_back(b);
            }
        }
    }
    // Greedy: the bank that serves most of what is left, the earlier on ties, until nothing more is served.
    while (!missing.empty()) {
        std::size_t best = candidates.size();
        std::vector<std::uint16_t> best_ids;
        for (std::size_t c = 0; c < candidates.size(); ++c) {
            std::vector<std::uint16_t> ids;
            for (const GuestSound* s : missing) {
                const std::vector<std::string>& owners = s->banks.empty() ? own_banks : s->banks;
                if (contains(owners, candidates[c]) && has_tone(candidates[c], s->program, s->note)) {
                    ids.push_back(s->id);
                }
            }
            if (ids.size() > best_ids.size()) {
                best = c;
                best_ids = std::move(ids);
            }
        }
        if (best == candidates.size()) {
            break;
        }
        plan.loads.push_back({candidates[best], best_ids});
        std::erase_if(missing, [&](const GuestSound* s) {
            return std::find(best_ids.begin(), best_ids.end(), s->id) != best_ids.end();
        });
        candidates.erase(candidates.begin() + static_cast<std::ptrdiff_t>(best));
    }
    for (const GuestSound* s : missing) {
        plan.unplayable.push_back(s->name);
    }
    return plan;
}

int GuestBanks::slot_of(const std::string& bank, bool* fresh) {
    *fresh = false;
    if (const auto it = slots_.find(bank); it != slots_.end()) {
        return it->second;
    }
    for (int slot = kFirstSlot; slot <= kLastSlot; ++slot) {
        const bool taken = std::any_of(slots_.begin(), slots_.end(), [slot](const auto& e) { return e.second == slot; });
        if (!taken) {
            slots_[bank] = slot;
            *fresh = true;
            return slot;
        }
    }
    return -1;
}

std::vector<int> GuestBanks::reset() {
    std::vector<int> slots;
    for (const auto& [bank, slot] : slots_) {
        slots.push_back(slot);
    }
    slots_.clear();
    redirect_.clear();
    return slots;
}

}  // namespace sh::audio
