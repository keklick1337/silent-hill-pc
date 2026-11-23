#include "engine/hooks/hooks_console.h"

#include "engine/hooks/catalog.h"
#include "engine/hooks/hook_bus.h"
#include "engine/hooks/hook_trace.h"
#include "engine/log/sh_log.h"

#include <algorithm>
#include <string>
#include <vector>
#include <utility>

namespace sh::hooks {

namespace {

using sh::console::ArgSpec;
using sh::console::ArgType;
using sh::console::Args;
using sh::console::Context;

void print_event(Context& ctx, const std::string& name, bool cancellable) {
    const auto handlers = bus().handlers(name);
    ctx.out.info(name + (cancellable ? " [cancellable]" : "") + ": " + std::to_string(handlers.size()) +
                 " handlers, emitted " + std::to_string(bus().emitted(name)));
    for (const HandlerInfo& h : handlers) {
        ctx.out.info("  " + h.owner + (h.id.empty() ? "" : ":" + h.id) + " priority " + std::to_string(h.priority));
    }
}

Result<void> list(Context& ctx, const Args& args) {
    const std::string only = args.str(0);
    std::vector<std::string> known;
    for (const EventInfo& e : catalog()) {
        known.emplace_back(e.name);
        if (only.empty() || e.name == only) {
            print_event(ctx, std::string(e.name), e.cancellable);
        }
    }
    // Events mods define for each other ("<mod>:<name>") are not in the catalog; list the ones with handlers.
    std::string last;
    for (const HandlerInfo& h : bus().handlers()) {
        if (h.event == last || std::find(known.begin(), known.end(), h.event) != known.end()) {
            continue;
        }
        last = h.event;
        if (only.empty() || h.event == only) {
            print_event(ctx, h.event, false);
        }
    }
    return Result<void>::ok();
}

Result<void> trace(Context& ctx, const Args& args) {
    const bool on = args.has(0) ? args.boolean(0) : !trace_on(bus());
    const std::size_t n = trace_set(bus(), on, [](const std::string& line) {
        ShLog_Write(SH_LOGCAT_CONSOLE, SH_LOGLV_INFO, "%s", line.c_str());
    });
    ctx.out.info(on ? "hooks_trace on: " + std::to_string(n) + " events logged as [HOOK] lines" : "hooks_trace off");
    return Result<void>::ok();
}

}  // namespace

void register_console_commands(sh::console::Registry& console) {
    sh::console::CommandSpec spec;
    spec.name = "hooks";
    spec.group = "mods";
    spec.help = "hook events: handlers per event, how often each fired";
    spec.args = {ArgSpec{"event", ArgType::String, true, "one event name", {}, {}}};
    spec.run = list;
    (void)console.add(std::move(spec));

    sh::console::CommandSpec t;
    t.name = "hooks_trace";
    t.group = "mods";
    t.help = "log every hook event as [HOOK] <event> {fields} (game.tick every 150th)";
    t.args = {ArgSpec{"state", ArgType::Bool, true, "on/off; omitted toggles", {}, {}}};
    t.run = trace;
    (void)console.add(std::move(t));
}

}  // namespace sh::hooks
