// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/save/save_catalog.h"

#include <utility>

namespace sh::save {

SaveCatalog::SaveCatalog(Lister lister, int refresh_ticks)
    : lister_(std::move(lister)), refresh_ticks_(refresh_ticks < 1 ? 1 : refresh_ticks) {}

void SaveCatalog::poll() {
    if (--due_ > 0) {
        return;
    }
    due_ = refresh_ticks_;
    int n = lister_ ? lister_(numbers_, kMaxListed) : 0;
    if (n < 0) {
        n = 0;
    }
    count_ = n < kMaxListed ? n : kMaxListed;
}

}  // namespace sh::save
