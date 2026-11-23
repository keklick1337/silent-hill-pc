#include "framework/sh_test.h"

#include "engine/console/completion.h"

#include <string>
#include <vector>

using namespace sh::console;

namespace {

ArgSpec arg(const char* name, ArgType type, bool optional = false, const char* help = "") {
    ArgSpec a;
    a.name = name;
    a.type = type;
    a.optional = optional;
    a.help = help;
    return a;
}

sh::Result<void> noop(Context&, const Args&) { return sh::Result<void>::ok(); }

struct Fixture {
    Registry r;
    std::string last_prefix = "<unset>";
    int calls = 0;

    Fixture() {
        CommandSpec spawn;
        spawn.name = "spawn";
        spawn.help = "spawn a character";
        spawn.aliases = {"sp"};
        ArgSpec who = arg("character", ArgType::String, false, "who to spawn");
        who.complete = [this](std::string_view prefix, std::vector<Candidate>& out) {
            last_prefix = std::string(prefix);
            ++calls;
            out.push_back({"Harry", "hero"});
            out.push_back({"Cybil", ""});
            out.push_back({"harry", "dup"});
            out.push_back({"Big Dog", ""});
        };
        ArgSpec state = arg("state", ArgType::Enum, true, "initial state");
        state.values = {"idle", "walk", "Attack"};
        spawn.args = {who, state, arg("note", ArgType::Rest, true, "free text")};
        spawn.run = noop;
        r.add(spawn);

        CommandSpec toggle;
        toggle.name = "toggle";
        toggle.help = "flip";
        toggle.args = {arg("on", ArgType::Bool, false, "new value")};
        toggle.run = noop;
        r.add(toggle);

        CommandSpec set;
        set.name = "setting";
        set.args = {arg("a", ArgType::String), arg("b", ArgType::String)};
        set.run = noop;
        r.add(set);

        CommandSpec xs;
        xs.name = "xspawn";
        xs.run = noop;
        r.add(xs);
        r.add_alias("sm", "spawn Harry");
    }
};

std::vector<std::string> texts(const Completion& c) {
    std::vector<std::string> out;
    for (auto& i : c.items) out.push_back(i.text);
    return out;
}

using V = std::vector<std::string>;

}  // namespace

SH_TEST(cmp_helpers) {
    SH_CHECK(starts_with_ci("Spawn", "SPA") && !starts_with_ci("sp", "spa") && starts_with_ci("x", ""));
    SH_CHECK(contains_ci("xSPAWN", "paw") && !contains_ci("abc", "d") && contains_ci("abc", ""));
    std::vector<Candidate> v{{"Spawn", ""}, {"spark", ""}, {"SPAT", ""}};
    SH_CHECK(common_prefix(v) == "Spa");
    SH_CHECK(common_prefix({}) == "");
    SH_CHECK(common_prefix({{"only", ""}}) == "only");
}

SH_TEST(cmp_command_names_ranking) {
    Fixture f;
    Completion c = complete(f.r, "sp", 2);
    // prefix matches first (alphabetical, aliases included), then substring matches
    SH_CHECK((texts(c) == V{"sp", "spawn", "xspawn"}));
    SH_CHECK(c.word_index == 0 && c.replace_begin == 0 && c.replace_end == 2 && c.prefix == "sp");
    // "sp" is a known alias of spawn, so the usage of spawn shows
    SH_CHECK(c.usage == "spawn <character> [state] [note...]");
    SH_CHECK(c.active_arg == -1);
}

SH_TEST(cmp_command_names_empty_prefix) {
    Fixture f;
    Completion c = complete(f.r, "", 0);
    SH_CHECK(c.total == c.items.size());
    SH_CHECK((texts(c) == V{"setting", "sm", "sp", "spawn", "toggle", "xspawn"}));
    SH_CHECK(c.items[1].note == "alias: spawn Harry");
    SH_CHECK(c.items[2].note == "alias of spawn");
    SH_CHECK(c.problem.empty());
}

