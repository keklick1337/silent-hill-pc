// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// A bind keeps the command text exactly as typed and runs it, split at ';', when its key goes down. Keeping the
// text is what makes the saved line the typed line.

#include "input/bind/console_binds.h"

#include <algorithm>
#include <cctype>

namespace sh::input {
namespace {

// Keyboard positions the binds treat specially (USB HID usage numbers).
namespace usage {
constexpr int kReturn = 40;
constexpr int kBackspace = 42;
constexpr int kGrave = 53;
constexpr int kKeypadEnter = 88;
constexpr int kFirstModifier = 224;  // left Ctrl, Shift, Alt, GUI, then the right ones
constexpr int kLastModifier = 231;
}  // namespace usage

// The console shows lines up to this long.
constexpr std::size_t kLineChars = 255;

bool is_blank(char c) { return c == ' ' || c == '\t'; }
bool is_line_end(char c) { return c == '\r' || c == '\n'; }

std::string_view trimmed(std::string_view s) {
    while (!s.empty() && is_blank(s.front())) {
        s.remove_prefix(1);
    }
    while (!s.empty() && (is_blank(s.back()) || is_line_end(s.back()))) {
        s.remove_suffix(1);
    }
    return s;
}

bool starts_with_word(std::string_view s, std::string_view word) {
    if (s.size() < word.size()) {
        return false;
    }
    for (std::size_t i = 0; i < word.size(); ++i) {
        if (std::toupper(static_cast<unsigned char>(s[i])) != word[i]) {
            return false;
        }
    }
    return true;
}

// Calls `each` with every command of a ';' separated list, trimmed; an empty one too.
template <typename F>
void for_each_command(std::string_view list, F each) {
    while (!list.empty()) {
        const std::size_t semi = list.find(';');
        each(trimmed(list.substr(0, semi)));
        if (semi == std::string_view::npos) {
            break;
        }
        list.remove_prefix(semi + 1);
    }
}

std::string line(std::string text) {
    if (text.size() > kLineChars) {
        text.resize(kLineChars);
    }
    return text;
}

// The first word and what follows its spaces; false when there is one word only.
bool split_key(std::string_view text, std::string_view* key, std::string_view* rest) {
    const std::size_t space = text.find(' ');
    if (space == std::string_view::npos) {
        return false;
    }
    *key = text.substr(0, std::min(space, ConsoleBinds::kKeyChars));
    text.remove_prefix(space);
    while (!text.empty() && text.front() == ' ') {
        text.remove_prefix(1);
    }
    *rest = text;
    return true;
}

}  // namespace

void ConsoleBinds::usage() {
    host_.print("bind <key> <command>[;<command>...]  bind console commands to a key");
    host_.print("  bind k kill");
    host_.print("  bind f give shotgun;give shotgunammo;spawn groaner");
    host_.print("bind list        show every bind");
    host_.print("unbind <key>     clear one");
    host_.print("unbindall        clear them all");
    host_.print("Separate from the control binds: a key already used by the");
    host_.print("game can carry one, and editing controls never touches these.");
}

// Only the keys that would take the console or the keyboard itself away from the player are refused; a key the
// game uses is fine.
const char* ConsoleBinds::refusal(int scancode) const {
    if (scancode == 0) {
        return "unknown key";
    }
    const int console_key = host_.console_scancode();
    if ((console_key != 0 && scancode == console_key) || scancode == usage::kGrave) {
        return "that is the console key";
    }
    if (scancode == usage::kReturn || scancode == usage::kKeypadEnter || scancode == usage::kBackspace) {
        return "that key is part of the console";
    }
    if (scancode >= usage::kFirstModifier && scancode <= usage::kLastModifier) {
        return "modifier keys cannot be bound";
    }
    return nullptr;
}

int ConsoleBinds::find(int scancode) const {
    for (std::size_t i = 0; i < binds_.size(); ++i) {
        if (binds_[i].scancode == scancode) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void ConsoleBinds::save() {
    std::vector<std::string> lines;
    lines.reserve(binds_.size());
    for (const Entry& b : binds_) {
        lines.push_back("bind " + b.key + " " + b.commands);
    }
    host_.save(lines);
}

bool ConsoleBinds::set(std::string_view key_in, std::string_view commands_in, bool quiet) {
    const std::string_view key = trimmed(key_in.substr(0, kKeyChars));
    std::string_view commands = trimmed(commands_in.substr(0, kCommandChars));
    if (key.empty() || commands.empty()) {
        return false;
    }

    // Quotes around the whole list are not needed, but they are a habit from other consoles; a command that
    // begins with a quote would never run, so the pair is dropped.
    if (commands.size() >= 2 && (commands.front() == '"' || commands.front() == '\'') &&
        commands.back() == commands.front()) {
        commands = trimmed(commands.substr(1, commands.size() - 2));
        if (commands.empty()) {
            return false;
        }
    }

    // By key cap, as typed: the same line means the same cap on every keyboard layout.
    const int scancode = host_.scancode_of(key);
    if (const char* why = refusal(scancode)) {
        if (!quiet) {
            host_.print(line(std::string("bind: ") + why + " (" + std::string(key) + ")"));
        }
        return false;
    }

    // A bind that could change the binds would let a shared config rebind the keys around it when pressed.
    bool edits_binds = false;
    for_each_command(commands, [&](std::string_view one) {
        edits_binds = edits_binds || starts_with_word(one, "BIND") || starts_with_word(one, "UNBIND");
    });
    if (edits_binds) {
        if (!quiet) {
            host_.print("bind: a bind cannot contain bind/unbind");
        }
        return false;
    }

    int at = find(scancode);
    if (at < 0) {
        if (binds_.size() >= kMaxBinds) {
            if (!quiet) {
                host_.print("bind: full (" + std::to_string(kMaxBinds) + " binds)");
            }
            return false;
        }
        binds_.emplace_back();
        at = static_cast<int>(binds_.size()) - 1;
    }
    Entry& b = binds_[static_cast<std::size_t>(at)];
    b.key = key;
    b.commands = commands;
    b.scancode = scancode;
    // Held while it was being bound: it must not fire on the next frame.
    b.was_down = true;
    return true;
}

void ConsoleBinds::command_bind(std::string_view arg) {
    if (arg.empty()) {
        usage();
        return;
    }
    if (arg.size() == 4 && starts_with_word(arg, "LIST")) {
        if (binds_.empty()) {
            host_.print("no binds set (type 'bind' for usage)");
            return;
        }
        for (const Entry& b : binds_) {
            host_.print(line("bind " + b.key + " " + b.commands));
        }
        if (!host_.commands_allowed()) {
            host_.print("(binds are idle: allow_debug_controls is off)");
        }
        return;
    }

    std::string_view key;
    std::string_view commands;
    if (!split_key(arg, &key, &commands)) {
        host_.print(line("bind: no commands given for " + std::string(arg)));
        usage();
        return;
    }
    if (!set(key, commands, false)) {
        return;
    }
    save();
    host_.print(line("bound " + std::string(key) + " to " + std::string(commands)));
}

void ConsoleBinds::command_unbind(std::string_view arg) {
    if (arg.empty()) {
        host_.print("unbind <key>   (unbindall clears every bind)");
        return;
    }
    const int scancode = host_.scancode_of(arg);
    const int at = scancode != 0 ? find(scancode) : -1;
    if (at < 0) {
        host_.print(line("unbind: nothing bound to " + std::string(arg)));
        return;
    }
    const Entry& gone = binds_[static_cast<std::size_t>(at)];
    host_.print(line("unbound " + gone.key + " (was " + gone.commands + ")"));
    binds_.erase(binds_.begin() + at);
    save();
}

void ConsoleBinds::command_unbind_all() {
    const std::size_t count = binds_.size();
    binds_.clear();
    save();
    host_.print("cleared " + std::to_string(count) + (count == 1 ? " bind" : " binds"));
}

void ConsoleBinds::load_config_line(std::string_view rest) {
    while (!rest.empty() && is_blank(rest.front())) {
        rest.remove_prefix(1);
    }
    std::string_view key;
    std::string_view commands;
    if (!split_key(rest, &key, &commands)) {
        return;
    }
    if (!set(key, commands, true)) {
        host_.rejected_config_line(key, commands);
    }
}

void ConsoleBinds::update(std::span<const std::uint8_t> key_down) {
    // Binds run console commands, so the console's own switch covers them: otherwise a shared config could
    // hand somebody cheats they never turned on.
    if (binds_.empty() || !host_.commands_allowed()) {
        return;
    }

    // The config is read before the keyboard layout is known, when a cap can only mean its US position.
    if (!caps_resolved_) {
        caps_resolved_ = true;
        for (Entry& b : binds_) {
            if (const int scancode = host_.scancode_of(b.key); scancode != 0) {
                b.scancode = scancode;
            }
        }
    }

    for (std::size_t i = 0; i < binds_.size(); ++i) {
        Entry& b = binds_[i];
        const bool down =
            b.scancode > 0 && static_cast<std::size_t>(b.scancode) < key_down.size() && key_down[b.scancode] != 0;
        const bool pressed = down && !b.was_down;
        b.was_down = down;
        if (!pressed) {
            continue;
        }
        // Its own copy: the commands run against the console, which must not be able to pull the text away.
        const std::string commands = b.commands;
        for_each_command(commands, [&](std::string_view one) {
            if (!one.empty()) {
                host_.run(one);
            }
        });
    }
}

}  // namespace sh::input
