// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// sh.mods.*: the installed mods as the Mods screen (gamedata/menus/mods.lua) shows and edits them, over
// sh::mods::admin_registry() (engine/mods/mods_admin.h).
//
//   sh.mods.list()               -> { {id, name, version, description, authors = {...}, homepage,
//                                      state = "active"|"disabled"|"error", enabled, load_index, running,
//                                      problems = {...}, warnings = {...}, gameplay, scripts,
//                                      files_replaced, files_added, files_overridden}, ... }
//                                   in the pending order; state / enabled / problems are the pending choice,
//                                   load_index (1-based, 0 = not loaded) and the file counts the running set
//   sh.mods.set_enabled(id, on)  -> true when the mod exists (pending only; save() writes it)
//   sh.mods.move(id, delta)      -> true when it moved
//   sh.mods.save()               -> true | false, error   (mods/load_order.json)
//   sh.mods.restart_pending()    -> true when the saved choice loads another set or order
//   sh.mods.reload()             -> { "line", ... } what the hot reload did
//   sh.mods.dir()                -> the folder mods go into (<exe>/mods), "" when none
//   sh.mods.import_can(kind)     -> true when the system has a dialog for kind "folder" | "zip"
//   sh.mods.import_pick(kind)    -> true when the dialog opened (it answers later: import_poll)
//   sh.mods.import_poll()        -> state "idle"|"picking"|"ready", changed: every frame of the import page; a
//                                   picked path (or one dropped on the window meanwhile) is scanned here
//   sh.mods.import_scan(path)    -> state: scans a folder or .zip now
//   sh.mods.import_found()       -> {source, zip, error, detail, mods = {{index, id, name, version, status =
//                                      "new"|"update"|"same"|"older"|"invalid", reason, detail, installed_version,
//                                      notes = {{kind, detail}}, files, bytes, selected}, ...}}
//   sh.mods.import_select(i, on) -> true when mod i can be chosen
//   sh.mods.import_run()         -> true, {added = {ids}, replaced = {ids}} | false, error
//   sh.mods.import_last()        -> the last run's report or nil, its error ("" none)
//   sh.mods.import_clear()       -> forgets the scan and the last report
#pragma once

struct lua_State;

namespace sh::script {

// Sets field "mods" of the table on top of the stack (the `sh` table) to the sh.mods library.
void open_mods_api(lua_State* L);

}  // namespace sh::script
