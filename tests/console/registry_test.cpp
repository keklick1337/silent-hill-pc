#include "framework/sh_test.h"

#include "engine/console/registry.h"

#include <string>
#include <vector>

using namespace sh::console;

namespace {

struct Collect : Output {
    std::vector<std::pair<Level, std::string>> lines;
    void print(Level l, std::string_view s) override { lines.emplace_back(l, std::string(s)); }
    bool has(std::string_view needle) const {
        for (auto& [l, s] : lines) {
            if (s.find(needle) != std::string::npos) return true;
        }
        return false;
    }
};

ArgSpec arg(const char* name, ArgType type, bool optional = false) {
    ArgSpec a;
    a.name = name;
    a.type = type;
    a.optional = optional;
    return a;
}

CommandSpec cmd(const char* name, std::vector<ArgSpec> args, RunFn run) {
    CommandSpec c;
    c.name = name;
    c.args = std::move(args);
    c.run = std::move(run);
    return c;
}

RunFn noop() { return [](Context&, const Args&) { return sh::Result<void>::ok(); }; }

}  // namespace

SH_TEST(reg_usage_of) {
    CommandSpec c = cmd("spawn", {arg("character", ArgType::String), arg("state", ArgType::String, true),
                                  arg("text", ArgType::Rest, true)}, noop());
    SH_CHECK(usage_of(c) == "spawn <character> [state] [text...]");
    SH_CHECK(usage_of(cmd("x", {}, noop())) == "x");
}

SH_TEST(reg_add_validation) {
    Registry r;
    SH_CHECK(!r.add(cmd("", {}, noop())));
    SH_CHECK(!r.add(cmd("a b", {}, noop())));
    SH_CHECK(!r.add(cmd("nofn", {}, nullptr)));
    SH_CHECK(!r.add(cmd("badrest", {arg("r", ArgType::Rest), arg("x", ArgType::String)}, noop())));
    SH_CHECK(!r.add(cmd("badopt", {arg("o", ArgType::String, true), arg("x", ArgType::String)}, noop())));
    SH_CHECK(r.add(cmd("Good", {}, noop())));
    SH_CHECK(r.find("GOOD") != nullptr && r.find("good")->name == "good");
}

SH_TEST(reg_clash_and_replace) {
    Registry r;
    CommandSpec a = cmd("alpha", {}, noop());
    a.aliases = {"al"};
    SH_CHECK(r.add(a));
    SH_CHECK(r.find("AL") == r.find("alpha"));
    SH_CHECK(!r.add(cmd("al", {}, noop())));
    CommandSpec b = cmd("beta", {}, noop());
    b.aliases = {"alpha"};
    SH_CHECK(!r.add(b));
    CommandSpec other = cmd("alpha", {}, noop());
    other.owner = "mod";
    SH_CHECK(!r.add(other));  // same name, different owner
    CommandSpec again = cmd("alpha", {}, noop());
    again.aliases = {"a2"};
    SH_CHECK(r.add(again));  // same owner replaces, old aliases vanish
    SH_CHECK(r.find("al") == nullptr && r.find("a2") != nullptr);
    SH_CHECK(r.commands().size() == 1);
}

SH_TEST(reg_remove_owner) {
    Registry r;
    CommandSpec a = cmd("one", {}, noop());
    a.owner = "mod";
    a.aliases = {"o"};
    CommandSpec b = cmd("two", {}, noop());
    b.owner = "mod";
    SH_CHECK(r.add(a) && r.add(b) && r.add(cmd("keep", {}, noop())));
    SH_CHECK(r.remove_owner("mod") == 2);
    SH_CHECK(r.find("one") == nullptr && r.find("o") == nullptr && r.find("keep") != nullptr);
    r.remove("keep");
    SH_CHECK(r.commands().empty());
}

SH_TEST(reg_names_sorted_with_aliases) {
    Registry r;
    CommandSpec a = cmd("zed", {}, noop());
    a.aliases = {"zz"};
    r.add(a);
    r.add(cmd("alpha", {}, noop()));
    SH_CHECK(r.add_alias("Mine", "alpha"));
    auto n = r.names();
    SH_CHECK((n == std::vector<std::string>{"alpha", "mine", "zed", "zz"}));
    auto c = r.commands();
    SH_CHECK(c.size() == 2 && c[0]->name == "alpha" && c[1]->name == "zed");
    SH_CHECK(r.user_alias("MINE") && *r.user_alias("MINE") == "alpha");
    SH_CHECK(!r.add_alias("alpha", "zed"));  // cannot shadow a command
}

