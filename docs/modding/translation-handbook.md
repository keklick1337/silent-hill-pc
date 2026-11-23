# Translating the game text: translator workflow

How to translate the dialogue, messages, notes, item names and descriptions into a new language from the original English text (`gamedata/text/en/`), without using anyone else's translation. File format, markup and font: [assets.md](assets.md#text); the short user guide: [translation.md](translation.md).

## 1. What is translated

| Language files | What | Source |
|---|---|---|
| `common.json` | shared message block (`common.*` keys) | translated from the disc text |
| `maps/<map>.json` | map messages (keys `<map>.*`) | translated from the disc text |
| `items.json` | item names and descriptions (`item.<Name>.name`/`.desc`) | translated from the disc text |
| `menu.json`, `ui.json`, `options_menu.json`, `pause_menu.json`, `debug_menu.json`, `mods_menu.json`, `menu_engine.json`, `controls_*.json`, `setup.json`, `lang.json` | the engine's own interface | not part of this workflow |

Another translation (a fan disc, old language files) may be opened only to see the format and the key list; no sentences are taken from it or paraphrased.

## 2. Where the English is and how to get it

The English game text is in the repository (`gamedata/text/en/common.json`, `items.json`, `maps/*.json`). The import is needed only for the font metrics: `<G>` below is the `gamedata` directory next to the game after the first start, from which `convert/fonts/game.font.json` is read. Never write into `<G>`.

```
# draft template: every key with its English text in draft form (see section 5)
python3 -I tools/translation/build_lang.py xx .tmp/tr/en_template.txt --gamedata <G> --template

# the same as gettext PO, if a PO editor is more convenient
python3 -I tools/scripts/text_po.py export pot -o .tmp/tr/SilentHill.pot
```

Both are work files: keep them in `.tmp/` and do not commit them. For scripts: `tools/scripts/en_text.py` — `english()` returns `key → (file, text, ws)`, `english_keys()` the keys.

Context: the key order in a map file is the story order; who speaks and where shows in `gamedata/maps/<map>/events.lua`; the place of each map is its `description` in `gamedata/game.json`.

## 3. Markup to preserve

