#pragma once

#include "formats/common/status.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sh::convert::text {

// How the bytes of one string are shown as text. The bytes are what the game draws with; none of the dialects
// is a character set conversion, because the PS1 game stores its font indices as plain ASCII codes.
//
//   Ascii   Bytes 0x20..0x7E as they are. `\` is `\\`, 0x09 `\t`, 0x0A `\n`, every other byte `\xHH`.
//           Used where the font draws a real space (credits) and as the fallback for any string.
//   Game    Like Ascii, but the font's space is `_` (0x5F) in the data, so `_` is shown as a space and a real
//           0x20 is shown as `\s`. Item names, descriptions and menus.
//   Markup  Map messages. `_` is a space; spaces, tabs and newlines between tokens are ignored by the game and
//           are not shown. A command `~X<arg>` (X a letter, arg one byte after it) is shown as `{X}` or
//           `{X<arg>}`; `~J` takes everything up to the next space or tab, e.g. `{J0(2.0)}`. Literal `{`, `}` and
//           `\` are `\{`, `\}`, `\\`. Text is not byte exact in this dialect: the bytes in "raw" are authoritative.
enum class Dialect : std::uint8_t { Ascii, Game, Markup };

std::string_view dialect_name(Dialect dialect);
std::optional<Dialect> dialect_from_name(std::string_view name);

// Always succeeds for Ascii and Game. For Markup it returns nothing when the bytes use a command form the
// dialect cannot express (the caller then falls back to Ascii for that string).
std::optional<std::string> bytes_to_text(std::span<const std::uint8_t> bytes, Dialect dialect);

// The bytes for edited text. Fails with a message on a malformed escape, a character outside printable ASCII,
// or (Markup) an unclosed `{` or a literal `~`.
Result<std::vector<std::uint8_t>> text_to_bytes(std::string_view text, Dialect dialect);

// Lower-case hex of the bytes, and back. from_hex fails on an odd length or a non-hex digit.
std::string to_hex(std::span<const std::uint8_t> bytes);
std::optional<std::vector<std::uint8_t>> from_hex(std::string_view hex);

}  // namespace sh::convert::text
