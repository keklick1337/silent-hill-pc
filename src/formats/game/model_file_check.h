// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The check of a model file (.ILM, .PLM) a mod puts in place of a disc file, on the bytes as they are in the
// file, before anything follows an offset in them (the section on models and textures). The
// game reads the skeleton and the parts from such a file, so a broken one must be refused while the disc file can still
// be used instead.
//
// The rules keep the numbers the log has always printed (V1..V13; there is no V10):
//   V1  the header: magic, version 6, not yet converted
//   V2  every section lies inside the file, and the arrays of a mesh start on a 4-byte boundary
//   V3  a skeletal model has at most 56 parts
//   V4  every entry of the part order names a part
//   V5  a part's vertices fit the shared window of 256 slots (255 for a skeletal model: slot 255 marks a
//       triangle in a primitive's fourth corner)
//   V6  a part's normals fit the window of 256 slots
//   V7  a part's shade values fit the window
//   V8  the shade values the normals ask for fit the window
//   V9  the vertex, normal and shade arrays leave room behind them for readers that step in groups
//   V11 a skeletal model's primitives read only slots that a part drawn before or with them wrote
//   V12 a part of the far kind starts at slot 0
//   V13 a part named after a bone names a bone the animation has (when the bone count is known)

#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace sh::formats::game {

struct ModelFileRules {
    // Bytes that can be read past the end of the file in the buffer it is loaded into (V9). 0 checks the file on
    // its own, as an authoring tool would.
    std::uint32_t readable_tail = 0;
    // Bones of the animation the model is played with; below 1 = not known, V13 is skipped.
    int animation_bones = -1;
    // A character: V3, the tighter V5 and V11 apply. Held items and props take corners from the model that
    // carries them, so those rules do not hold for them.
    bool skeletal = false;
};

// Every model the game owns its own buffer for adds this many readable bytes behind the file.
inline constexpr std::uint32_t kModelBufferTail = 16;

struct ModelFileIssue {
    int rule = 0;      // the V number
    std::string text;  // one sentence, starts with the rule's name
};

// The first broken rule, in the order of the rule numbers, or nothing for a file the game can read.
std::optional<ModelFileIssue> check_model_file(std::span<const std::uint8_t> file, const ModelFileRules& rules);

}  // namespace sh::formats::game
