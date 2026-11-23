// sh.console: Lua mods add commands to the process-wide console registry.

#include "framework/sh_test.h"

#include "engine/console/completion.h"
#include "engine/console/instance.h"
#include "engine/script/script_host.h"

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;
namespace con = sh::console;
using sh::script::Host;
using sh::script::HostConfig;

struct Collect final : con::Output {
    std::vector<std::string> lines;
    void print(con::Level level, std::string_view line) override {
        lines.push_back(std::string(level == con::Level::Error ? "E:" : "") + std::string(line));
    }
    bool has(const std::string& part) const {
        for (const auto& l : lines) {
            if (l.find(part) != std::string::npos) return true;
        }
        return false;
    }
};

struct ConsoleScratch {
    fs::path root;
    std::vector<std::string> log;
    ConsoleScratch() : root(fs::path(SH_PROJECT_ROOT) / ".tmp" / "script" / "console_api") {
        fs::remove_all(root);
        fs::create_directories(root / "scripts");
    }
    ~ConsoleScratch() {
        std::error_code ec;
        fs::remove_all(root, ec);
    }
    std::unique_ptr<Host> host(const char* lua) {
        std::ofstream(root / "scripts" / "game.lua", std::ios::binary) << lua;
        HostConfig c;
        c.gamedata_dir = root;
        c.log = [this](int, const std::string& m) { log.push_back(m); };
        auto h = std::make_unique<Host>(c);
        SH_CHECK(h->load().value());
        return h;
    }
};

const char* const kScript = R"lua(
    sh.console.register{
        name = "spawn2", aliases = {"s2"}, help = "spawn", group = "mods",
        args = {
            {name = "character", type = "string",
             complete = function(prefix) return {"Lisa", {text = "Cybil", note = "cop"}, prefix .. "!"} end},
            {name = "distance", type = "number", optional = true},
            {name = "mode", type = "enum", values = {"a", "b"}, optional = true},
        },
        run = function(args)
            return "got " .. args.character .. " " .. tostring(args.distance) .. " " .. math.type(args[2] or 0)
                   .. " n=" .. args.n .. " " .. tostring(args.mode)
        end,
    }
    sh.console.register{name = "ints", args = {{name = "k", type = "int"}, {name = "f", type = "bool"}},
        run = function(a) return {tostring(math.type(a.k)) .. a.k, tostring(a.f)} end}
    sh.console.register{name = "boom", run = function() error("kaput") end}
    sh.console.register{name = "bad", args = {{name = "x", type = "string", complete = function() error("no") end}},
        run = function() end}
    sh.console.register{name = "echoer", run = function() sh.console.print("hello", "warn") end}
    sh.console.register{name = "viaexec", run = function() return tostring(sh.console.exec("ints 7 on")) end}
)lua";

}  // namespace

SH_TEST(console_api_typed_args_and_output) {
    ConsoleScratch t;
    auto host = t.host(kScript);
    Collect out;
    SH_CHECK(con::global_registry().execute("spawn2 Lisa 2.5 b", out));
    SH_CHECK(out.has("got Lisa 2.5 float n=3 b"));
    out.lines.clear();
    SH_CHECK(con::global_registry().execute("s2 Cybil", out));
    SH_CHECK(out.has("got Cybil nil integer n=1 nil"));
    out.lines.clear();
    SH_CHECK(con::global_registry().execute("ints 0x10 on", out));
    SH_CHECK(out.lines.size() == 2 && out.lines[0] == "integer16" && out.lines[1] == "true");
    out.lines.clear();
    SH_CHECK(con::global_registry().execute("viaexec", out));
    SH_CHECK(out.has("true"));
}

SH_TEST(console_api_validation_errors) {
    ConsoleScratch t;
    auto host = t.host(kScript);
    Collect out;
    SH_CHECK(!con::global_registry().execute("spawn2 Lisa 1 zzz", out));
    SH_CHECK(out.has("mode"));
    out.lines.clear();
    SH_CHECK(!con::global_registry().execute("ints abc on", out));
    SH_CHECK(out.has("expected integer"));
}

SH_TEST(console_api_completion) {
    ConsoleScratch t;
    auto host = t.host(kScript);
    const std::string line = "spawn2 Li";
    const con::Completion c = con::complete(con::global_registry(), line, line.size());
    bool lisa = false;
    for (const auto& i : c.items) lisa = lisa || i.text == "Lisa";
    SH_CHECK(lisa);
    const std::string line2 = "spawn2 ";
    const con::Completion c2 = con::complete(con::global_registry(), line2, line2.size());
    bool cybil = false;
    for (const auto& i : c2.items) cybil = cybil || (i.text == "Cybil" && i.note == "cop");
    SH_CHECK(cybil);
    // A throwing completion callback is ignored.
    const std::string line3 = "bad ";
    const con::Completion c3 = con::complete(con::global_registry(), line3, line3.size());
    SH_CHECK(c3.items.empty());
}

SH_TEST(console_api_errors_and_print) {
    ConsoleScratch t;
    auto host = t.host(kScript);
    Collect out;
    SH_CHECK(!con::global_registry().execute("boom", out));
    SH_CHECK(out.has("kaput"));
    // print without an active console goes to the host log; with one, to it.
    SH_CHECK(con::active_output() == nullptr);
    t.log.clear();
    SH_CHECK(con::global_registry().execute("echoer", out));
    bool logged = false;
    for (const auto& l : t.log) logged = logged || l.find("hello") != std::string::npos;
    SH_CHECK(logged);
    Collect active;
    con::set_active_output(&active);
    SH_CHECK(con::global_registry().execute("echoer", out));
    con::set_active_output(nullptr);
    SH_CHECK(active.has("hello"));
}

SH_TEST(console_api_unregister_and_host_destruction) {
    ConsoleScratch t;
    {
        auto host = t.host(kScript);
        SH_CHECK(con::global_registry().find("boom") != nullptr);
        SH_CHECK(con::global_registry().find("s2") != nullptr);
        host.reset();
    }
    SH_CHECK(con::global_registry().find("boom") == nullptr);
    SH_CHECK(con::global_registry().find("s2") == nullptr);
}

SH_TEST(console_api_unregister) {
    ConsoleScratch t;
    auto host = t.host(R"lua(
        sh.console.register{name = "tmp", run = function() end}
        function on_boot()
            sh.log("u1=" .. tostring(sh.console.unregister("tmp")))
            sh.log("u2=" .. tostring(sh.console.unregister("tmp")))
        end
    )lua");
    SH_CHECK(con::global_registry().find("tmp") != nullptr);
    host->on_boot();
    SH_CHECK(con::global_registry().find("tmp") == nullptr);
    bool u1 = false, u2 = false;
    for (const auto& l : t.log) {
        u1 = u1 || l.find("u1=true") != std::string::npos;
        u2 = u2 || l.find("u2=false") != std::string::npos;
    }
    SH_CHECK(u1 && u2);
}