SH_TEST(cmp_case_insensitive_and_unknown) {
    Fixture f;
    Completion c = complete(f.r, "SPA", 3);
    SH_CHECK((texts(c) == V{"spawn", "xspawn"}));
    Completion u = complete(f.r, "zzz", 3);
    SH_CHECK(u.items.empty() && u.problem == "unknown command 'zzz'");
    Completion u2 = complete(f.r, "zzz foo", 7);
    SH_CHECK(u2.items.empty() && u2.problem == "unknown command 'zzz'");
}

SH_TEST(cmp_dynamic_provider_prefix_and_dedupe) {
    Fixture f;
    Completion c = complete(f.r, "spawn ha", 8);
    SH_CHECK(f.last_prefix == "ha" && f.calls == 1);
    SH_CHECK((texts(c) == V{"Harry"}));  // "harry" is a case-insensitive duplicate
    SH_CHECK(c.items[0].note == "hero");
    SH_CHECK(c.word_index == 1 && c.active_arg == 0 && c.arg_help == "who to spawn");
    SH_CHECK(c.usage == "spawn <character> [state] [note...]" && c.command_help == "spawn a character");
    SH_CHECK(c.replace_begin == 6 && c.replace_end == 8);
    Completion s = complete(f.r, "spawn y", 7);
    SH_CHECK((texts(s) == V{"Cybil", "Harry"}));  // substring matches only
    Completion all = complete(f.r, "spawn ", 6);
    SH_CHECK((texts(all) == V{"Big Dog", "Cybil", "Harry"}));
    SH_CHECK(f.last_prefix.empty());
}

SH_TEST(cmp_prefix_before_substring) {
    Fixture f;
    // "b": "Big Dog" starts with it; "Cybil" contains it
    Completion c = complete(f.r, "spawn b", 7);
    SH_CHECK((texts(c) == V{"Big Dog", "Cybil"}));
}

SH_TEST(cmp_enum_values_and_active_arg) {
    Fixture f;
    Completion c = complete(f.r, "spawn Harry a", 13);
    SH_CHECK((texts(c) == V{"Attack", "walk"}));  // "walk" matches as a substring only
    SH_CHECK(c.active_arg == 1 && c.word_index == 2 && c.arg_help == "initial state");
    Completion w = complete(f.r, "spawn Harry ", 12);
    SH_CHECK((texts(w) == V{"Attack", "idle", "walk"}));
}

SH_TEST(cmp_rest_stays_active) {
    Fixture f;
    Completion c = complete(f.r, "spawn Harry idle one two three", 30);
    SH_CHECK(c.active_arg == 2 && c.word_index == 5 && c.problem.empty());
    SH_CHECK(c.arg_help == "free text");
}

SH_TEST(cmp_too_many_arguments) {
    Fixture f;
    Completion c = complete(f.r, "toggle on extra", 15);
    SH_CHECK(c.problem == "too many arguments" && c.items.empty() && c.active_arg == -1);
    SH_CHECK(c.usage == "toggle <on>");
}

SH_TEST(cmp_bool_defaults) {
    Fixture f;
    Completion c = complete(f.r, "toggle ", 7);
    SH_CHECK((texts(c) == V{"0", "1", "off", "on"}));
    Completion o = complete(f.r, "toggle o", 8);
    SH_CHECK((texts(o) == V{"off", "on"}));
}

SH_TEST(cmp_edits_the_current_command_only) {
    Fixture f;
    const std::string_view line = "toggle 1; spawn ha";
    Completion c = complete(f.r, line, line.size());
    SH_CHECK(c.word_index == 1 && c.active_arg == 0 && c.replace_begin == 16 && c.replace_end == 18);
    SH_CHECK((texts(c) == V{"Harry"}));
    Completion d = complete(f.r, line, 7);
    SH_CHECK(d.word_index == 1 && d.usage == "toggle <on>");
    Completion e = complete(f.r, "toggle 1; ", 10);
    SH_CHECK(e.word_index == 0 && e.replace_begin == 10 && e.usage.empty());
}

SH_TEST(cmp_caret_in_middle_of_name_with_usage) {
    Fixture f;
    Completion c = complete(f.r, "spawn Harry", 3);
    SH_CHECK(c.word_index == 0 && c.prefix == "spa");
    SH_CHECK(c.usage == "spawn <character> [state] [note...]" && c.active_arg == -1);
}

