// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
#include "engine/cutscene/cutscene_director.h"

namespace sh::cutscene {

void Director::assign(SlotKey slot, std::string game_path) {
    slots_[slot].pending = std::move(game_path);
}

bool Director::start(SlotKey slot, std::string* error) {
    auto it = slots_.find(slot);
    if (it == slots_.end() || it->second.pending.empty()) {
        return it != slots_.end() && it->second.cutscene != nullptr;
    }
    Slot& s = it->second;
    std::string path = std::move(s.pending);
    s.pending.clear();
    auto loaded = loader_ ? loader_(path) : Result<Cutscene>::fail("no cutscene loader");
    if (!loaded) {
        if (error) {
            *error = loaded.error();
        }
        s.cutscene.reset();
        s.path.clear();
        return false;
    }
    s.path = std::move(path);
    s.cutscene = std::make_shared<const Cutscene>(std::move(loaded.value()));
    last_started_ = s.cutscene;
    return true;
}

void Director::reset() {
    slots_.clear();
    last_started_.reset();
}

const Cutscene* Director::playing(SlotKey slot) const {
    if (auto it = slots_.find(slot); it != slots_.end() && it->second.cutscene) {
        return it->second.cutscene.get();
    }
    return last_started_.get();
}

const std::string* Director::assigned_path(SlotKey slot) const {
    auto it = slots_.find(slot);
    if (it == slots_.end()) {
        return nullptr;
    }
    return it->second.pending.empty() ? &it->second.path : &it->second.pending;
}

}  // namespace sh::cutscene
