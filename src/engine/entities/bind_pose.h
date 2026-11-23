// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The bind pose of a character's bones from its animation file as it lies in memory (the ANM layout of
// formats/convert/anim/anm_file.h): the frame hierarchy and the parts no keyframe animates.

#pragma once

#include "engine/entities/character_record.h"

namespace sh::entities {

// Bone 0 becomes a root (cleared, identity, no parent, marked computed). Every further bone of the file gets its
// parent frame; its translation is the file's bind offset (<< scale) when no keyframe translates it, its rotation
// identity when no keyframe rotates it; everything else of the frame is left as it was. Without a file only the
// root is set.
void bind_pose_init(const void* anm_header, BoneCoord* bones);

}  // namespace sh::entities
