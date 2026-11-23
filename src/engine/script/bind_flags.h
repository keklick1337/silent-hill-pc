// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#pragma once

struct lua_State;

namespace sh::script {

class Host;

// Pushes the flags family into the sh table on top of the stack: flat flag_get, flag_set,
// flag_set_alt and the canonical sh.flag.{get,set,clear,set_alt}; each pair shares one function.
void install_flag_family(lua_State* L, Host* host);

}  // namespace sh::script
