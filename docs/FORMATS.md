# Formats

Specifications of the disc formats the import reads and of the engine data formats not described in the subsystem documents. The layout of `gamedata/`: [ARCHITECTURE.md](ARCHITECTURE.md).

## SILENT. and HILL.

Public API: `src/formats/archive/sh_archive.h`. Reading of all 2074 entries matches the upstream `extract.py` byte for byte on the USA v1.1 image.

### What it is

`SILENT.` and `HILL.` are two files with no directory and no names, into which almost all game files are packed. ISO9660 sees only them, `SLUS_007.07` and `SYSTEM.CNF`. The index is not in the containers but in the executable `SLUS_007.07`.

### Where the table is

- `SLUS_007.07` is an ISO9660 file at LBA 24, 81920 bytes. The table starts at file offset **0xB91C** of this file. This is not an address in PS-EXE memory.
- Size: 2074 entries of 12 bytes (24888 = 0x6138 bytes), ending at 0x11A54.
- The release is identified by the CRC32 of the first 4096 bytes of the file: **0xCF9CD8E5** — NTSC 1.1 (SLUS-00707). The value matches `RELEASES` in `extract.py`. Other releases (demos, prototypes, PAL, NTSC-J) have a different offset and entry count and are not supported here.
- The table has no end marker. The entry count comes from the release. The entry number (0…2073) is the table order; the "number path" list is `tools/data/disc_files.USA.txt`.

### Table entry

Three u32 words, little-endian.

| Word | Bits | Content |
| --- | --- | --- |
| meta | 0–18 | LBA: absolute sector number on the disc |
| meta | 19–31 | size in 256-byte blocks |
| file1 | 0–3 | folder index: `1ST ANIM BG CHARA ITEM MISC SND TEST TIM VIN XA` (0…10) |
| file1 | 4–27 | name characters 0–3, 6 bits each |
| file1 | 28–31 | unused, 0 in all entries |
| file2 | 0–23 | name characters 4–7, 6 bits each |
| file2 | 24–27 | type index: `TIM VAB BIN DMS ANM PLM IPD ILM TMD DAT KDT CMP TXT UU1 UU2 ""` (0…15) |
| file2 | 28–31 | unused, 0 in all entries |

- A character is `32 + value` (6 bits). Value 0 is a space, short names are padded with it; spaces at the edges are trimmed. Only `0–9`, `A–Z`, `_` occur in this release.
- The entry path is `FOLDER/NAME.TYPE`, for example `1ST/BASE.VAB`. For XA (type 15, empty) it is `XA/NAME` without extension, for example `XA/ZZ_14239`.
- Names are unique, case-insensitively as well.

### Two containers

| | SILENT. | HILL. |
| --- | --- | --- |
| entries | 2044 (all folders except XA) | 30 (XA folder, id 2044–2073) |
| first sector on the disc | 64 (0x40) | 39359 (0x99BF) |
| sectors | 39295 | 222606 |
| ISO9660 record size | 80 476 160 bytes | 455 897 088 bytes (= 222606 × 2048) |
| sector payload | 2048 bytes: raw bytes 24…2071 (Mode 2 Form 1, data) | 2336 bytes: raw bytes 16…2351 (XA subheader, audio, EDC; Mode 2 Form 2, the first sectors have submode 0x64) |

- LBAs in the table are absolute disc sector numbers. The original game passes them to the file queue as a disc position; the engine reads sectors by them (`sh::disc`).
- Entry offset inside the container: `(LBA − first sector of the container) × sector payload size`.

### Sizes

The rules match `extract.py`, `_extract`.

- A SILENT entry with a non-zero size field: `blocks × 256` bytes. The real length can be 0…255 bytes shorter: for all 92 VABs the `fsize` field in the header lies in `[blocks×256 − 255, blocks×256]`. The extra tail is bytes from the disc.
- Otherwise — XA and 7 SILENT entries with a zero field: up to the LBA of the next entry of the same container, in sectors. For the last HILL. entry — up to the end of the container.
- The size field of XA entries is not used. For `ZZ_14239` it is 1419, which is not the track length.
- The original game reads `ceil(blocks × 256 / 2048)` sectors, that is, the same rounding.
- The number at the end of an XA track name equals the track's sector count: `05_02152` — 2152 sectors, `ZZ_14239` — 14239 (all 30 entries).
- Zero-size entries (7): `ANIM/HB_M1S04.ANM`, `ANIM/HB_M2S03.ANM`, `ANIM/HB_M4S00.ANM`, `ANIM/HB_M4S06.ANM`, `ANIM/HB_M6S05.ANM`, `VIN/MAPT_S00.BIN`, `VIN/MAPX_S00.BIN`. The next entry starts in the same sector, the file is empty.
- Three entry sizes. The logical one (`size`: `blocks × 256` for SILENT, `sectors × 2336` for HILL) is returned by `read_entry` and by the `size` field of the import index (`index.tsv`). The stored one: `sectors × 2048` for SILENT, with the tail up to the sector boundary, and `sectors × 2336` for HILL. The tail lies in the container, `read_entry` does not return it. The bytes are raw, as on the disc.
- Known values: CRC32 of the stored bytes of `CHARA/HERO.ILM` = `2D30905C`, `1ST/BODYPROG.BIN` = `AC9CDBC1` (import index values). CRC32 of the logical raw bytes of `1ST/BODYPROG.BIN` (674560) = `A8966201`. The tests `EncryptedOverlayIsReadAsOnDisc` and `StoredSectorsKeepTheTail` check both values.

