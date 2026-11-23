// The Lua editor's knowledge of the code (engine/devtools/lua_assist.h): the
// names of a script state, the highlighter's use of them, completion, the call at the caret, the API descriptions
// and the files "New script" writes.

#include "engine/devtools/lua_assist.h"
#include "engine/devtools/lua_edit.h"

#include "framework/sh_test.h"

#include <string>
#include <vector>

namespace {

namespace dt = sh::devtools;

dt::LuaNames sample_names() {
    dt::LuaNames library;
    dt::read_lua_names("t string\nf string.format\nf pairs\nt math\nf math.floor\n", nullptr, library);
    dt::LuaNames names;
    dt::read_lua_names("t sh\nf sh.log\nt sh.npc\nf sh.npc.spawn\nf sh.npc.spawn_position_set\nf sh.npc.clear\n"
                       "v sh.NO_VALUE\nt string\nf string.format\nf pairs\nf wait\nf MapEvent_Door\nbroken line\n",
                       &library, names);
    return names;
}

std::vector<std::string> spans(std::string_view line, const dt::LuaNames* names) {
    dt::LuaLexState st;
    std::vector<std::string> out;
    for (const dt::LuaSpan& s : dt::tokenize_lua_line(line, st, names)) {
        const char* k = "?";
        switch (s.kind) {
        case dt::LuaToken::Keyword: k = "K"; break;
        case dt::LuaToken::String: k = "S"; break;
        case dt::LuaToken::Comment: k = "C"; break;
        case dt::LuaToken::Number: k = "N"; break;
        case dt::LuaToken::Api: k = "A"; break;
        case dt::LuaToken::ApiUnknown: k = "U"; break;
        case dt::LuaToken::Library: k = "L"; break;
        case dt::LuaToken::FunctionDef: k = "D"; break;
        case dt::LuaToken::Call: k = "F"; break;
        default: break;
        }
        out.push_back(std::string(k) + ":" + std::string(line.substr(s.begin, s.end - s.begin)));
    }
    return out;
}

std::vector<std::string> words(const std::vector<dt::LuaCompletion>& list) {
    std::vector<std::string> out;
    for (const dt::LuaCompletion& c : list) out.push_back(c.text);
    return out;
}

bool has(const std::vector<std::string>& list, const std::string& word) {
    for (const std::string& w : list)
        if (w == word) return true;
    return false;
}

}  // namespace

SH_TEST(lua_assist_names_origin_and_members) {
    const dt::LuaNames names = sample_names();
    SH_CHECK(names.size() == 12);
    SH_CHECK(names.find("sh.npc.spawn") != nullptr);
    SH_CHECK(names.find("sh.npc.spawn")->origin == dt::LuaOrigin::Engine);
    SH_CHECK(names.find("sh.npc")->kind == dt::LuaNameKind::Table);
    SH_CHECK(names.find("string.format")->origin == dt::LuaOrigin::Library);
    SH_CHECK(names.find("wait")->origin == dt::LuaOrigin::Script);
    SH_CHECK(names.find("sh.nope") == nullptr);
    // Direct members only, in name order.
    const auto sh_members = names.members("sh");
    SH_CHECK(sh_members.size() == 3);
    SH_CHECK(sh_members[0]->name == "sh.NO_VALUE");
    SH_CHECK(sh_members[2]->name == "sh.npc");
    SH_CHECK(names.members("sh.npc").size() == 3);
    SH_CHECK(names.members("").size() == 5);
}

SH_TEST(lua_assist_tokenizer_api_library_calls_and_definitions) {
    const dt::LuaNames names = sample_names();
    auto t = spans("sh.npc.spawn(1) string.format(\"x\") foo(2) sh.npc.nope() obj:method()", &names);
    SH_CHECK(t.size() == 9);
    SH_CHECK(t[0] == "A:sh.npc.spawn");
    SH_CHECK(t[1] == "N:1");
    SH_CHECK(t[2] == "L:string.format");
    SH_CHECK(t[3] == "S:\"x\"");
    SH_CHECK(t[4] == "F:foo");
    SH_CHECK(t[5] == "N:2");
    SH_CHECK(t[6] == "A:sh.npc");
    SH_CHECK(t[7] == "U:nope");
    SH_CHECK(t[8] == "F:method");
    // The name after `function` is a definition, with or without a table and a method.
    t = spans("local function helper(a) end function M.sub:run() end x = function() end", &names);
    SH_CHECK(has(t, "D:helper"));
    SH_CHECK(has(t, "D:M.sub:run"));
    SH_CHECK(!has(t, "D:x"));
    // A field of some other value named like a global is not that global; the scripts' own globals stay plain.
    t = spans("local a = t.string.format wait(1) v = sh.NO_VALUE", &names);
    SH_CHECK(!has(t, "L:string.format"));
    SH_CHECK(has(t, "F:wait"));
    SH_CHECK(has(t, "A:sh.NO_VALUE"));
    // Without names only calls and definitions are told apart.
    t = spans("sh.log(\"a\") .. tostring(b)", nullptr);
    SH_CHECK(t.size() == 3);
    SH_CHECK(t[0] == "F:log");
    SH_CHECK(t[2] == "F:tostring");
}

