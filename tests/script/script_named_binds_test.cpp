// The named-bind registry (engine/script/named_binds.h): registered names are served in front of the
// chain, other names go on unchanged, the list carries both, and Call checks arguments and room.

#include "framework/sh_test.h"

#include "engine/script/named_binds.h"

#include <cstring>
#include <string>

namespace {

namespace nb = sh::script::named;

int g_next_calls = 0;

int next_call(const char* name, int, const ShScriptArg*, long long* out, int, char*, int) {
    ++g_next_calls;
    out[0] = std::strcmp(name, "old.value") == 0 ? 7 : -7;
    return 1;
}

const char* const* next_list() {
    static const char* const kNames[] = {"old.value", nullptr};
    return kNames;
}

int twice(nb::Call& c) {
    long long v[1];
    if (!c.ints(1, v)) {
        return -1;
    }
    return c.reply({v[0] * 2, v[0] + 1});
}

int ranged(nb::Call& c) {
    long long v[1];
    if (!c.ints(1, v) || !c.in_range(v[0], 0, 2)) {
        return -1;
    }
    return 0;
}

const nb::Bind kBinds[] = {{"test.twice", twice}, {"test.ranged", ranged}};
const nb::Bind kDuplicate[] = {{"test.twice", ranged}};

ShScriptArg integer(long long v) { return ShScriptArg{1, v, nullptr}; }

}  // namespace

SH_TEST(script_named_binds_chain) {
    nb::reset();
    nb::add(kBinds);
    nb::add(kDuplicate);
    ShScriptGameApi api{};
    api.named_call = next_call;
    api.named_list = next_list;
    nb::install(api);
    SH_CHECK(nb::find("test.twice") == twice);
    SH_CHECK(nb::names().size() == 2);

    long long out[4] = {};
    char err[128] = {};
    const ShScriptArg three[] = {integer(3)};
    SH_CHECK(api.named_call("test.twice", 1, three, out, 4, err, sizeof err) == 2);
    SH_CHECK(out[0] == 6 && out[1] == 4);
    // No room for two answers.
    SH_CHECK(api.named_call("test.twice", 1, three, out, 1, err, sizeof err) == -1);
    SH_CHECK(std::string(err).find("no room") != std::string::npos);
    // Range and count checks.
    SH_CHECK(api.named_call("test.ranged", 1, three, out, 4, err, sizeof err) == -1);
    SH_CHECK(std::string(err) == "test.ranged: 3 is not in 0..2");
    SH_CHECK(api.named_call("test.ranged", 0, nullptr, out, 4, err, sizeof err) == -1);
    SH_CHECK(std::string(err) == "test.ranged: 1 arguments expected, got 0");
    const ShScriptArg text[] = {ShScriptArg{2, 0, "x"}};
    SH_CHECK(api.named_call("test.ranged", 1, text, out, 4, err, sizeof err) == -1);
    SH_CHECK(std::string(err) == "test.ranged: argument 1 must be a number");

    // Other names go on to the functions installed before.
    g_next_calls = 0;
    SH_CHECK(api.named_call("old.value", 0, nullptr, out, 4, err, sizeof err) == 1);
    SH_CHECK(out[0] == 7 && g_next_calls == 1);

    const char* const* list = api.named_list();
    std::string all;
    for (; *list != nullptr; ++list) {
        all += *list;
        all += ";";
    }
    SH_CHECK(all == "old.value;test.twice;test.ranged;");
    nb::reset();
}
