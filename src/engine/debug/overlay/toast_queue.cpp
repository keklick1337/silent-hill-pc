// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/debug/overlay/toast_queue.h"

namespace sh::debug {

void ToastQueue::push(std::string_view line, std::uint32_t now_ms) {
    if (lines_.empty() || now_ms - last_ms_ > kHoldMs) {
        lines_.clear();
    } else if (lines_.size() >= kMaxLines) {
        lines_.erase(lines_.begin());
    }
    lines_.emplace_back(line.substr(0, kColumns - 1));
    last_ms_ = now_ms;
}

int ToastQueue::alpha(std::uint32_t now_ms) const {
    if (lines_.empty()) return 0;
    const std::uint32_t age = now_ms - last_ms_;
    if (age < kFadeInMs) return static_cast<int>(255u * age / kFadeInMs);
    if (age < kHoldMs) return 255;
    if (age < kHoldMs + kFadeOutMs) return 255 - static_cast<int>(255u * (age - kHoldMs) / kFadeOutMs);
    return 0;
}

}  // namespace sh::debug
