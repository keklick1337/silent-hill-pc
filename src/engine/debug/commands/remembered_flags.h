// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The event flags the console set by hand (`setflag`, `setending`). A map warp starts a new game and clears
// every flag of the save, so the console keeps what was set and the warp sets it again before play starts:
// "set the ending's flags on the title, then warp to the ending" works. A short fixed list; no game types.

#pragma once

#include <cstddef>
#include <vector>

namespace sh::debug {

class RememberedFlags {
public:
    static constexpr std::size_t kCapacity = 24;

    struct Entry {
        int flag = 0;
        bool value = false;
    };

    // Remembers the flag's value (the newest value of a flag wins); a new flag past the capacity is dropped.
    void remember(int flag, bool value);
    void forget_all() { entries_.clear(); }
    const std::vector<Entry>& entries() const { return entries_; }

private:
    std::vector<Entry> entries_;
};

}  // namespace sh::debug
