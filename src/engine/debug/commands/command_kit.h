// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// What the game's console command files share: the registration call, argument spec builders, the plain
// printf-style output of a command, word helpers and a line wrapper for long lists. Every command of
// src/engine/debug/commands is a sh::console::CommandSpec whose body writes to its Context.

#pragma once

#include "engine/console/registry.h"

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sh::debug::commands {

using sh::console::ArgSpec;
using sh::console::ArgType;
using sh::console::Args;
using sh::console::Candidate;
using sh::console::CommandSpec;
using sh::console::CompleteFn;
using sh::console::Context;
using sh::console::Registry;
using sh::console::RunFn;

// The longest line a command prints; longer text is cut (the log's line limit).
inline constexpr std::size_t kLineLimit = 255;

// One plain output line of a command (sh::debug::print_command_line: the log's queued lines go in above it).
void say(Context& ctx, const char* format, ...) __attribute__((format(printf, 2, 3)));
void say_text(Context& ctx, std::string_view line);

// The success every body returns: failures of these commands are told as plain lines, not as registry errors.
inline Result<void> done() { return Result<void>::ok(); }

std::string upper(std::string text);
std::string lower(std::string text);
bool same_word(std::string_view a, std::string_view b);        // ASCII case-insensitive
bool contains_word(std::string_view text, std::string_view part);  // case-insensitive substring; empty part matches

// The argument words as the command line had them: joined with single spaces and split on spaces again (a
// quoted word with spaces inside counts as several).
std::vector<std::string> spaced_words(const Args& args);

// Numbers the way the C library reads them: leading digits count, anything else is 0.
int whole(const std::string& word);
double real(const std::string& word);

// An on/off argument: the state it names, or the opposite of `current` when it is absent.
bool switched(const Args& args, std::size_t index, bool current);

// ---- registration ----

ArgSpec arg(std::string name, ArgType type, bool optional, std::string help, std::vector<std::string> values = {},
            CompleteFn complete = {});
ArgSpec opt_number(std::string name, std::string help);
ArgSpec opt_integer(std::string name, std::string help);
ArgSpec opt_switch(std::string help);
// An optional free word with suggested completions (the body accepts more than the suggestions).
ArgSpec opt_word(std::string name, std::string help, std::vector<std::string> suggestions);

// Completion over a fixed list of words.
CompleteFn words_completion(std::vector<std::string> words);

void add(Registry& registry, std::string name, std::string group, std::string help, std::vector<ArgSpec> args,
         RunFn run, std::vector<std::string> aliases = {}, std::string details = {});

// ---- the common shapes of a live setting ----
//
// A value another module keeps, as the function that reaches it. `line` is the printf line that reports it.
template <typename T>
using Setting = T& (*)();

// "<command> [number]": sets the value when a number is given, then reports it (one %f-style conversion).
RunFn number_setting(Setting<float> value, const char* line);
// "<command> [integer]": the same for a whole number (one %d).
RunFn whole_setting(Setting<int> value, const char* line);
// "<command> [on|off]": sets the state or flips it, then reports it with `on` or `off` (one %s).
RunFn switch_setting(Setting<int> value, const char* line, const char* on = "ON", const char* off = "OFF");

// Packs short items into output lines: an item that would bring the line to `limit` characters starts a new one.
class LineWrapper {
public:
    // `slack` is counted on top of every item when testing the limit.
    LineWrapper(Context& ctx, std::size_t limit, std::size_t slack, std::string_view separator);
    void add(std::string_view item);
    // `measure` stands in for the item's length in the limit test.
    void add(std::string_view item, std::size_t measure);
    void flush();

private:
    Context& ctx_;
    std::size_t limit_;
    std::size_t slack_;
    std::string separator_;
    std::string line_;
};

}  // namespace sh::debug::commands
