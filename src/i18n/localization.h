// SPDX-License-Identifier: GPL-3.0-or-later
// The game's side of localization: which languages exist and which is in force, the text of
// the game in that language, and how a string gets from the data to the drawer.
//
// Every text is data: gamedata/text/<language>/ and the mods' text/<language>/ (the English of the game itself is
// the player's disc text, i18n/text_bridge.h). A language exists because one of those directories holds a
// descriptor, lang.json; nothing here lists languages.
//
// Strings come back in reading order. The one place that turns them into what the left-to-right drawer needs
// (shaping, visual order, mirrored brackets) is shown(), which the text drawer's host calls for each string; the
// measuring calls below do the same, so a width is the width of what is drawn. Map messages are made ready when a
// map's table is installed, line wrapping included.
#pragma once

#include "i18n/core/language_set.h"

#include <cstdint>
#include <string>
#include <vector>

namespace sh::i18n {

// Finds the languages, selects the configured one and loads the font. Once, when the game data is known.
void init();
// The mods changed: text, languages and fonts are read again and the loaded map's messages reinstalled.
void reload();

// ---- languages ----
const std::vector<Language>& languages();
int language_index();          // of the language in force, in languages()
const Language& language();    // the language in force
// Switches the language: the config key `language` is saved, the font's preferred families follow, the loaded
// map's messages are installed again. False when no language has that index.
bool set_language(int index);
bool right_to_left();
// Counts up when the language changes, so a screen can tell its cached text is stale.
unsigned language_generation();
// The name of language `index` in that language, in the game font's bytes ('_' for a space), for the Language
// option; drawn as data, not looked up.
std::string language_label(int index);
// The language can be changed where the original options allowed it: in the options opened from the title.
bool language_switch_allowed();

// ---- text drawn with the game font ----
// The text of a literal the game draws ("Brightness_Level"): the language's text for the key whose English it is,
// the English of the mods, or the literal itself.
const char* menu_text(const char* literal);
// The text of an interface key ("ui.dialog.yes") in the drawer's bytes; the key itself when no layer has it.
const char* ui_menu_text(const char* key);

// How the next strings handed to shown() are treated.
enum class DrawMode : std::uint8_t {
    Translate,  // a literal of the game: looked up with menu_text, then made ready
    Verbatim,   // text that is data (a file name, a translated string): made ready as it is
    Ready,      // already in drawing order: drawn as it is
};
class ScopedDrawMode {
public:
    explicit ScopedDrawMode(DrawMode mode);
    ~ScopedDrawMode();
    ScopedDrawMode(const ScopedDrawMode&) = delete;
    ScopedDrawMode& operator=(const ScopedDrawMode&) = delete;

private:
    DrawMode before_;
};
DrawMode draw_mode();
// The string the drawer walks for `text` under the mode in force. Valid until the next call.
const char* shown(const char* text);
// `text` (in reading order) made ready whatever the mode; the same string when nothing changes.
const char* ready(const char* text);

// Pixel width of the first line of `text`, and the glyph budget the drawer needs to draw all of it; both for the
// string as it is drawn (text is in reading order and is not looked up).
int text_width(const char* text);
int glyph_count(const char* text);

// ---- text for the TrueType overlays (UTF-8) ----
// The text of an interface key, in reading order; the key itself when no layer has it.
const char* ui_text(const char* key);
// The text of the interface key whose English is `english` (screens that name their text by it); the
// English itself when no key has it.
const char* ui_text_of(const char* english);
// ui_text_of with one {placeholder} replaced by `value`. Valid until a few calls later.
const char* ui_text_filled(const char* english, const char* placeholder, const char* value);

// ---- items ----
// Name and description of the item in inventory slot `index` (item number - 32), null when it has none.
const char* item_name(int index);
const char* item_description(int index);

// ---- map messages ----
// Installs the message table of the loaded map `map_index` in the language in force: every slot's text made ready
// for the message box (wrapped to the box, ordered, shaped).
void install_map_messages(int map_index);
// The name a message goes by in the mods' voice files, "MAP1_S00.23" (the map, the slot) and "COMMON.5" for the
// block every map shares; a message a mod added (to the map or as a message of no map) goes by its key
// ("mymod:hello"). Empty when the map index carries no text.
std::string message_voice_name(int map_index, int slot);

// ---- diagnostics ----
// Prints the languages, the layers of the one in force and the font. The `langcheck` console command.
void describe(void (*out)(const char*, ...));

}  // namespace sh::i18n
