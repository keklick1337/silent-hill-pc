// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The remembered ground point: the height and the kind of ground under one (x, z), asked again and again by the
// same caller in a tick (the NPC behaviours, the map scripts, the room picker). The last point is kept and a query
// on the same point is answered from it. Where there is no ground the answer is the game's: 8 m down, and on two
// maps whose open ground is a drop (overlay.json "void_drop": map5_s01's south half, map6_s00) 4 m down with the
// "open ground" kind 7.

#pragma once

#include <cstdint>

namespace sh::world {

// Where the map's missing ground is a drop (the map's overlay.json "void_drop").
struct VoidDrop {
    bool no_ground = false;          // where the collision has no ground
    bool no_ground_bounded = false;  // ... only up to no_ground_max_z
    std::int32_t no_ground_max_z = 0;  // Q12
    bool open_ground = false;        // on the open ground kind too
};

// Where the cache asks: the ground under a point (height Q12, kind 0: none) and the loaded map's drop rule.
class GroundPointSource {
public:
    virtual ~GroundPointSource() = default;
    virtual void ground(std::int32_t x, std::int32_t z, std::int32_t* height, int* kind) = 0;
    virtual VoidDrop void_drop() const = 0;
};

class GroundPointCache {
public:
    void bind(GroundPointSource* source) { source_ = source; }

    // Ground height (Q12, +Y down) and ground kind at (x, z) (Q12 metres).
    std::int32_t height(std::int32_t x, std::int32_t z);
    int kind(std::int32_t x, std::int32_t z);

private:
    void fill(std::int32_t x, std::int32_t z);

    GroundPointSource* source_ = nullptr;
    std::int32_t x_ = 0, z_ = 0;
    std::int32_t height_ = 0;
    int kind_ = -1;  // -1: nothing remembered
};

// The game's one cache.
GroundPointCache& ground_point_cache();

}  // namespace sh::world