SH_TEST(cmp_quoted_word_prefix) {
    Fixture f;
    Completion c = complete(f.r, "spawn \"big d", 12);
    SH_CHECK(f.last_prefix == "big d");
    SH_CHECK((texts(c) == V{"Big Dog"}));
    SH_CHECK(c.replace_begin == 6 && c.replace_end == 12);
}

SH_TEST(cmp_cap) {
    Registry r;
    CommandSpec c;
    c.name = "many";
    c.run = noop;
    ArgSpec a = arg("x", ArgType::String);
    a.complete = [](std::string_view, std::vector<Candidate>& out) {
        for (int i = 0; i < 300; ++i) out.push_back({"item" + std::to_string(1000 + i), ""});
    };
    c.args = {a};
    r.add(c);
    Completion res = complete(r, "many ", 5);
    SH_CHECK(res.total == 300 && res.items.size() == kMaxCandidates);
    SH_CHECK(res.items[0].text == "item1000");
}

SH_TEST(apply_command_name) {
    Fixture f;
    Completion c = complete(f.r, "spa", 3);
    Applied a = apply(f.r, "spa", c, c.items[0]);
    SH_CHECK(a.line == "spawn " && a.caret == 6);
    // an existing space is reused and the text after the word is preserved
    Completion d = complete(f.r, "spa Harry", 3);
    Applied b = apply(f.r, "spa Harry", d, d.items[0]);
    SH_CHECK(b.line == "spawn Harry" && b.caret == 6);
}

SH_TEST(apply_argument_space_rules) {
    Fixture f;
    Completion c = complete(f.r, "spawn Ha", 8);
    Applied a = apply(f.r, "spawn Ha", c, c.items[0]);
    SH_CHECK(a.line == "spawn Harry " && a.caret == 12);
    Completion t = complete(f.r, "toggle o", 8);
    Applied b = apply(f.r, "toggle o", t, t.items[0]);
    SH_CHECK(b.line == "toggle off" && b.caret == 10);
    Completion r = complete(f.r, "spawn Harry idle x", 18);
    Candidate pick{"extra", ""};
    Applied d = apply(f.r, "spawn Harry idle x", r, pick);
    SH_CHECK(d.line == "spawn Harry idle extra" && d.caret == 22);
    Completion e = complete(f.r, "spawn Harry wa", 14);
    Applied g = apply(f.r, "spawn Harry wa", e, e.items[0]);
    SH_CHECK(g.line == "spawn Harry walk " && g.caret == 17);
}

SH_TEST(apply_quoting) {
    Fixture f;
    Completion c = complete(f.r, "spawn big", 9);
    SH_CHECK(texts(c).size() == 1);
    Applied a = apply(f.r, "spawn big", c, c.items[0]);
    SH_CHECK(a.line == "spawn \"Big Dog\" ");
    // a quote the user opened is kept and closed
    Completion q = complete(f.r, "spawn \"big", 10);
    Applied b = apply(f.r, "spawn \"big", q, q.items[0]);
    SH_CHECK(b.line == "spawn \"Big Dog\" ");
    Completion s = complete(f.r, "spawn 'cy", 9);
    Applied d = apply(f.r, "spawn 'cy", s, s.items[0]);
    SH_CHECK(d.line == "spawn 'Cybil' ");
    // a double quote inside the candidate is escaped
    Candidate weird{"say \"hi\"", ""};
    Applied e = apply(f.r, "spawn \"s", q, weird);
    SH_CHECK(e.line == "spawn \"say \\\"hi\\\"\" ");
}

SH_TEST(apply_in_second_command) {
    Fixture f;
    const std::string_view line = "toggle 1; spa";
    Completion c = complete(f.r, line, line.size());
    Applied a = apply(f.r, line, c, c.items[0]);
    SH_CHECK(a.line == "toggle 1; spawn " && a.caret == a.line.size());
}

SH_TEST(apply_replaces_whole_word_with_following_text) {
    Fixture f;
    const std::string_view line = "spawn Ha idle";
    Completion c = complete(f.r, line, 8);
    Applied a = apply(f.r, line, c, c.items[0]);
    SH_CHECK(a.line == "spawn Harry idle" && a.caret == 12);
}
