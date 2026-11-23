// SPDX-License-Identifier: GPL-3.0-or-later
// Reading the effect files of gamedata/effects/: the last mod
// that has the file replaces it whole, otherwise the base file is read. Parse helpers
// shared by the effect parsers report the line and column of a bad value.

#pragma once

#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <string>
#include <string_view>

#include "formats/common/json.h"
#include "formats/common/status.h"

namespace sh::effects {

// Text of <gamedata>/<relative> or of the last mod's copy; *source gets the path read.
Result<std::string> read_effect_file(const std::filesystem::path& gamedata_dir, const std::filesystem::path& mods_dir,
                                     const std::filesystem::path& relative, std::string* source);

namespace parse {

// Every helper throws std::runtime_error with "line L, column C: ..." on a bad value.
[[noreturn]] void fail(const json::Value& at, std::string_view what);
const json::Value& member(const json::Value& parent, std::string_view key);
// Keys starting with "_" are comments.
void reject_unknown(const json::Value& object, std::initializer_list<std::string_view> known);
std::int32_t integer(const json::Value& v, std::int64_t lo, std::int64_t hi);
std::int32_t integer_of(const json::Value& parent, std::string_view key, std::int64_t lo, std::int64_t hi);
// A sound effect by its name in gamedata/sounds ("incubus_motion_6") or, the older form, by its
// number; the number the game plays.
std::int32_t sound(const json::Value& v);
std::int32_t sound_of(const json::Value& parent, std::string_view key);
bool boolean_of(const json::Value& parent, std::string_view key);
// [r, g, b], each 0..255.
void rgb(const json::Value& v, std::uint8_t out[3]);

}  // namespace parse

}  // namespace sh::effects
