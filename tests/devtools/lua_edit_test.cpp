// The Lua editor's pure parts (engine/devtools/lua_edit.h): the highlighter's
// tokenizer, the line of a Lua error, the save-to-mod paths and the mod.json it writes.

#include "engine/devtools/lua_edit.h"

#include "formats/common/json.h"
#include "framework/sh_test.h"

#include <string>
#include <vector>

namespace {

namespace dt = sh::devtools;

// The spans of `line` as "kind:text" words.
std::vector<std::string> spans(std::string_view line, dt::LuaLexState& st) {
    std::vector<std::string> out;
    for (const dt::LuaSpan& s : dt::tokenize_lua_line(line, st)) {
        const char* k = s.kind == dt::LuaToken::Keyword  ? "K"
                        : s.kind == dt::LuaToken::String ? "S"
                        : s.kind == dt::LuaToken::Comment ? "C"
                        : s.kind == dt::LuaToken::Number ? "N"
                                                          : "T";
        out.push_back(std::string(k) + ":" + std::string(line.substr(s.begin, s.end - s.begin)));
    }
    return out;
}

}  // namespace

SH_TEST(lua_edit_tokenizer_basic) {
    dt::LuaLexState st;
    const auto t = spans("local x = 0x1F + 2.5e-3 -- note", st);
    SH_CHECK(t.size() == 4);
    SH_CHECK(t[0] == "K:local");
    SH_CHECK(t[1] == "N:0x1F");
    SH_CHECK(t[2] == "N:2.5e-3");
    SH_CHECK(t[3] == "C:-- note");
    // Identifiers that start like keywords are not keywords; escapes stay inside the string.
    const auto u = spans("if endx then s = \"a\\\"b\" .. 'c' end", st);
    SH_CHECK(u.size() == 5);
    SH_CHECK(u[0] == "K:if" && u[1] == "K:then" && u[2] == "S:\"a\\\"b\"" && u[3] == "S:'c'" && u[4] == "K:end");
    SH_CHECK(st.long_level == -1);
}

SH_TEST(lua_edit_tokenizer_long_brackets) {
    dt::LuaLexState st;
    auto a = spans("x = [==[ open", st);
    SH_CHECK(a.size() == 1 && a[0] == "S:[==[ open");
    SH_CHECK(st.long_level == 2 && !st.long_comment);
    auto b = spans("still ]] inside", st);
    SH_CHECK(b.size() == 1 && b[0] == "S:still ]] inside");
    auto c = spans("done ]==] return 1", st);
    SH_CHECK(c.size() == 3 && c[0] == "S:done ]==]" && c[1] == "K:return" && c[2] == "N:1");
    SH_CHECK(st.long_level == -1);
    auto d = spans("--[[ comment", st);
    SH_CHECK(d.size() == 1 && st.long_comment && st.long_level == 0);
    auto e = spans("]] nil", st);
    SH_CHECK(e.size() == 2 && e[0] == "C:]]" && e[1] == "K:nil");
    // An index is not a long string.
    auto f = spans("t[1]", st);
    SH_CHECK(f.size() == 1 && f[0] == "N:1");
}

SH_TEST(lua_edit_error_line) {
    SH_CHECK(dt::lua_error_line("[string \"maps/a:b/events.lua\"]:12: 'end' expected") == 12);
    SH_CHECK(dt::lua_error_line("snippet:3: attempt to call a nil value") == 3);
    SH_CHECK(dt::lua_error_line("/x/gamedata/maps/map0_s01/events.lua:741: unexpected symbol near 'x'") == 741);
    SH_CHECK(dt::lua_error_line("cannot open file") == 0);
}

SH_TEST(lua_edit_save_paths) {
    SH_CHECK(dt::valid_lua_relative_path("maps/map0_s01/events.lua"));
    SH_CHECK(dt::valid_lua_relative_path("scripts/main.lua"));
    SH_CHECK(!dt::valid_lua_relative_path("maps/../../etc.lua"));
    SH_CHECK(!dt::valid_lua_relative_path("/abs/x.lua"));
    SH_CHECK(!dt::valid_lua_relative_path("maps\\x.lua"));
    SH_CHECK(!dt::valid_lua_relative_path("C:/x.lua"));
    SH_CHECK(!dt::valid_lua_relative_path("maps//x.lua"));
    SH_CHECK(!dt::valid_lua_relative_path("maps/x.json"));
    SH_CHECK(!dt::valid_lua_relative_path("./x.lua"));
    const auto p = dt::mod_file_for("/m/my_mod", "maps/map0_s01/events.lua");
    SH_CHECK(p && p.value() == std::filesystem::path("/m/my_mod/maps/map0_s01/events.lua"));
    SH_CHECK(!dt::mod_file_for("", "maps/x.lua"));
    SH_CHECK(!dt::mod_file_for("/m/my_mod", "../x.lua"));
}

SH_TEST(lua_edit_manifest) {
    const std::string fresh = dt::new_mod_manifest("lua_edits");
    const auto v = sh::json::parse(fresh);
    SH_CHECK(v && v.value().find("id")->string() == "lua_edits");
    SH_CHECK(v.value().find("version")->string() == "1.0.0");
    SH_CHECK(v.value().find("scripts")->boolean());

    bool changed = false;
    const auto a = dt::manifest_for_saved_lua(fresh, "maps/map0_s01/events.lua", true, &changed);
    SH_CHECK(a && changed);
    const auto av = sh::json::parse(a.value());
    SH_CHECK(av.value().find("replace")->array().size() == 1);
    // Saving the same path again (any case) changes nothing.
    const auto b = dt::manifest_for_saved_lua(a.value(), "MAPS/map0_s01/events.lua", true, &changed);
    SH_CHECK(b && !changed && b.value() == a.value());
    // A mod without "scripts": true gets it; without replace_whole no list appears.
    const auto c = dt::manifest_for_saved_lua("{\"id\":\"x\",\"version\":\"1\"}", "scripts/main.lua", false, &changed);
    SH_CHECK(c && changed);
    const auto cv = sh::json::parse(c.value());
    SH_CHECK(cv.value().find("scripts")->boolean() && cv.value().find("replace") == nullptr);
    SH_CHECK(cv.value().find("id")->string() == "x");
    SH_CHECK(!dt::manifest_for_saved_lua("[1]", "a.lua", true, &changed));
    SH_CHECK(!dt::manifest_for_saved_lua("{\"replace\":3}", "a.lua", true, &changed));
}
