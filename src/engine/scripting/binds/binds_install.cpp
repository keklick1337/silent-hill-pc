// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/scripting/binds/binds.h"
#include "engine/script/sh_script.h"

namespace sh::script::binds {

void install(ShScriptGameApi& api) {
    static bool registered = false;
    if (!registered) {
        registered = true;
        for (const auto families : {game_binds(), music_binds(), world_binds(), chara_binds(), math_binds(),
                                    ending_binds(), panel_binds()}) {
            named::add(families);
        }
    }
    named::install(api);
}

}  // namespace sh::script::binds

extern "C" void ShScriptBinds_Install(ShScriptGameApi* api) { sh::script::binds::install(*api); }

// Last binder: the named binds of the map scripts go first in the named chain (registration order: sh_script.h).
static ShScriptGameBinderEntry s_ScriptBinder(1000, ShScriptBinds_Install);
