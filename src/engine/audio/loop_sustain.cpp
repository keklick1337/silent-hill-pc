#include "engine/audio/loop_sustain.h"

namespace sh::audio {

void LoopSustain::record(const SustainedLoop& loop) {
    if (sweeping_) {
        return;
    }
    Place* place = nullptr;
    Place* free = nullptr;
    for (Place& p : places_) {
        if (p.used && p.loop.id == loop.id) {
            place = &p;
            break;
        }
        if (!p.used && free == nullptr) {
            free = &p;
        }
    }
    if (place == nullptr) {
        if (free == nullptr) {
            return;
        }
        place = free;
    }
    place->used = true;
    place->fresh = true;
    place->loop = loop;
}

void LoopSustain::sweep(const std::function<bool(std::uint16_t id)>& playing,
                        const std::function<void(const SustainedLoop& loop)>& replay) {
    sweeping_ = true;
    for (Place& p : places_) {
        if (!p.used) {
            continue;
        }
        if (!playing(p.loop.id)) {
            p.used = false;
            continue;
        }
        if (!p.fresh) {
            replay(p.loop);
        }
        p.fresh = false;
    }
    sweeping_ = false;
}

void LoopSustain::reset() {
    for (Place& p : places_) {
        p.used = false;
    }
}

int LoopSustain::tracked() const {
    int n = 0;
    for (const Place& p : places_) {
        n += p.used ? 1 : 0;
    }
    return n;
}

}  // namespace sh::audio
