// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Console key binds (src/input/bind/console_binds.h) with a host in memory: a US keyboard where a letter's
// position is its USB usage number.

#include "framework/sh_test.h"

#include "input/bind/console_binds.h"

#include <array>
#include <cctype>
#include <string>
#include <vector>

using namespace sh::input;

namespace {

constexpr int kKeyK = 14, kKeyF = 9, kKeyA = 4, kGrave = 53, kReturn = 40, kBackspace = 42, kLeftShift = 225;
constexpr int kF1 = 58, kKeypad1 = 89;

class Host final : public ConsoleBindHost {
public:
    int scancode_of(std::string_view cap) const override {
        std::string s;
        for (const char c : cap) {
            s += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }
        if (s.size() == 1 && s[0] >= 'A' && s[0] <= 'Z') return 4 + (s[0] - 'A');
        if (s == "`") return kGrave;
        if (s == "RETURN") return kReturn;
        if (s == "BACKSPACE") return kBackspace;
        if (s == "LEFT SHIFT") return kLeftShift;
        if (s == "F1") return kF1;
        if (s == "KEYPAD") return kKeypad1;  // only the first word of "KEYPAD 1" reaches a bind
        return 0;
    }
    int console_scancode() const override { return console_key; }
    bool commands_allowed() const override { return allowed; }
    void print(std::string_view line) override { printed.emplace_back(line); }
    void run(std::string_view command) override { ran.emplace_back(command); }
    void save(std::span<const std::string> lines) override {
        ++saves;
        saved.assign(lines.begin(), lines.end());
    }
    void rejected_config_line(std::string_view key, std::string_view commands) override {
        rejected.push_back(std::string(key) + "|" + std::string(commands));
    }

    int console_key = kGrave;
    bool allowed = true;
    int saves = 0;
    std::vector<std::string> printed, ran, saved, rejected;
};

struct Keys {
    std::array<std::uint8_t, 512> down{};
    std::span<const std::uint8_t> span() const { return down; }
};

}  // namespace

SH_TEST(console_binds_set_saves_the_typed_line) {
    Host host;
    ConsoleBinds binds(host);
    binds.command_bind("k kill");
    SH_CHECK(host.saves == 1);
    SH_CHECK(host.saved == std::vector<std::string>{"bind k kill"});
    SH_CHECK(host.printed.back() == "bound k to kill");

    binds.command_bind("f   give shotgun;give shotgunammo;spawn groaner  ");
    SH_CHECK((host.saved == std::vector<std::string>{"bind k kill", "bind f give shotgun;give shotgunammo;spawn groaner"}));
    // The echo shows what was typed after the key, spaces included.
    SH_CHECK(host.printed.back() == "bound f to give shotgun;give shotgunammo;spawn groaner  ");

    // The same key again replaces its bind in place.
    binds.command_bind("K god");
    SH_CHECK((host.saved == std::vector<std::string>{"bind K god", "bind f give shotgun;give shotgunammo;spawn groaner"}));
    SH_CHECK(binds.entries().size() == 2);
}

SH_TEST(console_binds_config_lines_round_trip) {
    Host host;
    ConsoleBinds loaded(host);
    const std::vector<std::string> file = {"bind k kill", "bind f give shotgun;spawn groaner", "bind a noclip"};
    for (const std::string& line : file) {
        loaded.load_config_line(std::string_view(line).substr(5));
    }
    SH_CHECK(host.saves == 0 && host.printed.empty());  // loading never writes and never prints
    SH_CHECK(loaded.entries().size() == 3);
    // Any later save writes the same lines back.
    loaded.command_bind("f1 fps");
    std::vector<std::string> want = file;
    want.push_back("bind f1 fps");
    SH_CHECK(host.saved == want);
}

SH_TEST(console_binds_config_line_forms) {
    Host host;
    ConsoleBinds binds(host);
    binds.load_config_line("  \tk   \"kill; god\"\r\n");
    SH_CHECK(binds.entries().size() == 1);
    SH_CHECK(binds.entries()[0].key == "k" && binds.entries()[0].commands == "kill; god");
    binds.load_config_line("f 'noclip'");
    SH_CHECK(binds.entries()[1].commands == "noclip");
    binds.load_config_line("a");          // no commands: ignored without a report
    binds.load_config_line("a \"\"");     // empty after the quotes
    binds.load_config_line("zz kill");    // no such key
    binds.load_config_line("` kill");     // the console key
    SH_CHECK(binds.entries().size() == 2);
    SH_CHECK((host.rejected == std::vector<std::string>{"a|\"\"", "zz|kill", "`|kill"}));
    SH_CHECK(host.printed.empty());
}

SH_TEST(console_binds_refused_keys_and_commands) {
    Host host;
    ConsoleBinds binds(host);
    binds.command_bind("zz kill");
    SH_CHECK(host.printed.back() == "bind: unknown key (zz)");
    binds.command_bind("` kill");
    SH_CHECK(host.printed.back() == "bind: that is the console key (`)");
    host.console_key = kKeyA;
    binds.command_bind("a kill");
    SH_CHECK(host.printed.back() == "bind: that is the console key (a)");
    binds.command_bind("return kill");
    SH_CHECK(host.printed.back() == "bind: that key is part of the console (return)");
    binds.command_bind("backspace kill");
    SH_CHECK(host.printed.back() == "bind: that key is part of the console (backspace)");
    binds.command_bind("k kill;unbind f");
    SH_CHECK(host.printed.back() == "bind: a bind cannot contain bind/unbind");
    binds.command_bind("k god ; BIND f kill");
    SH_CHECK(host.printed.back() == "bind: a bind cannot contain bind/unbind");
    binds.command_bind("k");
    SH_CHECK(host.printed[host.printed.size() - 9] == "bind: no commands given for k");
    SH_CHECK(binds.entries().empty() && host.saves == 0);
}

