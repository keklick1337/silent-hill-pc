#include "framework/sh_test.h"

#include "engine/console/tokenizer.h"

using namespace sh::console;

SH_TEST(tok_plain_words) {
    auto t = tokenize("  spawn  Harry\t3 ");
    SH_CHECK(t.size() == 3);
    SH_CHECK(t[0].text == "spawn" && t[0].begin == 2 && t[0].end == 7);
    SH_CHECK(t[1].text == "Harry" && !t[1].quoted);
    SH_CHECK(t[2].text == "3" && t[2].end == 16);
    SH_CHECK(tokenize("").empty());
    SH_CHECK(tokenize("   ").empty());
}

SH_TEST(tok_quotes) {
    auto t = tokenize("say \"hello world\" 'a b' x");
    SH_CHECK(t.size() == 4);
    SH_CHECK(t[1].text == "hello world" && t[1].quoted && t[1].closed);
    SH_CHECK(t[1].begin == 4 && t[1].end == 17);
    SH_CHECK(t[2].text == "a b");
    SH_CHECK(t[3].text == "x");
}

SH_TEST(tok_escape_and_apostrophe) {
    auto t = tokenize("echo \"a\\\"b\\\\c\" don't");
    SH_CHECK(t.size() == 3);
    SH_CHECK(t[1].text == "a\"b\\c");
    SH_CHECK(t[2].text == "don't" && !t[2].quoted);
}

SH_TEST(tok_unterminated_quote) {
    auto t = tokenize("go \"half typed");
    SH_CHECK(t.size() == 2);
    SH_CHECK(t[1].text == "half typed" && t[1].quoted && !t[1].closed);
    SH_CHECK(t[1].end == 14);
}

SH_TEST(tok_adjacent_after_closing_quote) {
    auto t = tokenize("\"a\"b");
    SH_CHECK(t.size() == 2 && t[0].text == "a" && t[1].text == "b");
}

SH_TEST(split_semicolons) {
    auto c = split_commands("a 1; b 2 ;;  c ");
    SH_CHECK(c.size() == 3);
    SH_CHECK(c[0] == "a 1" && c[1] == "b 2" && c[2] == "c");
    SH_CHECK(split_commands("").empty());
    SH_CHECK(split_commands(" ; ; ").empty());
}

SH_TEST(split_respects_quotes) {
    auto c = split_commands("echo \"a; b\"; echo 'x;y' ; z");
    SH_CHECK(c.size() == 3);
    SH_CHECK(c[0] == "echo \"a; b\"" && c[1] == "echo 'x;y'" && c[2] == "z");
    auto u = split_commands("echo \"a; b");
    SH_CHECK(u.size() == 1);
}

SH_TEST(cursor_basic) {
    const std::string_view line = "spawn Har";
    Cursor c = cursor_at(line, line.size());
    SH_CHECK(c.index == 1 && c.prefix == "Har" && c.replace_begin == 6 && c.replace_end == 9 && !c.quoted);
    c = cursor_at(line, 3);
    SH_CHECK(c.index == 0 && c.prefix == "spa" && c.replace_begin == 0 && c.replace_end == 5);
    c = cursor_at(line, 0);
    SH_CHECK(c.index == 0 && c.prefix.empty() && c.replace_begin == 0 && c.replace_end == 0);
}

SH_TEST(cursor_free_space) {
    Cursor c = cursor_at("spawn ", 6);
    SH_CHECK(c.index == 1 && c.prefix.empty() && c.replace_begin == 6 && c.replace_end == 6);
    c = cursor_at("a  b", 2);
    SH_CHECK(c.index == 1 && c.replace_begin == 2 && c.replace_end == 2);
    c = cursor_at("", 0);
    SH_CHECK(c.index == 0 && c.prefix.empty());
}

SH_TEST(cursor_after_semicolon) {
    const std::string_view line = "a b; cc dd";
    Cursor c = cursor_at(line, line.size());
    SH_CHECK(c.index == 1 && c.prefix == "dd" && c.replace_begin == 8 && c.replace_end == 10);
    c = cursor_at(line, 4);  // right after ';'
    SH_CHECK(c.index == 0 && c.replace_begin == 4 && c.replace_end == 4);
    c = cursor_at(line, 3);  // end of "b"
    SH_CHECK(c.index == 1 && c.prefix == "b" && c.replace_begin == 2 && c.replace_end == 3);
    c = cursor_at("a; ", 3);
    SH_CHECK(c.index == 0 && c.replace_begin == 3);
}

SH_TEST(cursor_in_quotes) {
    const std::string_view line = "go \"ab cd\" x";
    Cursor c = cursor_at(line, 7);  // after "ab "
    SH_CHECK(c.index == 1 && c.quoted && c.prefix == "ab " && c.replace_begin == 3 && c.replace_end == 10);
    c = cursor_at("go \"ab", 6);
    SH_CHECK(c.index == 1 && c.prefix == "ab" && c.quoted);
    // a ';' inside an unterminated quote does not start a new command
    c = cursor_at("go \"a;b", 7);
    SH_CHECK(c.index == 1 && c.prefix == "a;b");
}

SH_TEST(quote_if_needed_cases) {
    SH_CHECK(quote_if_needed("Harry") == "Harry");
    SH_CHECK(quote_if_needed("a b") == "\"a b\"");
    SH_CHECK(quote_if_needed("") == "\"\"");
    SH_CHECK(quote_if_needed("a;b") == "\"a;b\"");
    SH_CHECK(quote_if_needed("say \"x\"") == "\"say \\\"x\\\"\"");
    auto t = tokenize(quote_if_needed("a \"b\" \\ c"));
    SH_CHECK(t.size() == 1 && t[0].text == "a \"b\" \\ c");
}
