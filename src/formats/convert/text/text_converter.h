#pragma once

#include "formats/convert/converter.h"

#include <memory>

namespace sh::convert::text {

// Game text to UTF-8 JSON, without loss. Covers the string tables of NTSC USA 1.1: the map messages of the 43 map
// overlays (VIN/MAP*_S*.BIN), and the menu, item, results, save/load and credits strings of 1ST/BODYPROG.BIN,
// VIN/OPTION.BIN, VIN/SAVELOAD.BIN and VIN/STF_ROLL.BIN. The files are code with the strings inside, so the converter
// writes two outputs per game file:
//
//   text/<path without extension>.json         the tables: per entry {id, text, raw, offset, capacity}
//   text/<path without extension>.frame.bin    the file with every string slot zeroed (BODYPROG.BIN decrypted)
//
// encode() puts the strings back into the frame and re-encrypts when needed. Which of "text" and "raw" wins:
//
//   * the bytes of "raw" are used when "text" is absent, or when "text" is what "raw" shows as text;
//   * otherwise "text" was edited and its bytes are used. They must fit the slot ("capacity", NUL included).
//
// To change bytes by hand, edit "raw" and delete the entry's "text". Entries with "alias_of" share the storage of
// the named earlier entry; they are copies for reading and encode() ignores them.
std::unique_ptr<sh::convert::Converter> make_converter();

}  // namespace sh::convert::text
