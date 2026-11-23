/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The keyed text of the game, gamedata/text/<lang>/ and the mods' text/<lang>/, behind plain
 * calls: the store is owned by text_bridge.cpp. Strings returned stay valid until the language changes
 * (Pc_TextSetLang with a different code) or the mods are reloaded; the English layers never change. Text comes
 * back in reading order; i18n/localization.h makes it ready for the drawer. */
#ifndef I18N_TEXT_BRIDGE_H
#define I18N_TEXT_BRIDGE_H

#ifdef __cplusplus
extern "C" {
#endif

/* Layers, lowest priority first: base English (gamedata/text/en: the game's own messages and item text with
 * the index tables of the message numbers, and the interface text), the mods'
 * text/en, the selected language (gamedata/text/<code>), the mods' text/<code>. */
#define PC_TEXT_BASE      0x1
#define PC_TEXT_EN_MODS   0x2
#define PC_TEXT_LANG      0x4
#define PC_TEXT_LANG_MODS 0x8
#define PC_TEXT_EN        (PC_TEXT_BASE | PC_TEXT_EN_MODS)
#define PC_TEXT_UPPER     (PC_TEXT_LANG | PC_TEXT_LANG_MODS)
#define PC_TEXT_ALL       0xF

/* Selects the language whose layers sit above English ("en", NULL or "" = none).
 * The English layers load on first use; a change of code reloads the upper two. */
void Pc_TextSetLang(const char* code);
const char* Pc_TextLang(void);

/* `mods reload`: reads every layer again (the mods' text/ as the active mods have it
 * now), keeping the language and the map. Strings returned before stay valid: the old
 * store is kept, so a line on screen is not cut. Returns the number of mods with layers. */
int Pc_TextReload(void);

/* The map the message calls below resolve keys against ("map0_s01"). */
void        Pc_TextSetMap(const char* map);

/* Message slots the base index gives the map (15 common + its own), 0 when the
 * base has no table for it. These are the slots the compiled table has too. */
int Pc_TextMapCount(const char* map);
/* The same plus the messages mods add after the base table (new slots). */
int Pc_TextMapTotal(const char* map);

/* Engine-format string of message `slot` of `map` from the given layers, or
 * NULL when none of them has the slot's key. A slot a mod added falls back to
 * the English layers, since no disc or pack has text for it. */
const char* Pc_TextMapMsg(const char* map, int slot, int layers);

/* The message slot of `key` in the current map (or the common block), -1 if none. */
int Pc_TextMsgIndex(const char* key);

/* Any key, current language first, then English: readable / engine format. NULL if absent. */
const char* Pc_TextGet(const char* key);
/* The winner among `layers` only: readable (engine = 0) or engine format. NULL if absent. */
const char* Pc_TextGetIn(const char* key, int layers, int engine);

/* Game-font text by the literal the game draws: the key(s) whose base English is `literal` (compared without \x01
 * kerning and outer padding, '=' as '-': menu.* keys, and ui.* keys for a literal no menu.* key has), and for them
 * the text of the selected language, else the English of the mods, in the drawer's bytes. NULL when no key carries
 * the literal or nothing replaces it, so the literal's own bytes (kerning included) stay what is drawn. */
const char* Pc_TextMenuText(const char* literal);

/* The same for overlay text: the ui.* key whose base English is `en`, and its readable text in the selected
 * language, else the English of the mods. NULL when none or unchanged. */
const char* Pc_TextUiText(const char* en);

/* Item text of the English layers by slot (item id - 32): `desc` 0 name, 1 description. */
const char* Pc_TextItemEnglish(int itemIdx, int desc);
/* The same from the given layers only (PC_TEXT_BASE: the shipped English, the retail US text). */
const char* Pc_TextItemIn(int itemIdx, int desc, int layers);

/* The text directories languages are looked for in: gamedata/text, then every mod's text/ in load order (a later
 * one wins a language code). Returns the count. */
int Pc_TextEachDir(void (*fn)(const char* dir, void* user), void* user);

/* How many keys of the selected language's own layers (PC_TEXT_UPPER) start with `prefix` ("menu.", "ui.", "item.",
 * "map1_s00."); with a NULL prefix, all of them. */
int Pc_TextCountKeys(const char* prefix);

#ifdef __cplusplus
}
#endif

#endif /* I18N_TEXT_BRIDGE_H */
