// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/debug/commands/remembered_flags.h"

namespace sh::debug {

void RememberedFlags::remember(int flag, bool value) {
    for (Entry& entry : entries_) {
        if (entry.flag == flag) {
            entry.value = value;
            return;
        }
    }
    if (entries_.size() < kCapacity) entries_.push_back({flag, value});
}

}  // namespace sh::debug