SH_TEST(lua_assist_completion_context) {
    const std::string text = "local s = \"sh.npc.\" -- sh.\nsh.npc.sp\nfoo():ba\nx = 12.";
    auto at = [&](const char* marker, int extra) {
        return static_cast<int>(text.find(marker)) + static_cast<int>(std::string(marker).size()) + extra;
    };
    dt::LuaCompletionContext c = dt::completion_context(text, at("\nsh.npc.sp", 0));
    SH_CHECK(c.valid && c.table == "sh.npc" && c.partial == "sp" && !c.loose);
    SH_CHECK(text.substr(static_cast<std::size_t>(c.begin), 2) == "sp");
    c = dt::completion_context(text, at("\nsh.npc.", 0));
    SH_CHECK(c.valid && c.table == "sh.npc" && c.partial.empty());
    c = dt::completion_context(text, at("\nsh", 0));
    SH_CHECK(c.valid && c.table.empty() && c.partial == "sh");
    // After a ':' or a '.' on a value only the file's own words can follow.
    c = dt::completion_context(text, at("foo():ba", 0));
    SH_CHECK(c.valid && c.loose && c.partial == "ba");
    // Not in a string, a comment or a number, nor where nothing is being typed.
    SH_CHECK(!dt::completion_context(text, at("\"sh.npc.", 0)).valid);
    SH_CHECK(!dt::completion_context(text, at("-- sh.", 0)).valid);
    SH_CHECK(!dt::completion_context(text, at("x = 12.", 0)).valid);
    SH_CHECK(!dt::completion_context(text, at("local s =", 0)).valid);
    // A long string left open by an earlier line.
    const std::string long_text = "local t = [[\nsh.npc.\n]]\nsh.npc.";
    SH_CHECK(!dt::completion_context(long_text, static_cast<int>(long_text.find("\n]]"))).valid);
    SH_CHECK(dt::completion_context(long_text, static_cast<int>(long_text.size())).valid);
}

SH_TEST(lua_assist_completions) {
    const dt::LuaNames names = sample_names();
    std::string text = "local spare = 1\nlocal cfg = {}\ncfg.speed = 2\nsh.npc.sp";
    int caret = static_cast<int>(text.size());
    auto list = words(dt::lua_completions(dt::completion_context(text, caret), names, text, caret));
    SH_CHECK(list.size() == 2);
    SH_CHECK(list[0] == "spawn" && list[1] == "spawn_position_set");
    // Every member after the dot; the dotted name of a completion is the key of its description.
    text = "sh.npc.";
    caret = static_cast<int>(text.size());
    const auto full = dt::lua_completions(dt::completion_context(text, caret), names, text, caret);
    SH_CHECK(full.size() == 3);
    SH_CHECK(full[0].full == "sh.npc.clear" && full[0].kind == dt::LuaCompletionKind::Function);
    SH_CHECK(full[0].origin == dt::LuaOrigin::Engine);
    // A bare word: keywords, globals and the file's identifiers, the prefix matches first, the case ignored.
    text = "local spare = 1\nlocal Speed = 2\nsp";
    caret = static_cast<int>(text.size());
    list = words(dt::lua_completions(dt::completion_context(text, caret), names, text, caret));
    SH_CHECK(list.size() == 2);
    SH_CHECK(list[0] == "spare" && list[1] == "Speed");
    text = "wh";
    list = words(dt::lua_completions(dt::completion_context(text, 2), names, text, 2));
    SH_CHECK(list.size() == 1 && list[0] == "while");
    text = "pa";
    list = words(dt::lua_completions(dt::completion_context(text, 2), names, text, 2));
    SH_CHECK(has(list, "pairs") && !has(list, "pa"));
    // Members of a table the state does not know: the fields the file itself uses.
    text = "local cfg = {}\ncfg.speed = 2\ncfg.size = 3\nprint(cfg.s";
    caret = static_cast<int>(text.size());
    list = words(dt::lua_completions(dt::completion_context(text, caret), names, text, caret));
    SH_CHECK(list.size() == 2);
    SH_CHECK(has(list, "speed") && has(list, "size"));
    // Words inside strings and comments are not offered.
    text = "-- spooky\nlocal s = \"sparkle\"\nsp";
    caret = static_cast<int>(text.size());
    SH_CHECK(dt::lua_completions(dt::completion_context(text, caret), names, text, caret).empty());
}