### Order in the container

- SILENT.: files follow without gaps. Each takes `ceil(blocks×256 / 2048)` sectors, the next one starts right after. The last file ends at sector 39359, where HILL. starts.
- HILL.: tracks follow in table order, the first starts at sector 39359.

### Files that extract.py transforms

- `1ST/BODYPROG.BIN`, `1ST/B_KONAMI.BIN`: encrypted on the disc with an XOR keystream (`seed = (seed + 0x01309125) * 0x03A452F7` per word). The original game decrypts them on load with the same keystream.
- `BG/HP_SAFE1.BIN`, `BG/S__SAFE2.BIN`: a raw block of 2816 bytes. The first 4 bytes are the unpacked size (2372), then LZSS and the same keystream (`extract.py`).
- `TEST/*.CMP` (5 files): LZSS with a size header, for example `RESIDENT.CMP` — 31488 raw bytes and 99872 after unpacking. `extract.py` writes the raw file and a `.dec` beside it.

The reader returns the bytes as they lie on the disc. Decryption and unpacking are the consumer's job.

### Reference and verification

- The reference is the extraction script `silentassets/extract.py` from the upstream decompilation tools; its file list matches `tools/data/disc_files.USA.txt`, and all 2074 entries match its output byte for byte (the four transformed files through the same transformations).
- Automated tests: `tests/archive/archive_test.cpp`, on the real image from `data/`; without the image — `SH_SKIP`.

### What is not verified

- Releases other than NTSC 1.1.
- How the game reads `HP_SAFE*` and `*.CMP` at run time.
- The purpose of the size field of XA entries.

## Animation clip tables

A character's clip table (in the original, the `s_AnimInfo` array in the map overlay) says with which function and at which speed each clip is played. The import writes the tables losslessly to `gamedata/tables/anim/<name>.json` (D36); the registry of names, sizes and disc locations is `src/engine/tables/anim_table_registry.{h,cpp}`, the reader is `src/engine/tables/anim_table.{h,cpp}` (`sh_tables`). Editing a table is a gameplay mod.

```json
{
  "format": "sh-anim-table", "version": 1, "symbol": "CAT_ANIM_INFOS",
  "source": { "file": "VIN/MAP1_S01.BIN", "offset": "0x133AC", "va": "0x800DC924", "stride": 16 },
  "entries": [
    { "index": 2, "kind": "blend_linear", "status": 2, "variable_duration": false, "link": 3,
      "speed": 64, "speed_q12": 262144, "start_kf": -1, "end_kf": 7 }
  ]
}
```

| Field | Meaning |
|---|---|
| `entries[i]` | the entry for packed status `i` |
| `kind` | `blend_linear`, `playback_once`, `playback_loop`, `none` (no playback function) |
| `status` | `(clip << 1) \| active`, 255 — none |
| `variable_duration` | `true` — the speed is given by the function `duration_func` (a name the engine knows, for example `Player_VariableAnimDurationGet`) |
| `link` | the status that follows this one; 255 — no link |
| `speed`, `speed_q12` | frames per second; `speed_q12 = speed × 4096` truncated like `Q12()`; either one or both may be given (then they are equal) |
| `start_kf`, `end_kf` | first and last frame; `start_kf = -1` — from the current frame |

Unknown keys are ignored; `index`, `speed`, `source`, `copies`, `help` only help the reader and are checked if present.

Table structure (verified on all tables of the disc):

- Clip N takes two consecutive entries: `[2N]` — blending (`blend_linear`, `status = 2N`), `[2N+1]` — playback (`playback_once` or `playback_loop`, `status = 2N+1`). For blending `link = status + 1`, `start_kf = -1`, `end_kf` is the first frame of playback. Exception: five pairs of the Monster Cybil table (entries 20, 22, 28, 32, 34) end blending at the last frame of playback; 20 and 22 have a negative speed (backwards).
- Entries 0 and 1 are either clip 0 or a stub (entry 0 with `status = 255`, entry 1 with `kind = none`).
- Speed is frames per second (Q12); each tick it is multiplied by the time step. For blending it is the weight growth rate: `speed = 64` finishes blending in one tick at any rate. `0` holds the pose, a negative value plays backwards.
- `link = 255`: the status stays as it is.
- The `FLOATSTINGER_ANIM_INFOS` table holds 26 clip entries and two extra ones (neighbouring overlay data, meaning not verified); `HARRY_M6S04_EXTRA_ANIM_INFOS` (`harry_m6s04_extra.json`) — entries 16–23 of Harry's map on map6_s04, the link between indices and statuses is not proven.

In the engine the tables are `ClipInfo` records (`src/engine/chara/game/clip_tables.{h,cpp}`): filled at load and again at the load of each map; the player is chosen by `kind`. Harry's base clips — `gamedata/chara/Harry/base_clips.json` (the same format, the engine table `src/engine/inventory/game/harry_clip_table.cpp`); Puppet Nurse variants — `gamedata/chara/PuppetNurse/variants.json` (`src/engine/chara/game/puppet_nurse_variants.cpp`); element types of map tables (`tables.json`) — `src/engine/loader/map_table_types.cpp`.