Full table in [assets.md](assets.md#message-markup). The game text uses:

| Code | Meaning | Rule |
|---|---|---|
| `{wait N}`, `{wait1 N}`, `{wait2 N}` | the line holds N seconds for the voice (the digit is the sound mode) | at the start, number unchanged |
| `{end}` | end of message; without it the message continues with the next key | at the end, never add or remove |
| `{color N}…{/color}` | colour: 2 item, 5 map, 3 sign or note (without a closing code, to the end) | the word may move within the sentence, the pair stays |
| `{choice 4}`, `{choice 3}` | Yes/No choice; 3 = three options (the next three keys) | at the end |
| `{pos 4}` | centred "grid" window (notes, newspapers, poems) | at the start |
| `{instant}` | the page appears at once, budget 400 glyphs | at the start |
| `{hires}` | HD glyphs (maps) | at the start |
| `{center}` | centred lines | at the start |

Codes, arguments and order must match the English string of the key. Keys without words (`{wait 0.9} `, empty `msgNN`, ` {choice 3}`) are copied as is. `{page}` does not occur in the disc text. The `ws` field is not written to a translation.

Characters: bytes below 0x80 mean the same as in the disc font (`=` draws a dash, `*` draws "=", `[` `]` are “ ”). In a translation write the real characters: "—" (U+2014), « », „ “ exist in the game font. Noisy recording fragments (`=====`) become a row of dashes in place of the lost syllables. The `*` in the police note is kept.

## 4. Window limits (the engine does NOT wrap lines)

Line breaks are placed by the translator or by `build_lang.py`. The renderer (`src/engine/text/message_layout.cpp`) centres the block on its widest line; anything longer runs off the screen.

| What | Limit | Why |
|---|---|---|
| message line width | 300 px of the 12×16 font | 320 px screen; size fitting keeps 304 px (`fit_half_width` 152); the widest English line is 298 px |
| lines per page | max(3, the English count); up to 9 in a `{pos 4}` window | bottom windows move up one line per extra line; `line_cap` = 9 |
| `{instant}` page | 400 glyphs | `text_style.json` `message.display_all_length` |
| item name | one line, 202 px | the quantity label sits on the same line |
| item description | 3 lines of 270 px | the widest English description |

Width uses the imported font metrics (`<G>/convert/fonts/game.font.json`, `advance`); space is 6 px. Wide scripts need condensing: Cyrillic averages about 10.7 px per letter against 8.5 for Latin, so a line holds 26–28 Russian letters against 34–36 English ones.

## 5. Pipeline

```
# 1. template (section 2) → copy to .tmp/tr/<lang>.txt, write the translation in place of the English
# 2. build: codes are taken from the English, lines are wrapped, gamedata/text/<lang>/ is written
python3 -I tools/translation/build_lang.py <lang> .tmp/tr/<lang>.txt --gamedata <G>
# 3. check the translation: 0 problems
python3 -I tools/scripts/check_translation.py <lang> --gamedata <G>
# 4. check the format of all texts in the repository
python3 -I tools/scripts/gen_text.py --check
```

The draft has one line per key, `key<TAB>text`; a section `### maps/map0_s01.json` sets the file and keys are written without the file prefix. Only words are written:

- start and end codes (`{wait 2.0}`, `{pos 4}{center}{color 3}`, `{end}`, `{choice 4}`) are taken from the English;
- a coloured word inside a sentence is written `<...>` and gets the English `{color N}…{/color}`;
- `|` is a manual line break; other lines are wrapped automatically: fewest lines, as even as possible, preferring breaks after a full stop or comma, not after a preposition and not before a particle (lists `CLINGS` and `TRAILERS` in the script, extended per language);
- keys without words are not written;
- a draft line containing `{` is taken whole, as final.

The build prints `note:` where a line still ends with a preposition. The source of truth is `gamedata/text/<lang>/`; edit the JSON directly or rebuild from the draft, which is not kept in git.

`check_translation.py` checks: every English key present in its file, no extras; codes match in kind, arguments and order; the string encodes to engine bytes; the limits of section 4; no English left (identical strings; for a non-Latin script, Latin words outside the glossary's `keep_latin`); every character has a glyph in the game font (else add a sheet `gamedata/font/glyphs/*.glyphs`); no spaces at line edges and no double spaces unless the English has them.

New language: a directory `gamedata/text/<code>/` with `lang.json`; `text_po.py import … --name --english-name --direction --fonts` creates it.

## 6. Glossary

Glossary first, then the text. `tools/translation/glossary_en.json` holds the source terms (names, places, items, occult words, recurring templates) with what each is and where it occurs, and no English sentences of the game. `tools/translation/glossary_<lang>.json` has the same keys with the language's form, a `keep_latin` list and a `style` section (address, quotes, templates).

Rules:

- Names are transliterated identically in all files; the name on a sign, a key and an item description is the same string.
- Allusions follow the accepted translation of the book: "The Wizard of Oz" (Lion, Tin Man, Scarecrow), "Alice in Wonderland" (Mock Turtle, Mad Hatter, Cheshire Cat, Queen of Hearts).
- Occult names use the language's established form (Samael, Metatron — the original has the typo "Metraton", Flauros, Aglaophotis, Ouroboros, Melchior, ankh).
- Templates are identical everywhere and, where the language allows, need no case or gender of the item (Russian: «Здесь <предмет>. Взять?», «Использовано: <предмет>.», «Получено: <предмет>.»).
- The item name in a message equals the inventory name; long names are shortened to 202 px with the detail in the description.
- Words characters repeat after each other are translated with one word ("darkness": Dahlia → Harry → Cybil → Lisa; "the other church"; "the water works").

## 7. Voices and speaker gender

In languages where verbs carry gender, every first-person line needs a known speaker. Scene order:

| Map | Scene | Speakers (in line order) |
|---|---|---|
| `map0_s00` | opening, alley | Harry |
| `map0_s01` | café | Cybil asks, Harry answers; Cybil gives the pistol |
| `map1_s02`, `map4_s03` | Cheryl's voice | Cheryl ("daddy"), Harry |
| `map2_s01` | church | Harry asks, Dahlia prophesies |
| `map3_s00` | hospital, office | Kaufmann shoots, Harry stops him; then a dialogue |
| `map3_s04`, `map4_s04`, `map7_s00`, `map7_s01`/`map7_s02` | Lisa | Lisa and Harry; in `map7` Lisa realises she is dead |
| `map3_s03` and `map7_s02` | videotape | Lisa (recording): `map3_s03` has noisy fragments, `map7_s02` the full text — the fragments must be pieces of the full translation |
| `map3_s06` | hospital after the nightmare | Dahlia and Harry |
| `map4_s01` | antique shop | Cybil and Harry; the altar; then Harry's monologue |
| `map5_s02`, `map5_s03` | bar, motel | Harry and Kaufmann; Harry's monologue; diary entries (author not named) |
| `map6_s01` | boat | Harry and Cybil, then Dahlia; then Harry's thoughts |
| `map6_s02` | lighthouse; UFO ending | Harry; the last lines address the aliens (plural) |
| `map6_s04` | carousel, park | Harry and Cybil (story about Cheryl); Harry and Alessa; Dahlia and Alessa ("mommy") |
| `map7_s02` | memories | Dahlia and Kaufmann (the deal); Dahlia and little Alessa |
| `map7_s03` | finale | Dahlia (monologue), Cybil, Harry, Kaufmann; Cheryl's farewell; Cybil |

Register (as in Russian): Harry–Cybil and Harry–Lisa informal; Harry–Kaufmann formal; Dahlia informal to everyone, elevated speech; Harry formal to Dahlia, informal in the finale. Notes and newspapers are dry and telegraphic; Lisa's diary is in short clipped sentences; the writings in Alessa's room address the girl (feminine).

## 8. Puzzles: what must survive

Check against the map script (`gamedata/maps/<map>/events.lua`, `world_objects.lua`).

| Place | Keys | What to keep |
|---|---|---|
| Clock tower, school (`map1_s00`) | `10_00_…`, `12_00_…`, `5_00_…`, `a_golden_sun`, `a_silver_moon`, `the_hands_are_stopped_at*` | the times 10:00, 12:00, 5:00 as digits; 10:00 laboratory, gold in the old man's clenched hand, exchanged for "the sage's water" (acid); 12:00 place of songs (music room), silver, silenced "tongues", a set order; 5:00 darkness and heat (boiler room), flame, "the door of time" |
| Piano (`map1_s01`) | `a_tale_of_birds_without`, `a_raven_flies_in_flying`, `finally_out_comes_a_crow` | order Pelican, Dove, Raven, Swan, Crow; bird colour = key colour (white: pelican, dove, swan; black: raven, crow) — do not substitute birds; hints: the dove beyond the pelican, to the limit; the raven above the dove; the swan next to another bird; the crow stops at once; "silver reward" at the end (`PianoTune`) |
| Tale of the hunter and the lizard (`map1_s03`) | `hearing_this_the_hunter_armed` … `and_the_lizard_fell_down` | boss hint: shoot into the open mouth |
| Doghouse and three keys (`map2_s00`) | `to_school`, `doghouse_levin_st`, the keys | Cheryl's writings literally as in note names and descriptions; three locks, three keys |
| Plates, hospital (`map3_s03`) | `clouds_flowing_over_a_hill` … `liquid_flowing_from_a_slashed`, plate descriptions | the eight colour images literally (clouds, sunny sky, bitter mandarins, clover, violets, dandelions, sleep time, blood from a wrist); plates: Turtle blue, Hatter green, Cat yellow, Queen red |
| Vines on the grate (`map3_s05`) | `no_the_fire_goes_out`, `poured_disinfecting_alcohol_on`, `the_vines_absorbed_the_alcohol` | order: alcohol, then lighter |
| Delivery note (`map5_s02`) | `deliver_to_back_door_daily` | back door code 0886 as digits, Norman Young's signature; diary dates (August 20, September 12, September 10) |
| Newspapers and notes (`map2_s04`, `map5_s03`, `map7_s01`/`map7_s02`) | `investigation_stalled_ptv`, `fire_broke_out_in_town`, `investigations_show_source_as`, `white_claudia_…`, `seeds_contain_hallucinogen` | PTV in Latin letters; the mayor and the narcotics officer, heart failure; 6 houses, Alessa Gillespie (7); boiler in the Gillespie basement; the fire date is the day Cheryl was found; White Claudia is a hallucinogen from seeds |
| Lisa's diary (`map7_s01`/`map7_s02`) | `ask_doctor_to_let_me` … `help_me` | feminine, growing brevity; medicine/drug stays ambiguous |
| Zodiac lithograph (`map7_s01`/`map7_s02`) | `names_engraved_on_a_lithograph`, `yes_the_headcount_is_set`, `then_the_pathway_opens` | "headcount" (count), "by seniority/age" (order), "the path will open" |
| Light (`map7_s01`/`map7_s02`) | `the_light_to_the_future`, `the_light_illuminating_the` | two different inscriptions — do not merge them |
| Doors with spirit names (`map7_s01`/`map7_s02`) | `the_word_*_is_carved`, the keys | name on the door = name on the key |
| Five items (`map7_s02`) | `thou_possess_them_to_guard` | the number five ("five rites") |
| Clock and electrified key (`map7_s01`) | `old_clock_there_is_a`, `the_hands_of_the_clock`, `electricity_is_flowing_to_the`, `there_is_a_key_in` | square hole, hands as a key, glass cover; current must be switched off; key in the tap |
| Valve (`map1_s02`) | `turn_the_valve_left`, `turn_the_valve_right`, `stop_turning_the_valve` | three choices in the same order: left, right, stop |

## 9. Pitfalls

- The same English string sits in several maps under different keys (hospital sign, "locked", newspaper, diary, Lisa's lines in `map7_s01` and `map7_s02`); the translations must match (search the template by English).
- A message without `{end}` continues with the next key: a sentence may be split across keys ("…listen carefully:" + "before…"); translate the pair together, each part in its own key.
- `{color 3}` without a closing code colours to the end — do not add one.
- A line with leading spaces (the delivery note signature) is right-aligned with spaces: recompute for your width.
- Gender of speaker (section 7) and addressee: the UFO ending, the writings in Alessa's room.
- An item name wider than 202 px pushes the quantity label to the next line.
- Item descriptions: purpose first, then where it was found; drop the secondary.
- English disc text errors, fixed by meaning: `Metraton` (Metatron); `The time is neigh` (nigh); `it's own tail`; `brokenface` and `book..` in `map7_s02` (copies of `map7_s01` strings); `Florescent`; an extra space in `Basement key .` (`map3_s01`), `the darkness` with a leading space (`map7_s02`), ` after using` (`map1_s01`); `{wait 20}` and `{wait 10}` without a fraction (`map2_s00.what`, `map6_s04.msg16`, `cybil`) — codes are copied as is.
- Short obvious phrases ("Locked.", "What's that?") will match other translations — that is not borrowing; rewrite long matches in your own words.

## 10. Disputed places in the Russian translation (decisions)

| Key | Decision | Why disputed |
|---|---|---|
| `item.HealthDrink.name` | «Тоник» | "health drink" does not fit |
| `item.ChannelingStone.name` | «Камень контакта» | channeling means contact with spirits/aliens |
| `item.RockDrill.name`, `item.Hammer.name` | «Отбойный молоток», «Молот» | exact tool per the model |
| `item.AntiqueShopKey.name`, `LibraryReserveKey`, `BasementStoreroomKey`, `SewerExitKey` | shortened, detail in the description | 202 px limit |
| `item.StoneOfTime.desc` | «Большой камень-циферблат…» | condensed to 270 px |
| `map2_s01.here_the_flauros_a_cage` | «клеть покоя» | obscure phrase in the original |
| `map3_s06.strength_must_overcome_petty` | «Сила должна одолеть ничтожное желание. Детский лепет во сне.» | obscure phrase, kept close to the text |
| `map6_s01.to_seal_this_town_to` | «Запечатать город для бездны — вот что такое знак Самаэля.» | ambiguous syntax |
| `map6_s01.how_did_you_get_back` … `you_were_worried_where_did` | Harry asks, Cybil went through the sewers, Cybil was worried | speaker reconstructed from the scene |
| `map3_s00.hold_it` | «Стой...» | speaker unclear |
| `map7_s00.im_being_summoned` | «Меня призывают...» | «меня зовут» would read as introducing a name |
| `map5_s02.august_20` … `thought_of_leaving_town_but`, `map5_s03.september_10` … | diary by a man («отдал», «забрал», «думал») | the author's gender is not stated |
| `map4_s04.glad_youre_ok` | Harry speaks («Рад, что ты цела.») | speaker reconstructed from the scene |
| `map7_s01.need_drug` | «Нужен препарат.» | medicine or narcotic |
| `map7_s01.thief`, `go_home`, `drop_dead` | «Воровка.», «Убирайся домой.», «Сдохни.» | the writings address Alessa |
| `map7_s02.no_no_these_are_just` | «Это всего лишь проволочки.» | stalling — the soul's resistance |
| `map7_s03.he_has_been_nurtured_by` | «Этот кошмар вскормил Его.» | He is the cult's deity |
| `map7_s03.this_is_the_day_of` | «Настал судный день.» | day of reckoning |
| `map7_s03.i_was_shocked_to_realize` | «печать Метатрона» | the original says talisman of Metraton |
| `map1_s04.no_stage` and copies | «НЕТ УРОВНЯ!» | service string of unused maps |
| `common.now_making` | «В разработке!» | service string |

## 11. Language notes

`build_lang.py` has `CLINGS` (and for some `TRAILERS`) word lists for the space-separated languages below; `ja` and `zh` use the `NO_SPACE` rules. Speaker address per character pair is in each glossary's `style`.

### pt (Brazilian Portuguese)

- All characters are in the game font (ã õ ç, º, “ ”, —).
- Address: Harry–Cybil and Harry–Lisa `você`; Harry to Kaufmann `o senhor`, Kaufmann to Harry `você`; Dahlia to everyone `tu` (archaic); Harry to Dahlia `a senhora`, `você` in the finale.
- Gender shows in adjectives and participles (`obrigado/obrigada`, `sozinha`, `chocada`); disputed speakers get gender-free sentences (`Eu me preocupei com você`, `Estão me chamando...`).
- Templates with article by item gender: `Há um/uma <x> aqui. Pegar?`, `Usei o/a <x>.`, `Peguei o/a <x>.`; "locked" is `Está trancada.` (door), `Está trancado.` for the safe (`map5_s02.its_locked`).
- Same English, different translation: `what_are_you_talking_about` (Dahlia in `map7_s03`: `De que falas?`), `let_me_ask_you_have` (`map6_s02`, plural).
- Short names: `Chave do acervo`, `Chave da ponte`, `Chave do depósito`, `Chave da saída`, `Nota “Casinha do cão”`, `Cartuchos de escopeta` (so the shotgun is `Escopeta`).
- Piano birds: Pelicano, Pomba, Corvo, Cisne, Gralha (the crow is `Gralha`, also black). Oz keys: Leão, Lenhador, Espantalho; plates: Tartaruga Falsa, Chapeleiro Maluco, Gato de Cheshire, Rainha de Copas.

### pl (Polish)

- All Polish letters, „ ” and the dash are in the game font; lines nearly always fit the English line count.
- Address: Harry–Cybil and Harry–Lisa "ty", Harry–Kaufmann "pan" both ways, Dahlia "ty" to everyone, elevated; Harry to Dahlia "pani", "ty" in the finale. Past-tense verbs carry gender: section 7 is mandatory.
- Templates use the nominative («Jest tu <X>. Zabrać?», «Są tu <X>.», «Użyto: <X>.», «Zdobyto: <X>.»); four sentences decline the name where the verb requires it.
- Oz keys in the genitive without quotes («Klucz Stracha na Wróble», exactly 202 px); spirit keys and plates with quotes («Klucz „Ophiel”»).
- Floors use the Polish count (American 2nd floor is «1. piętro»). The quote „ is treated like « when wrapping.

### es (Spanish)

- Neutral Spanish for Spain and Latin America: plural "you" is `ustedes`, words without regional colour (`llevarse`, `accionar`, `encendedor`, `refrigerador`, `casillero`), simple past.
- Address: Harry–Cybil and Harry–Lisa `tú`, Harry–Kaufmann `usted`, Dahlia `tú` to everyone, Harry to Dahlia `usted`, `tú` in the finale.
- Templates agree by hand: `Hay un/una <name>. ¿Te lo/la llevas?`, `Usas el/la <name>.` — the article outside the coloured word, the word equals the inventory name without the capital. Quotes “ ”.
- Short names: `Llave del puente`, `Llave del depósito`, `Llave de la salida`, `Llave “Espantapájaros”`. Piano birds: Pelícano, Paloma, Cisne white; Cuervo, Corneja black.

### fr (French)

- The space before `? ! : ;` and inside « » is a normal space (no narrow no-break space in the font); `build_lang.py` (`glue`) keeps the mark with its word. œ and ç are in the font.
- Address: Harry–Cybil "vous" in the café, "tu" from the antique shop on; Harry–Lisa "tu"; Harry–Kaufmann "vous"; Dahlia "tu" to everyone; Harry to Dahlia "vous", "tu" in the finale; Dahlia–Kaufmann "tu"; Lisa on the tape to the doctor "vous".
- Templates: «Il y a un/une <x>. Prendre ?», «<Nom> utilisé(e).», «J'ai trouvé un <reçu>.». Names with a figure have no article: «Clé « Lion »», «Plaque « Chat »».
- Weapons: «Carabine de chasse», «Fusil à pompe»; ammo «Balles de carabine», «Cartouches de fusil».
- Floors «niveau 1/2», «2e sous-sol» (as the game maps' 1F/2F/B2); metric units.
- Disputed: `need_drug` «Il me faut le produit.»; `map6_s01` boat meeting and `map4_s04.glad_youre_ok` avoid the speaker's gender; `map6_s04.now_youre_half_indebted_to` stays close to the obscure original.

### it (Italian)

- Address: Harry–Cybil and Harry–Lisa "tu"; Harry–Kaufmann "lei" both ways; Dahlia "tu" to everyone, elevated; Harry to Dahlia "lei", "tu" in the finale (`map7_s03`); Lisa to the doctor "lei".
- Templates by item gender: `C'è una <chiave di casa>. Prenderla?`, `Ho usato la <…>.`, `Ho trovato uno <…>.`.
- `[ ]` become « »; spirit keys and plates in quotes (`Chiave «Ophiel»`, `Piastra «Gatto»`), Oz keys without (`Chiave Spaventapasseri`) to fit 202 px.
- Raven `Corvo`, crow `Cornacchia`, dove `Colomba`; Dahlia's "darkness" is `le tenebre`. Unclear speakers (`map6_s01.im_glad_you_made_it`, `you_were_worried_where_did`) avoid gender.

### tr (Turkish)

- The game font lacks `ğ Ğ ş Ş İ`: listed under `fallback_glyphs` in `glossary_tr.json`; check and build measure them as the base letter (`g`, `s`, `I`) and the system font draws them.
- Case: `i → İ`, `ı → I`; uppercase inscriptions are typed by hand (`upper()` is wrong for Turkish).
- No gender: section 7 matters only for address (sen/siz, `style.address`).
- Choice questions use the impersonal passive («Alınsın mı?», «Düğmeye basılsın mı?»); item names without a case ending.
- Metric units (10–15 inches → 25–40 cm, 3 feet → 1 metre).

### ar (Arabic, Modern Standard)

- Right-to-left, outside the game font: `lang.json` has `"direction": "rtl"` and a `fonts` list (fallback font). `"glyph_cell_height": 21`: drawn letters fill 21-row cells instead of 16 (extra rows split 13:3 around the baseline) and lines are 21 rows apart. Text is stored in logical order, unshaped: only U+0600–06FF letters, no presentation forms (U+FB50–FDFF, U+FE70–FEFF), no tatweel (U+0640), no direction controls, no vowel marks. `check_translation.py` checks this and estimates 7 px per letter, 5 px space, 0 for a combining mark (`FALLBACK_SCRIPTS`, `SHAPED`); `build_lang.py` wraps with the same space.
- The renderer does: bidi per line; shaping and the lam-alef ligature; mirroring of « » and ( ); LTR runs for digits (`10:00`, `0886`, `200`, `(7)`), `PTV` and interface key names (`Ctrl`, `Esc`, `F9`, `PGXP`, `HRTF`, `V-Sync`, `Hor+`, `{key}`, `{name}`); ؟ ، ؛ and ASCII `. ! : ...` follow the paragraph direction; a `{color N}…{/color}` range is logical; the leading-space signature of `map5_s02.deliver_to_back_door_daily` moves to the left edge; digits are Western.
- Gender of speaker and addressee both matter (section 7). The player is masculine singular («هل تأخذه؟»); the pronoun agrees with the item (`تأخذه` / `تأخذها`). The UFO ending is plural; Alessa's room is feminine.
- Piano birds: pelican «البجع», swan «التم» (kept apart on purpose); raven «الغداف», crow «الغراب». Only `PTV` stays Latin.

### ko (Korean)

- Hangul is drawn by the fallback system font. The check counts an East Asian wide character (`unicodedata.east_asian_width` W/F) as 12 px with no glyph required; space 6 px.
- The script is detected from the strings (no field in `lang.json`); `keep_latin`: `PTV`, `K`, `ml`.
- Breaks only between words (eojeol): determiners and short adverbs do not end a line, bound nouns (`수`, `것`, `뿐`) do not start one.
- Particles depend on the item name's last syllable (`이/가`, `을/를`, `이라는/라는`), chosen by hand per string.
- Speech levels per character pair in `style.address` of `glossary_ko.json`; identical short English lines differ by speaker and addressee.
- Interface files are translated from `gamedata/text/en/`; `menu.*_ids.*` strings stay as in English; `{kern}` codes are dropped, `{byte 07}` kept.

### de (German)

- Long compounds hit 202 px: «Schlüssel „Scheuche“», «Sprechzimmerschlüssel», «Ladenschlüssel», «Magazinschlüssel», «Hallenschlüssel», detail in the description; some lines need manual `|`.
- Templates without an article: «<X> gefunden. Mitnehmen?», «<X> benutzt.», «<X> erhalten.».
- Address: Harry–Cybil Sie in the café, du from the antique shop on; Harry–Lisa du; Harry–Kaufmann Sie; Dahlia du to everyone, Harry Sie to her, du in the finale; UFO ending ihr.
- Floors match the game maps: Erdgeschoss, Obergeschoss, 2. Untergeschoss; metric units. No capital ß in the font: «WEISSE CLAUDIA».

### ja (Japanese)

- Kana and kanji are drawn by a fallback font from `fonts` in `lang.json` (Noto Sans CJK JP etc.), else the bitmap `gamedata/font/shinonome16.shbf` (JIS X 0208). A wide character is a 12 px cell: up to 25 per message line, 16 in an item name, 22 per description line.
- Kinsoku (ja, zh, ko — any line with wide characters): no line starts with a closing mark (`。、」？！`), small kana or `ー`, none ends with an opening bracket; an ellipsis may start a line.
- `NO_SPACE` languages (`ja`, `zh`): `build_lang.py` breaks after punctuation, before `「` and a coloured word, and for `ja` inside text only at a kana → kanji/katakana boundary. A space in the draft marks an allowed break and is dropped (a real space is U+3000).
- Digits, `PTV` and `K` are half-width ASCII; `？！、。` full-width; `[ ]` become `「 」`; the noise row `=====` becomes `―` (U+2015).
- Japanese is shorter: do not split lines just to match the English count.
- Voices (役割語) in `glossary_ja.json` → `style.voices`; Dahlia is prophetic with Harry and Kaufmann, motherly with Alessa. Names follow the series' Japanese forms (ハリー・メイソン, シビル, ダリア, アレッサ, ホワイト・クロジェア, メタトロンの印章); every sentence is written anew.

### zh (Simplified Chinese)

- Hanzi and full-width punctuation are drawn by the fallback font; a wide character counts 12 px (25 per line, 16 per item name, 22 per description line). “ ”, — and … come from the game font (6, 11 and 12 px).
- `NO_SPACE`: breaks between any two wide characters, not before a closing mark, ellipsis or dash, not after an opening one, not inside Latin or numbers; it knows no words, so split long strings by hand (`|`). A draft space is a break point and reaches the text only between Latin letters or digits. Kinsoku as for `ja`.
- `fonts` in `lang.json` names Simplified Chinese families (Japanese fonts draw different glyph shapes).
- Names follow the Chinese fan community (寂静岭, 哈利·梅森, 雪柔, 里世界); only `PTV` and `K` stay Latin (K·戈登); times, 0886, dates and counts are digits; `[...]` becomes “...”, book titles and the bird riddles 《...》.
- Address 你 everywhere; Dahlia literary (岂不正如我所言, 万事皆休); the five-items inscription uses 汝; the deity 祂; the aliens 你们.
- The videotape fragments (`map3_s03`) are built from the words of the full text (`map7_s02`), gaps are rows of "—".
