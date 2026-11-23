// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// What the console commands leave running after they return, as the frame loop and the game call it: the
// queued movie and map event, the GPU benchmark, the spawn preview, the scripted-run aids (a held pad, stalled
// updates, a capture of every presented frame) and the event flags that ride along on a map warp.
// The commands themselves are in src/engine/debug/commands.

#pragma once

#include <cstdint>

namespace sh::debug {

// Once per frame from the debug overlay: settles the spawn preview and starts a queued map event when the
// game can take it.
void tick_command_services();

// `fmv`: plays the queued movie once the fade to black has landed, then fades back in.
void play_queued_movie();
// `gpubench`: counts this frame into the running benchmark step.
void gpu_bench_frame();

// `pad`: the buttons a script holds. apply clears their bits in the pad word (0 = pressed).
bool script_pad_held();
void script_pad_apply(std::uint16_t* buttons_up);
// `stall`: sleeps this update when the share says so.
void stall_test_frame();
// `shotseq`: called after every present with a note about the frame (its kind, its time).
void shot_sequence_frame(const char* note);

// `setflag` / `setending`: sets the remembered flags again, after a map warp has cleared the save.
void apply_remembered_flags();

// The name `spawn` uses for a character type (its folder in capitals); null for an unknown type.
const char* spawn_name_of(int chara_id);

}  // namespace sh::debug
