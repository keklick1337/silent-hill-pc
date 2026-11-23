#pragma once

// Readable game text <-> the engine's message bytes.
//
// gamedata/text/<lang>/*.json holds text people edit: real spaces and newlines, {markup} for
// the renderer's ~codes. The renderers still take the original bytes, where '_' draws a space
// and ' ', '\t', '\n' are layout they skip, so every string is encoded on load. The skipped
// layout does not survive in readable text; a per-string "ws" record (written by
// tools/scripts/gen_text.py) restores it, which makes the shipped English encode to exactly
// the compiled bytes. Without a record the encoder writes canonical layout, which draws the
// same. tools/scripts/sh_text.py is the Python twin and must produce identical bytes.

#include "formats/common/status.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace sh::convert::text::markup {

// Message: map messages (Gfx_MapMsg_*: ~N/~E/~J/~C/~S..., '\n' is layout).
// Ui: item and menu strings (Gfx_StringDraw: '\n' breaks the line, \v and \x01 are spacing).
// Plain: PC interface text ("ui.*"). No markup: braces are literal ({n}, {key} are the overlays'
// placeholders); the engine form only turns spaces into the game font's '_'.
enum class Style : std::uint8_t { Message, Ui, Plain };

// "item.*" and "menu.*" keys are Ui text, "ui.*" Plain, every other key is a message.
Style style_for_key(std::string_view key);

struct Decoded {
    std::string text;  // readable
    std::string ws;    // exact layout record, empty when the canonical layout reproduces the bytes
};

Decoded decode(std::string_view engine, Style style);

// Fails on unknown or malformed markup.
Result<std::string> encode(std::string_view text, std::string_view ws, Style style);

// FNV-1a 32 of the readable text's bytes, eight lowercase hex digits: the ws record's guard.
std::string text_hash(std::string_view text);

}  // namespace sh::convert::text::markup
