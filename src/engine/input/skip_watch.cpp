// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/input/skip_watch.h"

#include <atomic>
#include <chrono>

namespace sh::input {
namespace {

std::atomic<long long> g_asked_ms{-1};

long long now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

}  // namespace

void note_skip_asked() { g_asked_ms.store(now_ms(), std::memory_order_relaxed); }

bool skip_asked_within(int ms) {
    const long long at = g_asked_ms.load(std::memory_order_relaxed);
    return at >= 0 && now_ms() - at <= ms;
}

}  // namespace sh::input
