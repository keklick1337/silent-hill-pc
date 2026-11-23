// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Keys that run console commands: `bind k kill;spawn groaner` runs both commands when K goes down in the game.
//
// This is not the Controls screen's table (bind_table.h) and shares nothing with it: no schemes, no conflict
// check, a key the game already uses may carry commands too, and neither side ever edits the other. A bind is kept
// and written to config.cfg as the line the player typed, so a set of them can be copied out of one config and
// pasted into another.

#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sh::input {

// What the binds need from the program around them.
class ConsoleBindHost {
public:
    virtual ~ConsoleBindHost() = default;
    // Keyboard position (USB usage number, as SDL scancodes are) of the key with this cap; 0 when there is none.
    virtual int scancode_of(std::string_view key_cap) const = 0;
    // Position of the key that opens the console; 0 when unknown.
    virtual int console_scancode() const = 0;
    // False while console commands are switched off (allow_debug_controls).
    virtual bool commands_allowed() const = 0;
    virtual void print(std::string_view line) = 0;
    virtual void run(std::string_view command) = 0;
    // Replaces the bind lines of config.cfg.
    virtual void save(std::span<const std::string> lines) = 0;
    virtual void rejected_config_line(std::string_view key, std::string_view commands) = 0;
};

class ConsoleBinds {
public:
    static constexpr std::size_t kMaxBinds = 48;
    static constexpr std::size_t kKeyChars = 31;       // a longer key name is cut
    static constexpr std::size_t kCommandChars = 191;  // and so is a longer command list

    explicit ConsoleBinds(ConsoleBindHost& host) : host_(host) {}

    // `bind`: nothing prints the usage, `list` the binds, `<key> <commands>` sets one (replacing the key's bind).
    void command_bind(std::string_view arg);
    void command_unbind(std::string_view arg);
    void command_unbind_all();

    // A config.cfg line without its leading "bind ". Never writes the file and never prints.
    void load_config_line(std::string_view rest);

    // Once a frame while the game listens to the keyboard: key_down[scancode] is nonzero for a held key.
    void update(std::span<const std::uint8_t> key_down);

    struct Entry {
        std::string key;       // as typed: "K", "KEYPAD 1"
        std::string commands;  // as typed, ';' between commands
        int scancode = 0;
        bool was_down = false;
    };
    std::span<const Entry> entries() const { return binds_; }

private:
    bool set(std::string_view key, std::string_view commands, bool quiet);
    const char* refusal(int scancode) const;
    int find(int scancode) const;
    void save();
    void usage();

    ConsoleBindHost& host_;
    std::vector<Entry> binds_;
    bool caps_resolved_ = false;
};

// The game's binds (console_binds_host.cpp).
ConsoleBinds& console_binds();

}  // namespace sh::input
