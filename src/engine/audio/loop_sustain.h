#pragma once

// Looping sounds placed in the world keep fading with distance on their own.
//
// Every caller of a falloff loop (wheelchair squeak, machinery hum) sits inside a proximity or state test, and the
// same test gated the call that started the loop. Walk out of it and nobody updates the loop any more: the voice
// keeps its last volume for the rest of the room. On the console the frozen voice was usually stolen by the next
// sound; the engine's voices are not recycled that eagerly. So each loop's last request is remembered and replayed on
// every frame its caller skipped: the distance is measured again, leaving the area fades the loop on its own curve
// and coming back restores it. Frames the caller updates change nothing.
//
// Loops on reserved voices (the radio) are excluded by the caller: their update restarts a voice that is off, so
// sustaining them would never let them stop.

#include <array>
#include <cstdint>
#include <functional>

namespace sh::audio {

struct SustainedLoop {
    std::uint16_t id = 0;
    std::array<std::int32_t, 3> position{};  // Q12 world point
    std::int32_t volume = 0;
    std::int32_t falloff = 0;
    std::int32_t pitch = 0;
};

class LoopSustain {
public:
    static constexpr int kCapacity = 16;

    // A caller updated the loop this frame. Ignored while sweeping (the replay itself) and when all places are
    // taken by other loops.
    void record(const SustainedLoop& loop);
    // Once per frame after the game's own updates: forgets loops whose sound has stopped (`playing` false) and
    // replays the ones nobody updated since the last sweep.
    void sweep(const std::function<bool(std::uint16_t id)>& playing,
               const std::function<void(const SustainedLoop& loop)>& replay);
    // Map change: the positions belong to the map that goes away.
    void reset();

    int tracked() const;

private:
    struct Place {
        bool used = false;
        bool fresh = false;  // a caller wrote the loop since the last sweep
        SustainedLoop loop;
    };
    std::array<Place, kCapacity> places_{};
    bool sweeping_ = false;
};

}  // namespace sh::audio
