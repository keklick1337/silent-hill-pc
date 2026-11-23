// The engine's event bus for mods: the game emits named events
// ("item.pickup", "map.room_enter"...), every handler registered for the name runs in a deterministic order, and a
// handler may change the event's fields or cancel it where the event allows. Several mods hook the same event
// without knowing of each other; nothing is replaced.
//
// No Lua here: the script layer (sh.hooks.on/off) wraps Lua functions into Handlers, the game calls emit().
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace sh::hooks {

using Value = std::variant<std::monostate, bool, std::int64_t, double, std::string>;

class Event {
public:
    explicit Event(std::string name, bool cancellable = false) : name_(std::move(name)), cancellable_(cancellable) {}

    const std::string& name() const { return name_; }

    // A field the emitter fills. A read-only field keeps its value when a handler sets it (the set is refused).
    Event& set(std::string_view key, Value value, bool read_only = false);
    // A handler's write; false when the field is read-only.
    bool update(std::string_view key, Value value);
    const Value* get(std::string_view key) const;
    std::int64_t get_int(std::string_view key, std::int64_t fallback = 0) const;
    std::string get_string(std::string_view key, std::string_view fallback = {}) const;
    bool get_bool(std::string_view key, bool fallback = false) const;
    bool read_only(std::string_view key) const;

    struct Field {
        std::string key;
        Value value;
        bool read_only = false;
    };
    const std::vector<Field>& fields() const { return fields_; }

    bool cancellable() const { return cancellable_; }
    // False (and nothing happens) when the event cannot be cancelled.
    bool cancel(std::string_view by = {});
    bool cancelled() const { return cancelled_; }
    const std::string& cancelled_by() const { return cancelled_by_; }

private:
    Field* find(std::string_view key);
    const Field* find(std::string_view key) const;

    std::string name_;
    std::vector<Field> fields_;
    bool cancellable_ = false;
    bool cancelled_ = false;
    std::string cancelled_by_;
};

struct HandlerOptions {
    std::string owner = "silenthill";  // mod id, "silenthill" for the base game
    std::string id;                    // optional; on() with the same owner + event + id replaces that handler
    int priority = 0;                  // higher runs first
};

using Handler = std::function<void(Event&)>;
using Token = std::uint64_t;

struct HandlerInfo {
    std::string event;
    std::string owner;
    std::string id;
    int priority = 0;
    Token token = 0;
    std::string group;
};

class Bus {
public:
    // `group` says who registered it inside the owner: one Lua state of a mod (the game script, a map's
    // script...). A state that closes drops its handlers with off_group; the same id in another group is another
    // handler.
    Token on(std::string_view event, Handler handler, HandlerOptions options = {}, std::string_view group = {});
    bool off(Token token);
    bool off(std::string_view owner, std::string_view event, std::string_view id);
    std::size_t off_owner(std::string_view owner);  // every handler of a mod (its script state closed)
    std::size_t off_group(std::string_view group);  // every handler registered by one script state
    void clear();

    // True when some handler listens: the emitter skips building the event otherwise.
    bool has(std::string_view event) const;

    // Runs the handlers: priority (higher first), then the owner's load order (base first, then mods as loaded),
    // then registration order. A cancelled event stops at the handler that cancelled it. A handler that throws
    // is logged with its owner and skipped; the event goes on. Returns false when the event was cancelled.
    bool emit(Event& event);

    // The load index of an owner (base -1, mods 0..); unknown owners sort after every mod.
    void set_owner_order(std::function<int(std::string_view owner)> order);
    void set_logger(std::function<void(const std::string& message)> log);

    std::vector<HandlerInfo> handlers(std::string_view event = {}) const;  // in run order; all when empty
    std::uint64_t emitted(std::string_view event) const;                   // how many times since start

private:
    struct Entry {
        HandlerInfo info;
        Handler handler;
        std::uint64_t sequence = 0;
    };
    std::vector<Entry> ordered(std::string_view event) const;

    mutable std::recursive_mutex mutex_;
    std::map<std::string, std::vector<Entry>, std::less<>> handlers_;
    std::map<std::string, std::uint64_t, std::less<>> counts_;
    std::function<int(std::string_view)> order_;
    std::function<void(const std::string&)> log_;
    Token next_token_ = 1;
    std::uint64_t next_sequence_ = 0;
};

// The process-wide bus.
Bus& bus();

}  // namespace sh::hooks
