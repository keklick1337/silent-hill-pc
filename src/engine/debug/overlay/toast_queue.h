// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The message toast: the newest feedback lines (a cheat switched, a setting changed) shown for a moment in the
// corner while the console is closed. Lines that arrive close together stack, up to a few; a line after a quiet
// spell starts a new stack. Time comes from the caller in milliseconds, so the queue runs in unit tests.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace sh::debug {

class ToastQueue {
public:
    static constexpr std::size_t kMaxLines = 4;
    static constexpr std::size_t kColumns = 64;      // the panel's width; a line keeps one column less
    static constexpr std::uint32_t kFadeInMs = 120;
    static constexpr std::uint32_t kHoldMs = 1800;   // also the quiet spell after which a new stack starts
    static constexpr std::uint32_t kFadeOutMs = 600;

    void push(std::string_view line, std::uint32_t now_ms);

    // Opacity 0..255 of the stack at `now_ms`; 0 when there is nothing to draw.
    int alpha(std::uint32_t now_ms) const;

    const std::vector<std::string>& lines() const { return lines_; }

private:
    std::vector<std::string> lines_;
    std::uint32_t last_ms_ = 0;
};

}  // namespace sh::debug
