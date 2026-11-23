#include "engine/hooks/hook_trace.h"

#include "engine/hooks/catalog.h"

#include <memory>

namespace sh::hooks {

namespace {

constexpr std::string_view kOwner = "silenthill";
constexpr std::string_view kId = "trace";

std::string value_text(const Value& v) {
    if (const auto* b = std::get_if<bool>(&v)) {
        return *b ? "true" : "false";
    }
    if (const auto* i = std::get_if<std::int64_t>(&v)) {
        return std::to_string(*i);
    }
    if (const auto* d = std::get_if<double>(&v)) {
        return std::to_string(*d);
    }
    if (const auto* s = std::get_if<std::string>(&v)) {
        return *s;
    }
    return "nil";
}

}  // namespace

std::string format_fields(const Event& event) {
    std::string out = "{";
    for (const Event::Field& f : event.fields()) {
        if (out.size() > 1) {
            out += ", ";
        }
        out += f.key + "=" + value_text(f.value);
    }
    return out + "}";
}

std::size_t trace_set(Bus& bus, bool on, std::function<void(const std::string& line)> sink, int tick_every) {
    for (const EventInfo& e : catalog()) {
        bus.off(kOwner, e.name, kId);
    }
    if (!on || !sink) {
        return 0;
    }
    auto shared_sink = std::make_shared<std::function<void(const std::string&)>>(std::move(sink));
    std::size_t added = 0;
    for (const EventInfo& e : catalog()) {
        const bool tick = e.name == "game.tick";
        auto count = std::make_shared<std::uint64_t>(0);
        bus.on(
            e.name,
            [shared_sink, tick, tick_every, count](Event& ev) {
                ++*count;
                if (tick && tick_every > 1 && (*count % static_cast<std::uint64_t>(tick_every)) != 1) {
                    return;
                }
                (*shared_sink)("[HOOK] " + ev.name() + " " + format_fields(ev));
            },
            // Lowest priority: the trace shows what the event looks like after the mods' handlers ran.
            HandlerOptions{std::string(kOwner), std::string(kId), -1000000});
        ++added;
    }
    return added;
}

bool trace_on(const Bus& bus) {
    for (const HandlerInfo& h : bus.handlers("map.load")) {
        if (h.owner == kOwner && h.id == kId) {
            return true;
        }
    }
    return false;
}

}  // namespace sh::hooks
