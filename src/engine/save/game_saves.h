// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The game's saves: the store of .savegames/ driven with the live progress of the
// session. A save takes the progress record and the settings of sh::game::session() (the game block), a load puts
// a save's progress back there and the settings of the newest save; names come from sh::ids and the host's maps.
// The save screen (ui/screens/save_screen.cpp), the title and the boot logos call this; what the game does around
// a save (script hooks, the blood colour mirror, map names) is the host's.

#pragma once

#include "engine/save/boot_check.h"
#include "engine/save/save_catalog.h"
#include "engine/save/saved_mods.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace sh::save {

class GameSavesHost {
public:
    virtual ~GameSavesHost() = default;
    // Map registry: name of a map number (nullptr: none), number of a name (-1: unknown).
    virtual const char* map_name(int number) = 0;
    virtual int map_number(const char* name) = 0;
    // Script hooks: before a save is written, after a load put a save in place.
    virtual void saving(int number) { (void)number; }
    virtual void loaded(int number) { (void)number; }
    // The same around an instant quick save (`index`: the quick slot, 0-based).
    virtual void quick_saving(int index) { (void)index; }
    virtual void quick_loaded(int index) { (void)index; }
    // A save file outside the lists was loaded (load_file).
    virtual void file_loaded() {}
    // What `persistent_remains` keeps (JSON of the RMNS chunk; empty: nothing), and a loaded save's (empty: none).
    virtual std::string remains_text() { return {}; }
    virtual void remains_loaded(const char* text) { (void)text; }
    // The mods a save records it was made with (the run's active mods); nothing: the save does not say.
    virtual std::optional<std::vector<SavedMod>> active_mods() { return std::nullopt; }
    // The blood colour the loaded settings hold (it re-palettes effects loaded later).
    virtual void blood_color_loaded(std::uint8_t color) { (void)color; }
};

class GameSaves {
public:
    // The host stays installed for the process (static storage).
    void set_host(GameSavesHost* host) { host_ = host; }
    GameSavesHost* host() const { return host_; }

    // Once: the store under `exe_dir` (`setting`: config savegame_dir), the names, then the saves of older builds
    // (card files in gamedata/save under the working directory, the exe directory and `gamedata_dir`).
    void open(const char* exe_dir, const char* setting, const char* gamedata_dir);

    // The save list the title and the pause read (SaveCatalog).
    void poll_catalog() { catalog_.poll(); }
    int catalog_count() const { return catalog_.count(); }

    // The boot's check and the options of the newest save (BootCheck): 0 pending, 1 no save, 2 a save.
    int boot_check();
    void boot_begin_load() { boot_.begin_load(); }
    bool boot_poll_load();

    // Writes the session's progress (number <= 0: a new save); returns the number written or -1. The save count
    // goes up first, except at the Next Fear save point; a written save becomes the automatic copy.
    int save(int number);
    // The save's progress into the session (the session is untouched unless it returns 1), the settings of the
    // newest save with it. 0 nothing there, -1 damaged or a map this build lacks.
    int load(int number);

    // Instant quick saves (quick_save_instant): the session's progress and settings into the
    // quick slot `index` (0-based) with no list and no prompt; 1 written, -1 failed. Counts as a save like save().
    int quick_save(int index);
    // The quick slot's progress into the session (untouched unless it returns 1); the settings stay as they are.
    // 0 nothing there, -1 damaged or a map this build lacks.
    int quick_load(int index);
    // 1 when the quick slot holds a save that reads, 0 empty, -1 damaged.
    static int quick_state(int index);

    // A state dump's save (`dump`): the session's progress into a save file at `path` as a quick save
    // would write it, but the running game does not count a save; 1 written, -1 failed.
    int save_file(const std::string& path);
    // That file's progress into the session, as quick_load() (same results).
    int load_file(const std::string& path);
    static int file_state(const std::string& path);

    // FNV-1a of the progress record: the journal shows it at every save and load.
    static std::uint32_t fingerprint();

private:
    bool apply_options(std::int32_t number);

    GameSavesHost* host_ = nullptr;
    bool opened_ = false;
    SaveCatalog catalog_{[](std::int32_t* numbers, int capacity) { return list_newest(numbers, capacity); }};
    // The original's card driver took 11 waits to read the options of the newest save (boot timing, replay dumps); the
    // card check itself never waited once the logos had run.
    BootCheck boot_{catalog_, [this](std::int32_t number) { return apply_options(number); }, 0, 9};

    static int list_newest(std::int32_t* numbers, int capacity);
};

GameSaves& game_saves();

}  // namespace sh::save
