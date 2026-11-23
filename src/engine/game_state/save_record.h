// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The session's progress records (Session::game.save and .autosave, progress_record.h).

#pragma once

#include "engine/game_state/progress_record.h"
#include "engine/game_state/session.h"

namespace sh::game {

// The live record and the automatic copy a death returns to.
inline SaveRecord& save_record() { return session().game.save; }
inline SaveRecord& autosave_record() { return session().game.autosave; }

}  // namespace sh::game
