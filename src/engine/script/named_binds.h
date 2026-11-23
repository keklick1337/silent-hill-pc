// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Registration of the named engine API sh.<group>.<name>(...): a subsystem
// registers a table of {"group.name", handler}; install() puts the registered names in front of the
// game API's named_call / named_list chain, so a registered name is served here and every other
// name goes on to the functions installed before. A handler reads its arguments and writes its
// answer through Call.

#pragma once

#include <initializer_list>
#include <span>
#include <string_view>

#include "engine/script/sh_script.h"

namespace sh::script::named {

// One call of a named function: its arguments (kinds: 0 nil, 1 integer, 2 string, 3 boolean) and
// the place for its answer and for an error message.
class Call {
public:
    Call(const char* name, int argc, const ShScriptArg* argv, long long* out, int out_cap, char* err, int err_cap)
        : name_(name), argc_(argc), argv_(argv), out_(out), out_cap_(out_cap), err_(err), err_cap_(err_cap) {}

    const char* name() const { return name_; }
    int argc() const { return argc_; }

    // Integers and booleans read as integers.
    bool is_int(int i) const { return i >= 0 && i < argc_ && (argv_[i].kind == 1 || argv_[i].kind == 3); }
    bool is_text(int i) const { return i >= 0 && i < argc_ && argv_[i].kind == 2 && argv_[i].s != nullptr; }
    // Missing, nil, or the integer 0.
    bool is_nil_or_zero(int i) const;
    long long integer(int i, long long fallback = 0) const { return is_int(i) ? argv_[i].i : fallback; }
    const char* text(int i) const { return is_text(i) ? argv_[i].s : nullptr; }

    // Exactly `n` arguments, all integers, copied into v; otherwise false with the error set.
    bool ints(int n, long long* v);
    // Arguments [from, from + n) are integers (more may follow).
    bool ints_at(int from, int n) const;
    // v in lo..hi; otherwise false with the error set.
    bool in_range(long long v, long long lo, long long hi);

    // The error message (printf format); returns -1, the call's failure.
    int fail(const char* fmt, ...)
#if defined(__GNUC__)
        __attribute__((format(printf, 2, 3)))
#endif
        ;
    // The answer: the values, or a failure when there is no room for them.
    int reply(std::initializer_list<long long> values);
    int reply(std::span<const long long> values);

private:
    const char* name_;
    int argc_;
    const ShScriptArg* argv_;
    long long* out_;
    int out_cap_;
    char* err_;
    int err_cap_;
};

// A handler returns the number of answers (0 for none), -1 after Call::fail, or kPass when the call is not its
// to serve (a name shared with the functions installed before): the call goes on to them unchanged.
constexpr int kPass = -2;
using Handler = int (*)(Call&);

struct Bind {
    const char* name;  // "group.name"
    Handler fn;
};

// Adds binds to the registry (the tables must outlive the process). A name already registered keeps
// its first handler; the second registration is reported in the log.
void add(std::span<const Bind> binds);
// The registered handler of `name`, or nullptr.
Handler find(std::string_view name);
// Every registered name, in registration order.
std::span<const char* const> names();
// Drops every registration and the chain (tests).
void reset();

// Puts the registry in front of the api's named_call / named_list.
void install(ShScriptGameApi& api);

}  // namespace sh::script::named
