// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The registry of game API binders (sh_script.h, ShScript_RegisterGameBinder): the one entry point
// through which the engine's own binds and the game's remaining bridges fill ShScriptGameApi.

#include "engine/script/sh_script.h"

#include <algorithm>
#include <vector>

namespace {

struct Binder {
    int order;
    ShScriptGameBinder fn;
};

// Function-local: binders register from static constructors of any translation unit.
std::vector<Binder>& binders() {
    static std::vector<Binder> list;
    return list;
}

}  // namespace

extern "C" void ShScript_RegisterGameBinder(int order, ShScriptGameBinder binder) {
    if (binder != nullptr) {
        binders().push_back({order, binder});
    }
}

extern "C" void ShScript_InstallGameApi(void) {
    std::vector<Binder> list = binders();
    std::stable_sort(list.begin(), list.end(), [](const Binder& a, const Binder& b) { return a.order < b.order; });
    ShScriptGameApi api{};
    for (const Binder& b : list) {
        b.fn(&api);
    }
    ShScript_SetGameApi(&api);
}
