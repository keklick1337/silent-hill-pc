// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/save/boot_check.h"

#include <utility>

namespace sh::save {

BootCheck::BootCheck(SaveCatalog& catalog, OptionsApplier apply, int check_polls, int load_polls)
    : catalog_(catalog), apply_(std::move(apply)), check_polls_(check_polls < 0 ? 0 : check_polls),
      load_polls_(load_polls < 0 ? 0 : load_polls) {}

BootCheck::Outcome BootCheck::check() {
    if (checks_ < check_polls_) {
        ++checks_;
        return Outcome::Pending;
    }
    // The list is read afresh: the check is the one place that must not use a count from before the boot.
    catalog_.invalidate();
    catalog_.poll();
    return catalog_.any() ? Outcome::HasSave : Outcome::NoSave;
}

void BootCheck::begin_load() {
    loads_ = 0;
    applied_ = false;
}

bool BootCheck::poll_load() {
    if (loads_ < load_polls_) {
        ++loads_;
        return false;
    }
    const std::int32_t newest = catalog_.newest();
    applied_ = newest > 0 && apply_ && apply_(newest);
    return true;
}

}  // namespace sh::save
