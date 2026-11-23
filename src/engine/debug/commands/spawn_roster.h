// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The characters a developer can put into the world: every folder of the character library (base and mods)
// except the player, listed for the console's `spawn` and for the debug menu's spawn page, with the calls that
// place one in front of Harry. A character the map did not load is loaded on demand.

#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace sh::debug {

struct SpawnEntry {
    std::string name;      // the character's folder: what `spawn` takes
    std::string id;        // "silenthill:Name", "<mod>:Name"
    std::string category;  // the spawn menu's category (character.json "category"; "Mods" for a mod's own)
    int chara = 0;         // the type's number in this run
    int state = 0;         // first behaviour step of a spawn (character.json "spawn.state")
    bool spawnable = false;  // the model and animation loaders know the type
    bool has_ai = false;     // the folder carries a behavior.lua
};

class SpawnRoster {
public:
    // The list as the library has it now; rebuilt on every call (a mod set may have been loaded since).
    const std::vector<SpawnEntry>& entries();
    // By folder name or full id, any case; null when unknown. Valid until the next entries() / find().
    const SpawnEntry* find(std::string_view name);

    // The debug menu addresses entries by their place in the list.
    int count();
    const char* name(int index);
    bool spawnable(int index);
    const char* category(int index);
    // What the entry runs on the current map with the menu's AI: "map", "own", "fallback" or "none".
    const char* ai_run(int index);
    // The categories in use (base ones, other, "Mods" last).
    int category_count() const;
    const char* category_name(int index);

    // The AI the menu's spawns ask for (a PcSpawnAi; the console passes its own).
    int menu_ai() const { return menu_ai_; }
    void set_menu_ai(int ai) { menu_ai_ = ai >= 0 && ai <= 3 ? ai : 0; }

    // True once the entry's model and animations are resident.
    bool resident(const SpawnEntry& entry) const;
    bool make_resident(const SpawnEntry& entry);
    // One character `metres` in front of Harry, `turn_q12` off his heading, facing him. `ai` < 0 asks for the
    // menu's AI. Returns its slot (1..) or -1.
    int spawn(const SpawnEntry& entry, int state, int metres, int turn_q12, int ai = -1);
    // The menu's spawn action: entry `index` at 1..20 metres; state < 0 is the entry's own.
    void spawn_at(int index, int metres, int state);

    // The preview: the real character is created in front of Harry and, a few ticks later (its behaviour has
    // posed it by then), held still. keep leaves it as a normal spawn; cancel removes it.
    bool preview_show(int index, int metres, int state);
    bool preview_keep();
    void preview_cancel();
    void preview_tick();

private:
    std::vector<SpawnEntry> entries_;
    std::string category_name_;
    int menu_ai_ = 0;
    int preview_slot_ = -1;
    int preview_chara_ = 0;
    int preview_ticks_ = 0;
};

SpawnRoster& spawn_roster();

}  // namespace sh::debug
