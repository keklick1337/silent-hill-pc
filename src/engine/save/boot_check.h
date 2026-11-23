// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
#pragma once

// What the boot asks of the saves before the title: is there a save, and the
// options of the newest one come back into the game. Works on the store's list through a SaveCatalog; there is no
// card to wait for. `check_polls` and `load_polls` keep the number of waits the original's card driver cost (the boot
// timing and the replay dumps depend on it); 0 makes both instant.

#include "engine/save/save_catalog.h"

#include <cstdint>
#include <functional>

namespace sh::save {

class BootCheck {
public:
    enum class Outcome { Pending, NoSave, HasSave };

    // Puts the options record of save `number` into the game; false when it has none the game accepts.
    using OptionsApplier = std::function<bool(std::int32_t number)>;

    BootCheck(SaveCatalog& catalog, OptionsApplier apply, int check_polls, int load_polls);

    // One wait of the boot's card check. Pending until the check is done, then NoSave or HasSave from the list.
    Outcome check();

    // Starts reading the options of the newest save; poll_load() is called once per wait until it returns true.
    void begin_load();
    bool poll_load();
    // The newest save's options reached the game (valid when poll_load() has returned true).
    bool options_applied() const { return applied_; }

private:
    SaveCatalog&   catalog_;
    OptionsApplier apply_;
    int            check_polls_;
    int            load_polls_;
    int            checks_ = 0;
    int            loads_ = 0;
    bool           applied_ = false;
};

}  // namespace sh::save
