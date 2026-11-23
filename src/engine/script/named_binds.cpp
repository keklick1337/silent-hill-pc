// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/script/named_binds.h"

#include <cstdarg>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

namespace sh::script::named {

namespace {

using NamedCall = decltype(ShScriptGameApi::named_call);
using NamedList = decltype(ShScriptGameApi::named_list);

struct Registry {
    std::unordered_map<std::string_view, Handler> handlers;
    std::vector<const char*> order;
    NamedCall next_call = nullptr;
    NamedList next_list = nullptr;
    // The chained list: the names installed before, then ours; null-terminated.
    std::vector<const char*> list;
};

Registry& registry() {
    static Registry r;
    return r;
}

int chained_call(const char* name, int argc, const ShScriptArg* argv, long long* out, int out_cap, char* err,
                 int err_cap) {
    Registry& r = registry();
    if (name != nullptr) {
        if (const auto it = r.handlers.find(name); it != r.handlers.end()) {
            Call call(name, argc, argv, out, out_cap, err, err_cap);
            const int n = it->second(call);
            if (n != kPass) {
                return n;
            }
        }
    }
    if (r.next_call != nullptr) {
        return r.next_call(name, argc, argv, out, out_cap, err, err_cap);
    }
    std::snprintf(err, static_cast<std::size_t>(err_cap), "unknown function '%s'", name != nullptr ? name : "");
    return -1;
}

const char* const* chained_list() {
    Registry& r = registry();
    if (r.list.empty()) {
        if (r.next_list != nullptr) {
            for (const char* const* n = r.next_list(); n != nullptr && *n != nullptr; ++n) {
                r.list.push_back(*n);
            }
        }
        // A name the chain already lists (one a handler shares with it) is listed once.
        const std::size_t before = r.list.size();
        for (const char* n : r.order) {
            bool listed = false;
            for (std::size_t i = 0; i < before && !listed; ++i) {
                listed = std::string_view(r.list[i]) == n;
            }
            if (!listed) {
                r.list.push_back(n);
            }
        }
        r.list.push_back(nullptr);
    }
    return r.list.data();
}

}  // namespace

bool Call::is_nil_or_zero(int i) const {
    return i >= argc_ || argv_[i].kind == 0 || (is_int(i) && argv_[i].i == 0);
}

bool Call::ints(int n, long long* v) {
    if (argc_ != n) {
        fail("%s: %d arguments expected, got %d", name_, n, argc_);
        return false;
    }
    for (int i = 0; i < n; ++i) {
        if (!is_int(i)) {
            fail("%s: argument %d must be a number", name_, i + 1);
            return false;
        }
        v[i] = argv_[i].i;
    }
    return true;
}

bool Call::ints_at(int from, int n) const {
    for (int i = from; i < from + n; ++i) {
        if (!is_int(i)) {
            return false;
        }
    }
    return true;
}

bool Call::in_range(long long v, long long lo, long long hi) {
    if (v < lo || v > hi) {
        fail("%s: %lld is not in %lld..%lld", name_, v, lo, hi);
        return false;
    }
    return true;
}

int Call::fail(const char* fmt, ...) {
    if (err_ != nullptr && err_cap_ > 0) {
        va_list args;
        va_start(args, fmt);
        std::vsnprintf(err_, static_cast<std::size_t>(err_cap_), fmt, args);
        va_end(args);
    }
    return -1;
}

int Call::reply(std::span<const long long> values) {
    if (static_cast<int>(values.size()) > out_cap_) {
        return fail("%s: no room for the result", name_);
    }
    int n = 0;
    for (const long long v : values) {
        out_[n++] = v;
    }
    return n;
}

int Call::reply(std::initializer_list<long long> values) {
    return reply(std::span<const long long>(values.begin(), values.size()));
}

void add(std::span<const Bind> binds) {
    Registry& r = registry();
    for (const Bind& b : binds) {
        if (!r.handlers.emplace(b.name, b.fn).second) {
            std::fprintf(stderr, "[SCRIPT] named function %s is registered twice; the first one is kept\n", b.name);
            continue;
        }
        r.order.push_back(b.name);
    }
    r.list.clear();
}

Handler find(std::string_view name) {
    Registry& r = registry();
    const auto it = r.handlers.find(name);
    return it != r.handlers.end() ? it->second : nullptr;
}

std::span<const char* const> names() { return registry().order; }

void reset() { registry() = Registry{}; }

void install(ShScriptGameApi& api) {
    Registry& r = registry();
    r.next_call = api.named_call;
    r.next_list = api.named_list;
    r.list.clear();
    api.named_call = chained_call;
    api.named_list = chained_list;
}

}  // namespace sh::script::named
