// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The completion engine: for a line and a caret it works out which word is being typed, what the command's
// usage signature is, and the filtered, ranked candidates (command names for the first word; for later words
// the argument's enum values and its dynamic CompleteFn). No SDL, no drawing.

#pragma once

#include "engine/console/registry.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace sh::console {

struct Completion {
    std::vector<Candidate> items;      // ranked: prefix matches (case-insensitive, alphabetical) first, then
                                       // substring matches; capped at kMaxCandidates
    std::size_t total = 0;             // matches before the cap
    std::size_t replace_begin = 0;     // byte span of the word an accepted candidate replaces
    std::size_t replace_end = 0;
    std::string prefix;                // what was typed of that word
    std::size_t word_index = 0;        // 0 = command name; n = n-th argument (counting from 1)

    // Usage line of the command being typed ("spawn <character> [state] [distance]") and which argument the
    // caret is in (-1: none / the name itself); empty usage when the first word is not a known command.
    std::string usage;
    int active_arg = -1;
    std::string arg_help;              // ArgSpec::help of the active argument
    std::string command_help;          // CommandSpec::help of the command
    std::string problem;               // "unknown command 'x'" / "too many arguments", else empty
};

inline constexpr std::size_t kMaxCandidates = 256;

// Computes everything above for `line` with the caret at byte `caret`. Command-name completion also offers
// user aliases. An empty prefix on the first word returns every command (the popup opens on Tab).
Completion complete(const Registry& registry, std::string_view line, std::size_t caret);

// The line after accepting `candidate`: the word span replaced (quoted if needed); a space is appended after a
// command name and after any argument that is not the command's last. Returns the new line and the caret.
struct Applied {
    std::string line;
    std::size_t caret = 0;
};
Applied apply(const Registry& registry, std::string_view line, const Completion& completion,
              const Candidate& candidate);

// Longest case-insensitive common prefix of the candidates' text (original spelling of the first).
std::string common_prefix(const std::vector<Candidate>& items);

// Case-insensitive ASCII helpers shared by the console.
bool starts_with_ci(std::string_view text, std::string_view prefix);
bool contains_ci(std::string_view text, std::string_view needle);

}  // namespace sh::console
