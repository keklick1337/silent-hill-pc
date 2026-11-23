// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The soft reset rules as data: the held combination and its time, the
// press combinations, the demo time-out, and the states in which no reset may happen at all (boot, a save
// being read or written). The watch itself is sh::loop::ResetWatch.
//
// Numbers are gamedata/config/soft_reset.json; a mod replaces the file whole.

#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include "engine/loop/reset_watch.h"
#include "formats/common/status.h"

namespace sh::gameflow {

struct ResetBlockedSteps {
    std::int32_t game_state = 0;
    std::vector<std::int32_t> steps;  // the state's first step counter
};

struct SoftResetRules {
    sh::loop::ResetSpec spec;
    std::int32_t blocked_below_state = 0;  // every state before this one (boot, logos, intro)
    std::vector<ResetBlockedSteps> blocked_steps;

    static SoftResetRules builtin();
    static Result<SoftResetRules> parse(std::string_view json);

    bool blocked(std::int32_t game_state, std::int32_t step) const;
};

}  // namespace sh::gameflow
