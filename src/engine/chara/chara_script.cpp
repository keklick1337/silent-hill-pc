// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/chara/chara_script.h"

namespace sh::chara::detail {

namespace {
ScriptCharaDeps g_deps;
}

void set_script_deps(const ScriptCharaDeps& deps) { g_deps = deps; }

const CharaLibrary* script_library() { return g_deps.library; }

CharaActions* script_actions(int charaId) {
    if (charaId < 0 || g_deps.actions_provider == nullptr) {
        return nullptr;
    }
    return g_deps.actions_provider();
}

}  // namespace sh::chara::detail