SH_TEST(reg_parse_typed_args) {
    ArgSpec e = arg("mode", ArgType::Enum);
    e.values = {"Fast", "slow"};
    CommandSpec c = cmd("t", {arg("i", ArgType::Int), arg("f", ArgType::Float), arg("b", ArgType::Bool), e,
                              arg("s", ArgType::String, true)}, noop());
    auto ok = Registry::parse_args(c, {"-0x10", "1.5", "Yes", "FAST"});
    SH_CHECK(ok);
    const Args& a = ok.value();
    SH_CHECK(a.integer(0) == -16 && a.number(1) == 1.5 && a.boolean(2) && a.str(3) == "Fast");
    SH_CHECK(!a.has(4) && a.str(4).empty() && a.integer(4, 7) == 7 && a.boolean(4, true));
    SH_CHECK(Registry::parse_args(c, {"+12", "2", "off", "slow", "x"}).value().integer(0) == 12);
    SH_CHECK(Registry::parse_args(c, {"010", "2", "off", "slow"}).value().integer(0) == 10);

    auto bad = Registry::parse_args(c, {"abc", "1", "1", "fast"});
    SH_CHECK(!bad && bad.error() == "argument 'i': expected integer, got 'abc'");
    SH_CHECK(!Registry::parse_args(c, {"1", "1.5x", "1", "fast"}));
    SH_CHECK(!Registry::parse_args(c, {"1", "", "1", "fast"}));
    SH_CHECK(!Registry::parse_args(c, {"1", "1", "maybe", "fast"}));
    SH_CHECK(!Registry::parse_args(c, {"0x", "1", "1", "fast"}));
    SH_CHECK(!Registry::parse_args(c, {"99999999999999999999", "1", "1", "fast"}));
    auto en = Registry::parse_args(c, {"1", "1", "1", "medium"});
    SH_CHECK(!en && en.error().find("Fast, slow") != std::string::npos);
    const CommandSpec b = cmd("b", {arg("b", ArgType::Bool)}, noop());
    for (const char* t : {"true", "On", "1", "YES"}) SH_CHECK(Registry::parse_args(b, {t}).value().boolean(0));
    for (const char* t : {"false", "OFF", "0", "no"}) {
        SH_CHECK(!Registry::parse_args(b, {t}).value().boolean(0, true));
    }
}

SH_TEST(reg_parse_arg_counts_and_rest) {
    CommandSpec c = cmd("c", {arg("a", ArgType::String), arg("b", ArgType::String, true)}, noop());
    auto few = Registry::parse_args(c, {});
    SH_CHECK(!few && few.error().find("c <a> [b]") != std::string::npos);
    auto many = Registry::parse_args(c, {"1", "2", "3"});
    SH_CHECK(!many && many.error().find("c <a> [b]") != std::string::npos);
    CommandSpec r = cmd("r", {arg("a", ArgType::String), arg("rest", ArgType::Rest)}, noop());
    auto ok = Registry::parse_args(r, {"x", "hello", "big", "world"});
    SH_CHECK(ok && ok.value().size() == 2 && ok.value().str(1) == "hello big world");
    SH_CHECK(ok.value().joined(0) == "x hello big world");
    SH_CHECK(ok.value().joined(1) == "hello big world");
    SH_CHECK(!Registry::parse_args(r, {"x"}));  // required Rest
}

SH_TEST(reg_execute_basic) {
    Registry r;
    std::vector<long long> got;
    r.add(cmd("set", {arg("n", ArgType::Int)}, [&](Context&, const Args& a) {
        got.push_back(a.integer(0));
        return sh::Result<void>::ok();
    }));
    r.add(cmd("fail", {}, [](Context&, const Args&) { return sh::Result<void>::fail("boom"); }));
    Collect out;
    SH_CHECK(r.execute("SET 1; set \"2\" ;; set 0x3", out));
    SH_CHECK((got == std::vector<long long>{1, 2, 3}));
    SH_CHECK(out.lines.empty());

    got.clear();
    SH_CHECK(!r.execute("set 1; fail; set 2", out));
    SH_CHECK((got == std::vector<long long>{1}));
    SH_CHECK(out.lines.size() == 1 && out.lines[0].first == Level::Error && out.lines[0].second == "boom");

    Collect o2;
    SH_CHECK(!r.execute("nope 1", o2));
    SH_CHECK(o2.has("unknown command 'nope'"));
    Collect o3;
    SH_CHECK(!r.execute("set abc", o3));
    SH_CHECK(o3.has("argument 'n': expected integer, got 'abc'"));
    Collect o4;
    SH_CHECK(!r.execute("set", o4));
    SH_CHECK(o4.has("usage: set <n>"));
    SH_CHECK(r.execute("", o4) && r.execute(" ; ", o4));
}

