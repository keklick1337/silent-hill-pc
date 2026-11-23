#include "framework/sh_test.h"

#include "engine/console/history.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

namespace fs = std::filesystem;
using sh::console::History;

namespace {

fs::path hist_scratch(const char* name) {
    const fs::path dir = fs::temp_directory_path() / (std::string("sh_console_history_") + name);
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    return dir;
}

void drop(const fs::path& p) {
    std::error_code ec;
    fs::remove_all(p, ec);
}

}  // namespace

SH_TEST(console_history_add_rules) {
    History h(3);
    h.add("  one  ");
    h.add("");
    h.add("   ");
    h.add("one");
    SH_CHECK(h.size() == 1 && h.entries()[0] == "one");
    h.add("two");
    h.add("one");
    h.add("three");
    SH_CHECK(h.size() == 3 && h.entries().front() == "two" && h.entries().back() == "three");
    SH_CHECK(*h.find_prefix("t") == "three");
    SH_CHECK(*h.find_prefix("tw") == "two");
    SH_CHECK(h.find_prefix("x") == nullptr);
    SH_CHECK(*h.find_prefix("") == "three");
    SH_CHECK(h.find_prefix("Two") == nullptr);
    h.clear();
    SH_CHECK(h.size() == 0 && !h.browsing());
}

SH_TEST(console_history_browsing_parks_the_live_line) {
    History h;
    SH_CHECK(h.prev("x") == nullptr);
    SH_CHECK(h.next() == nullptr);
    h.add("a");
    h.add("b");
    h.add("c");
    SH_CHECK(*h.prev("draft") == "c" && h.browsing());
    SH_CHECK(*h.prev("ignored") == "b");
    SH_CHECK(*h.prev("") == "a");
    SH_CHECK(*h.prev("") == "a");
    SH_CHECK(*h.next() == "b");
    SH_CHECK(*h.next() == "c");
    SH_CHECK(*h.next() == "draft" && !h.browsing());
    SH_CHECK(h.next() == nullptr);
    h.prev("again");
    h.add("d");
    SH_CHECK(!h.browsing());
    h.prev("q");
    h.reset_browsing();
    SH_CHECK(!h.browsing());
}

SH_TEST(console_history_file_roundtrip_and_tolerance) {
    const fs::path dir = hist_scratch("rt");
    const fs::path file = dir / "sub" / "dir" / "history.txt";
    History h;
    SH_CHECK(h.load(file));  // missing file is fine
    SH_CHECK(h.size() == 0);
    h.add("say \xD0\xBF\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82");
    h.add("two");
    SH_CHECK(h.save(file));
    SH_CHECK(!fs::exists(file.string() + ".tmp"));
    History g;
    SH_CHECK(g.load(file));
    SH_CHECK(g.entries() == h.entries());

    {
        std::ofstream out(dir / "crlf.txt", std::ios::binary);
        out << "a\r\nb\r\n\r\nc";
    }
    SH_CHECK(g.load(dir / "crlf.txt"));
    SH_CHECK(g.size() == 3 && g.entries()[0] == "a" && g.entries()[1] == "b" && g.entries()[2] == "c");

    History small(2);
    SH_CHECK(small.load(dir / "crlf.txt"));
    SH_CHECK(small.size() == 2 && small.entries()[0] == "b" && small.entries()[1] == "c");
    drop(dir);
}
