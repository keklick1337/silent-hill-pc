// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
#pragma once

// What the title menu and the pause menu ask of the saves: are there any, how many does the list show. Reads the
// store's list directly; no card model, no waits. The list is re-read at most
// once per `refresh_ticks` polls, so a save copied in by hand shows up within a second without a directory scan
// every frame.

#include <cstdint>
#include <functional>

namespace sh::save {

class SaveCatalog {
public:
    // Fills `numbers` with the readable saves, newest first, at most `capacity`; returns how many there are.
    using Lister = std::function<int(std::int32_t* numbers, int capacity)>;

    // The list never shows more than this many saves to the menus (the original's card file held 11 entries).
    static constexpr int kMaxListed = 11;

    explicit SaveCatalog(Lister lister, int refresh_ticks = 30);

    // One tick: re-reads the list when it is due (always on the first tick).
    void poll();
    // Forces the next poll to re-read (after a save, a delete or a load).
    void invalidate() { due_ = 0; }

    int count() const { return count_; }
    bool any() const { return count_ > 0; }
    // Number of the newest save, 0 when there is none.
    std::int32_t newest() const { return count_ > 0 ? numbers_[0] : 0; }

private:
    Lister       lister_;
    int          refresh_ticks_;
    int          due_ = 0;
    int          count_ = 0;
    std::int32_t numbers_[kMaxListed] = {};
};

}  // namespace sh::save
