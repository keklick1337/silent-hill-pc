// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The draw record of a world object as the map's words and the engine's object lists keep it: the part record of a
// skeleton (its flags word cleared with a new name) and the model slot the world drawer caches its lookup in, then
// the object's pose. Map variables bound to world objects ("wo:<name>") address these bytes directly.

#pragma once

#include <cstdint>

#include "engine/world/object_queue.h"
#include "engine/world/skeleton.h"

namespace sh::world {

struct WorldObjectDrawRecord {
    SkeletonPartModel part;
    ObjectModel model;
};

// An object with its position and rotation (Q12).
struct WorldObjectPoseRecord {
    WorldObjectDrawRecord object;
    ObjectPosition position;
    ObjectRotation rotation;
};

// An object with a position only.
struct WorldObjectPlacementRecord {
    WorldObjectDrawRecord object;
    ObjectPosition position;
};

// The strides the map variables have always been addressed with on this platform.
static_assert(sizeof(WorldObjectPoseRecord) == 72 && sizeof(WorldObjectPlacementRecord) == 64);

}  // namespace sh::world
