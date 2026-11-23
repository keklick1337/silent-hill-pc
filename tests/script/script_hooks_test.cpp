// sh.hooks from Lua (bind_hooks.cpp), layered map scripts with extend(), mod.json "replace", and the cross-mod
// API (mod.export / sh.mods.get).

#include "framework/sh_test.h"

#include "engine/hooks/hook_bus.h"
#include "engine/script/script_host.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>
#include "framework/mod_fixture.h"

namespace fs = std::filesystem;
using sh::hooks::bus;
using sh::hooks::Event;
using sh::script::Host;
using sh::script::HostConfig;

namespace {

struct Scratch {
    fs::path root;
    explicit Scratch(const char* name) : root(fs::path(SH_PROJECT_ROOT) / ".tmp" / "script_hooks" / name) {
        fs::remove_all(root);
        fs::create_directories(root / "base");
        fs::create_directories(root / "mods");
    }
    ~Scratch() {
        std::error_code ec;
        fs::remove_all(root, ec);
    }
    void write(const fs::path& relative, std::string_view text) const {
        fs::create_directories((root / relative).parent_path());
        std::ofstream(root / relative, std::ios::binary) << text;
        sh::test::mod_file_written(root / relative);
    }
    void mod(const std::string& id, std::string_view extra = "") const {
        write("mods/" + id + "/mod.json", R"({"id": ")" + id + R"(", "version": "1.0.0", "scripts": true)" +
                                              (extra.empty() ? "" : ", " + std::string(extra)) + "}");
    }
};

struct Logged {
    std::vector<std::string> lines;
    HostConfig config(const fs::path& root) {
        HostConfig c;
        c.gamedata_dir = root / "base";
        c.mods_dir = root / "mods";
        c.log = [this](int level, const std::string& message) {
            lines.push_back(std::to_string(level) + "|" + message);
        };
        return c;
    }
    int count(std::string_view part) const {
        int n = 0;
        for (const std::string& line : lines) {
            n += line.find(part) != std::string::npos ? 1 : 0;
        }
        return n;
    }
    bool has(std::string_view part) const { return count(part) > 0; }
    int line_of(std::string_view part) const {
        for (std::size_t i = 0; i < lines.size(); ++i) {
            if (lines[i].find(part) != std::string::npos) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }
};

// The scratch mods load alphabetically (no hints); the bus orders owners like the game does at boot.
struct OwnerOrder {
    OwnerOrder() {
        bus().set_owner_order([](std::string_view owner) {
            return owner == "alpha" ? 0 : owner == "beta" ? 1 : owner == "gamma" ? 2 : -1;
        });
    }
    ~OwnerOrder() { bus().set_owner_order({}); }
};

}  // namespace

SH_TEST(script_hooks_two_mods_and_base_in_order) {
    Scratch dir("order");
    OwnerOrder order;
    dir.write("base/scripts/game.lua", R"lua(
        sh.hooks.on("test.order", function(ev) ev.trace = ev.trace .. "base," end)
    )lua");
    dir.mod("alpha");
    dir.write("mods/alpha/scripts/main.lua", R"lua(
        sh.hooks.on("test.order", function(ev) ev.trace = ev.trace .. "alpha," end, {id = "main"})
        -- The same id again replaces the handler instead of adding one.
        sh.hooks.on("test.order", function(ev) ev.trace = ev.trace .. "alpha2," end, {id = "main"})
    )lua");
    dir.mod("beta");
    dir.write("mods/beta/scripts/main.lua", R"lua(
        sh.hooks.on("test.order", function(ev) ev.trace = ev.trace .. "beta," end)
        sh.hooks.on("test.order", function(ev) ev.trace = ev.trace .. "beta_first," end, {priority = 10})
        local gone = sh.hooks.on("test.order", function(ev) ev.trace = ev.trace .. "never," end)
        sh.hooks.off(gone)
        function on_boot()
            for _, h in ipairs(sh.hooks.list("test.order")) do
                sh.log("list " .. h.owner .. " " .. h.id .. " " .. h.priority)
            end
            sh.log("has " .. tostring(sh.hooks.has("test.order")) .. " " .. tostring(sh.hooks.has("nobody")))
        end
    )lua");
    Logged log;
    {
        Host host(log.config(dir.root));
        SH_CHECK(host.load().value());
        host.on_boot();
        Event ev("test.order");
        ev.set("trace", std::string());
        SH_CHECK(bus().emit(ev));
        SH_CHECK(ev.get_string("trace") == "beta_first,base,alpha2,beta,");
        SH_CHECK(host.hook_count() == 4);
        SH_CHECK(log.line_of("list beta  10") >= 0 && log.line_of("list beta  10") < log.line_of("list silenthill"));
        SH_CHECK(log.line_of("list alpha main 0") > log.line_of("list silenthill"));
        SH_CHECK(log.has("has true false"));
        const auto info = bus().handlers("test.order");
        SH_CHECK(info.size() == 4 && info[0].group == host.hook_group());
    }
    SH_CHECK(!bus().has("test.order"));  // the closed state took its handlers along
}

SH_TEST(script_hooks_modify_cancel_read_only) {
    Scratch dir("modify");
    OwnerOrder order;
    dir.write("base/scripts/game.lua", R"lua(
        sh.hooks.on("item.pickup", function(ev)
            ev.count = ev.count * 2
            ev.item = "Other"          -- read-only: refused, warned once
            ev.note = "base saw " .. ev.item
        end)
    )lua");
    dir.mod("alpha");
    dir.write("mods/alpha/scripts/main.lua", R"lua(
        sh.hooks.on("item.pickup", function(ev)
            if ev.count > 20 then ev:cancel() end
            sh.log("alpha cancelled=" .. tostring(ev.cancelled) .. " name=" .. ev.name)
        end)
        sh.hooks.on("plain.event", function(ev) sh.log("cancel ok=" .. tostring(ev:cancel())) end)
        -- A mod's own event: another mod hooks it, the emitter sees the result.
        function on_boot()
            local fields, cancelled = sh.hooks.emit("alpha:ping", {n = 1, who = "alpha"}, true)
            sh.log("ping n=" .. fields.n .. " cancelled=" .. tostring(cancelled))
            local _, plain = sh.hooks.emit("alpha:ping", {n = 1})
            sh.log("plain cancelled=" .. tostring(plain))
        end
    )lua");
    dir.mod("beta");
    dir.write("mods/beta/scripts/main.lua", R"lua(
        sh.hooks.on("item.pickup", function(ev) sh.log("beta ran") end)
        sh.hooks.on("alpha:ping", function(ev) ev.n = ev.n + 1; ev:cancel() end)
    )lua");
    Logged log;
    Host host(log.config(dir.root));
    SH_CHECK(host.load().value());
    for (int round = 0; round < 2; ++round) {
        Event ev("item.pickup", true);
        ev.set("item", std::string("HandgunBullets"), true).set("count", std::int64_t{15});
        SH_CHECK(!bus().emit(ev));
        SH_CHECK(ev.get_int("count") == 30);
        SH_CHECK(ev.get_string("item") == "HandgunBullets");
        SH_CHECK(ev.get_string("note") == "base saw HandgunBullets");
        SH_CHECK(ev.cancelled_by() == "alpha");
    }
    SH_CHECK(log.count("field 'item' is read-only") == 1);
    SH_CHECK(log.has("alpha cancelled=true name=item.pickup"));
    SH_CHECK(!log.has("beta ran"));  // the cancel stopped the event

    Event small("item.pickup", true);
    small.set("item", std::string("Ampoule"), true).set("count", std::int64_t{2});
    SH_CHECK(bus().emit(small) && small.get_int("count") == 4);
    SH_CHECK(log.has("beta ran"));

    Event plain("plain.event");
    SH_CHECK(bus().emit(plain));
    SH_CHECK(log.has("cancel ok=false"));
    SH_CHECK(log.has("the event cannot be cancelled"));

    host.on_boot();
    SH_CHECK(log.has("ping n=2 cancelled=true"));
    SH_CHECK(log.has("plain cancelled=false"));  // not cancellable: beta's cancel did nothing
}

SH_TEST(script_hooks_errors_are_contained) {
    Scratch dir("errors");
    OwnerOrder order;
    dir.mod("alpha");
    dir.write("mods/alpha/scripts/main.lua", R"lua(
        sh.hooks.on("test.err", function(ev) error("boom") end)
        sh.hooks.on("test.err", function(ev) while true do end end)
        sh.hooks.on("test.err", function(ev) ev.table = {} end)
        kept = nil
        sh.hooks.on("test.err", function(ev) kept = ev; ev.ran = true end)
        function on_boot()
            local ok, err = pcall(function() return kept.ran end)
            sh.log("stale ok=" .. tostring(ok) .. " " .. tostring(err))
        end
    )lua");
    Logged log;
    Host host(log.config(dir.root));
    SH_CHECK(host.load().value());
    for (int round = 0; round < 3; ++round) {
        Event ev("test.err");
        SH_CHECK(bus().emit(ev));
        SH_CHECK(ev.get_bool("ran"));
    }
    SH_CHECK(log.count("hook test.err of alpha error") == 1);  // once per owner and event
    SH_CHECK(log.has("boom"));
    SH_CHECK(host.hook_count() == 1);                          // the three broken ones are off
    SH_CHECK(bus().handlers("test.err").size() == 1);
    host.on_boot();
    SH_CHECK(log.has("stale ok=false"));
    SH_CHECK(log.has("this event is over"));
}

SH_TEST(script_hooks_host_close_removes_only_its_handlers) {
    Scratch dir("close");
    dir.write("base/maps/m/events.lua", R"lua(
        sh.hooks.on("test.close", function(ev) ev.map = (ev.map or 0) + 1 end, {id = "same"})
    )lua");
    Logged log;
    Host first(log.config(dir.root));
    SH_CHECK(first.run_layered("maps/m/events.lua").value());
    {
        Host second(log.config(dir.root));
        SH_CHECK(second.run_layered("maps/m/events.lua").value());
        SH_CHECK(bus().handlers("test.close").size() == 2);  // same owner and id, another state
        Event ev("test.close");
        bus().emit(ev);
        SH_CHECK(ev.get_int("map") == 2);
    }
    SH_CHECK(bus().handlers("test.close").size() == 1);
    SH_CHECK(bus().handlers("test.close")[0].group == first.hook_group());
}

// base -> alpha -> beta: each extend() wraps what the scripts saw before it; a new global adds a handler.
SH_TEST(script_layers_extend_chains_in_load_order) {
    Scratch dir("extend");
    OwnerOrder order;
    dir.write("base/maps/m/events.lua", R"lua(
        trace = "base-global"
        function Door() sh.log("base door") end
        function Score(a, b) return a + b end
        function on_boot() sh.log("base boot") end
        sh.hooks.on("test.layer", function(ev) ev.t = ev.t .. "base," end)
    )lua");
    dir.mod("alpha");
    dir.write("mods/alpha/maps/m/events.lua", R"lua(
        extend("Door", function(base, ...)
            sh.log("alpha before")
            base(...)
            sh.log("alpha after")
        end)
        extend("Score", function(base, a, b) return base(a, b) * 10 end)
        function Window() sh.log("alpha window sees " .. trace .. " mod=" .. mod.id) end
        function on_boot() sh.log("alpha boot") end
        extend("Missing", function(base) base(); sh.log("alpha missing") end)
        sh.hooks.on("test.layer", function(ev) ev.t = ev.t .. "alpha," end)
    )lua");
    dir.mod("beta");
    dir.write("mods/beta/maps/m/events.lua", R"lua(
        extend("Door", function(base)
            sh.log("beta before")
            base()
            sh.log("beta after")
        end)
        extend("Score", function(base, a, b) return base(a, b) + 1 end)
        trace = "beta-own"   -- its own global; the base and alpha keep theirs
        sh.hooks.on("test.layer", function(ev) ev.t = ev.t .. "beta," end)
    )lua");
    dir.mod("broken");
    dir.write("mods/broken/maps/m/events.lua", "error('broken layer')");
    Logged log;
    Host host(log.config(dir.root));
    SH_CHECK(host.run_layered("maps/m/events.lua").value());
    SH_CHECK((host.extension_mods() == std::vector<std::string>{"alpha", "beta"}));
    SH_CHECK(log.has("broken layer"));
    SH_CHECK(log.has("extend('Missing'): nothing of that name"));
    host.on_boot();
    SH_CHECK(host.has_global("Door") && host.has_global("Window") && !host.has_global("Nothing"));
    SH_CHECK(host.call_global("Door"));
    const int b0 = log.line_of("beta before");
    const int a0 = log.line_of("alpha before");
    const int base = log.line_of("base door");
    const int a1 = log.line_of("alpha after");
    const int b1 = log.line_of("beta after");
    SH_CHECK(b0 >= 0 && b0 < a0 && a0 < base && base < a1 && a1 < b1);
    SH_CHECK(host.call_global("Window"));
    SH_CHECK(log.has("alpha window sees base-global mod=alpha"));
    long long score = 0;
    SH_CHECK(host.call_global_int("Score", {2, 3}, &score));
    SH_CHECK(score == 51);  // (2 + 3) * 10 + 1
    SH_CHECK(host.call_global("Missing") && log.has("alpha missing"));
    SH_CHECK(log.line_of("base boot") >= 0 && log.line_of("base boot") < log.line_of("alpha boot"));

    Event ev("test.layer");
    ev.set("t", std::string());
    bus().emit(ev);
    SH_CHECK(ev.get_string("t") == "base,alpha,beta,");
}

// "replace": the mod's copy runs instead of the base; the other mods still extend it.
SH_TEST(script_layers_replace) {
    Scratch dir("replace");
    OwnerOrder order;
    dir.write("base/maps/m/events.lua", "function Door() sh.log('base door') end");
    dir.mod("alpha", R"("replace": ["maps/m/events.lua"])");
    dir.write("mods/alpha/maps/m/events.lua", R"lua(
        function Door() sh.log("alpha door") end
        sh.hooks.on("test.replace", function(ev) ev.owner_seen = "x" end)
    )lua");
    dir.mod("beta");
    dir.write("mods/beta/maps/m/events.lua", R"lua(
        extend("Door", function(base) base(); sh.log("beta door") end)
    )lua");
    Logged log;
    Host host(log.config(dir.root));
    SH_CHECK(host.run_layered("maps/m/events.lua").value());
    SH_CHECK(host.base_owner() == "alpha");
    SH_CHECK(host.call_global("Door"));
    SH_CHECK(!log.has("base door"));
    SH_CHECK(log.line_of("alpha door") >= 0 && log.line_of("alpha door") < log.line_of("beta door"));
    SH_CHECK(log.has("mod alpha:") && log.has("replaces the base script"));
    const auto info = bus().handlers("test.replace");
    SH_CHECK(info.size() == 1 && info[0].owner == "alpha");
}

SH_TEST(script_mods_export_and_get) {
    Scratch dir("export");
    dir.write("base/scripts/game.lua", R"lua(
        function on_boot()
            local a = sh.mods.get("alpha")
            sh.log("base sees " .. a.name .. " api=" .. tostring(a.api ~= nil))
        end
    )lua");
    dir.mod("alpha");
    dir.write("mods/alpha/scripts/main.lua", R"lua(
        local count = 0
        mod.export({ greet = function(who) count = count + 1; return "hi " .. who .. " #" .. count end })
    )lua");
    dir.mod("beta", R"("optional_dependencies": ["alpha"])");
    dir.write("mods/beta/scripts/main.lua", R"lua(
        function on_boot()
            local a = sh.mods.get("alpha")
            sh.log("beta got " .. a.id .. " " .. a.version .. " " .. a.api.greet("beta"))
            sh.log("beta ghost " .. tostring(sh.mods.get("ghost")))
            local self = sh.mods.get("beta")
            sh.log("beta self api " .. tostring(self.api))
            local ok = pcall(mod.export, 5)
            sh.log("bad export ok=" .. tostring(ok))
        end
    )lua");
    Logged log;
    Host host(log.config(dir.root));
    SH_CHECK(host.load().value());
    host.on_boot();
    SH_CHECK(log.has("base sees alpha api=true"));
    SH_CHECK(log.has("beta got alpha 1.0.0 hi beta #1"));
    SH_CHECK(log.has("beta ghost nil"));
    SH_CHECK(log.has("beta self api nil"));
    SH_CHECK(log.has("bad export ok=false"));
}