SH_TEST(lua_assist_call_at) {
    std::string text = "sh.npc.spawn(1, foo(2, 3), {4, 5}, ";
    dt::LuaCallContext c = dt::call_at(text, static_cast<int>(text.size()));
    SH_CHECK(c.in_call && c.name == "sh.npc.spawn" && c.argument == 3);
    c = dt::call_at(text, static_cast<int>(text.find("3")));
    SH_CHECK(c.in_call && c.name == "foo" && c.argument == 1);
    // A grouping parenthesis is not a call; the search goes on outwards.
    text = "sh.log((a + b";
    c = dt::call_at(text, static_cast<int>(text.size()));
    SH_CHECK(c.in_call && c.name == "sh.log" && c.argument == 0);
    // `if (` and `function (` are not calls: the name under the caret, or none.
    text = "if (x) then sh.npc.clear";
    c = dt::call_at(text, static_cast<int>(text.size()) - 2);
    SH_CHECK(!c.in_call && c.name == "sh.npc.clear");
    c = dt::call_at("local x = 1 + ", 14);
    SH_CHECK(!c.in_call && c.name.empty());
}

SH_TEST(lua_assist_api_docs) {
    // Raw strings are kept out of the check macro: the preprocessor does not take them as arguments.
    const std::string first = R"json({"description": "x", "functions": {
        "sh.flag_get": {"signature": "sh.flag_get(flag) -> bool", "description": "Reads an event flag."},
        "sh.log": {"signature": "sh.log(...)"}}})json";
    const std::string second = R"json({"functions": {"sh.log": {"signature": "sh.log(msg)", "description": "Logs."},
                                                 "mymod.call": {"signature": "mymod.call()"}}})json";
    const std::string broken = R"json({"functions": {"sh.a": {"signature": "sh.a()"}, "sh.b": {"description": "none"}}})json";
    dt::LuaApiDocs docs;
    const bool first_ok = static_cast<bool>(docs.parse(first));
    SH_CHECK(first_ok);
    SH_CHECK(docs.size() == 2);
    SH_CHECK(docs.find("sh.flag_get")->description == "Reads an event flag.");
    SH_CHECK(docs.find("sh.log")->description.empty());
    SH_CHECK(docs.find("sh.nope") == nullptr);
    // A second file adds to the first and replaces the names it repeats.
    const bool second_ok = static_cast<bool>(docs.parse(second));
    SH_CHECK(second_ok);
    SH_CHECK(docs.size() == 3);
    SH_CHECK(docs.find("sh.log")->signature == "sh.log(msg)");
    // A broken file changes nothing.
    const bool broken_ok = static_cast<bool>(docs.parse(broken));
    const bool empty_ok = static_cast<bool>(docs.parse("{}"));
    SH_CHECK(!broken_ok && !empty_ok);
    SH_CHECK(docs.size() == 3 && docs.find("sh.a") == nullptr);
    // The same function under its other name.
    SH_CHECK(dt::signature_as("sh.flag_get(flag) -> bool", "sh.flag_get", "sh.flag.get") == "sh.flag.get(flag) -> bool");
    SH_CHECK(dt::signature_as("sh.flag_get_all()", "sh.flag_get", "sh.flag.get") == "sh.flag_get_all()");
}

SH_TEST(lua_assist_new_script) {
    SH_CHECK(dt::new_script_kinds().size() == 6);
    auto s = dt::new_script("map_events", "map1_s03", "my_mod");
    SH_CHECK(s);
    SH_CHECK(s.value().relative == "maps/map1_s03/events.lua");
    SH_CHECK(s.value().text.find("my_mod") != std::string::npos);
    SH_CHECK(s.value().text.find("maps/map1_s03/handlers.json") != std::string::npos);
    SH_CHECK(s.value().text.find("<s>") == std::string::npos && s.value().text.find("<mod>") == std::string::npos);
    SH_CHECK(dt::new_script("main", "", "my_mod").value().relative == "scripts/main.lua");
    SH_CHECK(dt::new_script("map_hooks", "map0_s00", "m").value().relative == "maps/map0_s00/hooks.lua");
    SH_CHECK(dt::new_script("map_world_objects", "map0_s00", "m").value().relative == "maps/map0_s00/world_objects.lua");
    SH_CHECK(dt::new_script("chara_behavior", "Stalker", "m").value().relative == "chara/Stalker/behavior.lua");
    SH_CHECK(dt::new_script("menu", "extras", "m").value().relative == "menus/extras.lua");
    // A name cannot leave the mod's folder or be empty; the kind must be known.
    SH_CHECK(!dt::new_script("map_events", "../x", "m"));
    SH_CHECK(!dt::new_script("map_events", "", "m"));
    SH_CHECK(!dt::new_script("menu", "a/b", "m"));
    SH_CHECK(!dt::new_script("plain", "x", "m"));
    SH_CHECK(!dt::new_script("main", "", ""));
}