SH_TEST(reg_execute_aliases) {
    Registry r;
    register_builtin_commands(r);
    Collect out;
    SH_CHECK(r.execute("alias hi echo hello", out));
    SH_CHECK(r.execute("hi world; HI", out));
    SH_CHECK(out.lines.size() == 2);
    SH_CHECK(out.lines[0].second == "hello world" && out.lines[1].second == "hello");
    SH_CHECK(r.execute("alias both \"echo a; echo b\"", out));
    out.lines.clear();
    SH_CHECK(r.execute("both", out));
    SH_CHECK(out.lines.size() == 2 && out.lines[0].second == "a" && out.lines[1].second == "b");
    SH_CHECK(r.execute("unalias hi", out));
    SH_CHECK(!r.execute("hi", out));
    SH_CHECK(!r.execute("unalias hi", out));
}

SH_TEST(reg_alias_loops) {
    Registry r;
    register_builtin_commands(r);
    r.add_alias("a", "b");
    r.add_alias("b", "a");
    Collect out;
    SH_CHECK(!r.execute("a", out));
    SH_CHECK(out.has("too deep"));
    r.add_alias("self", "self x");
    Collect o2;
    SH_CHECK(!r.execute("self", o2));
    // the registry is still usable afterwards (depth restored)
    r.add_alias("ok", "echo fine");
    Collect o3;
    SH_CHECK(r.execute("ok", o3) && o3.lines.size() == 1);
}

SH_TEST(reg_builtin_help_listing) {
    Registry r;
    register_builtin_commands(r);
    CommandSpec g = cmd("spawn", {}, noop());
    g.group = "game";
    g.help = "spawn a character";
    r.add(g);
    CommandSpec ungrouped = cmd("zzz", {}, noop());
    ungrouped.help = "sleeps";
    r.add(ungrouped);
    Collect out;
    SH_CHECK(r.execute("help", out));
    bool saw_game = false, saw_other = false;
    for (auto& [l, s] : out.lines) {
        if (s == "game:") saw_game = l == Level::Help;
        if (s == "other:") saw_other = l == Level::Help;
    }
    SH_CHECK(saw_game && saw_other);
    SH_CHECK(out.has("  spawn - spawn a character"));
    SH_CHECK(out.has("  zzz - sleeps"));
    SH_CHECK(out.has("  echo - "));
}

SH_TEST(reg_builtin_help_command) {
    Registry r;
    register_builtin_commands(r);
    CommandSpec g = cmd("spawn", {arg("who", ArgType::String)}, noop());
    g.help = "spawn a character";
    g.details = "line one\nline two";
    g.aliases = {"sp", "mk"};
    r.add(g);
    Collect out;
    SH_CHECK(r.execute("help SP", out));
    SH_CHECK(out.lines.size() == 5);
    SH_CHECK(out.lines[0].second == "usage: spawn <who>");
    SH_CHECK(out.lines[1].second == "spawn a character");
    SH_CHECK(out.lines[2].second == "line one" && out.lines[3].second == "line two");
    SH_CHECK(out.lines[4].second == "aliases: sp, mk");
    Collect o2;
    SH_CHECK(!r.execute("help nothing", o2));
}

SH_TEST(reg_builtin_echo_and_alias_list) {
    Registry r;
    register_builtin_commands(r);
    Collect out;
    SH_CHECK(r.execute("echo \"a  b\" c   d", out));
    SH_CHECK(out.lines.back().second == "a  b c d");
    SH_CHECK(r.execute("alias", out));
    SH_CHECK(out.has("no aliases"));
    r.execute("alias q echo 1", out);
    out.lines.clear();
    r.execute("alias", out);
    SH_CHECK(out.has("q = echo 1"));
    out.lines.clear();
    r.execute("alias q", out);
    SH_CHECK(out.has("q = echo 1"));
    SH_CHECK(!r.execute("alias help echo", out));  // cannot shadow a command
}
