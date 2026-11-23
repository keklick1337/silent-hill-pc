// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The numbers the original game knows its things by. gamedata/ and
// saves name everything; the numbers are needed only where the original's own layout addresses
// things by number: the game code (inventory bytes, event flag words, e_SfxId) and the
// saves of the original (version 1 files, PS1 memory cards). They come from the data, by place,
// at load: the n-th name of the "event_flags" list of original_order.json is save bit n (from 1;
// gamedata/flags/*.json declare the flags themselves), the n-th page of paper_maps.json
// is page n, the n-th bank tone of sounds/sounds.json is 1281 + n and an XA line is 4096 + its
// "xa_item"; original_order.json lists items and weapons in groups of 32, characters and common
// pickups in one list each. A new thing of a mod gets a free number at run time (the same one for
// the same set of mods) and is stored nowhere: an item a free byte (its group's first, else any), a
// weapon (melee or gun) a number whose attack rows lie past the original's table.
//
// The tables load on first use from the directory configure() gave (else <exe dir>/gamedata, the
// working directory's gamedata, the source tree's), so tools and tests need no setup.

#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace sh::ids {

// Rule 1 of the short names: while a scope lives on this thread, the lookups below try a
// short name in `ns` first ("Lantern" -> "mymod:Lantern"), then as usual. The script layer opens one for the calls
// of a mod's scripts; scopes nest, an empty namespace switches the rule off.
class NamespaceScope {
public:
    explicit NamespaceScope(std::string_view ns);
    ~NamespaceScope();
    NamespaceScope(const NamespaceScope&) = delete;
    NamespaceScope& operator=(const NamespaceScope&) = delete;

private:
    std::string previous_;
};

// Where the base data lives and where the mods are (mods/<mod>/flags.json and flags/*.json add flags). Reloads.
void configure(const std::filesystem::path& gamedata_dir, const std::filesystem::path& mods_dir);
// `mods reload`: reads the base tables and the flag declarations again and keeps every number already handed out
// (a mod's items, weapons with their attack rows, characters and flags), so inventory slots, the session and the
// save keep meaning the same things; only things new to the data get new numbers.
void refresh();

// Every lookup takes a full or short name and answers -1 / nullptr when unknown.
int item_number(const char* name);
const char* item_name(int number);  // full id ("silenthill:HealthDrink")
int common_pickup_number(const char* name);
int weapon_number(const char* name);
int chara_number(const char* name);

// A mod's weapon (melee or gun) takes a number whose packed attacks (number + 10 * input) are rows past the
// original's attack table. Harry's code decodes the original's packed attacks by arithmetic (below 32 melee, the
// weapon as attack % 10); these answer for the rows a mod's weapon took and -1 / false for every other attack.
int mod_attack_weapon(int packed_attack);  // the mod's weapon number owning the row
int mod_attack_input(int packed_attack);   // 0 tap, 1 hold, 2 multitap
// Whether a packed attack is a gun's: a mod's row by its weapon's class, else the original's rule (handgun's tap on).
bool attack_is_gun(int packed_attack);
// The weapon of a packed attack: a mod's row's owner, else the original's decoding (melee number % 10, a gun's own).
int attack_weapon(int packed_attack);

int flag_number(const char* name);  // save bit; former names (aliases) resolve too
const char* flag_name(int bit);     // full id, nullptr for a bit without a name
// A name a save written before a rename may hold that names another flag now ("old_save_names" of
// gamedata/flags): the full id of the flag it meant there; nullptr for any other name.
const char* flag_name_in_old_save(const char* name);
// The flag only marks the paper map (gamedata/flags/paper_map_marks.json): the event that sets it may run again.
bool flag_is_paper_map_mark(int bit);

int sound_number(const char* name);  // e_SfxId of a base game's sound

// The flag that makes a mod's "pickup_once" item count once ("mymod:Lantern" -> "mymod:Lantern_PickedOnce"). It is
// declared for every such item of the active mods; flag_number() of it is -1 for any other item.
std::string pickup_once_flag_name(std::string_view item_id);

int paper_map_number(const char* name);
const char* paper_map_name(int number);

// Installs the numbering into the libraries that load named data (items, characters, sounds, map
// data import). Done before main() wherever this file is linked; tools call it explicitly.
void install_game_numbering();

}  // namespace sh::ids