SH_TEST(console_binds_modifiers_cannot_be_bound) {
    Host host;
    ConsoleBinds binds(host);
    // "left shift x" is the key "left" with commands "shift x" for the command; the host knows no key "left".
    binds.load_config_line("left shift");
    SH_CHECK(binds.entries().empty());
    struct ShiftHost final : ConsoleBindHost {
        int scancode_of(std::string_view) const override { return kLeftShift; }
        int console_scancode() const override { return 0; }
        bool commands_allowed() const override { return true; }
        void print(std::string_view line) override { last = line; }
        void run(std::string_view) override {}
        void save(std::span<const std::string>) override {}
        void rejected_config_line(std::string_view, std::string_view) override {}
        std::string last;
    } shift;
    ConsoleBinds other(shift);
    other.command_bind("shift kill");
    SH_CHECK(shift.last == "bind: modifier keys cannot be bound (shift)");
}

SH_TEST(console_binds_usage_and_list) {
    Host host;
    ConsoleBinds binds(host);
    binds.command_bind("");
    SH_CHECK(host.printed.size() == 8);
    SH_CHECK(host.printed[0] == "bind <key> <command>[;<command>...]  bind console commands to a key");
    host.printed.clear();
    binds.command_bind("list");
    SH_CHECK(host.printed == std::vector<std::string>{"no binds set (type 'bind' for usage)"});
    binds.command_bind("k kill");
    host.printed.clear();
    host.allowed = false;
    binds.command_bind("LIST");
    SH_CHECK((host.printed == std::vector<std::string>{"bind k kill", "(binds are idle: allow_debug_controls is off)"}));
}

SH_TEST(console_binds_unbind) {
    Host host;
    ConsoleBinds binds(host);
    binds.command_bind("k kill");
    binds.command_bind("f god");
    binds.command_unbind("");
    SH_CHECK(host.printed.back() == "unbind <key>   (unbindall clears every bind)");
    binds.command_unbind("a");
    SH_CHECK(host.printed.back() == "unbind: nothing bound to a");
    binds.command_unbind("K");
    SH_CHECK(host.printed.back() == "unbound k (was kill)");
    SH_CHECK(host.saved == std::vector<std::string>{"bind f god"});
    binds.command_unbind_all();
    SH_CHECK(host.printed.back() == "cleared 1 bind");
    SH_CHECK(host.saved.empty());
    binds.command_unbind_all();
    SH_CHECK(host.printed.back() == "cleared 0 binds");
}

SH_TEST(console_binds_fire_on_the_press_only) {
    Host host;
    ConsoleBinds binds(host);
    Keys keys;
    keys.down[kKeyK] = 1;  // held while it is bound
    binds.command_bind("k kill; ;god;");
    binds.update(keys.span());
    SH_CHECK(host.ran.empty());
    keys.down[kKeyK] = 0;
    binds.update(keys.span());
    keys.down[kKeyK] = 1;
    binds.update(keys.span());
    SH_CHECK((host.ran == std::vector<std::string>{"kill", "god"}));
    binds.update(keys.span());
    SH_CHECK(host.ran.size() == 2);  // still held
}

SH_TEST(console_binds_are_idle_without_console_commands) {
    Host host;
    ConsoleBinds binds(host);
    Keys keys;
    binds.load_config_line("k kill");
    host.allowed = false;
    keys.down[kKeyK] = 1;
    binds.update(keys.span());
    SH_CHECK(host.ran.empty());
    // Switched on while the key is down: a bind starts as held, so the key has to go up first.
    host.allowed = true;
    binds.update(keys.span());
    SH_CHECK(host.ran.empty());
    keys.down[kKeyK] = 0;
    binds.update(keys.span());
    keys.down[kKeyK] = 1;
    binds.update(keys.span());
    SH_CHECK(host.ran == std::vector<std::string>{"kill"});
}

SH_TEST(console_binds_limits) {
    Host host;
    ConsoleBinds binds(host);
    const std::string long_commands(300, 'x');
    binds.load_config_line("k " + long_commands);
    SH_CHECK(binds.entries()[0].commands.size() == ConsoleBinds::kCommandChars);

    struct ManyKeys final : ConsoleBindHost {
        int scancode_of(std::string_view cap) const override { return 100 + std::stoi(std::string(cap)); }
        int console_scancode() const override { return 0; }
        bool commands_allowed() const override { return true; }
        void print(std::string_view line) override { last = line; }
        void run(std::string_view) override {}
        void save(std::span<const std::string>) override {}
        void rejected_config_line(std::string_view, std::string_view) override {}
        std::string last;
    } many;
    ConsoleBinds full(many);
    for (int i = 0; i < 48; ++i) {
        full.command_bind(std::to_string(i) + " kill");
    }
    SH_CHECK(full.entries().size() == 48);
    full.command_bind("48 kill");
    SH_CHECK(many.last == "bind: full (48 binds)");
    SH_CHECK(full.entries().size() == 48);
    full.command_bind("7 god");  // a bound key can still be changed
    SH_CHECK(full.entries()[7].commands == "god");
}
