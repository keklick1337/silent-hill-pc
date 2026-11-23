// SPDX-License-Identifier: GPL-3.0-or-later
// gamedata/menu/staff_roll.json: the roll's lines and logo in a readable markup, the
// 24-pixel font's advances, logo glyphs and colors, and the roll's style (colors, the
// tilted plane, the logo animation), and per ending the roll's music and timing. Mods patch
// it with the data-merge rules (mods/data_merge.h); every key is checked.

#pragma once

#include <filesystem>
#include <string>
#include <string_view>

#include "engine/screens/staff_roll.h"
#include "formats/common/status.h"

namespace sh::screens {

// Encodes one line of roll markup into the roll's byte codes:
//   printable ASCII as is, "\n" a new line;
//   {c1}..{c7}             a color of the font's table;
//   {+1}..{+128}, {-1}..{-128}  move by the advance of codes 0x10..0x1F (the shipped
//                          table gives them exactly these pixel distances);
//   {span}/{/span}, {spanN}/{/spanN} (N 1..7)  measure the text between them;
//   {fwd_half}, {fwd_full}, {back_half}, {back_full} (+N)  move by the measured width;
//   {kcet}...{/kcet}       letters drawn with the logo glyphs;
//   {xNN}                  any byte by its hex value.
Result<std::string> encode_roll_markup(std::string_view markup, const RollFont& font);

Result<StaffRollData> parse_staff_roll(std::string_view json_text);

// Reads staff_roll.json from the last mod (in load order) that has menu/staff_roll.json,
// else from the gamedata directory. `source` receives the file read.
Result<StaffRollData> load_staff_roll(const std::filesystem::path& gamedata_dir,
                                      const std::filesystem::path& mods_dir, std::string* source = nullptr);

}  // namespace sh::screens
