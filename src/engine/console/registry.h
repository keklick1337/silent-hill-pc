// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The command registry of the debug console: every engine module, the game layer and Lua mods register
// commands with typed arguments here. The registry knows nothing about the game; completion sources are
// functions the registrant supplies.

#pragma once

#include "formats/common/status.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace sh::console {

// Colour class of an output line (the console maps them to colours).
enum class Level : std::uint8_t { Plain, Info, Warn, Error, Debug, Command, Help };

// One completion candidate: the text inserted, and an optional dimmed note shown beside it in the popup.
struct Candidate {
    std::string text;
    std::string note;
};

enum class ArgType : std::uint8_t {
    String,   // any word
    Int,      // integer (decimal, 0x hex)
    Float,    // decimal number
    Bool,     // 0/1/on/off/true/false/yes/no
    Enum,     // one of ArgSpec::values
    Rest,     // the whole remaining line, joined with single spaces (must be last)
};

// Candidates for the word being typed: the callback appends to `out` every candidate it knows for `prefix`
// (it may ignore the prefix: the engine filters and ranks). Called on every keystroke, so keep it cheap.
using CompleteFn = std::function<void(std::string_view prefix, std::vector<Candidate>& out)>;

struct ArgSpec {
    std::string name;
    ArgType type = ArgType::String;
    bool optional = false;
    std::string help;                 // one line, shown beside the usage signature
    std::vector<std::string> values;  // Enum: the allowed words (also the completion list)
    CompleteFn complete;              // dynamic candidates, in addition to `values`
};

// Parsed arguments of one invocation (already validated against the spec).
class Args {
public:
    std::size_t size() const { return words_.size(); }
    bool has(std::size_t i) const { return i < words_.size(); }
    const std::string& str(std::size_t i) const;  // empty string when absent
    // Typed reads; the registry has validated the type, so these only fall back for absent optional words.
    long long integer(std::size_t i, long long fallback = 0) const;
    double number(std::size_t i, double fallback = 0.0) const;
    bool boolean(std::size_t i, bool fallback = false) const;
    // Words from `i` on joined with single spaces (an ArgType::Rest word is already one string).
    std::string joined(std::size_t i) const;
    const std::vector<std::string>& words() const { return words_; }

    explicit Args(std::vector<std::string> words) : words_(std::move(words)) {}
    Args() = default;

private:
    std::vector<std::string> words_;
};

// Where a command writes. The console implements it; tests collect lines.
class Output {
public:
    virtual ~Output() = default;
    virtual void print(Level level, std::string_view line) = 0;
    void info(std::string_view line) { print(Level::Info, line); }
    void warn(std::string_view line) { print(Level::Warn, line); }
    void error(std::string_view line) { print(Level::Error, line); }
};

class Registry;

struct Context {
    Output& out;
    Registry& registry;
};

// A command's body. A failure is printed as an error line by the caller.
using RunFn = std::function<Result<void>(Context& ctx, const Args& args)>;

struct CommandSpec {
    std::string name;                  // canonical, lower case, no spaces
    std::vector<std::string> aliases;  // other names for it
    std::string help;                  // one line
    std::string details;               // optional longer text (newlines allowed) shown by `help <cmd>`
    std::string group;                 // "game", "graphics", "debug", ... for the help listing; may be empty
    std::vector<ArgSpec> args;
    RunFn run;
    std::string owner;                 // "" = engine; a mod's folder name for Lua commands (remove_owner)
};

// "name <required> [optional] [rest...]": the usage signature shown while typing and by `help`.
std::string usage_of(const CommandSpec& spec);

class Registry {
public:
    // Fails on an empty/invalid name, a missing run, or a clash with another command's name or alias
    // (re-registering the same canonical name from the same owner replaces it, so scripts can reload).
    Result<void> add(CommandSpec spec);
    // A user alias: `name` expands to `expansion` (a full line, may contain several ';'-separated commands).
    Result<void> add_alias(std::string name, std::string expansion);
    void remove(std::string_view name);
    std::size_t remove_owner(std::string_view owner);

    // Name or alias, case-insensitive; null when unknown. Pointers stay valid until the registry changes.
    const CommandSpec* find(std::string_view name) const;
    std::vector<const CommandSpec*> commands() const;      // sorted by name
    std::vector<std::string> names() const;                // every name, aliases and user aliases included
    const std::string* user_alias(std::string_view name) const;

    // Runs a line: splits ';' commands, expands user aliases (guarded against loops, depth 8), tokenizes,
    // validates the arguments against the spec and calls run. Prints errors ("unknown command 'x'",
    // "usage: ...", "argument 'n': expected integer, got 'abc'") to `out` and returns false when any command
    // failed (execution stops at the first failure).
    bool execute(std::string_view line, Output& out);

    // Parses and validates the words of one command against `spec` without running it.
    static Result<Args> parse_args(const CommandSpec& spec, const std::vector<std::string>& words);

private:
    std::map<std::string, CommandSpec, std::less<>> commands_;  // canonical name -> spec
    std::map<std::string, std::string, std::less<>> alias_to_name_;
    std::map<std::string, std::string, std::less<>> user_aliases_;
    int depth_ = 0;
};

// Registers the engine's own commands that need no game: help, alias, unalias, echo. (`clear` and `history`
// belong to the Console, which owns the scrollback and history.) `help` with no argument lists commands by
// group; `help <cmd>` prints usage, help, details, aliases.
void register_builtin_commands(Registry& registry);

}  // namespace sh::console
