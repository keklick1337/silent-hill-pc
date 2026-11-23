// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Bone model index stream and draw ordering of the skeleton path. Pure data walks, no game types.

#pragma once

#include <cstddef>
#include <cstdint>

namespace sh::world {

inline constexpr std::int8_t kBoneStreamMulti = -3;  // "range of models" marker
inline constexpr std::int8_t kBoneStreamEnd = -2;    // end of the stream

// A byte stream of model indices: a plain byte is a single model index; the
// marker -3 followed by a count N expands to the running index 0..N-2 (the
// running index continues from the previous value, as the original kept it
// between streams — it is deliberately not reset by `reset`).
class BoneModelStream {
public:
    // Start reading `bytes`; the running index is left as it was.
    void reset(const std::int8_t* bytes) { cur_ = bytes; }
    // Next model index (kBoneStreamEnd when exhausted by a stream that ends with -2).
    std::int8_t next();

private:
    const std::int8_t* cur_ = nullptr;
    std::int8_t run_ = 0;
};

// Draw order of bones: for every model in `model_order` (in that order) the
// bones whose model index equals it, in bone order. Writes bone positions to
// `out` (capacity bone_count) and returns how many were written.
std::size_t bone_link_order(const std::uint8_t* model_order, std::size_t model_count,
                            const std::int32_t* bone_model_idx, std::size_t bone_count,
                            std::uint32_t* out);

}  // namespace sh::world
