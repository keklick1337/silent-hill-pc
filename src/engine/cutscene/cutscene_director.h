// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Which cutscene is playing where. The game names a cutscene by the file it loads and then refers to it by a
// slot (an opaque key: the address of the load buffer the script chose). The director keeps, per slot,
// the cutscene that was assigned (loaded) and the one that was started; starting a slot makes the assigned
// cutscene current there. Sampling a slot with no current cutscene uses the cutscene started last, as the
// original did when a script sampled a buffer that a later load had already reused.
#pragma once

#include "engine/cutscene/cutscene.h"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>

namespace sh::cutscene {

using SlotKey = std::uintptr_t;

// Loads a cutscene by game path ("ANIM/CAFE.DMS"); a failure is reported once per start and leaves the slot empty.
using CutsceneLoader = std::function<Result<Cutscene>(const std::string& game_path)>;

class Director {
public:
    explicit Director(CutsceneLoader loader) : loader_(std::move(loader)) {}

    // A load of `game_path` into `slot` finished (or was queued): the next start() of the slot plays it.
    void assign(SlotKey slot, std::string game_path);
    // Makes the assigned cutscene current in `slot`. Without a new assignment the slot keeps what it plays.
    // Returns false when the assigned file does not load (`error` says why).
    bool start(SlotKey slot, std::string* error = nullptr);
    // Forgets everything (new game, tests).
    void reset();

    // The cutscene sampled for `slot`: its own, else the one started last, else null.
    const Cutscene* playing(SlotKey slot) const;
    const std::string* assigned_path(SlotKey slot) const;

private:
    struct Slot {
        std::string pending;  // game path assigned and not started yet
        std::string path;     // game path of `cutscene`
        std::shared_ptr<const Cutscene> cutscene;
    };
    CutsceneLoader loader_;
    std::map<SlotKey, Slot> slots_;
    std::shared_ptr<const Cutscene> last_started_;
};

}  // namespace sh::cutscene
